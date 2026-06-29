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
} frame;

struct LocalLightRecord {
    vec4 positionRadius;
    vec4 colorIntensity;
    vec4 directionType;
    vec4 parameters;
};

struct LightTileRecord {
    uvec4 offsetCount;
    uvec4 overflowOffsetCount;
};

layout(std430, set = 0, binding = 1) readonly buffer LightData {
    vec4 directionalLight;
    vec4 ambientLight;
    vec4 lightCounts;
    vec4 tileInfo;
    LocalLightRecord localLights[64];
    LightTileRecord lightTiles[8192];
    uvec4 tileLightIndexGroups[32768];
    uint tileOverflowLightIndices[65536];
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

layout(std430, set = 0, binding = 4) readonly buffer DirectionalShadowCascades {
    vec4 cascadeInfo;
    vec4 splitDepths;
    vec4 texelWorldSizes;
    vec4 cascadeBlendControls;
    mat4 fallbackViewProjection;
    mat4 viewProjections[4];
} shadowCascades;

struct LocalShadowTileRecord {
    mat4 viewProjection;
    uvec4 tileInfo;
    vec4 lightInfo;
};

layout(std430, set = 0, binding = 5) readonly buffer LocalShadowData {
    uvec4 atlasInfo;
    uvec4 atlasInfo2;
    vec4 filterControls;
    vec4 softShadowControls;
    LocalShadowTileRecord tiles[64];
} localShadows;

layout(push_constant) uniform ObjectPushConstants {
    mat4 model;
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
layout(set = 1, binding = 12) uniform sampler2D localShadowSampler;

const float PI = 3.14159265359;
const int MAX_LOCAL_LIGHTS = 64;
const int MAX_LIGHT_TILES = 8192;
const int MAX_LIGHTS_PER_TILE = 16;
const int LIGHT_INDEX_GROUPS_PER_TILE = 4;
const int MAX_LIGHT_TILE_OVERFLOW_INDICES = 65536;
const int MAX_FRAME_MATERIALS = 256;
const int MAX_DIRECTIONAL_SHADOW_CASCADES = 4;
const int MAX_LOCAL_SHADOW_TILES = 64;

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

float DistributionGGX(vec3 normal, vec3 halfDir, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float nDotH = max(dot(normal, halfDir), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denom = nDotH2 * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, 0.000001);
}

float GeometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
    return GeometrySchlickGGX(max(dot(normal, viewDir), 0.0), roughness) *
        GeometrySchlickGGX(max(dot(normal, lightDir), 0.0), roughness);
}

float Pow5(float value) {
    float value2 = value * value;
    return value2 * value2 * value;
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * Pow5(clamp(1.0 - cosTheta, 0.0, 1.0));
}

vec3 DirectPbrContribution(
    vec3 baseColor,
    float roughness,
    float metallic,
    vec3 normal,
    vec3 viewDir,
    vec3 lightDir,
    vec3 radiance,
    float specularStrength,
    vec3 specularColorFactor,
    float clearcoat,
    float clearcoatRoughness,
    out vec3 specularOut
) {
    vec3 halfDir = normalize(lightDir + viewDir);
    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 dielectricF0 = clamp(vec3(0.04) * specularColorFactor, vec3(0.0), vec3(1.0));
    vec3 f0 = mix(dielectricF0, baseColor, metallic);
    vec3 fresnel = FresnelSchlick(max(dot(halfDir, viewDir), 0.0), f0);
    float distribution = DistributionGGX(normal, halfDir, roughness);
    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);
    vec3 specular = ((distribution * geometry * fresnel) /
        max(4.0 * nDotV * nDotL, 0.000001)) * specularStrength;
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * baseColor / PI;
    float coatRoughness = clamp(mix(0.04, 0.55, clearcoatRoughness), 0.04, 1.0);
    float coatDistribution = DistributionGGX(normal, halfDir, coatRoughness);
    float coatGeometry = GeometrySmith(normal, viewDir, lightDir, coatRoughness);
    vec3 coatFresnel = FresnelSchlick(max(dot(halfDir, viewDir), 0.0), vec3(0.04));
    vec3 coatSpecular = ((coatDistribution * coatGeometry * coatFresnel) /
        max(4.0 * nDotV * nDotL, 0.000001)) *
        specularStrength *
        clamp(clearcoat, 0.0, 1.0);

    specularOut = (specular + coatSpecular) * radiance * nDotL;
    return (diffuse + specular + coatSpecular) * radiance * nDotL;
}

vec3 EnvironmentRadiance(vec3 direction, vec3 sunDirection, float roughness) {
    float up = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyTop = vec3(0.37, 0.50, 0.72);
    vec3 skyHorizon = vec3(0.68, 0.71, 0.76);
    vec3 ground = vec3(0.09, 0.085, 0.08);
    vec3 base = mix(ground, skyTop, smoothstep(0.05, 1.0, up));
    base = mix(base, skyHorizon, (1.0 - abs(direction.y)) * 0.22);

    float sunAmount = max(dot(direction, sunDirection), 0.0);
    float sunPower = mix(1024.0, 24.0, roughness);
    float sunDisk = pow(sunAmount, sunPower);
    return base + vec3(1.12, 1.08, 1.0) * sunDisk * mix(5.0, 2.2, roughness);
}

