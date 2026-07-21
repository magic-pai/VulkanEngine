#include "Common.hlsl"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure g_scene_acceleration_structure;
[[vk::binding(1, 1)]] Texture2D<float> g_depth_buffer;
[[vk::binding(2, 1)]] Texture2D<float4> g_normal;
[[vk::binding(3, 1)]] Texture2D<float> g_roughness;
[[vk::binding(4, 1)]] Texture2D<float2> g_blue_noise_texture;
[[vk::binding(5, 1)]] Texture2D<float> g_sssr_hit_confidence;
[[vk::binding(6, 1)]] Buffer<uint> g_ray_list;
[[vk::binding(7, 1)]] RWBuffer<uint> g_ray_counter;
[[vk::binding(8, 1)]] RWTexture2D<uint2> g_ray_query_result;

[[vk::binding(9, 1)]] cbuffer RayQueryControls : register(b1) {
    float g_max_ray_distance;
    float g_screen_hit_confidence_threshold;
    float g_origin_bias_min;
    float g_origin_bias_scale;
    float g_origin_bias_max;
    uint g_ray_query_enabled;
    uint g_ray_query_contract_version;
    uint g_ray_query_diagnostics_enabled;
};

[[vk::binding(10, 1)]] globallycoherent RWStructuredBuffer<uint>
    g_ray_query_diagnostics;

static const uint kDiagnosticCandidateRayCount = 0u;
static const uint kDiagnosticScreenHitAcceptedCount = 1u;
static const uint kDiagnosticTraceCount = 2u;
static const uint kDiagnosticCommittedHitCount = 3u;
static const uint kDiagnosticMissCount = 4u;
static const uint kDiagnosticInvalidRayCount = 5u;
static const uint kDiagnosticHitDistanceSumMillimeters = 6u;
static const uint kDiagnosticHitDistanceMinMillimeters = 7u;
static const uint kDiagnosticHitDistanceMaxMillimeters = 8u;
static const uint kDiagnosticResultPixelWriteCount = 9u;

void DiagnosticAdd(uint index, uint value) {
    if (g_ray_query_diagnostics_enabled != 0u) {
        InterlockedAdd(g_ray_query_diagnostics[index], value);
    }
}

void DiagnosticMin(uint index, uint value) {
    if (g_ray_query_diagnostics_enabled != 0u) {
        InterlockedMin(g_ray_query_diagnostics[index], value);
    }
}

void DiagnosticMax(uint index, uint value) {
    if (g_ray_query_diagnostics_enabled != 0u) {
        InterlockedMax(g_ray_query_diagnostics[index], value);
    }
}

float3 SampleGgxVndf(
    float3 viewDirection,
    float alphaX,
    float alphaY,
    float randomX,
    float randomY
) {
    float3 stretchedView = normalize(float3(
        alphaX * viewDirection.x,
        alphaY * viewDirection.y,
        viewDirection.z
    ));
    float lensq = dot(stretchedView.xy, stretchedView.xy);
    float3 tangentX = lensq > 0.0
        ? float3(-stretchedView.y, stretchedView.x, 0.0) * rsqrt(lensq)
        : float3(1.0, 0.0, 0.0);
    float3 tangentY = cross(stretchedView, tangentX);
    float radius = sqrt(randomX);
    float phi = 6.28318530718 * randomY;
    float t1 = radius * cos(phi);
    float t2 = radius * sin(phi);
    float interpolation = 0.5 * (1.0 + stretchedView.z);
    t2 = (1.0 - interpolation) * sqrt(max(0.0, 1.0 - t1 * t1)) +
        interpolation * t2;
    float3 hemisphereNormal = tangentX * t1 + tangentY * t2 +
        stretchedView * sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2));
    return normalize(float3(
        alphaX * hemisphereNormal.x,
        alphaY * hemisphereNormal.y,
        max(0.0, hemisphereNormal.z)
    ));
}

float3x3 CreateTangentBasis(float3 normal) {
    float3 tangent;
    if (abs(normal.z) > 0.0) {
        float lengthYZ = max(length(normal.yz), 1.0e-6);
        tangent = float3(0.0, -normal.z / lengthYZ, normal.y / lengthYZ);
    } else {
        float lengthXY = max(length(normal.xy), 1.0e-6);
        tangent = float3(normal.y / lengthXY, -normal.x / lengthXY, 0.0);
    }
    return transpose(float3x3(tangent, cross(normal, tangent), normal));
}

float3 ReflectionDirection(
    float3 viewDirection,
    float3 surfaceNormal,
    float roughness,
    uint2 pixel
) {
    float3x3 tangentBasis = CreateTangentBasis(surfaceNormal);
    float3 tangentView = mul(-viewDirection, tangentBasis);
    float2 randomVector = g_blue_noise_texture.Load(int3(pixel % 128u, 0));
    float3 sampledNormal = SampleGgxVndf(
        tangentView,
        roughness,
        roughness,
        randomVector.x,
        randomVector.y
    );
    if ((g_environment_fallback_control & 0x20000000u) != 0u) {
        sampledNormal = float3(0.0, 0.0, 1.0);
    }
    float3 tangentReflection = reflect(-tangentView, sampledNormal);
    return mul(tangentReflection, transpose(tangentBasis));
}

