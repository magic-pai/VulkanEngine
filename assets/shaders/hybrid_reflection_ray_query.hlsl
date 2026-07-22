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
    uint submissionIndex;
    uint reflectionAuditObjectId;
};

struct HybridMaterialRecord {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 surfaceControls;
    float4 uvTransform;
    float4 uvControls;
    uint4 textureInfo;
    uint4 textureExtent;
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
    uint g_material_table_count;
    uint g_material_table_capacity;
    uint g_material_textures_enabled;
    uint g_material_table_contract_version;
    uint g_hit_lighting_enabled;
    uint g_hit_lighting_contract_version;
    uint g_directional_light_count;
    uint g_local_light_count;
    uint g_ibl_prefiltered_mip_count;
    uint g_hit_lighting_visibility_mode;
    uint g_ibl_resources_ready;
    uint g_hit_lighting_reserved;
    uint g_shadow_visibility_enabled;
    uint g_shadow_visibility_contract_version;
    uint g_max_shadowed_local_lights;
    uint g_rectangle_shadow_sample_count;
    uint g_denoiser_injection_enabled;
    uint g_denoiser_bridge_contract_version;
    uint g_diagnostic_target_instance_index;
    uint g_ray_query_runtime_flags;
};

[[vk::binding(10, 1)]] globallycoherent RWStructuredBuffer<uint>
    g_ray_query_diagnostics;

[[vk::binding(11, 1)]] StructuredBuffer<InstanceMetadata>
    g_instance_metadata;
[[vk::binding(12, 1)]] StructuredBuffer<HybridMaterialRecord>
    g_material_table;
[[vk::binding(13, 1)]] Texture2D<float4>
    g_material_albedo_textures[256];
[[vk::binding(14, 1)]] SamplerState g_material_samplers[256];
[[vk::binding(15, 1), vk::image_format("rgba16f")]]
RWTexture2D<float4> g_hit_surface_payload;
[[vk::binding(16, 1)]] ByteAddressBuffer g_light_data;
[[vk::binding(17, 1)]] Texture2D<float2> g_ibl_brdf_lut;
[[vk::binding(18, 1)]] TextureCube<float4> g_ibl_irradiance;
[[vk::binding(19, 1)]] TextureCube<float4> g_ibl_prefiltered;
[[vk::binding(20, 1)]] SamplerState g_ibl_sampler;
[[vk::binding(21, 1)]] RWTexture2D<float4> g_denoiser_radiance;
[[vk::binding(22, 1)]] RWTexture2D<float> g_denoiser_hit_confidence;
#if defined(SE_HYBRID_REFLECTION_FULL_AUDIT)
[[vk::binding(23, 1)]] Texture2D<uint> g_reflection_audit_object_id;
#endif

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
static const uint kDiagnosticMaterialRecordResolvedCount = 27u;
static const uint kDiagnosticMaterialRecordFallbackCount = 28u;
static const uint kDiagnosticTextureSampleResolvedCount = 29u;
static const uint kDiagnosticTextureSampleFallbackCount = 30u;
static const uint kDiagnosticTextureSampleInvalidCount = 31u;
static const uint kDiagnosticFiniteSampledColorCount = 32u;
static const uint kDiagnosticSampleLodMinMillilevels = 33u;
static const uint kDiagnosticSampleLodMaxMillilevels = 34u;
static const uint kDiagnosticHitSurfacePayloadWriteCount = 35u;
static const uint kDiagnosticHitSurfacePayloadChecksum = 36u;
static const uint kDiagnosticHitSurfaceLuminanceMinMilliunits = 37u;
static const uint kDiagnosticHitSurfaceLuminanceMaxMilliunits = 38u;
static const uint kDiagnosticHitLightingResolvedCount = 39u;
static const uint kDiagnosticHitLightingInvalidCount = 40u;
static const uint kDiagnosticDirectionalLightEvaluationCount = 41u;
static const uint kDiagnosticDirectionalLightContributionCount = 42u;
static const uint kDiagnosticPointLightEvaluationCount = 43u;
static const uint kDiagnosticPointLightContributionCount = 44u;
static const uint kDiagnosticSpotLightEvaluationCount = 45u;
static const uint kDiagnosticSpotLightContributionCount = 46u;
static const uint kDiagnosticRectLightEvaluationCount = 47u;
static const uint kDiagnosticRectLightContributionCount = 48u;
static const uint kDiagnosticFiniteDirectRadianceCount = 49u;
static const uint kDiagnosticFiniteIblRadianceCount = 50u;
static const uint kDiagnosticFiniteEmissiveRadianceCount = 51u;
static const uint kDiagnosticFiniteRadianceCount = 52u;
static const uint kDiagnosticDirectLuminanceSumMilliunits = 53u;
static const uint kDiagnosticIblLuminanceSumMilliunits = 54u;
static const uint kDiagnosticEmissiveLuminanceSumMilliunits = 55u;
static const uint kDiagnosticRadianceLuminanceMinMilliunits = 56u;
static const uint kDiagnosticRadianceLuminanceMaxMilliunits = 57u;
static const uint kDiagnosticRadianceChecksum = 58u;
static const uint kDiagnosticShadowVisibilityResolvedCount = 59u;
static const uint kDiagnosticShadowRayCount = 60u;
static const uint kDiagnosticShadowVisibleCount = 61u;
static const uint kDiagnosticShadowOccludedCount = 62u;
static const uint kDiagnosticShadowInvalidCount = 63u;
static const uint kDiagnosticDirectionalShadowRayCount = 64u;
static const uint kDiagnosticPointShadowRayCount = 65u;
static const uint kDiagnosticSpotShadowRayCount = 66u;
static const uint kDiagnosticRectShadowRayCount = 67u;
static const uint kDiagnosticLocalShadowCandidateCount = 68u;
static const uint kDiagnosticLocalShadowSelectedCount = 69u;
static const uint kDiagnosticLocalShadowDroppedCount = 70u;
static const uint kDiagnosticUnshadowedDirectLuminanceSumMilliunits = 71u;
static const uint kDiagnosticVisibleDirectLuminanceSumMilliunits = 72u;
static const uint kDiagnosticShadowSelfIntersectionCandidateCount = 73u;
static const uint kDiagnosticShadowHitDistanceMinMillimeters = 74u;
static const uint kDiagnosticShadowHitDistanceMaxMillimeters = 75u;
static const uint kDiagnosticShadowVisibilityMinPermille = 76u;
static const uint kDiagnosticShadowVisibilityMaxPermille = 77u;
static const uint kDiagnosticLocalShadowDroppedLuminanceSumMilliunits = 78u;
static const uint kDiagnosticDenoiserInjectionResolvedCount = 79u;
static const uint kDiagnosticDenoiserRadiancePixelWriteCount = 80u;
static const uint kDiagnosticDenoiserConfidencePixelWriteCount = 81u;
static const uint kDiagnosticDenoiserConfidenceSumPermille = 82u;
static const uint kDiagnosticTargetCommittedHitCount = 83u;
static const uint kDiagnosticTargetAttributeResolvedCount = 84u;
static const uint kDiagnosticTargetDenoiserWriteCount = 85u;

static const uint kRayQueryRuntimeForceAllRaysBit = 1u << 0u;
static const uint kRayQueryRuntimeDisableBackFaceCullBit = 1u << 1u;
static const uint kRayQueryRuntimeTargetAttributionBit = 1u << 2u;
static const uint kRayQueryRuntimeFullAuditBit = 1u << 3u;

static const uint kFullAuditHeaderWordCount = 128u;
static const uint kFullAuditInstanceCounterCount = 8u;
static const uint kFullAuditRayRecordWordCount = 24u;
static const uint kFullAuditMaxRayRecords = 1048576u;
static const uint kFullAuditInstanceCounterBase =
    kFullAuditHeaderWordCount;