vec3 LocalShadowAtlasDebugColor(vec2 uv) {
    int assignedTileCount = clamp(int(localShadows.atlasInfo.x), 0, MAX_LOCAL_SHADOW_TILES);
    int tileSize = int(localShadows.atlasInfo.y);
    int tileColumns = int(localShadows.atlasInfo.z);
    int tileRows = int(localShadows.atlasInfo.w);
    int tileCapacity = int(localShadows.atlasInfo2.x);
    int requestedTileCount = int(localShadows.atlasInfo2.y);
    int droppedTileCount = int(localShadows.atlasInfo2.z);
    if (assignedTileCount <= 0 || tileSize <= 0 || tileColumns <= 0 || tileRows <= 0) {
        return vec3(0.12, 0.02, 0.05);
    }

    vec2 grid = uv * vec2(float(tileColumns), float(tileRows));
    ivec2 tileCoord = ivec2(floor(grid));
    if (tileCoord.x < 0 || tileCoord.y < 0 ||
        tileCoord.x >= tileColumns || tileCoord.y >= tileRows) {
        return vec3(0.02, 0.02, 0.025);
    }

    int tileIndex = tileCoord.y * tileColumns + tileCoord.x;
    vec2 tileUv = fract(grid);
    float atlasDepth = texture(localShadowSampler, uv).r;
    vec3 depthColor = mix(
        vec3(0.02, 0.025, 0.035),
        vec3(0.92, 0.94, 0.98),
        clamp(1.0 - atlasDepth, 0.0, 1.0)
    );
    if (tileIndex >= assignedTileCount) {
        if (tileIndex < tileCapacity) {
            return mix(vec3(0.015, 0.025, 0.035), vec3(0.08, 0.11, 0.13), 0.35);
        }
        return vec3(0.01, 0.012, 0.014);
    }

    LocalShadowTileRecord tile = localShadows.tiles[tileIndex];
    uint lightKind = tile.tileInfo.w;
    vec3 kindTint = vec3(0.12, 0.70, 1.00);
    if (lightKind == 2u) {
        kindTint = vec3(1.00, 0.74, 0.18);
    } else if (lightKind == 3u) {
        kindTint = vec3(1.00, 0.28, 0.72);
    }

    vec3 color = mix(depthColor, kindTint, 0.38);
    float localGridLine = step(tileUv.x, 0.025) +
        step(tileUv.y, 0.025) +
        step(0.975, tileUv.x) +
        step(0.975, tileUv.y);
    if (localGridLine > 0.0) {
        color = mix(color, vec3(1.0), 0.75);
    }

    float occupancy = clamp(float(assignedTileCount) / max(float(tileCapacity), 1.0), 0.0, 1.0);
    color = mix(color, vec3(1.0, 0.30, 0.08), occupancy * 0.22);
    if (droppedTileCount > 0 || requestedTileCount > tileCapacity) {
        color = mix(color, vec3(1.0, 0.04, 0.02), 0.35);
    }
    return clamp(color, vec3(0.0), vec3(1.0));
}

int DominantPointShadowFace(vec3 fromLight) {
    vec3 absDir = abs(fromLight);
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        return fromLight.x >= 0.0 ? 0 : 1;
    }
    if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        return fromLight.y >= 0.0 ? 2 : 3;
    }
    return fromLight.z >= 0.0 ? 4 : 5;
}

vec3 PointShadowFaceColor(int faceIndex) {
    if (faceIndex == 0) {
        return vec3(1.00, 0.18, 0.10);
    }
    if (faceIndex == 1) {
        return vec3(0.12, 0.64, 1.00);
    }
    if (faceIndex == 2) {
        return vec3(0.18, 0.95, 0.26);
    }
    if (faceIndex == 3) {
        return vec3(1.00, 0.76, 0.10);
    }
    if (faceIndex == 4) {
        return vec3(0.78, 0.28, 1.00);
    }
    return vec3(0.12, 1.00, 0.84);
}

float PointShadowFaceSeamRisk(vec3 fromLight) {
    vec3 absDir = abs(fromLight);
    float dominant = max(max(absDir.x, absDir.y), absDir.z);
    if (dominant <= 0.0001) {
        return 1.0;
    }

    float second = min(
        max(absDir.x, absDir.y),
        min(max(absDir.x, absDir.z), max(absDir.y, absDir.z))
    );
    float axisGap = clamp((dominant - second) / dominant, 0.0, 1.0);
    return 1.0 - smoothstep(0.02, 0.16, axisGap);
}

bool LocalShadowFaceDebugColor(
    vec3 worldPosition,
    out vec3 debugColor
) {
    int assignedTileCount = clamp(int(localShadows.atlasInfo.x), 0, MAX_LOCAL_SHADOW_TILES);
    int localLightCount = clamp(int(lights.lightCounts.z + 0.5), 0, MAX_LOCAL_LIGHTS);
    if (assignedTileCount <= 0 || localLightCount <= 0) {
        debugColor = vec3(0.08, 0.02, 0.05);
        return false;
    }

    float bestInfluence = 0.0;
    vec3 bestColor = vec3(0.04, 0.06, 0.08);
    bool foundPointShadow = false;
    for (int localLightIndex = 0; localLightIndex < MAX_LOCAL_LIGHTS; ++localLightIndex) {
        if (localLightIndex >= localLightCount) {
            break;
        }

        LocalLightRecord localLight = lights.localLights[localLightIndex];
        uint lightKind = uint(clamp(int(localLight.directionType.w + 0.5), 0, 3));
        if (lightKind != 0u) {
            continue;
        }

        float radius = max(localLight.positionRadius.w, 0.001);
        vec3 fromLight = worldPosition - localLight.positionRadius.xyz;
        float distanceToLight = length(fromLight);
        float attenuation = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
        if (attenuation <= 0.0001) {
            continue;
        }

        int faceIndex = DominantPointShadowFace(fromLight);
        bool hasFaceTile = false;
        for (int tileIndex = 0; tileIndex < MAX_LOCAL_SHADOW_TILES; ++tileIndex) {
            if (tileIndex >= assignedTileCount) {
                break;
            }

            LocalShadowTileRecord tile = localShadows.tiles[tileIndex];
            if (int(tile.tileInfo.y) == localLightIndex &&
                int(tile.tileInfo.z) == faceIndex &&
                tile.tileInfo.w == 1u) {
                hasFaceTile = true;
                break;
            }
        }

        if (!hasFaceTile) {
            continue;
        }

        float influence = attenuation * max(localLight.colorIntensity.w, 0.001);
        if (influence <= bestInfluence) {
            continue;
        }

        float seamRisk = PointShadowFaceSeamRisk(fromLight);
        vec3 faceColor = PointShadowFaceColor(faceIndex);
        bestColor = mix(faceColor * 0.72, vec3(1.0, 0.97, 0.78), seamRisk * 0.88);
        bestColor += vec3(attenuation) * 0.08;
        bestInfluence = influence;
        foundPointShadow = true;
    }

    debugColor = clamp(bestColor, vec3(0.0), vec3(1.0));
    return foundPointShadow;
}

