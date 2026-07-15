#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    mat4 lightViewProj;
    vec4 directionalLight;
    vec4 ambientLight;
    vec4 shadowControls;
    vec4 shadowFiltering;
    vec4 contactShadowControls;
    vec4 contactShadowStabilityControls;
    vec4 ssaoControls;
    vec4 ssrControls;
    vec4 reflectionProbeControls;
    vec4 localReflectionProbePositionRadius;
    vec4 localReflectionProbeControls;
    vec4 localReflectionProbeColor;
    vec4 localReflectionProbeBoxExtentsProjection;
    vec4 reflectionProbePositionRadius[4];
    vec4 reflectionProbeControlsArray[4];
    vec4 reflectionProbeColorArray[4];
    vec4 reflectionProbeBoxExtentsProjectionArray[4];
    vec4 reflectionProbeBlendControls;
    vec4 heightFogControls;
    vec4 heightFogColor;
    vec4 postProcessControls;
    vec4 colorGradingControls;
    vec4 toneMappingControls;
    vec4 probeGridOriginSpacing;
    vec4 probeGridSizeBlend;
    vec4 autoExposureControls;
    vec4 sharpeningControls;
    vec4 colorGradingLutControls;
    vec4 debugControls;
    vec4 reflectionProbeDiffuseLobes[24];
} frame;

struct MaterialRecord {
    vec4 baseColorFactor;
    vec4 materialControls;
    vec4 materialCustom;
    vec4 cameraControls;
    vec4 materialFlags;
    vec4 pbrFactors;
    vec4 emissiveFactor;
    vec4 specularFactor;
    vec4 uvTransform;
    vec4 uvControls;
    vec4 volumeFactor;
};

layout(set = 0, binding = 2) readonly buffer MaterialData {
    vec4 materialCounts;
    MaterialRecord materials[256];
} frameMaterials;

layout(std430, set = 0, binding = 4) readonly buffer DirectionalShadowCascades {
    vec4 cascadeInfo;
    vec4 splitDepths;
    vec4 texelWorldSizes;
    vec4 cascadeBlendControls;
    mat4 fallbackViewProjection;
    mat4 viewProjections[4];
} shadowCascades;

bool HasTextureFlag(float flags, float bit) {
    return mod(floor(flags / bit), 2.0) > 0.5;
}

bool TryGetMaterialRecord(float materialIdFloat, out MaterialRecord materialRecord) {
    materialRecord.baseColorFactor = vec4(1.0);
    materialRecord.materialControls = vec4(1.0, 0.0, 0.0, 0.0);
    materialRecord.materialCustom = vec4(0.0);
    materialRecord.cameraControls = vec4(0.0, 0.65, 0.0, 0.0);
    materialRecord.materialFlags = vec4(0.0, 0.0, 0.0, 1.0);
    materialRecord.pbrFactors = vec4(1.0, 1.0, 0.0, 0.0);
    materialRecord.emissiveFactor = vec4(0.0, 0.0, 0.0, 1.0);
    materialRecord.specularFactor = vec4(1.0);
    materialRecord.uvTransform = vec4(0.0, 0.0, 1.0, 1.0);
    materialRecord.uvControls = vec4(0.0);
    materialRecord.volumeFactor = vec4(0.0);

    int materialId = int(materialIdFloat + 0.5);
    int materialCount = clamp(int(frameMaterials.materialCounts.x + 0.5), 0, 256);
    if (materialId <= 0 || materialId > materialCount) {
        return false;
    }

    materialRecord = frameMaterials.materials[materialId - 1];
    return true;
}

layout(push_constant) uniform ObjectPushConstants {
    mat4 model;
    mat4 previousModel;
    vec4 tint;
    vec4 materialBaseColorFactor;
    vec4 materialControls;
    vec4 materialCustom;
    vec4 viewport;
    vec4 cameraControls;
    vec4 cameraPosition;
    vec4 cameraDirection;
} objectData;

layout(set = 1, binding = 0) uniform sampler2D gBufferAlbedo;
layout(set = 1, binding = 1) uniform sampler2D gBufferNormalRoughness;
layout(set = 1, binding = 2) uniform sampler2D gBufferMaterial;
layout(set = 1, binding = 3) uniform sampler2D gBufferVelocity;
layout(set = 1, binding = 4) uniform sampler2D sceneDepth;
layout(set = 1, binding = 5) uniform sampler2D gBufferEmissive;
layout(set = 1, binding = 6) uniform sampler2D shadowSampler;