static const uint kFullAuditRayRecordBase =
    kFullAuditInstanceCounterBase + 4096u *
        kFullAuditInstanceCounterCount;
static const uint kFullAuditFlagCoordinatesValid = 1u << 0u;
static const uint kFullAuditFlagGBufferValid = 1u << 1u;
static const uint kFullAuditFlagScreenAccepted = 1u << 2u;
static const uint kFullAuditFlagProductionTrace = 1u << 3u;
static const uint kFullAuditFlagCommittedHit = 1u << 4u;
static const uint kFullAuditFlagAttributesResolved = 1u << 5u;
static const uint kFullAuditFlagMaterialResolved = 1u << 6u;
static const uint kFullAuditFlagLightingResolved = 1u << 7u;
static const uint kFullAuditFlagDenoiserWrite = 1u << 8u;
static const uint kFullAuditFlagReceiverResolved = 1u << 9u;
static const uint kFullAuditFlagAuditOnlyTrace = 1u << 10u;
static const uint kFullAuditFlagObjectIdValid = 1u << 11u;
static const uint kFullAuditFlagObjectIdMappedToTlas = 1u << 12u;
static const uint kFullAuditFlagSelfHit = 1u << 13u;
static const uint kFullAuditFlagNegativeHemisphere = 1u << 14u;
static const uint kFullAuditInvalidIndex = 0xffffffffu;

static const float kPi = 3.14159265359;
static const uint kMaxLocalLights = 64u;
static const uint kMaxShadowedLocalLights = 8u;
static const uint kLightDirectionalOffset = 0u;
static const uint kLightAmbientOffset = 16u;
static const uint kLightCountsOffset = 32u;
static const uint kLocalLightsOffset = 64u;
static const uint kLocalLightStride = 64u;

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

bool FullAuditEnabled() {
    return g_ray_query_diagnostics_enabled != 0u &&
        (g_ray_query_runtime_flags & kRayQueryRuntimeFullAuditBit) != 0u;
}

uint FullAuditRecordBase(uint rayIndex) {
    return kFullAuditRayRecordBase +
        rayIndex * kFullAuditRayRecordWordCount;
}

void FullAuditStore(uint rayIndex, uint wordOffset, uint value) {
    if (FullAuditEnabled() && rayIndex < kFullAuditMaxRayRecords) {
        g_ray_query_diagnostics[
            FullAuditRecordBase(rayIndex) + wordOffset
        ] = value;
    }
}

void FullAuditStoreFloat(uint rayIndex, uint wordOffset, float value) {
    FullAuditStore(rayIndex, wordOffset, asuint(value));
}

void FullAuditAddFlags(uint rayIndex, uint flags) {
    if (FullAuditEnabled() && rayIndex < kFullAuditMaxRayRecords) {
        InterlockedOr(
            g_ray_query_diagnostics[FullAuditRecordBase(rayIndex) + 1u],
            flags
        );
    }
}

void FullAuditInstanceAdd(uint instanceId, uint counter, uint value) {
    if (FullAuditEnabled() && instanceId < g_instance_metadata_count &&
        instanceId < 4096u && counter < kFullAuditInstanceCounterCount) {
        InterlockedAdd(
            g_ray_query_diagnostics[
                kFullAuditInstanceCounterBase +
                instanceId * kFullAuditInstanceCounterCount + counter
            ],
            value
        );
    }
}

void FullAuditInitialize(
    uint rayIndex,
    uint2 coordinates,
    bool copyHorizontal,
    bool copyVertical,
    bool copyDiagonal
) {
    if (!FullAuditEnabled() || rayIndex >= kFullAuditMaxRayRecords) {
        return;
    }
    uint copyFlags =
        (copyHorizontal ? 1u << 16u : 0u) |
        (copyVertical ? 1u << 17u : 0u) |
        (copyDiagonal ? 1u << 18u : 0u);
    FullAuditStore(
        rayIndex,
        0u,
        (coordinates.x & 0xffffu) | (coordinates.y << 16u)
    );
    FullAuditStore(rayIndex, 1u, copyFlags);
    FullAuditStore(rayIndex, 2u, kFullAuditInvalidIndex);
    FullAuditStore(rayIndex, 4u, kFullAuditInvalidIndex);
    FullAuditStore(rayIndex, 5u, kFullAuditInvalidIndex);
    FullAuditStore(rayIndex, 7u, 0u);
    FullAuditStore(rayIndex, 20u, 0u);
    FullAuditStoreFloat(rayIndex, 23u, -1.0);
}

uint ReflectionTraversalFlags() {
    bool disableBackFaceCull =
        (g_ray_query_runtime_flags &
            kRayQueryRuntimeDisableBackFaceCullBit) != 0u;
    uint traversalFlags = disableBackFaceCull
        ? RAY_FLAG_NONE
        : g_ray_query_contract_version >= 3u
            ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES
            : RAY_FLAG_CULL_FRONT_FACING_TRIANGLES;
#if defined(SE_HYBRID_REFLECTION_DIAGNOSTIC_DISABLE_BACK_FACE_CULL)
    traversalFlags = RAY_FLAG_NONE;
#endif
    return traversalFlags;
}

uint ResolveFullAuditReceiver(uint2 coordinates, out uint rawObjectId) {
    rawObjectId = 0u;
#if defined(SE_HYBRID_REFLECTION_FULL_AUDIT)
    if (!FullAuditEnabled()) {
        return kFullAuditInvalidIndex;
    }
    rawObjectId = g_reflection_audit_object_id.Load(int3(coordinates, 0));
    if (rawObjectId == 0u) {
        return kFullAuditInvalidIndex;
    }
    for (uint instanceId = 0u; instanceId < g_instance_metadata_count;
        ++instanceId) {
        if (g_instance_metadata[instanceId].reflectionAuditObjectId ==
            rawObjectId) {
            return instanceId;
        }
    }
#endif
    return kFullAuditInvalidIndex;
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

uint BoundedLuminanceMetric(float3 radiance) {
    float luminance = dot(max(radiance, 0.0), float3(0.2126, 0.7152, 0.0722));
    return min(SaturatedMetric(luminance, 1000.0), 1000000u);
}

struct HitLocalLightRecord {
    float4 positionRadius;
    float4 colorIntensity;
    float4 directionType;
    float4 parameters;
};

float4 LoadLightFloat4(uint byteOffset) {
    return asfloat(g_light_data.Load4(byteOffset));
}

HitLocalLightRecord LoadLocalLight(uint lightIndex) {
    uint baseOffset = kLocalLightsOffset + lightIndex * kLocalLightStride;
    HitLocalLightRecord light;
    light.positionRadius = LoadLightFloat4(baseOffset);
    light.colorIntensity = LoadLightFloat4(baseOffset + 16u);
    light.directionType = LoadLightFloat4(baseOffset + 32u);
    light.parameters = LoadLightFloat4(baseOffset + 48u);
    return light;
}

float DistributionGgx(float3 normal, float3 halfDirection, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float nDotH = saturate(dot(normal, halfDirection));
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPi * denominator * denominator, 1.0e-6);
}

float GeometrySchlickGgx(float nDotDirection, float roughness) {
    float r = roughness + 1.0;
    float k = r * r * 0.125;
    return nDotDirection /
        max(nDotDirection * (1.0 - k) + k, 1.0e-6);
}

float GeometrySmithGgx(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float roughness
) {
    return GeometrySchlickGgx(saturate(dot(normal, viewDirection)), roughness) *
        GeometrySchlickGgx(saturate(dot(normal, lightDirection)), roughness);
}

