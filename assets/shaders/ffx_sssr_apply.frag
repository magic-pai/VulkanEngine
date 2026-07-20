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
    mat4 previousView;
    mat4 previousProj;
    vec4 temporalJitter;
    vec4 temporalControls;
    vec4 temporalResolveControls;
    vec4 temporalRejectionControls;
    vec4 environmentControls;
    vec4 reflectionProbeMipControls[4];
} frame;

layout(std430, set = 0, binding = 1) readonly buffer LightData {
    vec4 directionalLight;
    vec4 ambientLight;
} lights;

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

layout(set = 0, binding = 6) uniform sampler2D brdfLut;
layout(set = 0, binding = 7) uniform samplerCube irradianceMap;
layout(set = 0, binding = 8) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 11) uniform samplerCube localReflectionProbeMaps[4];
layout(set = 1, binding = 0) uniform sampler2D gBufferAlbedo;
layout(set = 1, binding = 1) uniform sampler2D gBufferNormalRoughness;
layout(set = 1, binding = 2) uniform sampler2D gBufferMaterial;
layout(set = 1, binding = 4) uniform sampler2D sceneDepth;
layout(set = 1, binding = 5) uniform sampler2D gBufferEmissive;
layout(set = 1, binding = 7) uniform sampler2D gBufferMaterialAux;
layout(set = 1, binding = 17) uniform sampler2D ffxSssrCurrentRadiance;
layout(set = 1, binding = 18) uniform sampler2D ffxSssrHitConfidence;

const int MAX_FRAME_MATERIALS = 256;
const float FFX_SSSR_ROUGHNESS_THRESHOLD = 0.6;

bool HasTextureFlag(float flags, float bit) {
    return mod(floor(flags / bit), 2.0) > 0.5;
}

bool TryGetMaterialRecord(float materialIdFloat, out MaterialRecord materialRecord) {
    materialRecord.baseColorFactor = vec4(1.0);
    materialRecord.materialControls = vec4(1.0, 0.0, 0.2, 0.0);
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
    int materialCount = clamp(
        int(frameMaterials.materialCounts.x + 0.5),
        0,
        MAX_FRAME_MATERIALS
    );
    if (materialId <= 0 || materialId > materialCount) {
        return false;
    }
    materialRecord = frameMaterials.materials[materialId - 1];
    return true;
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

bool SsrProbeFallbackBlendEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 16384.0), 2.0) > 0.5;
}

bool FfxSssrHitProvenanceEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 524288.0), 2.0) > 0.5;
}

float SsrProbeFallbackBlendWeight(float confidence, float roughness) {
    float resolvedConfidence = clamp(confidence, 0.0, 1.0);
    if (!SsrProbeFallbackBlendEnabled()) {
        return resolvedConfidence;
    }

    float confidenceStability = resolvedConfidence * mix(
        0.65,
        1.0,
        smoothstep(0.08, 0.78, resolvedConfidence)
    );
    float roughnessReliability =
        1.0 - smoothstep(0.38, 0.88, clamp(roughness, 0.0, 1.0));
    float roughnessCeiling = mix(0.35, 1.0, roughnessReliability);
    return clamp(confidenceStability * roughnessCeiling, 0.0, 1.0);
}

bool ReflectionProbeDominantMirrorEnabled() {
    float mode = floor(frame.reflectionProbeBlendControls.w + 0.5);
    return mod(
        floor(mode / 4.0),
        2.0
    ) > 0.5;
}

float ReflectionProbeDominantMirrorFactor(float roughness) {
    return 1.0 - smoothstep(0.30, 0.60, clamp(roughness, 0.0, 1.0));
}

float IblSpecularStability(float roughness) {
    float r = clamp(roughness, 0.0, 1.0);
    return mix(0.48, 1.0, smoothstep(0.18, 0.72, r));
}