const int MAX_DIRECTIONAL_SHADOW_CASCADES = 4;

vec3 Turbo(float x) {
    x = clamp(x, 0.0, 1.0);
    vec4 kRed = vec4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
    vec4 kGreen = vec4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
    vec4 kBlue = vec4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
    vec2 v = vec2(1.0, x);
    vec4 powers = vec4(v, x * x, x * x * x);
    return clamp(vec3(dot(kRed, powers), dot(kGreen, powers), dot(kBlue, powers)), 0.0, 1.0);
}

vec3 ReconstructWorldPosition(vec2 uv, float depth) {
    vec4 clipPosition = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPosition = frame.invProj * clipPosition;
    if (abs(viewPosition.w) <= 0.000001) {
        return vec3(0.0);
    }
    viewPosition /= viewPosition.w;
    return (frame.invView * viewPosition).xyz;
}

ivec2 DebugTexelCoord(sampler2D source, vec2 uv) {
    ivec2 size = textureSize(source, 0);
    vec2 scaled = clamp(uv, vec2(0.0), vec2(0.999999)) * vec2(size);
    return clamp(ivec2(scaled), ivec2(0), size - ivec2(1));
}

vec2 DebugTexelCenterUv(sampler2D source, ivec2 texelCoord) {
    ivec2 size = textureSize(source, 0);
    return (vec2(texelCoord) + vec2(0.5)) / vec2(size);
}

vec3 DepthDebugColor(float depth) {
    float contrastDepth = pow(clamp((1.0 - depth) * 96.0, 0.0, 1.0), 0.35);
    vec3 farColor = vec3(0.00, 0.82, 1.00);
    vec3 nearColor = vec3(1.00, 0.96, 0.12);
    return mix(farColor, nearColor, contrastDepth);
}

int ActiveShadowCascadeCount() {
    return clamp(
        int(shadowCascades.cascadeInfo.x + 0.5),
        0,
        MAX_DIRECTIONAL_SHADOW_CASCADES
    );
}

bool DirectionalShadowReceiveEnabled() {
    return shadowCascades.cascadeInfo.y > 0.5;
}

int SelectShadowCascade(vec3 worldPosition, out float viewDepth) {
    viewDepth = abs((frame.view * vec4(worldPosition, 1.0)).z);
    int activeCascadeCount = ActiveShadowCascadeCount();
    if (activeCascadeCount <= 0) {
        return -1;
    }

    int cascadeIndex = activeCascadeCount - 1;
    for (int candidateIndex = 0; candidateIndex < MAX_DIRECTIONAL_SHADOW_CASCADES; ++candidateIndex) {
        if (candidateIndex >= activeCascadeCount) {
            break;
        }
        if (viewDepth <= shadowCascades.splitDepths[candidateIndex]) {
            cascadeIndex = candidateIndex;
            break;
        }
    }
    return cascadeIndex;
}

vec3 ShadowCascadePalette(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return vec3(0.10, 0.75, 1.00);
    }
    if (cascadeIndex == 1) {
        return vec3(0.15, 1.00, 0.38);
    }
    if (cascadeIndex == 2) {
        return vec3(1.00, 0.82, 0.18);
    }
    return vec3(1.00, 0.22, 0.55);
}

int ProjectShadowCascade(
    vec3 worldPosition,
    int cascadeIndex,
    out vec2 shadowUv,
    out float currentDepth
) {
    shadowUv = vec2(0.0);
    currentDepth = 1.0;
    if (cascadeIndex < 0 || cascadeIndex >= ActiveShadowCascadeCount()) {
        return 1;
    }

    vec4 lightSpacePosition =
        shadowCascades.viewProjections[cascadeIndex] * vec4(worldPosition, 1.0);
    if (abs(lightSpacePosition.w) <= 0.000001) {
        return 2;
    }

    vec3 projectionCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    shadowUv = projectionCoords.xy * 0.5 + 0.5;
    currentDepth = projectionCoords.z;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0) {
        return 3;
    }
    if (currentDepth < 0.0 || currentDepth > 1.0) {
        return 4;
    }

    return 0;
}