float3 FresnelSchlickGgx(float cosine, float3 f0) {
    float factor = saturate(1.0 - cosine);
    float factorSquared = factor * factor;
    float factorFifth = factorSquared * factorSquared * factor;
    return f0 + (1.0 - f0) * factorFifth;
}

float3 FresnelSchlickRoughnessGgx(
    float cosine,
    float3 f0,
    float roughness
) {
    float factor = saturate(1.0 - cosine);
    float factorSquared = factor * factor;
    float factorFifth = factorSquared * factorSquared * factor;
    return f0 + (max(1.0 - roughness, f0) - f0) * factorFifth;
}

float3 EvaluateDirectBrdf(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 radiance
) {
    float nDotL = saturate(dot(normal, lightDirection));
    float nDotV = saturate(dot(normal, viewDirection));
    if (nDotL <= 1.0e-6 || nDotV <= 1.0e-6) {
        return 0.0;
    }

    float3 halfVector = viewDirection + lightDirection;
    float halfLengthSquared = dot(halfVector, halfVector);
    if (halfLengthSquared <= 1.0e-8) {
        return 0.0;
    }
    float3 halfDirection = halfVector * rsqrt(halfLengthSquared);
    float3 f0 = lerp(0.04, baseColor, metallic);
    float3 fresnel = FresnelSchlickGgx(
        saturate(dot(halfDirection, viewDirection)),
        f0
    );
    float distribution = DistributionGgx(normal, halfDirection, roughness);
    float geometry = GeometrySmithGgx(
        normal,
        viewDirection,
        lightDirection,
        roughness
    );
    float3 specular = distribution * geometry * fresnel /
        max(4.0 * nDotV * nDotL, 1.0e-6);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) *
        baseColor / kPi;
    return (diffuse + specular) * radiance * nDotL;
}

float FiniteRadiusAttenuation(float distanceToLight, float radius) {
    float normalizedDistance = saturate(distanceToLight / max(radius, 0.001));
    float window = 1.0 - normalizedDistance * normalizedDistance;
    return window * window;
}

float OffsetRayOriginComponent(float position, float normal) {
    static const float kOrigin = 1.0 / 32.0;
    static const float kFloatScale = 1.0 / 65536.0;
    static const int kIntegerScale = 256;
    int integerOffset = int(float(kIntegerScale) * normal);
    float integerPosition = asfloat(
        asint(position) + (position < 0.0 ? -integerOffset : integerOffset)
    );
    return abs(position) < kOrigin
        ? position + kFloatScale * normal
        : integerPosition;
}

float3 OffsetRayOrigin(float3 position, float3 normal) {
    return float3(
        OffsetRayOriginComponent(position.x, normal.x),
        OffsetRayOriginComponent(position.y, normal.y),
        OffsetRayOriginComponent(position.z, normal.z)
    );
}

float TraceShadowVisibility(
    float3 worldPosition,
    float3 worldNormal,
    float3 lightDirection,
    float maximumDistance,
    uint sourceInstanceId,
    uint lightKind
) {
    DiagnosticAdd(kDiagnosticShadowRayCount, 1u);
    DiagnosticAdd(
        lightKind == 0u
            ? kDiagnosticDirectionalShadowRayCount
            : lightKind == 1u
                ? kDiagnosticPointShadowRayCount
                : lightKind == 2u
                    ? kDiagnosticSpotShadowRayCount
                    : kDiagnosticRectShadowRayCount,
        1u
    );
    if (!all(isfinite(worldPosition)) ||
        !all(isfinite(worldNormal)) ||
        !all(isfinite(lightDirection)) ||
        !isfinite(maximumDistance) ||
        dot(lightDirection, lightDirection) < 0.99 ||
        maximumDistance <= 1.0e-4) {
        DiagnosticAdd(kDiagnosticShadowInvalidCount, 1u);
        return 0.0;
    }

    float3 offsetNormal = dot(worldNormal, lightDirection) >= 0.0
        ? worldNormal
        : -worldNormal;
    RayDesc shadowRay;
    shadowRay.Origin = OffsetRayOrigin(worldPosition, offsetNormal);
    float originOffsetDistance = length(
        shadowRay.Origin - worldPosition
    );
    shadowRay.TMin = max(originOffsetDistance * 2.0, 1.0e-6);
    shadowRay.Direction = lightDirection;
    shadowRay.TMax = maximumDistance;
    RayQuery<
        RAY_FLAG_FORCE_OPAQUE |
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
    > shadowQuery;
    shadowQuery.TraceRayInline(
        g_scene_acceleration_structure,
        RAY_FLAG_NONE,
        0xffu,
        shadowRay
    );
    while (shadowQuery.Proceed()) {
    }

    if (shadowQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        float hitDistance = shadowQuery.CommittedRayT();
        uint hitDistanceMillimeters = SaturatedMetric(hitDistance, 1000.0);
        DiagnosticAdd(kDiagnosticShadowOccludedCount, 1u);
        DiagnosticMin(
            kDiagnosticShadowHitDistanceMinMillimeters,
            hitDistanceMillimeters
        );
        DiagnosticMax(
            kDiagnosticShadowHitDistanceMaxMillimeters,
            hitDistanceMillimeters
        );
        if (shadowQuery.CommittedInstanceID() == sourceInstanceId &&
            hitDistance <= shadowRay.TMin) {
            DiagnosticAdd(
                kDiagnosticShadowSelfIntersectionCandidateCount,
                1u
            );
        }
        return 0.0;
    }

    DiagnosticAdd(kDiagnosticShadowVisibleCount, 1u);
    return 1.0;
}

float3 EvaluatePointOrSpotLight(
    HitLocalLightRecord light,
    uint lightKind,
    float3 baseColor,
    float metallic,
    float roughness,
    float3 normal,
    float3 viewDirection,
    float3 worldPosition
) {
    float3 toLight = light.positionRadius.xyz - worldPosition;
    float distanceToLight = length(toLight);
    float radius = max(light.positionRadius.w, 0.001);
    if (distanceToLight >= radius || light.colorIntensity.w <= 0.0) {
        return 0.0;
    }

    float3 lightDirection = toLight / max(distanceToLight, 0.001);
    float attenuation = FiniteRadiusAttenuation(distanceToLight, radius);
    if (lightKind == 1u) {
        float3 spotDirection = light.directionType.xyz;
        spotDirection = dot(spotDirection, spotDirection) > 1.0e-4
            ? normalize(spotDirection)
            : float3(0.0, -1.0, 0.0);
        float innerCone = max(light.parameters.x, light.parameters.y);
        float outerCone = min(light.parameters.x, light.parameters.y);
        float coneCosine = dot(
            normalize(worldPosition - light.positionRadius.xyz),
            spotDirection
        );
        attenuation *= saturate(
            (coneCosine - outerCone) / max(innerCone - outerCone, 1.0e-4)
        );
    }
    float3 radiance = max(light.colorIntensity.rgb, 0.0) *
        max(light.colorIntensity.w, 0.0) * attenuation;
    return EvaluateDirectBrdf(
        baseColor,
        metallic,
        roughness,
        normal,
        viewDirection,
        lightDirection,
        radiance
    );
}

