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

struct InstanceMetadata {
    uint2 vertexAddress;
    uint2 indexAddress;
    uint vertexCount;
    uint indexCount;
    uint vertexStride;
    uint materialIndex;
};

[[vk::binding(9, 1)]] cbuffer RayQueryControls : register(b1) {
    float g_max_ray_distance;
    float g_screen_hit_confidence_threshold;
    float g_origin_bias_min;
    float g_origin_bias_scale;
    float g_origin_bias_max;
    uint g_ray_query_enabled;
    uint g_ray_query_contract_version;
    uint g_ray_query_diagnostics_enabled;
    uint g_instance_metadata_count;
    uint g_instance_material_count;
    uint g_expected_vertex_stride;
    uint g_hit_attributes_enabled;
};

[[vk::binding(10, 1)]] globallycoherent RWStructuredBuffer<uint>
    g_ray_query_diagnostics;

[[vk::binding(11, 1)]] StructuredBuffer<InstanceMetadata>
    g_instance_metadata;

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
static const uint kDiagnosticHitAttributeResolvedCount = 10u;
static const uint kDiagnosticInvalidInstanceCount = 11u;
static const uint kDiagnosticInvalidPrimitiveCount = 12u;
static const uint kDiagnosticInvalidVertexCount = 13u;
static const uint kDiagnosticInvalidBarycentricCount = 14u;
static const uint kDiagnosticInvalidAttributeValueCount = 15u;
static const uint kDiagnosticMaterialResolvedCount = 16u;
static const uint kDiagnosticMaterialFallbackCount = 17u;
static const uint kDiagnosticPositionMismatchCount = 18u;
static const uint kDiagnosticPositionErrorMaxMicrometers = 19u;
static const uint kDiagnosticNormalLengthMinPermille = 20u;
static const uint kDiagnosticNormalLengthMaxPermille = 21u;
static const uint kDiagnosticIdentityChecksum = 22u;
static const uint kDiagnosticPrimitiveChecksum = 23u;
static const uint kDiagnosticMaterialChecksum = 24u;
static const uint kDiagnosticBarycentricSumMinPermille = 25u;
static const uint kDiagnosticBarycentricSumMaxPermille = 26u;

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

uint HashDiagnosticIdentity(uint value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value | 1u;
}

uint SaturatedMetric(float value, float scale) {
    return (uint)min(max(value * scale + 0.5, 0.0), 4294967040.0);
}

uint64_t DeviceAddress(uint2 address) {
    return uint64_t(address.x) | (uint64_t(address.y) << 32u);
}

uint LoadTriangleIndex(uint64_t indexAddress, uint indexOffset) {
    return vk::RawBufferLoad<uint>(
        indexAddress + uint64_t(indexOffset) * 4u,
        4u
    );
}

float3 LoadVertexFloat3(
    uint64_t vertexAddress,
    uint vertexIndex,
    uint vertexStride,
    uint attributeOffset
) {
    return vk::RawBufferLoad<float3>(
        vertexAddress + uint64_t(vertexIndex) * uint64_t(vertexStride) +
            uint64_t(attributeOffset),
        4u
    );
}