vec3 ShadowCascadeDebugColor(vec3 worldPosition) {
    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(worldPosition, viewDepth);
    if (cascadeIndex < 0) {
        return vec3(1.0, 0.1, 0.1);
    }

    vec2 shadowUv = vec2(0.0);
    float currentDepth = 1.0;
    if (ProjectShadowCascade(worldPosition, cascadeIndex, shadowUv, currentDepth) != 0) {
        return vec3(0.08, 0.08, 0.10);
    }

    float cascadeStart = cascadeIndex == 0
        ? 0.0
        : shadowCascades.splitDepths[cascadeIndex - 1];
    float cascadeEnd = max(shadowCascades.splitDepths[cascadeIndex], cascadeStart + 0.0001);
    float depthInCascade = clamp((viewDepth - cascadeStart) / (cascadeEnd - cascadeStart), 0.0, 1.0);
    vec3 color = ShadowCascadePalette(cascadeIndex);
    color *= mix(0.55, 1.15, depthInCascade);
    float blendWidth = (cascadeEnd - cascadeStart) *
        clamp(shadowCascades.cascadeBlendControls.x, 0.0, 0.25);
    if (cascadeIndex + 1 < ActiveShadowCascadeCount() &&
        blendWidth > 0.0001 &&
        viewDepth >= cascadeEnd - blendWidth) {
        float blendFactor = smoothstep(cascadeEnd - blendWidth, cascadeEnd, viewDepth);
        color = mix(color, ShadowCascadePalette(cascadeIndex + 1), blendFactor * 0.6);
    }
    float fadeWidth = (cascadeEnd - cascadeStart) *
        clamp(shadowCascades.cascadeBlendControls.y, 0.0, 0.35);
    if (cascadeIndex + 1 >= ActiveShadowCascadeCount() &&
        fadeWidth > 0.0001 &&
        viewDepth >= cascadeEnd - fadeWidth) {
        float fadeFactor = smoothstep(cascadeEnd - fadeWidth, cascadeEnd, viewDepth);
        color = mix(color, vec3(1.0), fadeFactor * 0.75);
    }

    float edgeDistance = min(
        min(shadowUv.x, 1.0 - shadowUv.x),
        min(shadowUv.y, 1.0 - shadowUv.y)
    );
    if (edgeDistance < 0.015) {
        color = mix(color, vec3(1.0), 0.65);
    }

    return clamp(color, vec3(0.0), vec3(1.0));
}

vec3 ShadowCascadeAtlasDebugColor(vec2 uv) {
    int activeCascadeCount = ActiveShadowCascadeCount();
    if (activeCascadeCount <= 0) {
        return vec3(0.12, 0.02, 0.04);
    }

    vec2 grid = uv * vec2(2.0);
    ivec2 tileCoord = ivec2(floor(grid));
    if (tileCoord.x < 0 || tileCoord.y < 0 || tileCoord.x >= 2 || tileCoord.y >= 2) {
        return vec3(0.015, 0.015, 0.02);
    }

    int cascadeIndex = tileCoord.y * 2 + tileCoord.x;
    vec2 tileUv = fract(grid);
    float atlasDepth = texture(shadowSampler, uv).r;
    vec3 depthColor = mix(
        vec3(0.018, 0.022, 0.030),
        vec3(0.92, 0.94, 0.98),
        clamp(1.0 - atlasDepth, 0.0, 1.0)
    );

    if (cascadeIndex >= activeCascadeCount) {
        return mix(vec3(0.012, 0.015, 0.020), depthColor, 0.12);
    }

    vec3 color = mix(depthColor, ShadowCascadePalette(cascadeIndex), 0.34);
    float gridLine = step(tileUv.x, 0.018) +
        step(tileUv.y, 0.018) +
        step(0.982, tileUv.x) +
        step(0.982, tileUv.y);
    if (gridLine > 0.0) {
        color = mix(color, vec3(1.0), 0.70);
    }

    return clamp(color, vec3(0.0), vec3(1.0));
}