float SampleLocalShadowTileVisibility(
    LocalShadowTileRecord tile,
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir
) {
    int tileSize = int(localShadows.atlasInfo.y);
    int tileColumns = int(localShadows.atlasInfo.z);
    int tileRows = int(localShadows.atlasInfo.w);
    if (tileSize <= 0 || tileColumns <= 0 || tileRows <= 0) {
        return 1.0;
    }

    vec4 lightSpacePosition = tile.viewProjection * vec4(worldPosition, 1.0);
    if (abs(lightSpacePosition.w) <= 0.000001) {
        return 1.0;
    }

    vec3 projectionCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    vec2 shadowUv = projectionCoords.xy * 0.5 + 0.5;
    float currentDepth = projectionCoords.z;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0) {
        return 1.0;
    }

    int tileIndex = int(tile.tileInfo.x);
    int tileX = tileIndex % tileColumns;
    int tileY = tileIndex / tileColumns;
    if (tileX < 0 || tileY < 0 || tileX >= tileColumns || tileY >= tileRows) {
        return 1.0;
    }

    vec2 atlasTileScale = vec2(1.0 / float(tileColumns), 1.0 / float(tileRows));
    vec2 tileOrigin = vec2(float(tileX), float(tileY)) * atlasTileScale;
    vec2 atlasUv = tileOrigin + shadowUv * atlasTileScale;
    vec2 tileMax = tileOrigin + atlasTileScale;
    vec2 texelSize = 1.0 / vec2(textureSize(localShadowSampler, 0));
    float biasMin = max(localShadows.filterControls.x, 0.0);
    float biasSlope = max(localShadows.filterControls.y, 0.0);
    float nDotL = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = max(biasSlope * (1.0 - nDotL), biasMin);
    int kernelRadius = clamp(int(localShadows.filterControls.w + 0.5), 0, 2);
    if (kernelRadius <= 0) {
        float closestDepth = texture(localShadowSampler, atlasUv).r;
        float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
        return 1.0 - shadow * clamp(frame.shadowControls.y, 0.0, 1.0);
    }

    float shadow = 0.0;
    int sampleCount = 0;
    float filterRadius = max(localShadows.filterControls.z, 0.0);
    float pcssStrength = clamp(localShadows.softShadowControls.x, 0.0, 1.0);
    if (pcssStrength > 0.0001 && kernelRadius > 0) {
        float blockerDepthSum = 0.0;
        int blockerCount = 0;
        float searchRadius = max(filterRadius, 1.0) * (1.0 + pcssStrength);
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                vec2 blockerUv = clamp(
                    atlasUv + vec2(x, y) * texelSize * searchRadius,
                    tileOrigin + texelSize,
                    tileMax - texelSize
                );
                float blockerDepth = texture(localShadowSampler, blockerUv).r;
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
            filterRadius *= 1.0 + penumbra * pcssStrength * 3.0;
        }
    }

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
            float closestDepth = texture(localShadowSampler, sampleUv).r;
            shadow += currentDepth - bias > closestDepth ? 1.0 : 0.0;
            ++sampleCount;
        }
    }

    float occlusion = shadow / max(float(sampleCount), 1.0);
    return 1.0 - occlusion * clamp(frame.shadowControls.y, 0.0, 1.0);
}

float LocalShadowVisibility(
    int localLightIndex,
    LocalLightRecord localLight,
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir
) {
    if (frame.shadowControls.x < 0.5 || frame.shadowControls.y <= 0.001) {
        return 1.0;
    }

    int assignedTileCount = clamp(int(localShadows.atlasInfo.x), 0, MAX_LOCAL_SHADOW_TILES);
    if (assignedTileCount <= 0 || localLightIndex < 0) {
        return 1.0;
    }

    int targetFace = 0;
    uint lightKind = uint(clamp(int(localLight.directionType.w + 0.5), 0, 3));
    if (lightKind == 0u) {
        targetFace = DominantPointShadowFace(worldPosition - localLight.positionRadius.xyz);
    } else if (lightKind != 1u) {
        return 1.0;
    }

    for (int tileIndex = 0; tileIndex < MAX_LOCAL_SHADOW_TILES; ++tileIndex) {
        if (tileIndex >= assignedTileCount) {
            break;
        }
        LocalShadowTileRecord tile = localShadows.tiles[tileIndex];
        if (int(tile.tileInfo.y) != localLightIndex) {
            continue;
        }
        if (int(tile.tileInfo.z) != targetFace) {
            continue;
        }
        return SampleLocalShadowTileVisibility(
            tile,
            worldPosition,
            normal,
            lightDir
        );
    }

    return 1.0;
}

vec3 DirectionToLocalLight(LocalLightRecord localLight, vec3 worldPosition) {
    vec3 toLight = localLight.positionRadius.xyz - worldPosition;
    if (dot(toLight, toLight) <= 0.000001) {
        return vec3(0.0, 1.0, 0.0);
    }
    return normalize(toLight);
}

vec3 VolumeTransmittance(MaterialRecord materialRecord) {
    float thickness = max(materialRecord.volumeFactor.x, 0.0);
    float attenuationDistance = max(materialRecord.volumeFactor.y, 0.0001);
    if (materialRecord.volumeFactor.w < 0.5 || thickness <= 0.001) {
        return vec3(1.0);
    }

    vec3 attenuationColor = clamp(materialRecord.materialCustom.rgb, vec3(0.0), vec3(1.0));
    vec3 absorption = -log(max(attenuationColor, vec3(0.001))) / attenuationDistance;
    return exp(-absorption * thickness);
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

float SampleShadowCascadeVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int cascadeIndex
);

float ApplyShadowDistanceFade(
    float visibility,
    int cascadeIndex,
    int activeCount,
    float viewDepth
);

float ShadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDir) {
    if (frame.shadowControls.x < 0.5 || frame.shadowControls.y <= 0.001) {
        return 1.0;
    }

    int activeCascadeCount = clamp(
        int(shadowCascades.cascadeInfo.x + 0.5),
        0,
        MAX_DIRECTIONAL_SHADOW_CASCADES
    );
    if (activeCascadeCount <= 0) {
        return 1.0;
    }

    vec4 viewPosition = frame.view * vec4(worldPosition, 1.0);
    float viewDepth = abs(viewPosition.z);
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

    float visibility = SampleShadowCascadeVisibility(
        worldPosition,
        normal,
        lightDir,
        cascadeIndex
    );
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

    float nextVisibility = SampleShadowCascadeVisibility(
        worldPosition,
        normal,
        lightDir,
        cascadeIndex + 1
    );
    visibility = mix(visibility, nextVisibility, blendFactor);
    return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCascadeCount, viewDepth);
}