float3 EvaluateRectLight(
    HitLocalLightRecord light,
    float3 baseColor,
    float metallic,
    float roughness,
    float3 normal,
    float3 shadowOriginNormal,
    float3 viewDirection,
    float3 worldPosition,
    bool traceVisibility,
    uint sourceInstanceId
) {
    if (light.colorIntensity.w <= 0.0) {
        return 0.0;
    }

    float3 rectNormal = light.directionType.xyz;
    rectNormal = dot(rectNormal, rectNormal) > 1.0e-4
        ? normalize(rectNormal)
        : float3(0.0, -1.0, 0.0);
    float3 referenceAxis = abs(rectNormal.y) > 0.95
        ? float3(1.0, 0.0, 0.0)
        : float3(0.0, 1.0, 0.0);
    float3 rectTangent = normalize(cross(referenceAxis, rectNormal));
    float3 rectBitangent = normalize(cross(rectNormal, rectTangent));
    float2 halfSize = max(light.parameters.zw * 0.5, 0.001);
    float radius = max(light.positionRadius.w, length(halfSize));
    const float2 sampleSigns[4] = {
        float2(-0.57735, -0.57735),
        float2(0.57735, -0.57735),
        float2(-0.57735, 0.57735),
        float2(0.57735, 0.57735)
    };

    uint sampleCount = clamp(g_rectangle_shadow_sample_count, 1u, 4u);
    float sampleWeight = rcp(float(sampleCount));
    float3 result = 0.0;
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex) {
        if (sampleIndex >= sampleCount) {
            continue;
        }
        float3 samplePosition = light.positionRadius.xyz +
            rectTangent * halfSize.x * sampleSigns[sampleIndex].x +
            rectBitangent * halfSize.y * sampleSigns[sampleIndex].y;
        float3 toLight = samplePosition - worldPosition;
        float distanceToLight = length(toLight);
        if (distanceToLight >= radius) {
            continue;
        }
        float3 lightDirection = toLight / max(distanceToLight, 0.001);
        float emitterFacing = saturate(dot(-lightDirection, rectNormal));
        float attenuation = FiniteRadiusAttenuation(distanceToLight, radius) *
            emitterFacing;
        float3 radiance = max(light.colorIntensity.rgb, 0.0) *
            max(light.colorIntensity.w, 0.0) * attenuation * sampleWeight;
        float visibility = 1.0;
        if (traceVisibility) {
            float endpointContraction = max(0.001, distanceToLight * 1.0e-4);
            visibility = TraceShadowVisibility(
                worldPosition,
                shadowOriginNormal,
                lightDirection,
                distanceToLight - endpointContraction,
                sourceInstanceId,
                3u
            );
        }
        result += EvaluateDirectBrdf(
            baseColor,
            metallic,
            roughness,
            normal,
            viewDirection,
            lightDirection,
            radiance * visibility
        );
    }
    return result;
}