bool SampleShadowCascadeVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int cascadeIndex,
    out float visibility
) {
    visibility = 1.0;
    vec2 shadowUv = vec2(0.0);
    float currentDepth = 1.0;
    if (ProjectShadowCascade(worldPosition, cascadeIndex, shadowUv, currentDepth) != 0) {
        return false;
    }

    vec2 tileOrigin = vec2(float(cascadeIndex % 2), float(cascadeIndex / 2)) * 0.5;
    vec2 atlasUv = tileOrigin + shadowUv * 0.5;
    vec2 tileMax = tileOrigin + vec2(0.5);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowSampler, 0));
    float pcfRadius = clamp(frame.shadowFiltering.x, 0.0, 3.0);
    float biasMin = max(frame.shadowControls.z, 0.0);
    float biasSlope = max(frame.shadowControls.w, 0.0);
    float bias = max(biasSlope * (1.0 - dot(normal, lightDir)), biasMin);
    int kernelRadius = clamp(int(shadowCascades.cascadeBlendControls.z + 0.5), 0, 2);
    float pcssStrength = clamp(shadowCascades.cascadeBlendControls.w, 0.0, 1.0);
    float filterRadius = pcfRadius;
    if (pcssStrength > 0.0001 && kernelRadius > 0) {
        float blockerDepthSum = 0.0;
        int blockerCount = 0;
        float searchRadius = max(pcfRadius, 1.0) * (1.0 + pcssStrength);
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                vec2 blockerUv = clamp(
                    atlasUv + vec2(x, y) * texelSize * searchRadius,
                    tileOrigin + texelSize,
                    tileMax - texelSize
                );
                float blockerDepth = texture(shadowSampler, blockerUv).r;
                if (currentDepth - bias > blockerDepth) {
                    blockerDepthSum += blockerDepth;
                    ++blockerCount;
                }
            }
        }

        if (blockerCount > 0) {
            float averageBlockerDepth = blockerDepthSum / float(blockerCount);
            float penumbra = clamp(
                (currentDepth - averageBlockerDepth) / max(averageBlockerDepth, 0.0001),
                0.0,
                1.0
            );
            filterRadius = pcfRadius * (1.0 + penumbra * pcssStrength * 3.0);
        }
    }

    float shadow = 0.0;
    int sampleCount = 0;
    for (int x = -2; x <= 2; ++x) {
        if (abs(x) > kernelRadius) {
            continue;
        }
        for (int y = -2; y <= 2; ++y) {
            if (abs(y) > kernelRadius) {
                continue;
            }
            vec2 sampleUv = clamp(
                atlasUv + vec2(x, y) * texelSize * filterRadius,
                tileOrigin + texelSize,
                tileMax - texelSize
            );
            float closestDepth = texture(
                shadowSampler,
                sampleUv
            ).r;
            shadow += currentDepth - bias > closestDepth ? 1.0 : 0.0;
            ++sampleCount;
        }
    }

    float occlusion = shadow / max(float(sampleCount), 1.0);
    visibility = 1.0 - occlusion * clamp(frame.shadowControls.y, 0.0, 1.0);
    return true;
}

bool ResolveShadowCascadeVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int preferredCascadeIndex,
    int activeCascadeCount,
    out float visibility
) {
    visibility = 1.0;
    if (SampleShadowCascadeVisibility(
            worldPosition,
            normal,
            lightDir,
            preferredCascadeIndex,
            visibility)) {
        return true;
    }

    for (int offset = 1; offset < MAX_DIRECTIONAL_SHADOW_CASCADES; ++offset) {
        int lowerIndex = preferredCascadeIndex - offset;
        if (lowerIndex >= 0 &&
            SampleShadowCascadeVisibility(
                worldPosition,
                normal,
                lightDir,
                lowerIndex,
                visibility)) {
            return true;
        }

        int upperIndex = preferredCascadeIndex + offset;
        if (upperIndex < activeCascadeCount &&
            SampleShadowCascadeVisibility(
                worldPosition,
                normal,
                lightDir,
                upperIndex,
                visibility)) {
            return true;
        }
    }

    return false;
}

float ApplyShadowDistanceFade(
    float visibility,
    int cascadeIndex,
    int activeCount,
    float viewDepth
) {
    if (cascadeIndex + 1 < activeCount) {
        return visibility;
    }

    float cascadeStart = cascadeIndex == 0
        ? 0.0
        : shadowCascades.splitDepths[cascadeIndex - 1];
    float cascadeEnd = shadowCascades.splitDepths[cascadeIndex];
    float cascadeRange = max(cascadeEnd - cascadeStart, 0.0001);
    float fadeWidth = cascadeRange * clamp(shadowCascades.cascadeBlendControls.y, 0.0, 0.35);
    if (fadeWidth <= 0.0001) {
        return visibility;
    }

    float fadeFactor = smoothstep(cascadeEnd - fadeWidth, cascadeEnd, viewDepth);
    return mix(visibility, 1.0, fadeFactor);
}

float ShadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDir) {
    if (frame.shadowControls.x < 0.5 ||
        frame.shadowControls.y <= 0.001 ||
        !DirectionalShadowReceiveEnabled()) {
        return 1.0;
    }

    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(worldPosition, viewDepth);
    if (cascadeIndex < 0) {
        return 1.0;
    }

    int activeCascadeCount = ActiveShadowCascadeCount();
    float visibility = 1.0;
    bool visibilityValid = ResolveShadowCascadeVisibility(
        worldPosition,
        normal,
        lightDir,
        cascadeIndex,
        activeCascadeCount,
        visibility
    );
    if (!visibilityValid) {
        return 1.0;
    }
    if (cascadeIndex + 1 >= activeCascadeCount) {
        return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCascadeCount, viewDepth);
    }

    float cascadeStart = cascadeIndex == 0
        ? 0.0
        : shadowCascades.splitDepths[cascadeIndex - 1];
    float cascadeEnd = shadowCascades.splitDepths[cascadeIndex];
    float cascadeRange = max(cascadeEnd - cascadeStart, 0.0001);
    float blendWidth = cascadeRange * clamp(shadowCascades.cascadeBlendControls.x, 0.0, 0.25);
    if (blendWidth <= 0.0001) {
        return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCascadeCount, viewDepth);
    }

    float blendFactor = smoothstep(cascadeEnd - blendWidth, cascadeEnd, viewDepth);
    if (blendFactor <= 0.0001) {
        return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCascadeCount, viewDepth);
    }

    float nextVisibility = visibility;
    if (SampleShadowCascadeVisibility(
        worldPosition,
        normal,
        lightDir,
        cascadeIndex + 1,
        nextVisibility)) {
        visibility = mix(visibility, nextVisibility, blendFactor);
    }
    return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCascadeCount, viewDepth);
}

vec3 ShadowCascadeReceiverDebugColor(vec3 worldPosition, vec3 normal, vec3 lightDir) {
    if (frame.shadowControls.x < 0.5 ||
        frame.shadowControls.y <= 0.001 ||
        !DirectionalShadowReceiveEnabled()) {
        return vec3(0.55, 0.04, 0.06);
    }

    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(worldPosition, viewDepth);
    int activeCascadeCount = ActiveShadowCascadeCount();
    if (cascadeIndex < 0 || activeCascadeCount <= 0) {
        return vec3(0.95, 0.05, 0.12);
    }

    vec2 shadowUv = vec2(0.0);
    float currentDepth = 1.0;
    int projectionStatus =
        ProjectShadowCascade(worldPosition, cascadeIndex, shadowUv, currentDepth);
    float primaryVisibility = 1.0;
    bool primaryValid = projectionStatus == 0 &&
        SampleShadowCascadeVisibility(
            worldPosition,
            normal,
            lightDir,
            cascadeIndex,
            primaryVisibility
        );

    float resolvedVisibility = primaryVisibility;
    bool resolvedValid = primaryValid;
    if (!resolvedValid) {
        resolvedValid = ResolveShadowCascadeVisibility(
            worldPosition,
            normal,
            lightDir,
            cascadeIndex,
            activeCascadeCount,
            resolvedVisibility
        );
    }

    if (!resolvedValid) {
        if (projectionStatus == 2) {
            return vec3(1.0, 0.84, 0.08);
        }
        if (projectionStatus == 3) {
            return vec3(0.95, 0.08, 1.0);
        }
        if (projectionStatus == 4) {
            return vec3(0.05, 0.95, 1.0);
        }
        return vec3(1.0, 0.02, 0.08);
    }

    vec3 cascadeColor = ShadowCascadePalette(cascadeIndex);
    vec3 shadowed = vec3(0.025, 0.045, 0.080);
    vec3 lit = mix(vec3(0.86, 0.92, 1.0), cascadeColor, 0.58);
    vec3 color = mix(shadowed, lit, smoothstep(0.0, 1.0, resolvedVisibility));
    if (!primaryValid) {
        color = mix(color, vec3(1.0, 0.48, 0.06), 0.62);
    }

    float edgeDistance = min(
        min(shadowUv.x, 1.0 - shadowUv.x),
        min(shadowUv.y, 1.0 - shadowUv.y)
    );
    if (projectionStatus == 0 && edgeDistance < 0.018) {
        color = mix(color, vec3(1.0), 0.55);
    }

    return clamp(color, vec3(0.0), vec3(1.0));
}