vec3 GlobalEnvironmentRadiance(vec3 direction, vec3 sunDirection, float roughness) {
    float enabled = clamp(frame.reflectionProbeControls.x, 0.0, 1.0);
    if (enabled <= 0.0001) {
        return vec3(0.0);
    }
    float diffuseIntensity = clamp(frame.reflectionProbeControls.y, 0.0, 4.0);
    float specularIntensity = clamp(frame.reflectionProbeControls.z, 0.0, 4.0);
    float horizonBlend = clamp(frame.reflectionProbeControls.w, 0.0, 1.0);
    float up = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyTop = vec3(0.37, 0.50, 0.72);
    vec3 skyHorizon = vec3(0.68, 0.71, 0.76);
    vec3 ground = vec3(0.09, 0.085, 0.08);
    vec3 base = mix(ground, skyTop, smoothstep(0.05, 1.0, up));
    base = mix(base, skyHorizon, (1.0 - abs(direction.y)) * horizonBlend);
    float sunAmount = max(dot(direction, sunDirection), 0.0);
    float sunPower = mix(128.0, 24.0, roughness);
    float sunDisk = pow(max(sunAmount, 0.0001), sunPower);
    float intensity = mix(specularIntensity, diffuseIntensity, smoothstep(0.45, 1.0, roughness));
    vec3 procedural = (base + vec3(1.12, 1.08, 1.0) * sunDisk * mix(5.0, 2.2, roughness)) * intensity;
    float cubemapSampling = clamp(frame.reflectionProbeBlendControls.z, 0.0, 1.0);
    if (cubemapSampling <= 0.0001) {
        return procedural * enabled;
    }
    vec3 sampleDirection = dot(direction, direction) > 0.0001
        ? normalize(direction)
        : vec3(0.0, 1.0, 0.0);
    vec3 sampledDiffuse = max(texture(irradianceMap, sampleDirection).rgb, vec3(0.0)) * diffuseIntensity;
    vec3 sampledSpecular = max(textureLod(prefilteredMap, sampleDirection, clamp(roughness, 0.0, 1.0) * 4.0).rgb, vec3(0.0)) * specularIntensity;
    vec3 sampled = mix(sampledSpecular, sampledDiffuse, smoothstep(0.45, 1.0, roughness));
    return mix(procedural, sampled, 0.65 * cubemapSampling) * enabled;
}

float LocalProbeShapeCoordinate(int probeIndex, vec3 worldPosition) {
    vec4 positionRadius = frame.reflectionProbePositionRadius[probeIndex];
    vec4 box = frame.reflectionProbeBoxExtentsProjectionArray[probeIndex];
    if (box.w > 0.5) {
        vec3 normalizedBox = abs(worldPosition - positionRadius.xyz) / max(box.xyz, vec3(0.001));
        return max(max(normalizedBox.x, normalizedBox.y), normalizedBox.z);
    }
    return length(worldPosition - positionRadius.xyz) / max(positionRadius.w, 0.001);
}

float LocalProbeCoverage(int probeIndex, vec3 worldPosition) {
    vec4 controls = frame.reflectionProbeControlsArray[probeIndex];
    if (controls.x <= 0.0001 || controls.y <= 0.0001 || controls.z <= 0.0001) {
        return 0.0;
    }
    float edgeWidth = mix(0.08, 0.45, clamp(controls.z, 0.0, 1.0));
    return 1.0 - smoothstep(1.0 - edgeWidth, 1.0, LocalProbeShapeCoordinate(probeIndex, worldPosition));
}

vec3 BoxProjectedLocalReflectionDirection(int probeIndex, vec3 direction, vec3 worldPosition) {
    vec4 positionRadius = frame.reflectionProbePositionRadius[probeIndex];
    vec4 box = frame.reflectionProbeBoxExtentsProjectionArray[probeIndex];
    vec3 sampleDirection = dot(direction, direction) > 0.0001 ? normalize(direction) : vec3(0.0, 1.0, 0.0);
    if (box.w <= 0.5) {
        return sampleDirection;
    }
    vec3 localPosition = worldPosition - positionRadius.xyz;
    vec3 extents = max(box.xyz, vec3(0.001));
    vec3 safeDirection = sampleDirection;
    safeDirection.x = abs(safeDirection.x) < 0.0001
        ? (safeDirection.x < 0.0 ? -0.0001 : 0.0001)
        : safeDirection.x;
    safeDirection.y = abs(safeDirection.y) < 0.0001
        ? (safeDirection.y < 0.0 ? -0.0001 : 0.0001)
        : safeDirection.y;
    safeDirection.z = abs(safeDirection.z) < 0.0001
        ? (safeDirection.z < 0.0 ? -0.0001 : 0.0001)
        : safeDirection.z;
    vec3 tMin = (-extents - localPosition) / safeDirection;
    vec3 tMax = (extents - localPosition) / safeDirection;
    float hitDistance = min(min(max(tMin.x, tMax.x), max(tMin.y, tMax.y)), max(tMin.z, tMax.z));
    if (hitDistance <= 0.0001 || hitDistance > 100000.0) {
        return sampleDirection;
    }
    return normalize(localPosition + sampleDirection * hitDistance);
}