bool EvaluateHitRadiance(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 emissive,
    float3 normal,
    float3 geometricNormal,
    float3 viewDirection,
    float3 worldPosition,
    uint sourceInstanceId,
    out float3 directRadiance,
    out float3 iblRadiance,
    out float3 finalRadiance
) {
    directRadiance = 0.0;
    iblRadiance = 0.0;
    finalRadiance = 0.0;
    if (g_ibl_resources_ready == 0u ||
        (g_hit_lighting_visibility_mode != 1u &&
            g_hit_lighting_visibility_mode != 2u)) {
        return false;
    }

    normal = dot(normal, viewDirection) >= 0.0 ? normal : -normal;
    geometricNormal = dot(geometricNormal, viewDirection) >= 0.0
        ? geometricNormal
        : -geometricNormal;
    bool shadowVisibilityEnabled =
        g_shadow_visibility_enabled != 0u &&
        g_hit_lighting_visibility_mode == 2u;
    float4 directionalLight = LoadLightFloat4(kLightDirectionalOffset);
    float4 ambientLight = LoadLightFloat4(kLightAmbientOffset);
    float4 lightCounts = LoadLightFloat4(kLightCountsOffset);
    uint directionalCount = min(
        g_directional_light_count,
        (uint)clamp(lightCounts.y + 0.5, 0.0, 1.0)
    );
    uint localLightCount = min(
        min(g_local_light_count, kMaxLocalLights),
        (uint)clamp(lightCounts.z + 0.5, 0.0, float(kMaxLocalLights))
    );
    float3 unshadowedDirectRadiance = 0.0;
    float3 totalLocalCandidateRadiance = 0.0;
    float candidateScores[kMaxShadowedLocalLights];
    float3 candidateContributions[kMaxShadowedLocalLights];
    uint candidateLightIndices[kMaxShadowedLocalLights];
    uint candidateLightKinds[kMaxShadowedLocalLights];
    [unroll]
    for (uint candidateSlot = 0u;
        candidateSlot < kMaxShadowedLocalLights;
        ++candidateSlot) {
        candidateScores[candidateSlot] = 0.0;
        candidateContributions[candidateSlot] = 0.0;
        candidateLightIndices[candidateSlot] = 0u;
        candidateLightKinds[candidateSlot] = 0u;
    }
    uint localCandidateCount = 0u;
    uint localShadowBudget = clamp(
        g_max_shadowed_local_lights,
        1u,
        kMaxShadowedLocalLights
    );

    if (directionalCount > 0u) {
        DiagnosticAdd(kDiagnosticDirectionalLightEvaluationCount, 1u);
        float3 lightDirection = directionalLight.xyz;
        lightDirection = dot(lightDirection, lightDirection) > 1.0e-4
            ? normalize(-lightDirection)
            : normalize(float3(0.45, 0.82, 0.35));
        float3 contribution = EvaluateDirectBrdf(
            baseColor,
            metallic,
            roughness,
            normal,
            viewDirection,
            lightDirection,
            max(directionalLight.w, 0.0)
        );
        if (dot(contribution, contribution) > 1.0e-12) {
            DiagnosticAdd(kDiagnosticDirectionalLightContributionCount, 1u);
            if (shadowVisibilityEnabled) {
                unshadowedDirectRadiance += contribution;
                float visibility = TraceShadowVisibility(
                    worldPosition,
                    geometricNormal,
                    lightDirection,
                    max(g_max_ray_distance, 0.01),
                    sourceInstanceId,
                    0u
                );
                directRadiance += contribution * visibility;
            } else {
                directRadiance += contribution;
            }
        }
    }

    [loop]
    for (uint lightIndex = 0u; lightIndex < localLightCount; ++lightIndex) {
        HitLocalLightRecord light = LoadLocalLight(lightIndex);
        uint lightKind = (uint)clamp(light.directionType.w + 0.5, 0.0, 2.0);
        float3 contribution = 0.0;
        if (lightKind == 2u) {
            DiagnosticAdd(kDiagnosticRectLightEvaluationCount, 1u);
            contribution = EvaluateRectLight(
                light,
                baseColor,
                metallic,
                roughness,
                normal,
                geometricNormal,
                viewDirection,
                worldPosition,
                false,
                sourceInstanceId
            );
            if (dot(contribution, contribution) > 1.0e-12) {
                DiagnosticAdd(kDiagnosticRectLightContributionCount, 1u);
            }
        } else {
            if (lightKind == 1u) {
                DiagnosticAdd(kDiagnosticSpotLightEvaluationCount, 1u);
            } else {
                DiagnosticAdd(kDiagnosticPointLightEvaluationCount, 1u);
            }
            contribution = EvaluatePointOrSpotLight(
                light,
                lightKind,
                baseColor,
                metallic,
                roughness,
                normal,
                viewDirection,
                worldPosition
            );
            if (dot(contribution, contribution) > 1.0e-12) {
                DiagnosticAdd(
                    lightKind == 1u
                        ? kDiagnosticSpotLightContributionCount
                        : kDiagnosticPointLightContributionCount,
                    1u
                );
            }
        }
        if (dot(contribution, contribution) > 1.0e-12) {
            if (!shadowVisibilityEnabled) {
                directRadiance += contribution;
                continue;
            }

            ++localCandidateCount;
            totalLocalCandidateRadiance += contribution;
            float candidateScore = dot(
                contribution,
                float3(0.2126, 0.7152, 0.0722)
            );
            uint insertionSlot = localShadowBudget;
            [unroll]
            for (uint candidateSlot = 0u;
                candidateSlot < kMaxShadowedLocalLights;
                ++candidateSlot) {
                if (candidateSlot < localShadowBudget &&
                    insertionSlot == localShadowBudget &&
                    candidateScore > candidateScores[candidateSlot]) {
                    insertionSlot = candidateSlot;
                }
            }
            if (insertionSlot < localShadowBudget) {
                [unroll]
                for (int shiftSlot = int(kMaxShadowedLocalLights) - 1;
                    shiftSlot > 0;
                    --shiftSlot) {
                    if (uint(shiftSlot) < localShadowBudget &&
                        uint(shiftSlot) > insertionSlot) {
                        candidateScores[shiftSlot] =
                            candidateScores[shiftSlot - 1];
                        candidateContributions[shiftSlot] =
                            candidateContributions[shiftSlot - 1];
                        candidateLightIndices[shiftSlot] =
                            candidateLightIndices[shiftSlot - 1];
                        candidateLightKinds[shiftSlot] =
                            candidateLightKinds[shiftSlot - 1];
                    }
                }
                candidateScores[insertionSlot] = candidateScore;
                candidateContributions[insertionSlot] = contribution;
                candidateLightIndices[insertionSlot] = lightIndex;
                candidateLightKinds[insertionSlot] = lightKind;
            }
        }
    }

    if (shadowVisibilityEnabled) {
        uint selectedLocalCount = min(localCandidateCount, localShadowBudget);
        float3 selectedLocalRadiance = 0.0;
        [loop]
        for (uint candidateSlot = 0u;
            candidateSlot < selectedLocalCount;
            ++candidateSlot) {
            uint lightIndex = candidateLightIndices[candidateSlot];
            uint lightKind = candidateLightKinds[candidateSlot];
            HitLocalLightRecord light = LoadLocalLight(lightIndex);
            float3 unshadowedContribution =
                candidateContributions[candidateSlot];
            selectedLocalRadiance += unshadowedContribution;
            unshadowedDirectRadiance += unshadowedContribution;
            if (lightKind == 2u) {
                directRadiance += EvaluateRectLight(
                    light,
                    baseColor,
                    metallic,
                    roughness,
                    normal,
                    geometricNormal,
                    viewDirection,
                    worldPosition,
                    true,
                    sourceInstanceId
                );
            } else {
                float3 toLight = light.positionRadius.xyz - worldPosition;
                float distanceToLight = length(toLight);
                float3 lightDirection = toLight /
                    max(distanceToLight, 0.001);
                float endpointContraction = max(
                    0.001,
                    distanceToLight * 1.0e-4
                );
                float visibility = TraceShadowVisibility(
                    worldPosition,
                    geometricNormal,
                    lightDirection,
                    distanceToLight - endpointContraction,
                    sourceInstanceId,
                    lightKind == 1u ? 2u : 1u
                );
                directRadiance += unshadowedContribution * visibility;
            }
        }

        uint droppedLocalCount = localCandidateCount - selectedLocalCount;
        float3 droppedLocalRadiance = max(
            totalLocalCandidateRadiance - selectedLocalRadiance,
            0.0
        );
        float unshadowedLuminance = dot(
            max(unshadowedDirectRadiance, 0.0),
            float3(0.2126, 0.7152, 0.0722)
        );
        float visibleLuminance = dot(
            max(directRadiance, 0.0),
            float3(0.2126, 0.7152, 0.0722)
        );
        float visibilityRatio = unshadowedLuminance > 1.0e-8
            ? saturate(visibleLuminance / unshadowedLuminance)
            : 1.0;
        uint visibilityPermille = SaturatedMetric(visibilityRatio, 1000.0);
        DiagnosticAdd(kDiagnosticShadowVisibilityResolvedCount, 1u);
        DiagnosticAdd(
            kDiagnosticLocalShadowCandidateCount,
            localCandidateCount
        );
        DiagnosticAdd(
            kDiagnosticLocalShadowSelectedCount,
            selectedLocalCount
        );
        DiagnosticAdd(
            kDiagnosticLocalShadowDroppedCount,
            droppedLocalCount
        );
        DiagnosticAdd(
            kDiagnosticUnshadowedDirectLuminanceSumMilliunits,
            BoundedLuminanceMetric(unshadowedDirectRadiance)
        );
        DiagnosticAdd(
            kDiagnosticVisibleDirectLuminanceSumMilliunits,
            BoundedLuminanceMetric(directRadiance)
        );
        DiagnosticAdd(
            kDiagnosticLocalShadowDroppedLuminanceSumMilliunits,
            BoundedLuminanceMetric(droppedLocalRadiance)
        );
        DiagnosticMin(
            kDiagnosticShadowVisibilityMinPermille,
            visibilityPermille
        );
        DiagnosticMax(
            kDiagnosticShadowVisibilityMaxPermille,
            visibilityPermille
        );
    }

    float nDotV = saturate(dot(normal, viewDirection));
    float3 f0 = lerp(0.04, baseColor, metallic);
    float3 environmentFresnel = FresnelSchlickRoughnessGgx(
        nDotV,
        f0,
        roughness
    );
    float3 diffuseWeight = (1.0 - environmentFresnel) * (1.0 - metallic);
    float3 reflectionDirection = reflect(-viewDirection, normal);
    float2 environmentBrdf = max(
        g_ibl_brdf_lut.SampleLevel(
            g_ibl_sampler,
            float2(nDotV, roughness),
            0.0
        ),
        0.0
    );
    float3 diffuseEnvironment = max(
        g_ibl_irradiance.SampleLevel(g_ibl_sampler, normal, 0.0).rgb,
        0.0
    );
    float prefilteredLod = roughness *
        float(max(g_ibl_prefiltered_mip_count, 1u) - 1u);
    float3 specularEnvironment = max(
        g_ibl_prefiltered.SampleLevel(
            g_ibl_sampler,
            reflectionDirection,
            prefilteredLod
        ).rgb,
        0.0
    );
    iblRadiance = diffuseWeight * baseColor * diffuseEnvironment *
        max(ambientLight.x, 0.0) +
        specularEnvironment *
        max(f0 * environmentBrdf.x + environmentBrdf.y, 0.0) *
        max(ambientLight.y, 0.0);
    finalRadiance = directRadiance + iblRadiance + max(emissive, 0.0);
    return all(isfinite(directRadiance)) &&
        all(isfinite(iblRadiance)) &&
        all(isfinite(emissive)) &&
        all(isfinite(finalRadiance));
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

float2 TransformMaterialUv(float2 uv, HybridMaterialRecord material) {
    if (material.uvControls.y < 0.5) {
        return uv;
    }

    float2 transformed = uv * material.uvTransform.zw;
    float rotation = material.uvControls.x;
    if (abs(rotation) > 1.0e-5) {
        float cosine = cos(rotation);
        float sine = sin(rotation);
        transformed = float2(
            cosine * transformed.x - sine * transformed.y,
            sine * transformed.x + cosine * transformed.y
        );
    }
    return transformed + material.uvTransform.xy;
}

float EstimateMaterialTextureLod(
    float3 worldPosition0,
    float3 worldPosition1,
    float3 worldPosition2,
    float2 texCoord0,
    float2 texCoord1,
    float2 texCoord2,
    float hitDistance,
    uint2 textureExtent,
    uint mipCount
) {
    float worldDoubleArea = length(cross(
        worldPosition1 - worldPosition0,
        worldPosition2 - worldPosition0
    ));
    float2 uvEdge0 = texCoord1 - texCoord0;
    float2 uvEdge1 = texCoord2 - texCoord0;
    float uvDoubleArea = abs(
        uvEdge0.x * uvEdge1.y - uvEdge0.y * uvEdge1.x
    );
    if (worldDoubleArea <= 1.0e-12 || uvDoubleArea <= 1.0e-12) {
        return 0.0;
    }

    float texturePixels = max(
        float(textureExtent.x) * float(textureExtent.y),
        1.0
    );
    float texelsPerWorldUnit = sqrt(
        uvDoubleArea * texturePixels / worldDoubleArea
    );
    float projectionScale = max(abs(g_proj[1][1]), 1.0e-4);
    float pixelConeWidth = max(
        2.0 * max(hitDistance, 0.0) /
            (max(float(g_buffer_dimensions.y), 1.0) * projectionScale),
        1.0e-6
    );
    float lod = log2(max(pixelConeWidth * texelsPerWorldUnit, 1.0));
    return clamp(lod, 0.0, float(max(mipCount, 1u) - 1u));
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

void StoreHitSurfacePayload(
    uint2 coordinates,
    bool copyHorizontal,
    bool copyVertical,
    bool copyDiagonal,
    float4 payload
) {
    g_hit_surface_payload[coordinates] = payload;
    uint2 copyTarget = coordinates ^ 1u;
    if (copyHorizontal && copyTarget.x < g_buffer_dimensions.x) {
        g_hit_surface_payload[uint2(copyTarget.x, coordinates.y)] = payload;
    }
    if (copyVertical && copyTarget.y < g_buffer_dimensions.y) {
        g_hit_surface_payload[uint2(coordinates.x, copyTarget.y)] = payload;
    }
    if (copyDiagonal && all(copyTarget < g_buffer_dimensions)) {
        g_hit_surface_payload[copyTarget] = payload;
    }
}

void StoreDenoiserPayload(
    uint2 coordinates,
    bool copyHorizontal,
    bool copyVertical,
    bool copyDiagonal,
    float4 payload
) {
    if (g_denoiser_injection_enabled == 0u) {
        return;
    }

    g_denoiser_radiance[coordinates] = payload;
    g_denoiser_hit_confidence[coordinates] = 1.0;
    uint writeCount = 1u;
    uint2 copyTarget = coordinates ^ 1u;
    if (copyHorizontal && copyTarget.x < g_buffer_dimensions.x) {
        uint2 target = uint2(copyTarget.x, coordinates.y);
        g_denoiser_radiance[target] = payload;
        g_denoiser_hit_confidence[target] = 1.0;
        ++writeCount;
    }
    if (copyVertical && copyTarget.y < g_buffer_dimensions.y) {
        uint2 target = uint2(coordinates.x, copyTarget.y);
        g_denoiser_radiance[target] = payload;
        g_denoiser_hit_confidence[target] = 1.0;
        ++writeCount;
    }
    if (copyDiagonal && all(copyTarget < g_buffer_dimensions)) {
        g_denoiser_radiance[copyTarget] = payload;
        g_denoiser_hit_confidence[copyTarget] = 1.0;
        ++writeCount;
    }
    DiagnosticAdd(kDiagnosticDenoiserInjectionResolvedCount, 1u);
    DiagnosticAdd(kDiagnosticDenoiserRadiancePixelWriteCount, writeCount);
    DiagnosticAdd(kDiagnosticDenoiserConfidencePixelWriteCount, writeCount);
    DiagnosticAdd(
        kDiagnosticDenoiserConfidenceSumPermille,
        writeCount * 1000u
    );
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
    FullAuditInitialize(
        rayIndex,
        coordinates,
        copyHorizontal,
        copyVertical,
        copyDiagonal
    );

    if (any(coordinates >= g_buffer_dimensions)) {
        DiagnosticAdd(kDiagnosticInvalidRayCount, 1u);
        return;
    }
    FullAuditAddFlags(rayIndex, kFullAuditFlagCoordinatesValid);

    float screenConfidence = g_sssr_hit_confidence.Load(int3(coordinates, 0));
    FullAuditStoreFloat(rayIndex, 3u, screenConfidence);
    bool forceAllRayQueries =
        (g_ray_query_runtime_flags & kRayQueryRuntimeForceAllRaysBit) != 0u;
#if defined(SE_HYBRID_REFLECTION_DIAGNOSTIC_FORCE_ALL)
    forceAllRayQueries = true;
#endif
    bool screenAccepted = !forceAllRayQueries &&
        screenConfidence >= g_screen_hit_confidence_threshold;
    if (screenAccepted) {
        DiagnosticAdd(kDiagnosticScreenHitAcceptedCount, 1u);
        FullAuditAddFlags(rayIndex, kFullAuditFlagScreenAccepted);
        if (!FullAuditEnabled()) {
            return;
        }
    }
    bool productionTrace = !screenAccepted;

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
    FullAuditAddFlags(rayIndex, kFullAuditFlagGBufferValid);

    float2 uv = (float2(coordinates) + 0.5) * g_inv_buffer_dimensions;
    float3 screenOrigin = float3(uv, depth);
    float3 viewOrigin = InvProjectPosition(screenOrigin, g_inv_proj);
    float3 viewDirection = normalize(viewOrigin);
    float3 viewNormal = normalize(mul(g_view, float4(worldNormal, 0.0)).xyz);
    float roughness = max(0.001, g_roughness.Load(int3(coordinates, 0)));
    FullAuditStoreFloat(rayIndex, 8u, roughness);
    float3 viewReflection = ReflectionDirection(
        viewDirection,
        viewNormal,
        roughness,
        coordinates
    );
    float3 worldDirection = normalize(
        mul(g_inv_view, float4(viewReflection, 0.0)).xyz
    );
    float3 receiverWorldPosition = InvProjectPosition(
        screenOrigin,
        g_inv_view_proj
    );
    float3 worldOrigin = receiverWorldPosition;
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

    uint traversalFlags = ReflectionTraversalFlags();
    uint rawReceiverObjectId = 0u;
    uint receiverInstanceId = ResolveFullAuditReceiver(
        coordinates,
        rawReceiverObjectId
    );
    FullAuditStore(rayIndex, 20u, rawReceiverObjectId);
    if (rawReceiverObjectId != 0u) {
        FullAuditAddFlags(rayIndex, kFullAuditFlagObjectIdValid);
    }
    if (receiverInstanceId != kFullAuditInvalidIndex) {
        FullAuditStore(rayIndex, 2u, receiverInstanceId);
        FullAuditAddFlags(
            rayIndex,
            kFullAuditFlagReceiverResolved |
                kFullAuditFlagObjectIdMappedToTlas
        );
        FullAuditInstanceAdd(receiverInstanceId, 0u, 1u);
        if (screenAccepted) {
            FullAuditInstanceAdd(receiverInstanceId, 1u, 1u);
        }
    }
    FullAuditStoreFloat(rayIndex, 9u, worldOrigin.x);
    FullAuditStoreFloat(rayIndex, 10u, worldOrigin.y);
    FullAuditStoreFloat(rayIndex, 11u, worldOrigin.z);
    FullAuditStoreFloat(rayIndex, 12u, worldDirection.x);
    FullAuditStoreFloat(rayIndex, 13u, worldDirection.y);
    FullAuditStoreFloat(rayIndex, 14u, worldDirection.z);
    FullAuditStoreFloat(rayIndex, 16u, worldNormal.x);
    FullAuditStoreFloat(rayIndex, 17u, worldNormal.y);
    FullAuditStoreFloat(rayIndex, 18u, worldNormal.z);
    float normalDotReflection = dot(worldNormal, worldDirection);
    FullAuditStoreFloat(rayIndex, 19u, normalDotReflection);
    FullAuditStoreFloat(rayIndex, 21u, originBias);
    FullAuditStoreFloat(rayIndex, 22u, 1.0);
    if (normalDotReflection < 0.0) {
        FullAuditAddFlags(rayIndex, kFullAuditFlagNegativeHemisphere);
        DiagnosticAdd(kDiagnosticInvalidRayCount, 1u);
        return;
    }
    if (receiverInstanceId != kFullAuditInvalidIndex && productionTrace) {
        FullAuditInstanceAdd(receiverInstanceId, 2u, 1u);
    }
    FullAuditAddFlags(
        rayIndex,
        productionTrace
            ? kFullAuditFlagProductionTrace
            : kFullAuditFlagAuditOnlyTrace
    );

    if (productionTrace) {
        DiagnosticAdd(kDiagnosticTraceCount, 1u);
    }
    RayQuery<RAY_FLAG_FORCE_OPAQUE> rayQuery;
    RayDesc rayDescription;
    rayDescription.Origin = worldOrigin;
    rayDescription.TMin = max(originBias * 0.25, 1.0e-4);
    rayDescription.Direction = worldDirection;
    rayDescription.TMax = max(g_max_ray_distance, 0.01);
    rayQuery.TraceRayInline(
        g_scene_acceleration_structure,
        traversalFlags,
        0xffu,
        rayDescription
    );
    while (rayQuery.Proceed()) {
    }

    bool committedTriangle =
        rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (committedTriangle) {
        FullAuditAddFlags(rayIndex, kFullAuditFlagCommittedHit);
        FullAuditStore(rayIndex, 4u, rayQuery.CommittedInstanceID());
        FullAuditStore(rayIndex, 5u, rayQuery.CommittedPrimitiveIndex());
        FullAuditStoreFloat(rayIndex, 6u, rayQuery.CommittedRayT());
        if (receiverInstanceId != kFullAuditInvalidIndex &&
            rayQuery.CommittedInstanceID() == receiverInstanceId) {
            FullAuditAddFlags(rayIndex, kFullAuditFlagSelfHit);
            FullAuditStoreFloat(rayIndex, 23u, rayQuery.CommittedRayT());
        }
        FullAuditInstanceAdd(rayQuery.CommittedInstanceID(), 4u, 1u);
        if (productionTrace && receiverInstanceId != kFullAuditInvalidIndex) {
            FullAuditInstanceAdd(receiverInstanceId, 3u, 1u);
        }
    } else if (productionTrace &&
        receiverInstanceId != kFullAuditInvalidIndex) {
        FullAuditInstanceAdd(receiverInstanceId, 7u, 1u);
    }
    if (!productionTrace) {
        return;
    }

    if (committedTriangle) {
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

        bool diagnosticTargetHit =
            (g_ray_query_runtime_flags &
                kRayQueryRuntimeTargetAttributionBit) != 0u &&
            instanceId < g_instance_metadata_count &&
            instanceId == g_diagnostic_target_instance_index;
        if (diagnosticTargetHit) {
            DiagnosticAdd(kDiagnosticTargetCommittedHitCount, 1u);
        }

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
                            static const uint kColorOffset = 24u;
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
                            float3 color0 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.x,
                                metadata.vertexStride,
                                kColorOffset
                            );
                            float3 color1 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.y,
                                metadata.vertexStride,
                                kColorOffset
                            );
                            float3 color2 = LoadVertexFloat3(
                                vertexAddress,
                                vertexIndices.z,
                                metadata.vertexStride,
                                kColorOffset
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
                            float3 objectColor =
                                color0 * barycentrics.x +
                                color1 * barycentrics.y +
                                color2 * barycentrics.z;
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
                            float3 worldPosition0 = mul(
                                float4(position0, 1.0),
                                objectToWorld
                            );
                            float3 worldPosition1 = mul(
                                float4(position1, 1.0),
                                objectToWorld
                            );
                            float3 worldPosition2 = mul(
                                float4(position2, 1.0),
                                objectToWorld
                            );
                            float3 transformedNormal =
                                mul(worldToObject, objectNormal).xyz;
                            float transformedNormalLength =
                                length(transformedNormal);
                            float3 worldShadingNormal = transformedNormal /
                                max(transformedNormalLength, 1.0e-12);
                            float3 geometricNormalVector = cross(
                                worldPosition1 - worldPosition0,
                                worldPosition2 - worldPosition0
                            );
                            float geometricNormalLength =
                                length(geometricNormalVector);
                            float3 worldGeometricNormal =
                                geometricNormalVector /
                                max(geometricNormalLength, 1.0e-12);
                            bool attributesValid =
                                all(isfinite(objectPosition)) &&
                                all(isfinite(objectNormal)) &&
                                all(isfinite(objectColor)) &&
                                all(isfinite(hitTexCoord)) &&
                                all(isfinite(worldPosition)) &&
                                all(isfinite(worldPosition0)) &&
                                all(isfinite(worldPosition1)) &&
                                all(isfinite(worldPosition2)) &&
                                all(isfinite(worldShadingNormal)) &&
                                all(isfinite(worldGeometricNormal)) &&
                                transformedNormalLength > 1.0e-8 &&
                                geometricNormalLength > 1.0e-8;
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
                                FullAuditAddFlags(
                                    rayIndex,
                                    kFullAuditFlagAttributesResolved
                                );
                                FullAuditInstanceAdd(instanceId, 5u, 1u);
                                if (diagnosticTargetHit) {
                                    DiagnosticAdd(
                                        kDiagnosticTargetAttributeResolvedCount,
                                        1u
                                    );
                                }
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

                                if (g_material_textures_enabled != 0u) {
                                    float3 surfaceBaseColor = max(objectColor, 0.0);
                                    float3 surfaceEmissive = 0.0;
                                    float surfaceMetallic = 0.0;
                                    float surfaceRoughness = 1.0;
                                    float sampleLod = 0.0;
                                    bool textureSampleValid = false;
                                    bool materialRecordValid =
                                        metadata.materialIndex > 0u &&
                                        metadata.materialIndex <=
                                            g_material_table_count;
                                    if (materialRecordValid) {
                                        uint materialSlot =
                                            metadata.materialIndex - 1u;
                                        HybridMaterialRecord material =
                                            g_material_table[materialSlot];
                                        materialRecordValid =
                                            material.textureInfo.w != 0u &&
                                            material.textureInfo.x <
                                                g_material_table_capacity &&
                                            material.textureInfo.y <
                                                g_material_table_capacity &&
                                            material.textureInfo.z > 0u &&
                                            all(material.textureExtent.xy > 0u);
                                        if (materialRecordValid) {
                                            DiagnosticAdd(
                                                kDiagnosticMaterialRecordResolvedCount,
                                                1u
                                            );
                                            FullAuditStore(
                                                rayIndex,
                                                7u,
                                                metadata.materialIndex
                                            );
                                            FullAuditAddFlags(
                                                rayIndex,
                                                kFullAuditFlagMaterialResolved
                                            );
                                            surfaceMetallic = saturate(
                                                material.surfaceControls.y
                                            );
                                            surfaceRoughness = clamp(
                                                material.surfaceControls.z,
                                                0.04,
                                                1.0
                                            );
#if defined(SE_HYBRID_REFLECTION_DIAGNOSTIC_SILVER_TARGET)
                                            if (
                                                abs(surfaceMetallic - 0.68) <
                                                    0.001 &&
                                                abs(surfaceRoughness - 0.24) <
                                                    0.001
                                            ) {
                                                DiagnosticAdd(
                                                    kDiagnosticPositionMismatchCount,
                                                    1u
                                                );
                                            }
#endif
                                            surfaceEmissive = max(
                                                material.emissiveFactor.rgb,
                                                0.0
                                            );
                                            float2 materialUv =
                                                TransformMaterialUv(
                                                    hitTexCoord,
                                                    material
                                                );
                                            float2 materialUv0 =
                                                TransformMaterialUv(
                                                    texCoord0,
                                                    material
                                                );
                                            float2 materialUv1 =
                                                TransformMaterialUv(
                                                    texCoord1,
                                                    material
                                                );
                                            float2 materialUv2 =
                                                TransformMaterialUv(
                                                    texCoord2,
                                                    material
                                                );
                                            sampleLod = EstimateMaterialTextureLod(
                                                worldPosition0,
                                                worldPosition1,
                                                worldPosition2,
                                                materialUv0,
                                                materialUv1,
                                                materialUv2,
                                                hitDistance,
                                                material.textureExtent.xy,
                                                material.textureInfo.z
                                            );
                                            uint textureIndex =
                                                NonUniformResourceIndex(
                                                    material.textureInfo.x
                                                );
                                            uint samplerIndex =
                                                NonUniformResourceIndex(
                                                    material.textureInfo.y
                                                );
                                            float4 sampledBaseColor =
                                                g_material_albedo_textures[
                                                    textureIndex
                                                ].SampleLevel(
                                                    g_material_samplers[
                                                        samplerIndex
                                                    ],
                                                    materialUv,
                                                    sampleLod
                                                );
                                            if (all(isfinite(sampledBaseColor))) {
                                                float textureMix = saturate(
                                                    material.surfaceControls.x
                                                );
                                                surfaceBaseColor = lerp(
                                                    max(objectColor, 0.0),
                                                    max(sampledBaseColor.rgb, 0.0),
                                                    textureMix
                                                ) * max(
                                                    material.baseColorFactor.rgb,
                                                    0.0
                                                );
                                                textureSampleValid = all(
                                                    isfinite(surfaceBaseColor)
                                                ) && all(isfinite(surfaceEmissive));
                                                DiagnosticAdd(
                                                    kDiagnosticTextureSampleResolvedCount,
                                                    1u
                                                );
                                                uint lodMillilevels =
                                                    SaturatedMetric(
                                                        sampleLod,
                                                        1000.0
                                                    );
                                                DiagnosticMin(
                                                    kDiagnosticSampleLodMinMillilevels,
                                                    lodMillilevels
                                                );
                                                DiagnosticMax(
                                                    kDiagnosticSampleLodMaxMillilevels,
                                                    lodMillilevels
                                                );
                                            } else {
                                                DiagnosticAdd(
                                                    kDiagnosticTextureSampleInvalidCount,
                                                    1u
                                                );
                                            }
                                        }
                                    }
                                    if (!materialRecordValid) {
                                        DiagnosticAdd(
                                            kDiagnosticMaterialRecordFallbackCount,
                                            1u
                                        );
                                        DiagnosticAdd(
                                            kDiagnosticTextureSampleFallbackCount,
                                            1u
                                        );
                                    }
                                    if (textureSampleValid) {
                                        DiagnosticAdd(
                                            kDiagnosticFiniteSampledColorCount,
                                            1u
                                        );
                                    }
                                    if (g_hit_lighting_enabled != 0u) {
                                        float3 directRadiance;
                                        float3 iblRadiance;
                                        float3 finalRadiance;
                                        float3 hitViewDirection = normalize(
                                            -rayDescription.Direction
                                        );
                                        bool lightingValid =
                                            textureSampleValid &&
                                            EvaluateHitRadiance(
                                                clamp(
                                                    surfaceBaseColor,
                                                    0.0,
                                                    65504.0
                                                ),
                                                surfaceMetallic,
                                                surfaceRoughness,
                                                surfaceEmissive,
                                                worldShadingNormal,
                                                worldGeometricNormal,
                                                hitViewDirection,
                                                worldPosition,
                                                instanceId,
                                                directRadiance,
                                                iblRadiance,
                                                finalRadiance
                                            );
                                        if (lightingValid) {
                                            directRadiance = clamp(
                                                directRadiance,
                                                0.0,
                                                65504.0
                                            );
                                            iblRadiance = clamp(
                                                iblRadiance,
                                                0.0,
                                                65504.0
                                            );
                                            surfaceEmissive = clamp(
                                                surfaceEmissive,
                                                0.0,
                                                65504.0
                                            );
                                            finalRadiance = clamp(
                                                directRadiance + iblRadiance +
                                                    surfaceEmissive,
                                                0.0,
                                                65504.0
                                            );
                                            float4 payload = float4(
                                                finalRadiance,
                                                hitDistance
                                            );
                                            StoreHitSurfacePayload(
                                                coordinates,
                                                copyHorizontal,
                                                copyVertical,
                                                copyDiagonal,
                                                payload
                                            );
                                            StoreDenoiserPayload(
                                                coordinates,
                                                copyHorizontal,
                                                copyVertical,
                                                copyDiagonal,
                                                payload
                                            );
                                            FullAuditAddFlags(
                                                rayIndex,
                                                kFullAuditFlagLightingResolved |
                                                    kFullAuditFlagDenoiserWrite
                                            );
                                            FullAuditInstanceAdd(
                                                instanceId,
                                                6u,
                                                1u
                                            );
                                            if (diagnosticTargetHit) {
                                                DiagnosticAdd(
                                                    kDiagnosticTargetDenoiserWriteCount,
                                                    1u
                                                );
                                            }
                                            uint directLuminance =
                                                BoundedLuminanceMetric(
                                                    directRadiance
                                                );
                                            uint iblLuminance =
                                                BoundedLuminanceMetric(
                                                    iblRadiance
                                                );
                                            uint emissiveLuminance =
                                                BoundedLuminanceMetric(
                                                    surfaceEmissive
                                                );
                                            uint finalLuminance =
                                                BoundedLuminanceMetric(
                                                    finalRadiance
                                                );
                                            FullAuditStoreFloat(
                                                rayIndex,
                                                15u,
                                                float(finalLuminance) * 0.001
                                            );
                                            uint radianceHash =
                                                HashDiagnosticIdentity(
                                                    asuint(payload.x) ^
                                                    (asuint(payload.y) *
                                                        0x9e3779b9u) ^
                                                    (asuint(payload.z) *
                                                        0x85ebca6bu) ^
                                                    metadata.materialIndex
                                                );
                                            DiagnosticAdd(
                                                kDiagnosticHitLightingResolvedCount,
                                                1u
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticFiniteDirectRadianceCount,
                                                1u
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticFiniteIblRadianceCount,
                                                1u
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticFiniteEmissiveRadianceCount,
                                                1u
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticFiniteRadianceCount,
                                                1u
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticDirectLuminanceSumMilliunits,
                                                directLuminance
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticIblLuminanceSumMilliunits,
                                                iblLuminance
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticEmissiveLuminanceSumMilliunits,
                                                emissiveLuminance
                                            );
                                            DiagnosticMin(
                                                kDiagnosticRadianceLuminanceMinMilliunits,
                                                finalLuminance
                                            );
                                            DiagnosticMax(
                                                kDiagnosticRadianceLuminanceMaxMilliunits,
                                                finalLuminance
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticRadianceChecksum,
                                                radianceHash
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticHitSurfacePayloadWriteCount,
                                                1u
                                            );
                                            DiagnosticAdd(
                                                kDiagnosticHitSurfacePayloadChecksum,
                                                radianceHash
                                            );
                                            DiagnosticMin(
                                                kDiagnosticHitSurfaceLuminanceMinMilliunits,
                                                finalLuminance
                                            );
                                            DiagnosticMax(
                                                kDiagnosticHitSurfaceLuminanceMaxMilliunits,
                                                finalLuminance
                                            );
                                        } else {
                                            DiagnosticAdd(
                                                kDiagnosticHitLightingInvalidCount,
                                                1u
                                            );
                                        }
                                    }
                                }
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