void main() {
    int view = int(objectData.materialControls.x + 0.5);
    ivec2 albedoTexel = DebugTexelCoord(gBufferAlbedo, fragUv);
    ivec2 normalTexel = DebugTexelCoord(gBufferNormalRoughness, fragUv);
    ivec2 materialTexel = DebugTexelCoord(gBufferMaterial, fragUv);
    ivec2 emissiveTexel = DebugTexelCoord(gBufferEmissive, fragUv);
    ivec2 velocityTexel = DebugTexelCoord(gBufferVelocity, fragUv);
    ivec2 depthTexel = DebugTexelCoord(sceneDepth, fragUv);
    vec2 depthUv = DebugTexelCenterUv(sceneDepth, depthTexel);
    vec4 albedo = texelFetch(gBufferAlbedo, albedoTexel, 0);
    vec4 normalRoughness = texelFetch(gBufferNormalRoughness, normalTexel, 0);
    vec4 material = texelFetch(gBufferMaterial, materialTexel, 0);
    vec4 emissive = texelFetch(gBufferEmissive, emissiveTexel, 0);
    vec2 velocity = texelFetch(gBufferVelocity, velocityTexel, 0).rg;
    float depth = texelFetch(sceneDepth, depthTexel, 0).r;

    if (view == 0) {
        outColor = vec4(albedo.rgb, 1.0);
    } else if (view == 1) {
        outColor = vec4(normalRoughness.rgb, 1.0);
    } else if (view == 2) {
        outColor = vec4(vec3(normalRoughness.a), 1.0);
    } else if (view == 3) {
        outColor = vec4(vec3(material.r), 1.0);
    } else if (view == 4) {
        float materialId = material.a;
        float materialCount = max(frameMaterials.materialCounts.x, 0.0);
        if (materialId <= 0.5) {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        } else if (materialId > materialCount + 0.5) {
            outColor = vec4(1.0, 0.0, 0.15, 1.0);
        } else {
            outColor = vec4(Turbo(fract(materialId * 0.173)), 1.0);
        }
    } else if (view == 5) {
        outColor = depth >= 0.999999
            ? vec4(0.0, 0.0, 0.0, 1.0)
            : vec4(DepthDebugColor(depth), 1.0);
    } else if (view == 6) {
        outColor = vec4(emissive.rgb, 1.0);
    } else if (view == 7) {
        float velocityMagnitude = min(length(velocity) * 24.0, 1.0);
        outColor = vec4(abs(velocity) * 12.0, velocityMagnitude, 1.0);
    } else if (view == 8) {
        if (depth >= 0.999999 || albedo.a <= 0.001) {
            outColor = vec4(vec3(1.0), 1.0);
            return;
        }

        vec3 lightDirection = frame.directionalLight.xyz;
        if (dot(lightDirection, lightDirection) < 0.0001) {
            lightDirection = vec3(-0.45, -0.82, -0.35);
        }
        vec3 lightDir = normalize(-lightDirection);
        vec3 normal = normalize(normalRoughness.xyz * 2.0 - 1.0);
        vec3 worldPosition = ReconstructWorldPosition(depthUv, depth);
        outColor = vec4(vec3(ShadowVisibility(worldPosition, normal, lightDir)), 1.0);
    } else if (view == 9) {
        if (depth >= 0.999999 || albedo.a <= 0.001) {
            outColor = vec4(vec3(1.0), 1.0);
            return;
        }

        vec3 worldPosition = ReconstructWorldPosition(depthUv, depth);
        outColor = vec4(ShadowCascadeDebugColor(worldPosition), 1.0);
    } else if (view == 10) {
        outColor = vec4(ShadowCascadeAtlasDebugColor(fragUv), 1.0);
    } else if (view == 12) {
        if (depth >= 0.999999 || albedo.a <= 0.001) {
            outColor = vec4(vec3(0.015, 0.018, 0.024), 1.0);
            return;
        }

        vec3 lightDirection = frame.directionalLight.xyz;
        if (dot(lightDirection, lightDirection) < 0.0001) {
            lightDirection = vec3(-0.45, -0.82, -0.35);
        }
        vec3 lightDir = normalize(-lightDirection);
        vec3 normal = normalize(normalRoughness.xyz * 2.0 - 1.0);
        vec3 worldPosition = ReconstructWorldPosition(depthUv, depth);
        outColor = vec4(
            ShadowCascadeReceiverDebugColor(worldPosition, normal, lightDir),
            1.0
        );
    } else if (view == 11) {
        float occlusion = clamp(material.g, 0.0, 1.0);
        float highContrast = smoothstep(0.86, 1.0, occlusion);
        outColor = vec4(vec3(highContrast), 1.0);
    } else {
        outColor = vec4(albedo.rgb, 1.0);
    }
}