vec3 SampleLocalProbe(int slotIndex, vec3 direction, float lod) {
    if (slotIndex == 0) return textureLod(localReflectionProbeMaps[0], direction, lod).rgb;
    if (slotIndex == 1) return textureLod(localReflectionProbeMaps[1], direction, lod).rgb;
    if (slotIndex == 2) return textureLod(localReflectionProbeMaps[2], direction, lod).rgb;
    return textureLod(localReflectionProbeMaps[3], direction, lod).rgb;
}

vec3 SelectedLocalProbeRadiance(vec3 direction, float roughness, vec3 worldPosition, vec3 globalRadiance) {
    int probeCount = clamp(int(frame.reflectionProbeBlendControls.x + 0.5), 0, 4);
    if (frame.reflectionProbeBlendControls.y <= 0.5 || probeCount <= 0) {
        return globalRadiance;
    }
    float weights[4];
    float coverages[4];
    float priorities[4];
    float totalWeight = 0.0;
    float maxCoverage = 0.0;
    float bestPriority = 0.0;
    bool productionBlend =
        mod(floor(frame.reflectionProbeBlendControls.w + 0.5), 2.0) > 0.5;
    for (int index = 0; index < 4; ++index) {
        float coverage = index < probeCount ? LocalProbeCoverage(index, worldPosition) : 0.0;
        float priority = 0.0;
        if (index < probeCount) {
            vec4 positionRadius = frame.reflectionProbePositionRadius[index];
            vec4 box = frame.reflectionProbeBoxExtentsProjectionArray[index];
            if (box.w > 0.5) {
                vec3 extents = max(box.xyz, vec3(0.01));
                priority = 1.0 / max(extents.x * extents.y * extents.z, 0.000001);
            } else {
                float radius = max(positionRadius.w, 0.01);
                priority = 1.0 / (radius * radius * radius);
            }
        }
        coverages[index] = coverage;
        priorities[index] = priority;
        if (coverage > 0.0001) {
            bestPriority = max(bestPriority, priority);
        }
    }
    for (int index = 0; index < 4; ++index) {
        float weight = 0.0;
        if (index < probeCount) {
            weight = productionBlend
                ? coverages[index] * smoothstep(
                    0.35,
                    0.95,
                    priorities[index] / max(bestPriority, 0.000001)
                )
                : coverages[index];
        }
        weights[index] = weight;
        totalWeight += weight;
        maxCoverage = productionBlend
            ? max(maxCoverage, coverages[index])
            : clamp(totalWeight, 0.0, 1.0);
    }
    if (totalWeight <= 0.0001) {
        return globalRadiance;
    }
    int dominantProbeIndex = -1;
    float dominantRawWeight = 0.0;
    for (int index = 0; index < 4; ++index) {
        if (index < probeCount && weights[index] > dominantRawWeight) {
            dominantRawWeight = weights[index];
            dominantProbeIndex = index;
        }
    }
    float dominantMirrorFactor = productionBlend &&
        ReflectionProbeDominantMirrorEnabled() &&
        dominantProbeIndex >= 0
        ? ReflectionProbeDominantMirrorFactor(roughness)
        : 0.0;
    vec3 localBlend = vec3(0.0);
    float roughnessClamped = clamp(roughness, 0.0, 1.0);
    for (int index = 0; index < 4; ++index) {
        if (index >= probeCount || weights[index] <= 0.0001) continue;
        vec4 color = frame.reflectionProbeColorArray[index];
        vec4 controls = frame.reflectionProbeControlsArray[index];
        vec3 sampled = globalRadiance * max(color.rgb, vec3(0.0));
        if (color.a > 0.5 && frame.reflectionProbeMipControls[index].z > 0.5) {
            int slotIndex = clamp(int(color.a + 0.5) - 1, 0, 3);
            vec3 sampleDirection = BoxProjectedLocalReflectionDirection(index, direction, worldPosition);
            vec3 cubemap = max(SampleLocalProbe(slotIndex, sampleDirection, roughnessClamped * max(frame.reflectionProbeMipControls[index].x, 0.0)), vec3(0.0));
            sampled = mix(sampled, cubemap * max(color.rgb, vec3(0.0)), 1.0 - smoothstep(0.88, 1.0, roughnessClamped));
        }
        float effectiveWeight = mix(
            weights[index],
            index == dominantProbeIndex ? totalWeight : 0.0,
            dominantMirrorFactor
        );
        if (effectiveWeight <= 0.0001) continue;
        localBlend += sampled * clamp(controls.y, 0.0, 4.0) * IblSpecularStability(roughnessClamped) * (effectiveWeight / totalWeight);
    }
    return mix(globalRadiance, localBlend, maxCoverage);
}