float ContactShadowVisibility(
    vec2 uv,
    float depth,
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir
) {
    float strength = clamp(frame.contactShadowControls.x, 0.0, 1.0);
    float rayLength = clamp(frame.contactShadowControls.y, 0.0, 1.0);
    int stepCount = clamp(int(frame.contactShadowControls.z + 0.5), 0, 12);
    if (strength <= 0.0001 || rayLength <= 0.0001 || stepCount <= 0) {
        return 1.0;
    }

    float nDotL = max(dot(normal, lightDir), 0.0);
    if (nDotL <= 0.001) {
        return 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(sceneDepth, 0));
    vec4 viewPosition = frame.view * vec4(worldPosition, 1.0);
    float viewDepth = max(abs(viewPosition.z), 0.0001);
    float rayPixels = clamp(
        (rayLength / viewDepth) * 220.0,
        1.0,
        48.0
    );
    vec4 lightClipA = frame.proj * (viewPosition + vec4(lightDir * rayLength, 0.0));
    vec2 rayDir = vec2(0.0);
    if (abs(lightClipA.w) > 0.000001) {
        vec2 lightUv = lightClipA.xy / lightClipA.w * 0.5 + 0.5;
        rayDir = uv - lightUv;
    }
    if (dot(rayDir, rayDir) <= 0.000001) {
        rayDir = normalize(lightDir.xy + vec2(0.37, 0.19));
    } else {
        rayDir = normalize(rayDir);
    }

    float occlusion = 0.0;
    for (int index = 1; index <= 12; ++index) {
        if (index > stepCount) {
            break;
        }

        float t = float(index) / float(stepCount);
        vec2 sampleUv = uv + rayDir * texelSize * rayPixels * t;
        if (sampleUv.x <= 0.0 || sampleUv.x >= 1.0 ||
            sampleUv.y <= 0.0 || sampleUv.y >= 1.0) {
            continue;
        }

        float sampleDepth = texture(sceneDepth, sampleUv).r;
        if (sampleDepth >= 0.999999 || sampleDepth <= 0.0) {
            continue;
        }

        vec3 sampleWorld = ReconstructWorldPosition(sampleUv, sampleDepth);
        vec3 toSample = sampleWorld - worldPosition;
        float alongLight = dot(toSample, lightDir);
        float normalSeparation = dot(toSample, normal);
        if (alongLight > 0.0 &&
            alongLight < rayLength &&
            normalSeparation > 0.0005 &&
            normalSeparation < rayLength * 0.65) {
            float fade = 1.0 - t;
            occlusion = max(occlusion, fade * smoothstep(0.0, 0.08, normalSeparation));
        }
    }

    float grazingBoost = mix(1.0, 0.35, nDotL);
    return 1.0 - clamp(occlusion * strength * grazingBoost, 0.0, 1.0);
}

float SampleShadowCascadeVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int cascadeIndex
) {
    vec4 lightSpacePosition = shadowCascades.viewProjections[cascadeIndex] *
        vec4(worldPosition, 1.0);
    if (abs(lightSpacePosition.w) <= 0.000001) {
        return 1.0;
    }

    vec3 projectionCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    vec2 shadowUv = projectionCoords.xy * 0.5 + 0.5;
    float currentDepth = projectionCoords.z;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0) {
        return 1.0;
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
    return 1.0 - occlusion * clamp(frame.shadowControls.y, 0.0, 1.0);
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

int TileLightIndex(uint packedGroupOffset, int assignmentIndex) {
    uint groupIndex = packedGroupOffset + uint(assignmentIndex / 4);
    uint componentIndex = uint(assignmentIndex % 4);
    uvec4 packedIndices = lights.tileLightIndexGroups[groupIndex];
    if (componentIndex == 0u) {
        return int(packedIndices.x);
    }
    if (componentIndex == 1u) {
        return int(packedIndices.y);
    }
    if (componentIndex == 2u) {
        return int(packedIndices.z);
    }
    return int(packedIndices.w);
}

int TileOverflowLightIndex(uint overflowOffset, int overflowIndex) {
    uint absoluteIndex = overflowOffset + uint(overflowIndex);
    if (absoluteIndex >= uint(MAX_LIGHT_TILE_OVERFLOW_INDICES)) {
        return -1;
    }

    return int(lights.tileOverflowLightIndices[absoluteIndex]);
}

bool UseTileLightAssignments(vec2 fragCoord, out LightTileRecord tileRecord) {
    tileRecord.offsetCount = uvec4(0u);
    tileRecord.overflowOffsetCount = uvec4(0u);
    if (lights.lightCounts.w < 0.5) {
        return false;
    }

    int tileSize = max(int(lights.tileInfo.x + 0.5), 1);
    int tileCountX = int(lights.tileInfo.y + 0.5);
    int tileCountY = int(lights.tileInfo.z + 0.5);
    if (tileCountX <= 0 || tileCountY <= 0) {
        return false;
    }

    ivec2 tile = ivec2(floor(fragCoord)) / tileSize;
    if (tile.x < 0 || tile.y < 0 || tile.x >= tileCountX || tile.y >= tileCountY) {
        return false;
    }

    int tileIndex = tile.y * tileCountX + tile.x;
    if (tileIndex < 0 || tileIndex >= MAX_LIGHT_TILES) {
        return false;
    }

    tileRecord = lights.lightTiles[tileIndex];
    return true;
}

void AccumulateLocalLight(
    int localLightIndex,
    LocalLightRecord localLight,
    vec3 baseColor,
    float roughness,
    float metallic,
    vec3 normal,
    vec3 viewDir,
    vec3 worldPosition,
    float specularStrength,
    vec3 specularColorFactor,
    float clearcoat,
    float clearcoatRoughness,
    inout vec3 direct,
    inout vec3 directSpecular,
    inout float localLightInfluenceCount
) {
    vec4 positionRadius = localLight.positionRadius;
    vec4 colorIntensity = localLight.colorIntensity;
    vec4 directionType = localLight.directionType;
    vec4 parameters = localLight.parameters;
    vec3 toLight = positionRadius.xyz - worldPosition;
    float distanceToLight = length(toLight);
    float radius = max(positionRadius.w, 0.001);
    if (distanceToLight >= radius || colorIntensity.w <= 0.0) {
        return;
    }

    float lightInfluence = 1.0;
    vec3 localLightDir = toLight / max(distanceToLight, 0.001);
    float normalizedDistance = clamp(distanceToLight / radius, 0.0, 1.0);
    float attenuation = pow(1.0 - normalizedDistance * normalizedDistance, 2.0);
    if (directionType.w > 0.5 && directionType.w < 1.5) {
        vec3 spotDirection = directionType.xyz;
        if (dot(spotDirection, spotDirection) < 0.0001) {
            spotDirection = vec3(0.0, -1.0, 0.0);
        }
        spotDirection = normalize(spotDirection);

        float innerCone = max(parameters.x, parameters.y);
        float outerCone = min(parameters.x, parameters.y);
        float coneWidth = max(innerCone - outerCone, 0.0001);
        float coneCos = dot(normalize(worldPosition - positionRadius.xyz), spotDirection);
        float coneAttenuation = clamp((coneCos - outerCone) / coneWidth, 0.0, 1.0);
        attenuation *= coneAttenuation;
        lightInfluence *= coneAttenuation > 0.001 ? 1.0 : 0.0;
    }
    if (directionType.w >= 1.5) {
        vec3 rectNormal = directionType.xyz;
        if (dot(rectNormal, rectNormal) < 0.0001) {
            rectNormal = vec3(0.0, -1.0, 0.0);
        }
        rectNormal = normalize(rectNormal);

        vec3 rectTangent = normalize(cross(abs(rectNormal.y) > 0.95 ?
            vec3(1.0, 0.0, 0.0) :
            vec3(0.0, 1.0, 0.0),
            rectNormal
        ));
        vec3 rectBitangent = normalize(cross(rectNormal, rectTangent));
        vec3 fromRect = worldPosition - positionRadius.xyz;
        vec2 rectLocal = vec2(dot(fromRect, rectTangent), dot(fromRect, rectBitangent));
        vec2 halfSize = max(parameters.zw * 0.5, vec2(0.001));
        vec2 edgeDistance = abs(rectLocal) - halfSize;
        float outsideDistance = length(max(edgeDistance, vec2(0.0)));
        float rectRange = max(length(halfSize), 0.001);
        float rectMask = 1.0 - clamp(outsideDistance / rectRange, 0.0, 1.0);
        float facing = max(dot(normalize(worldPosition - positionRadius.xyz), rectNormal), 0.0);
        attenuation *= rectMask * facing;
        lightInfluence *= rectMask * facing > 0.001 ? 1.0 : 0.0;
    }
    if (attenuation > 0.001 && lightInfluence > 0.5) {
        localLightInfluenceCount += 1.0;
    }
    vec3 radiance = max(colorIntensity.rgb, vec3(0.0)) *
        colorIntensity.w *
        attenuation *
        LocalShadowVisibility(
            localLightIndex,
            localLight,
            worldPosition,
            normal,
            localLightDir
        );

    vec3 localSpecular = vec3(0.0);
    vec3 localDirect = DirectPbrContribution(
        baseColor,
        roughness,
        metallic,
        normal,
        viewDir,
        localLightDir,
        radiance,
        specularStrength,
        specularColorFactor,
        clearcoat,
        clearcoatRoughness,
        localSpecular
    );
    direct += localDirect;
    directSpecular += localSpecular;
}

void AccumulateRectAreaLight(
    LocalLightRecord localLight,
    vec3 baseColor,
    float roughness,
    float metallic,
    vec3 normal,
    vec3 viewDir,
    vec3 worldPosition,
    float specularStrength,
    vec3 specularColorFactor,
    float clearcoat,
    float clearcoatRoughness,
    inout vec3 direct,
    inout vec3 directSpecular,
    inout float localLightInfluenceCount
) {
    vec4 positionRadius = localLight.positionRadius;
    vec4 colorIntensity = localLight.colorIntensity;
    if (colorIntensity.w <= 0.0) {
        return;
    }

    vec3 rectNormal = localLight.directionType.xyz;
    if (dot(rectNormal, rectNormal) < 0.0001) {
        rectNormal = vec3(0.0, -1.0, 0.0);
    }
    rectNormal = normalize(rectNormal);
    vec3 rectTangent = normalize(cross(abs(rectNormal.y) > 0.95 ?
        vec3(1.0, 0.0, 0.0) :
        vec3(0.0, 1.0, 0.0),
        rectNormal
    ));
    vec3 rectBitangent = normalize(cross(rectNormal, rectTangent));
    vec2 halfSize = max(localLight.parameters.zw * 0.5, vec2(0.001));
    float radius = max(positionRadius.w, length(halfSize));
    vec2 sampleSigns[4] = vec2[](
        vec2(-0.57735, -0.57735),
        vec2(0.57735, -0.57735),
        vec2(-0.57735, 0.57735),
        vec2(0.57735, 0.57735)
    );

    float influence = 0.0;
    vec3 accumulatedDirect = vec3(0.0);
    vec3 accumulatedSpecular = vec3(0.0);
    for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex) {
        vec3 samplePosition =
            positionRadius.xyz +
            rectTangent * halfSize.x * sampleSigns[sampleIndex].x +
            rectBitangent * halfSize.y * sampleSigns[sampleIndex].y;
        vec3 toLight = samplePosition - worldPosition;
        float distanceToLight = length(toLight);
        if (distanceToLight >= radius) {
            continue;
        }

        vec3 localLightDir = toLight / max(distanceToLight, 0.001);
        float normalizedDistance = clamp(distanceToLight / radius, 0.0, 1.0);
        float attenuation = pow(1.0 - normalizedDistance * normalizedDistance, 2.0);
        float facing = max(dot(normalize(worldPosition - samplePosition), rectNormal), 0.0);
        attenuation *= facing;
        if (attenuation <= 0.001) {
            continue;
        }

        influence = max(influence, 1.0);
        vec3 radiance =
            max(colorIntensity.rgb, vec3(0.0)) *
            colorIntensity.w *
            attenuation *
            0.25;
        vec3 sampleSpecular = vec3(0.0);
        accumulatedDirect += DirectPbrContribution(
            baseColor,
            roughness,
            metallic,
            normal,
            viewDir,
            localLightDir,
            radiance,
            specularStrength,
            specularColorFactor,
            clearcoat,
            clearcoatRoughness,
            sampleSpecular
        );
        accumulatedSpecular += sampleSpecular;
    }

    if (influence > 0.5) {
        localLightInfluenceCount += 1.0;
    }
    direct += accumulatedDirect;
    directSpecular += accumulatedSpecular;
}

void AccumulateFrameLocalLight(
    int localLightIndex,
    LocalLightRecord localLight,
    vec3 baseColor,
    float roughness,
    float metallic,
    vec3 normal,
    vec3 viewDir,
    vec3 worldPosition,
    float specularStrength,
    vec3 specularColorFactor,
    float clearcoat,
    float clearcoatRoughness,
    inout vec3 direct,
    inout vec3 directSpecular,
    inout float localLightInfluenceCount
) {
    if (localLight.directionType.w >= 1.5) {
        AccumulateRectAreaLight(
            localLight,
            baseColor,
            roughness,
            metallic,
            normal,
            viewDir,
            worldPosition,
            specularStrength,
            specularColorFactor,
            clearcoat,
            clearcoatRoughness,
            direct,
            directSpecular,
            localLightInfluenceCount
        );
        return;
    }

    AccumulateLocalLight(
        localLightIndex,
        localLight,
        baseColor,
        roughness,
        metallic,
        normal,
        viewDir,
        worldPosition,
        specularStrength,
        specularColorFactor,
        clearcoat,
        clearcoatRoughness,
        direct,
        directSpecular,
        localLightInfluenceCount
    );
}