void StoreRayQueryResult(
    uint2 coordinates,
    bool copyHorizontal,
    bool copyVertical,
    bool copyDiagonal,
    uint2 result
) {
    g_ray_query_result[coordinates] = result;
    uint writeCount = 1u;
    uint2 copyTarget = coordinates ^ 1u;
    if (copyHorizontal && copyTarget.x < g_buffer_dimensions.x) {
        g_ray_query_result[uint2(copyTarget.x, coordinates.y)] = result;
        ++writeCount;
    }
    if (copyVertical && copyTarget.y < g_buffer_dimensions.y) {
        g_ray_query_result[uint2(coordinates.x, copyTarget.y)] = result;
        ++writeCount;
    }
    if (copyDiagonal && all(copyTarget < g_buffer_dimensions)) {
        g_ray_query_result[copyTarget] = result;
        ++writeCount;
    }
    DiagnosticAdd(kDiagnosticResultPixelWriteCount, writeCount);
}

[numthreads(8, 8, 1)]
void main(uint groupIndex : SV_GroupIndex, uint groupId : SV_GroupID) {
    uint rayIndex = groupId * 64u + groupIndex;
    if (g_ray_query_enabled == 0u || rayIndex >= g_ray_counter[1]) {
        return;
    }

    DiagnosticAdd(kDiagnosticCandidateRayCount, 1u);
    uint2 coordinates;
    bool copyHorizontal;
    bool copyVertical;
    bool copyDiagonal;
    UnpackRayCoords(
        g_ray_list[rayIndex],
        coordinates,
        copyHorizontal,
        copyVertical,
        copyDiagonal
    );

    if (any(coordinates >= g_buffer_dimensions)) {
        DiagnosticAdd(kDiagnosticInvalidRayCount, 1u);
        return;
    }

    float screenConfidence = g_sssr_hit_confidence.Load(int3(coordinates, 0));
    if (screenConfidence >= g_screen_hit_confidence_threshold) {
        DiagnosticAdd(kDiagnosticScreenHitAcceptedCount, 1u);
        return;
    }

    float depth = g_depth_buffer.Load(int3(coordinates, 0));
    float3 worldNormal = normalize(
        2.0 * g_normal.Load(int3(coordinates, 0)).xyz - 1.0
    );
    if (depth <= 0.0 || depth >= 1.0 || !all(isfinite(worldNormal))) {
        DiagnosticAdd(kDiagnosticInvalidRayCount, 1u);
        StoreRayQueryResult(
            coordinates,
            copyHorizontal,
            copyVertical,
            copyDiagonal,
            uint2(0u, 0u)
        );
        return;
    }

    float2 uv = (float2(coordinates) + 0.5) * g_inv_buffer_dimensions;
    float3 screenOrigin = float3(uv, depth);
    float3 viewOrigin = InvProjectPosition(screenOrigin, g_inv_proj);
    float3 viewDirection = normalize(viewOrigin);
    float3 viewNormal = normalize(mul(g_view, float4(worldNormal, 0.0)).xyz);
    float roughness = max(0.001, g_roughness.Load(int3(coordinates, 0)));
    float3 viewReflection = ReflectionDirection(
        viewDirection,
        viewNormal,
        roughness,
        coordinates
    );
    float3 worldDirection = normalize(
        mul(g_inv_view, float4(viewReflection, 0.0)).xyz
    );
    float3 worldOrigin = InvProjectPosition(screenOrigin, g_inv_view_proj);
    if (!all(isfinite(worldOrigin)) || !all(isfinite(worldDirection)) ||
        dot(worldDirection, worldDirection) < 0.99) {
        DiagnosticAdd(kDiagnosticInvalidRayCount, 1u);
        return;
    }

    float3 cameraPosition = mul(g_inv_view, float4(0.0, 0.0, 0.0, 1.0)).xyz;
    float cameraDistance = length(worldOrigin - cameraPosition);
    float originBias = clamp(
        max(g_origin_bias_min, cameraDistance * g_origin_bias_scale),
        g_origin_bias_min,
        g_origin_bias_max
    );
    worldOrigin += worldNormal * originBias;

    DiagnosticAdd(kDiagnosticTraceCount, 1u);
    RayQuery<
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES
    > rayQuery;
    RayDesc rayDescription;
    rayDescription.Origin = worldOrigin;
    rayDescription.TMin = max(originBias * 0.25, 1.0e-4);
    rayDescription.Direction = worldDirection;
    rayDescription.TMax = max(g_max_ray_distance, 0.01);
    rayQuery.TraceRayInline(
        g_scene_acceleration_structure,
        RAY_FLAG_NONE,
        0xffu,
        rayDescription
    );
    while (rayQuery.Proceed()) {
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float hitDistance = rayQuery.CommittedRayT();
        uint instanceId = rayQuery.CommittedInstanceID();
        uint distanceMillimeters = (uint)min(
            hitDistance * 1000.0 + 0.5,
            4294967295.0
        );
        DiagnosticAdd(kDiagnosticCommittedHitCount, 1u);
        DiagnosticAdd(
            kDiagnosticHitDistanceSumMillimeters,
            distanceMillimeters
        );
        DiagnosticMin(
            kDiagnosticHitDistanceMinMillimeters,
            distanceMillimeters
        );
        DiagnosticMax(
            kDiagnosticHitDistanceMaxMillimeters,
            distanceMillimeters
        );
        StoreRayQueryResult(
            coordinates,
            copyHorizontal,
            copyVertical,
            copyDiagonal,
            uint2(asuint(hitDistance), instanceId + 1u)
        );
    } else {
        DiagnosticAdd(kDiagnosticMissCount, 1u);
        StoreRayQueryResult(
            coordinates,
            copyHorizontal,
            copyVertical,
            copyDiagonal,
            uint2(0u, 0u)
        );
    }
}