float2 LoadVertexFloat2(
    uint64_t vertexAddress,
    uint vertexIndex,
    uint vertexStride,
    uint attributeOffset
) {
    return vk::RawBufferLoad<float2>(
        vertexAddress + uint64_t(vertexIndex) * uint64_t(vertexStride) +
            uint64_t(attributeOffset),
        4u
    );
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
        uint instanceIndex = rayQuery.CommittedInstanceIndex();
        uint primitiveIndex = rayQuery.CommittedPrimitiveIndex();
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

        if (g_hit_attributes_enabled != 0u) {
            if (instanceId >= g_instance_metadata_count ||
                instanceIndex >= g_instance_metadata_count ||
                instanceId != instanceIndex) {
                DiagnosticAdd(kDiagnosticInvalidInstanceCount, 1u);
            } else {
                InstanceMetadata metadata = g_instance_metadata[instanceId];
                uint64_t vertexAddress = DeviceAddress(metadata.vertexAddress);
                uint64_t indexAddress = DeviceAddress(metadata.indexAddress);
                bool instanceMetadataValid =
                    vertexAddress != 0u &&
                    indexAddress != 0u &&
                    metadata.vertexCount > 0u &&
                    metadata.indexCount >= 3u &&
                    metadata.vertexStride == g_expected_vertex_stride;
                if (!instanceMetadataValid) {
                    DiagnosticAdd(kDiagnosticInvalidInstanceCount, 1u);
                } else if (primitiveIndex >= metadata.indexCount / 3u) {
                    DiagnosticAdd(kDiagnosticInvalidPrimitiveCount, 1u);
                } else {
                    uint firstIndex = primitiveIndex * 3u;
                    uint3 vertexIndices = uint3(
                        LoadTriangleIndex(indexAddress, firstIndex),
                        LoadTriangleIndex(indexAddress, firstIndex + 1u),
                        LoadTriangleIndex(indexAddress, firstIndex + 2u)
                    );
                    if (any(vertexIndices >= metadata.vertexCount)) {
                        DiagnosticAdd(kDiagnosticInvalidVertexCount, 1u);
                    } else {
                        float2 triangleUv =
                            rayQuery.CommittedTriangleBarycentrics();
                        float3 barycentrics = float3(
                            1.0 - triangleUv.x - triangleUv.y,
                            triangleUv.x,
                            triangleUv.y
                        );
                        float barycentricSum =
                            barycentrics.x + barycentrics.y + barycentrics.z;
                        bool barycentricsValid =
                            all(isfinite(barycentrics)) &&
                            min(barycentrics.x, min(
                                barycentrics.y,
                                barycentrics.z
                            )) >= -1.0e-4 &&
                            max(barycentrics.x, max(
                                barycentrics.y,
                                barycentrics.z
                            )) <= 1.0001 &&
                            abs(barycentricSum - 1.0) <= 1.0e-4;
                        if (!barycentricsValid) {
                            DiagnosticAdd(
                                kDiagnosticInvalidBarycentricCount,
                                1u
                            );
                        } else {
                            static const uint kPositionOffset = 0u;
                            static const uint kNormalOffset = 12u;
                            static const uint kTexCoordOffset = 36u;
                            float3 position0 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.x,
                                metadata.vertexStride,
                                kPositionOffset
                            );
                            float3 position1 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.y,
                                metadata.vertexStride,
                                kPositionOffset
                            );
                            float3 position2 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.z,
                                metadata.vertexStride,
                                kPositionOffset
                            );
                            float3 normal0 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.x,
                                metadata.vertexStride,
                                kNormalOffset
                            );
                            float3 normal1 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.y,
                                metadata.vertexStride,
                                kNormalOffset
                            );
                            float3 normal2 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.z,
                                metadata.vertexStride,
                                kNormalOffset
                            );
                            float2 texCoord0 = LoadVertexFloat2(
                                vertexAddress,
                                vertexIndices.x,
                                metadata.vertexStride,
                                kTexCoordOffset
                            );
                            float2 texCoord1 = LoadVertexFloat2(
                                vertexAddress,
                                vertexIndices.y,
                                metadata.vertexStride,
                                kTexCoordOffset
                            );
                            float2 texCoord2 = LoadVertexFloat2(
                                vertexAddress,
                                vertexIndices.z,
                                metadata.vertexStride,
                                kTexCoordOffset
                            );
                            float3 objectPosition =
                                position0 * barycentrics.x +
                                position1 * barycentrics.y +
                                position2 * barycentrics.z;
                            float3 objectNormal =
                                normal0 * barycentrics.x +
                                normal1 * barycentrics.y +
                                normal2 * barycentrics.z;
                            float2 hitTexCoord =
                                texCoord0 * barycentrics.x +
                                texCoord1 * barycentrics.y +
                                texCoord2 * barycentrics.z;
                            float4x3 objectToWorld =
                                rayQuery.CommittedObjectToWorld4x3();
                            float4x3 worldToObject =
                                rayQuery.CommittedWorldToObject4x3();
                            float3 worldPosition = mul(
                                float4(objectPosition, 1.0),
                                objectToWorld
                            );
                            float3 transformedNormal =
                                mul(worldToObject, objectNormal).xyz;
                            float transformedNormalLength =
                                length(transformedNormal);
                            float3 worldShadingNormal = transformedNormal /
                                max(transformedNormalLength, 1.0e-12);
                            bool attributesValid =
                                all(isfinite(objectPosition)) &&
                                all(isfinite(objectNormal)) &&
                                all(isfinite(hitTexCoord)) &&
                                all(isfinite(worldPosition)) &&
                                all(isfinite(worldShadingNormal)) &&
                                transformedNormalLength > 1.0e-8;
                            if (!attributesValid) {
                                DiagnosticAdd(
                                    kDiagnosticInvalidAttributeValueCount,
                                    1u
                                );
                            } else {
                                float3 tracedWorldPosition =
                                    rayDescription.Origin +
                                    rayDescription.Direction * hitDistance;
                                float positionError = length(
                                    worldPosition - tracedWorldPosition
                                );
                                float positionTolerance = max(
                                    0.002,
                                    hitDistance * 2.0e-4
                                );
                                uint normalLengthPermille = SaturatedMetric(
                                    length(worldShadingNormal),
                                    1000.0
                                );
                                uint barycentricSumPermille = SaturatedMetric(
                                    barycentricSum,
                                    1000.0
                                );
                                DiagnosticAdd(
                                    kDiagnosticHitAttributeResolvedCount,
                                    1u
                                );
                                if (metadata.materialIndex > 0u &&
                                    metadata.materialIndex <=
                                        g_instance_material_count) {
                                    DiagnosticAdd(
                                        kDiagnosticMaterialResolvedCount,
                                        1u
                                    );
                                } else {
                                    DiagnosticAdd(
                                        kDiagnosticMaterialFallbackCount,
                                        1u
                                    );
                                }
                                if (positionError > positionTolerance) {
                                    DiagnosticAdd(
                                        kDiagnosticPositionMismatchCount,
                                        1u
                                    );
                                }
                                DiagnosticMax(
                                    kDiagnosticPositionErrorMaxMicrometers,
                                    SaturatedMetric(positionError, 1000000.0)
                                );
                                DiagnosticMin(
                                    kDiagnosticNormalLengthMinPermille,
                                    normalLengthPermille
                                );
                                DiagnosticMax(
                                    kDiagnosticNormalLengthMaxPermille,
                                    normalLengthPermille
                                );
                                DiagnosticAdd(
                                    kDiagnosticIdentityChecksum,
                                    HashDiagnosticIdentity(
                                        instanceId ^
                                        (instanceIndex * 0x9e3779b9u)
                                    )
                                );
                                DiagnosticAdd(
                                    kDiagnosticPrimitiveChecksum,
                                    HashDiagnosticIdentity(
                                        primitiveIndex ^
                                        (instanceId * 0x85ebca6bu)
                                    )
                                );
                                DiagnosticAdd(
                                    kDiagnosticMaterialChecksum,
                                    HashDiagnosticIdentity(
                                        metadata.materialIndex ^
                                        (instanceId * 0xc2b2ae35u)
                                    )
                                );
                                DiagnosticMin(
                                    kDiagnosticBarycentricSumMinPermille,
                                    barycentricSumPermille
                                );
                                DiagnosticMax(
                                    kDiagnosticBarycentricSumMaxPermille,
                                    barycentricSumPermille
                                );
                            }
                        }
                    }
                }
            }
        }
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