void main() {
    vec4 albedo = texture(gBufferAlbedo, fragUv);
    vec4 normalRoughness = texture(gBufferNormalRoughness, fragUv);
    vec4 material = texture(gBufferMaterial, fragUv);
    vec4 emissive = texture(gBufferEmissive, fragUv);
    vec2 velocity = texture(gBufferVelocity, fragUv).rg;
    float depth = texture(sceneDepth, fragUv).r;

    if (depth >= 0.999999 || albedo.a <= 0.001) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 baseColor = albedo.rgb;
    vec3 normal = normalize(normalRoughness.xyz * 2.0 - 1.0);
    float roughness = clamp(normalRoughness.w, 0.04, 1.0);
    float metallic = clamp(material.r, 0.0, 1.0);
    float occlusion = clamp(material.g, 0.0, 1.0);
    float specularTextureFactor = clamp(material.b, 0.0, 1.0);
    vec3 emissiveColor = max(emissive.rgb, vec3(0.0));
    MaterialRecord materialRecord;
    bool hasMaterialRecord = TryGetMaterialRecord(material.a, materialRecord);
    float materialMetallic = clamp(materialRecord.cameraControls.x, 0.0, 1.0);
    float materialRoughness = clamp(materialRecord.cameraControls.y, 0.04, 1.0);
    float materialSpecular = clamp(materialRecord.materialControls.z, 0.0, 2.0);
    vec4 specularFactor = hasMaterialRecord ? materialRecord.specularFactor : vec4(1.0);
    float materialTextureFlags = hasMaterialRecord ? materialRecord.materialFlags.y : 0.0;
    bool hasClearcoatTexture = HasTextureFlag(materialTextureFlags, 256.0);
    bool hasTransmissionTexture = HasTextureFlag(materialTextureFlags, 512.0);
    bool hasClearcoatRoughnessTexture = HasTextureFlag(materialTextureFlags, 1024.0);
    float clearcoat = hasClearcoatTexture
        ? clamp(emissive.a, 0.0, 1.0)
        : (hasMaterialRecord ? clamp(materialRecord.uvControls.w, 0.0, 1.0) : 0.0);
    float clearcoatRoughness = hasClearcoatRoughnessTexture
        ? clamp(velocity.x, 0.0, 1.0)
        : (hasMaterialRecord ? clamp(materialRecord.emissiveFactor.w, 0.0, 1.0) : 0.0);
    float transmission = hasTransmissionTexture
        ? clamp(velocity.y, 0.0, 1.0)
        : (hasMaterialRecord ? clamp(materialRecord.materialCustom.w, 0.0, 1.0) : 0.0);
    vec3 specularColorFactor = clamp(
        specularFactor.rgb * max(specularFactor.a, 0.0),
        vec3(0.0),
        vec3(2.0)
    );
    specularColorFactor *= specularTextureFactor;
    if (hasMaterialRecord) {
        roughness = clamp(mix(materialRoughness, roughness, materialRecord.cameraControls.z), 0.04, 1.0);
        metallic = clamp(mix(materialMetallic, metallic, materialRecord.cameraControls.z), 0.0, 1.0);
    }
    vec3 worldPosition = ReconstructWorldPosition(fragUv, depth);

    vec3 lightDirection = lights.directionalLight.xyz;
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = vec3(-0.45, -0.82, -0.35);
    }

    vec3 lightDir = normalize(-lightDirection);
    vec3 cameraPosition = frame.invView[3].xyz;
    vec3 viewDir = normalize(cameraPosition - worldPosition);

    float ambientStrength = max(lights.ambientLight.x, 0.08);
    float directIntensity = max(lights.directionalLight.w, 0.65);
    float specularStrength = max(lights.ambientLight.y, 0.2);
    if (hasMaterialRecord) {
        specularStrength = max(specularStrength, materialSpecular);
    }
    float cascadeShadowVisibility = ShadowVisibility(worldPosition, normal, lightDir);
    float contactShadowVisibility = ContactShadowVisibility(
        fragUv,
        depth,
        worldPosition,
        normal,
        lightDir
    );
    float shadowVisibility = min(cascadeShadowVisibility, contactShadowVisibility);

    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 directionalSpecular = vec3(0.0);
    vec3 direct = DirectPbrContribution(
        baseColor,
        roughness,
        metallic,
        normal,
        viewDir,
        lightDir,
        vec3(directIntensity * shadowVisibility),
        specularStrength,
        specularColorFactor,
        clearcoat,
        clearcoatRoughness,
        directionalSpecular
    );
    vec3 directSpecular = directionalSpecular;

    int localLightCount = clamp(int(lights.lightCounts.z + 0.5), 0, MAX_LOCAL_LIGHTS);
    float localLightInfluenceCount = 0.0;
    float localShadowVisibilitySum = 0.0;
    float localShadowVisibilityCount = 0.0;
    LightTileRecord tileRecord;
    bool usesTileLightAssignments = UseTileLightAssignments(gl_FragCoord.xy, tileRecord);
    int tileLightCount = 0;
    int tileOverflowLightCount = 0;
    if (usesTileLightAssignments) {
        tileLightCount = clamp(
            int(tileRecord.offsetCount.y),
            0,
            min(localLightCount, MAX_LIGHTS_PER_TILE)
        );
        for (int assignmentIndex = 0; assignmentIndex < tileLightCount; ++assignmentIndex) {
            int localLightIndex = TileLightIndex(tileRecord.offsetCount.x, assignmentIndex);
            if (localLightIndex < 0 || localLightIndex >= localLightCount) {
                continue;
            }

            LocalLightRecord localLight = lights.localLights[localLightIndex];
            float localVisibility = LocalShadowVisibility(
                localLightIndex,
                localLight,
                worldPosition,
                normal,
                DirectionToLocalLight(localLight, worldPosition)
            );
            localShadowVisibilitySum += localVisibility;
            localShadowVisibilityCount += 1.0;
            AccumulateFrameLocalLight(
                localLightIndex,
                localLight,
                baseColor,
                roughness,
                metallic,
                normal,
                viewDir,
                worldPosition,
                specularStrength,
                specularColorFactor,
                clearcoat,
                clearcoatRoughness,
                direct,
                directSpecular,
                localLightInfluenceCount
            );
        }
        tileOverflowLightCount = clamp(
            int(tileRecord.overflowOffsetCount.y),
            0,
            max(localLightCount - tileLightCount, 0)
        );
        for (int overflowIndex = 0; overflowIndex < tileOverflowLightCount; ++overflowIndex) {
            int localLightIndex = TileOverflowLightIndex(
                tileRecord.overflowOffsetCount.x,
                overflowIndex
            );
            if (localLightIndex < 0 || localLightIndex >= localLightCount) {
                continue;
            }

            LocalLightRecord localLight = lights.localLights[localLightIndex];
            float localVisibility = LocalShadowVisibility(
                localLightIndex,
                localLight,
                worldPosition,
                normal,
                DirectionToLocalLight(localLight, worldPosition)
            );
            localShadowVisibilitySum += localVisibility;
            localShadowVisibilityCount += 1.0;
            AccumulateFrameLocalLight(
                localLightIndex,
                localLight,
                baseColor,
                roughness,
                metallic,
                normal,
                viewDir,
                worldPosition,
                specularStrength,
                specularColorFactor,
                clearcoat,
                clearcoatRoughness,
                direct,
                directSpecular,
                localLightInfluenceCount
            );
        }
    } else {
        for (int index = 0; index < localLightCount; ++index) {
            LocalLightRecord localLight = lights.localLights[index];
            float localVisibility = LocalShadowVisibility(
                index,
                localLight,
                worldPosition,
                normal,
                DirectionToLocalLight(localLight, worldPosition)
            );
            localShadowVisibilitySum += localVisibility;
            localShadowVisibilityCount += 1.0;
            AccumulateFrameLocalLight(
                index,
                localLight,
                baseColor,
                roughness,
                metallic,
                normal,
                viewDir,
                worldPosition,
                specularStrength,
                specularColorFactor,
                clearcoat,
                clearcoatRoughness,
                direct,
                directSpecular,
                localLightInfluenceCount
            );
        }
    }

    vec3 reflection = reflect(-viewDir, normal);
    vec3 dielectricF0 = clamp(vec3(0.04) * specularColorFactor, vec3(0.0), vec3(1.0));
    f0 = mix(dielectricF0, baseColor, metallic);
    vec3 ambient = (
        baseColor * EnvironmentRadiance(normal, lightDir, 1.0) * 0.45 +
        EnvironmentRadiance(reflection, lightDir, roughness) * f0 * 0.16
    ) * ambientStrength * occlusion;
    float ambientShadowStrength = clamp(frame.shadowFiltering.y, 0.0, 1.0);
    ambient *= mix(1.0 - ambientShadowStrength, 1.0, shadowVisibility);
    if (transmission > 0.001) {
        vec3 volumeTransmittance = hasMaterialRecord ? VolumeTransmittance(materialRecord) : vec3(1.0);
        vec3 transmittedEnv =
            EnvironmentRadiance(-normal, lightDir, roughness) *
            baseColor *
            volumeTransmittance *
            ambientStrength *
            occlusion *
            0.32;
        direct *= mix(1.0, 0.78, transmission);
        ambient = mix(ambient, ambient * 0.88 + transmittedEnv, transmission);
    }

    int deferredDebugView = int(objectData.materialControls.w + 0.5);
    if (deferredDebugView == 1) {
        outColor = vec4(direct, 1.0);
        return;
    }
    if (deferredDebugView == 2) {
        outColor = vec4(ambient, 1.0);
        return;
    }
    if (deferredDebugView == 3) {
        outColor = vec4(directSpecular, 1.0);
        return;
    }
    if (deferredDebugView == 4) {
        float complexityScale = max(min(float(localLightCount), 8.0), 1.0);
        float complexity = clamp(localLightInfluenceCount / complexityScale, 0.0, 1.0);
        vec3 low = vec3(0.03, 0.10, 0.22);
        vec3 mid = vec3(0.95, 0.78, 0.18);
        vec3 high = vec3(1.0, 0.12, 0.08);
        vec3 heat = mix(low, mid, smoothstep(0.0, 0.5, complexity));
        heat = mix(heat, high, smoothstep(0.45, 1.0, complexity));
        outColor = vec4(heat + vec3(localLightInfluenceCount) * 0.035, 1.0);
        return;
    }
    if (deferredDebugView == 5) {
        if (!usesTileLightAssignments) {
            outColor = vec4(0.55, 0.02, 0.09, 1.0);
            return;
        }

        int rawTileLightCount = int(tileRecord.offsetCount.z);
        bool saturatedTile = tileRecord.offsetCount.w > 0u;
        bool overflowTile = tileRecord.overflowOffsetCount.y > 0u;
        bool droppedOverflowTile = tileRecord.overflowOffsetCount.z > 0u;
        float occupancyScale = max(float(MAX_LIGHTS_PER_TILE), 1.0);
        float occupancy = clamp(float(rawTileLightCount) / occupancyScale, 0.0, 1.0);
        vec3 low = vec3(0.02, 0.08, 0.20);
        vec3 mid = vec3(0.08, 0.72, 0.92);
        vec3 high = vec3(1.0, 0.36, 0.08);
        vec3 heat = mix(low, mid, smoothstep(0.0, 0.5, occupancy));
        heat = mix(heat, high, smoothstep(0.45, 1.0, occupancy));
        if (overflowTile) {
            heat = mix(heat, vec3(0.35, 1.0, 0.18), 0.45);
        }
        if (saturatedTile && !overflowTile) {
            heat = mix(heat, vec3(1.0, 0.62, 0.0), 0.45);
        }
        if (droppedOverflowTile) {
            heat = mix(heat, vec3(1.0, 0.02, 0.0), 0.75);
        }
        outColor = vec4(
            heat + vec3(float(tileLightCount + tileOverflowLightCount)) * 0.012,
            1.0
        );
        return;
    }
    if (deferredDebugView == 6) {
        if (!hasMaterialRecord) {
            outColor = vec4(1.0, 0.0, 0.15, 1.0);
            return;
        }

        vec3 tableColor = clamp(materialRecord.baseColorFactor.rgb, 0.0, 1.0);
        float renderClass = materialRecord.materialFlags.x;
        float textureFlags = materialRecord.materialFlags.y;
        float emissiveHint = materialRecord.materialFlags.z;
        float alpha = materialRecord.materialFlags.w;
        bool specularTextured = HasTextureFlag(textureFlags, 128.0);
        bool clearcoatTextured = HasTextureFlag(textureFlags, 256.0);
        bool transmissionTextured = HasTextureFlag(textureFlags, 512.0);
        bool clearcoatRoughnessTextured = HasTextureFlag(textureFlags, 1024.0);
        float normalScale = clamp(materialRecord.pbrFactors.x * 0.25, 0.0, 1.0);
        float occlusionStrength = clamp(materialRecord.pbrFactors.y, 0.0, 1.0);
        float alphaMode = materialRecord.pbrFactors.z;
        float alphaCutoff = clamp(materialRecord.pbrFactors.w, 0.0, 1.0);
        float uvTransformEnabled = materialRecord.uvControls.y;
        float doubleSided = materialRecord.uvControls.z;
        float tableClearcoat = clearcoatTextured
            ? clamp(emissive.a, 0.0, 1.0)
            : clamp(materialRecord.uvControls.w, 0.0, 1.0);
        float tableClearcoatRoughness = clearcoatRoughnessTextured
            ? clamp(velocity.x, 0.0, 1.0)
            : clamp(materialRecord.emissiveFactor.w, 0.0, 1.0);
        float tableTransmission = transmissionTextured
            ? clamp(velocity.y, 0.0, 1.0)
            : clamp(materialRecord.materialCustom.w, 0.0, 1.0);
        float tableVolume = materialRecord.volumeFactor.w > 0.5
            ? clamp(materialRecord.volumeFactor.x / max(materialRecord.volumeFactor.y, 0.0001), 0.0, 1.0)
            : 0.0;
        vec3 emissiveFactor = clamp(materialRecord.emissiveFactor.rgb, 0.0, 1.0);
        vec3 tableSpecularFactor = clamp(
            materialRecord.specularFactor.rgb * max(materialRecord.specularFactor.a, 0.0),
            vec3(0.0),
            vec3(1.0)
        );
        bool textured = textureFlags > 0.5 || materialRecord.materialControls.x > 0.001;
        vec3 classColor = vec3(0.15, 0.62, 1.0);
        if (renderClass > 0.5 && renderClass < 1.5) {
            classColor = vec3(1.0, 0.68, 0.12);
        } else if (renderClass >= 1.5) {
            classColor = vec3(0.85, 0.28, 1.0);
        }
        if (emissiveHint > 0.5) {
            classColor = max(classColor, vec3(0.18, 1.0, 0.42));
        }
        if (alphaMode > 0.5 && alphaMode < 1.5) {
            classColor = mix(classColor, vec3(0.96, 0.92, 0.12), 0.35);
        } else if (alphaMode >= 1.5) {
            classColor = mix(classColor, vec3(1.0, 0.36, 0.78), 0.35);
        }
        if (uvTransformEnabled > 0.5) {
            classColor = mix(classColor, vec3(0.18, 0.95, 1.0), 0.35);
        }
        if (doubleSided > 0.5) {
            classColor = mix(classColor, vec3(0.25, 1.0, 0.74), 0.42);
        }
        if (tableClearcoat > 0.001) {
            classColor = mix(classColor, vec3(0.55, 0.82, 1.0), 0.40);
        }
        if (clearcoatTextured) {
            classColor = mix(classColor, vec3(0.36, 0.62, 1.0), 0.38);
        }
        if (clearcoatRoughnessTextured) {
            classColor = mix(classColor, vec3(0.58, 0.78, 1.0), 0.38);
        }
        if (tableTransmission > 0.001) {
            classColor = mix(classColor, vec3(0.70, 1.0, 0.96), 0.40);
        }
        if (transmissionTextured) {
            classColor = mix(classColor, vec3(0.38, 1.0, 0.84), 0.38);
        }
        if (tableVolume > 0.001) {
            classColor = mix(classColor, vec3(0.35, 0.92, 1.0), 0.42);
        }
        if (specularTextured) {
            classColor = mix(classColor, vec3(1.0, 0.92, 0.20), 0.35);
        }
        if (textured) {
            tableColor = mix(tableColor, vec3(1.0), 0.18);
        }
        vec3 pbrBars = vec3(
            max(max(max(max(max(clamp(materialMetallic, 0.0, 1.0), emissiveFactor.r), tableSpecularFactor.r), specularTextureFactor), tableClearcoat), clearcoatTextured ? 1.0 : 0.0),
            max(max(max(max(max(clamp(materialRoughness, 0.0, 1.0), occlusionStrength * 0.8), tableSpecularFactor.g), uvTransformEnabled), max(doubleSided, tableClearcoat)), max(tableClearcoatRoughness, clearcoatRoughnessTextured ? 1.0 : 0.0)),
            max(max(max(max(max(max(clamp(alpha, 0.0, 1.0), normalScale), tableSpecularFactor.b), alphaCutoff), tableTransmission), transmissionTextured ? 1.0 : 0.0), tableVolume)
        );
        outColor = vec4(mix(mix(tableColor, classColor, 0.35), pbrBars, 0.35), 1.0);
        return;
    }
    if (deferredDebugView == 7) {
        outColor = vec4(LocalShadowAtlasDebugColor(fragUv), 1.0);
        return;
    }
    if (deferredDebugView == 8) {
        if (localShadowVisibilityCount <= 0.0) {
            outColor = vec4(0.08, 0.02, 0.05, 1.0);
            return;
        }

        float averageVisibility = clamp(
            localShadowVisibilitySum / localShadowVisibilityCount,
            0.0,
            1.0
        );
        vec3 shadowed = vec3(0.02, 0.08, 0.18);
        vec3 mid = vec3(0.90, 0.62, 0.14);
        vec3 lit = vec3(0.92, 0.96, 1.00);
        vec3 visibilityColor = mix(shadowed, mid, smoothstep(0.0, 0.65, averageVisibility));
        visibilityColor = mix(visibilityColor, lit, smoothstep(0.55, 1.0, averageVisibility));
        outColor = vec4(visibilityColor, 1.0);
        return;
    }
    if (deferredDebugView == 9) {
        vec3 blocked = vec3(0.02, 0.06, 0.10);
        vec3 partial = vec3(0.45, 0.72, 0.90);
        vec3 lit = vec3(0.96, 0.98, 1.0);
        vec3 contactColor = mix(blocked, partial, smoothstep(0.0, 0.72, contactShadowVisibility));
        contactColor = mix(contactColor, lit, smoothstep(0.58, 1.0, contactShadowVisibility));
        outColor = vec4(contactColor, 1.0);
        return;
    }
    if (deferredDebugView == 10) {
        vec3 faceColor = vec3(0.0);
        if (!LocalShadowFaceDebugColor(worldPosition, faceColor)) {
            outColor = vec4(0.08, 0.02, 0.05, 1.0);
            return;
        }

        outColor = vec4(faceColor, 1.0);
        return;
    }

    outColor = vec4(ambient + direct + emissiveColor, 1.0);
}