void main() {
    vec4 albedo = texture(gBufferAlbedo, fragUv);
    vec4 normalRoughness = texture(gBufferNormalRoughness, fragUv);
    vec4 material = texture(gBufferMaterial, fragUv);
    float depth = texture(sceneDepth, fragUv).r;
    if (depth >= 0.999999 || albedo.a <= 0.001) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float roughness = clamp(normalRoughness.w, 0.04, 1.0);
    float metallic = clamp(material.r, 0.0, 1.0);
    float occlusion = clamp(material.g, 0.0, 1.0);
    float specularTextureFactor = clamp(material.b, 0.0, 1.0);
    MaterialRecord materialRecord;
    bool hasMaterialRecord = TryGetMaterialRecord(material.a, materialRecord);
    if (hasMaterialRecord) {
        roughness = clamp(
            mix(
                clamp(materialRecord.cameraControls.y, 0.04, 1.0),
                roughness,
                materialRecord.cameraControls.z
            ),
            0.04,
            1.0
        );
        metallic = clamp(
            mix(
                clamp(materialRecord.cameraControls.x, 0.0, 1.0),
                metallic,
                materialRecord.cameraControls.z
            ),
            0.0,
            1.0
        );
    }
    if (roughness >= FFX_SSSR_ROUGHNESS_THRESHOLD) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 specularFactor = hasMaterialRecord
        ? materialRecord.specularFactor
        : vec4(1.0);
    vec3 specularColorFactor = clamp(
        specularFactor.rgb * max(specularFactor.a, 0.0),
        vec3(0.0),
        vec3(2.0)
    ) * specularTextureFactor;
    vec3 dielectricF0 = clamp(
        vec3(0.04) * specularColorFactor,
        vec3(0.0),
        vec3(1.0)
    );
    vec3 f0 = mix(dielectricF0, albedo.rgb, metallic);

    vec3 worldPosition = ReconstructWorldPosition(fragUv, depth);
    vec3 cameraPosition = frame.invView[3].xyz;
    vec3 viewDirection = normalize(cameraPosition - worldPosition);
    vec3 normal = normalize(normalRoughness.xyz * 2.0 - 1.0);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    vec2 envBrdf = max(texture(
        brdfLut,
        vec2(clamp(nDotV, 0.0, 1.0), roughness)
    ).rg, vec2(0.0));
    vec3 envSpecularBrdf = max(f0 * envBrdf.x + envBrdf.y, vec3(0.0));

    vec4 resolved = texture(ffxSssrCurrentRadiance, fragUv);
    vec3 lightDirection = lights.directionalLight.xyz;
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = vec3(-0.45, -0.82, -0.35);
    }
    vec3 lightDir = normalize(-lightDirection);
    vec3 reflection = reflect(-viewDirection, normal);
    vec3 globalFallback = GlobalEnvironmentRadiance(reflection, lightDir, roughness);
    vec3 probeFallback = SelectedLocalProbeRadiance(
        reflection,
        roughness,
        worldPosition,
        globalFallback
    );
    float provenanceConfidence = FfxSssrHitProvenanceEnabled()
        ? clamp(texture(ffxSssrHitConfidence, fragUv).r, 0.0, 1.0)
        : clamp(resolved.a, 0.0, 1.0);
    float blendWeight = SsrProbeFallbackBlendWeight(
        provenanceConfidence * clamp(frame.ssrControls.x, 0.0, 1.0),
        roughness
    );
    float ambientStrength = max(lights.ambientLight.x, 0.08);
    float specularScale = floor(frame.reflectionProbeBlendControls.w + 0.5) > 1.5
        ? 1.0
        : 0.36 * ambientStrength;
    vec3 reflectionDelta = FfxSssrHitProvenanceEnabled()
        ? (max(resolved.rgb, vec3(0.0)) - probeFallback)
        : max(resolved.rgb, vec3(0.0));
    vec3 contribution = reflectionDelta *
        envSpecularBrdf *
        max(specularScale, 0.0) *
        IblSpecularStability(roughness) *
        occlusion *
        blendWeight;
    outColor = vec4(contribution, 1.0);
}
