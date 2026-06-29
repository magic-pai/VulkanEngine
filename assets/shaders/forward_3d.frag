#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec4 fragTangent;
layout(location = 5) in vec4 fragLightSpacePosition;

layout(location = 0) out vec4 outColor;

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
    vec4 lightDirectionalLight;
    vec4 lightAmbientLight;
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

layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform sampler2D materialAuxSampler;
layout(set = 1, binding = 3) uniform sampler2D normalSampler;
layout(set = 1, binding = 4) uniform sampler2D occlusionSampler;
layout(set = 1, binding = 5) uniform sampler2D emissiveSampler;
layout(set = 1, binding = 6) uniform sampler2D shadowSampler;
layout(set = 1, binding = 7) uniform sampler2D opacitySampler;
layout(set = 1, binding = 8) uniform sampler2D specularSampler;
layout(set = 1, binding = 9) uniform sampler2D clearcoatSampler;
layout(set = 1, binding = 10) uniform sampler2D transmissionSampler;
layout(set = 1, binding = 11) uniform sampler2D clearcoatRoughnessSampler;
layout(set = 1, binding = 12) uniform sampler2D localShadowSampler;

const float PI = 3.14159265359;
const int MAX_LOCAL_LIGHTS = 64;
const int MAX_LIGHT_TILES = 8192;
const int MAX_LIGHTS_PER_TILE = 16;
const int MAX_LIGHT_TILE_OVERFLOW_INDICES = 65536;
const int MAX_DIRECTIONAL_SHADOW_CASCADES = 4;
const int MAX_LOCAL_SHADOW_TILES = 64;

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

vec2 TransformMaterialUv(vec2 uv, MaterialRecord materialRecord) {
    if (materialRecord.uvControls.y < 0.5) {
        return uv;
    }

    vec2 transformed = uv * materialRecord.uvTransform.zw;
    float rotation = materialRecord.uvControls.x;
    if (abs(rotation) > 0.00001) {
        float c = cos(rotation);
        float s = sin(rotation);
        transformed = mat2(c, s, -s, c) * transformed;
    }

    return transformed + materialRecord.uvTransform.xy;
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
    float nDotV = max(dot(normal, viewDir), 0.0);
    float nDotL = max(dot(normal, lightDir), 0.0);
    return GeometrySchlickGGX(nDotV, roughness) *
        GeometrySchlickGGX(nDotL, roughness);
}

float Pow5(float value) {
    float value2 = value * value;
    return value2 * value2 * value;
}

vec3 FresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * Pow5(clamp(1.0 - cosTheta, 0.0, 1.0));
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
        Pow5(clamp(1.0 - cosTheta, 0.0, 1.0));
}

vec2 EnvBrdfApprox(float roughness, float nDotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 ToneMapAces(vec3 value) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

int ForwardDebugView() {
    return int(frame.shadowFiltering.z + 0.5);
}

float DebugExposure() {
    return max(frame.shadowFiltering.w, 0.001);
}

vec4 DebugColor(vec3 color) {
    return vec4(clamp(color * DebugExposure(), 0.0, 1.0), objectData.materialBaseColorFactor.a);
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
    vec3 sunTint = vec3(1.12, 1.08, 1.0);
    vec3 sun = sunTint * sunDisk * mix(5.0, 2.2, roughness);
    return base + sun;
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

int ActiveShadowCascadeCount() {
    return clamp(
        int(shadowCascades.cascadeInfo.x + 0.5),
        0,
        MAX_DIRECTIONAL_SHADOW_CASCADES
    );
}

float ShadowViewDepth(vec3 worldPosition) {
    vec4 viewPosition = frame.view * vec4(worldPosition, 1.0);
    return abs(viewPosition.z);
}

int SelectShadowCascade(vec3 worldPosition, out float viewDepth) {
    int activeCount = ActiveShadowCascadeCount();
    viewDepth = ShadowViewDepth(worldPosition);
    if (activeCount <= 0) {
        return -1;
    }

    int selectedCascade = activeCount - 1;
    for (int cascadeIndex = 0; cascadeIndex < MAX_DIRECTIONAL_SHADOW_CASCADES; ++cascadeIndex) {
        if (cascadeIndex >= activeCount) {
            break;
        }
        if (viewDepth <= shadowCascades.splitDepths[cascadeIndex]) {
            selectedCascade = cascadeIndex;
            break;
        }
    }
    return selectedCascade;
}

vec2 ShadowCascadeTileOrigin(int cascadeIndex) {
    return vec2(float(cascadeIndex % 2), float(cascadeIndex / 2)) * 0.5;
}

vec2 ShadowAtlasUv(vec2 shadowUv, int cascadeIndex) {
    return ShadowCascadeTileOrigin(cascadeIndex) + shadowUv * 0.5;
}

vec2 ClampShadowAtlasSampleUv(vec2 atlasUv, int cascadeIndex, vec2 texelSize) {
    vec2 tileMin = ShadowCascadeTileOrigin(cascadeIndex);
    vec2 tileMax = tileMin + vec2(0.5);
    return clamp(atlasUv, tileMin + texelSize, tileMax - texelSize);
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

    vec2 atlasUv = ShadowAtlasUv(shadowUv, cascadeIndex);
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
                vec2 blockerUv = ClampShadowAtlasSampleUv(
                    atlasUv + vec2(x, y) * texelSize * searchRadius,
                    cascadeIndex,
                    texelSize
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
            vec2 sampleUv = ClampShadowAtlasSampleUv(
                atlasUv + vec2(x, y) * texelSize * filterRadius,
                cascadeIndex,
                texelSize
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

float ShadowVisibility(vec3 normal, vec3 lightDir) {
    if (frame.shadowControls.x < 0.5 || frame.shadowControls.y <= 0.001) {
        return 1.0;
    }

    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(fragWorldPosition, viewDepth);
    if (cascadeIndex < 0) {
        return 1.0;
    }

    float visibility = SampleShadowCascadeVisibility(
        fragWorldPosition,
        normal,
        lightDir,
        cascadeIndex
    );
    int activeCount = ActiveShadowCascadeCount();
    if (cascadeIndex + 1 >= activeCount) {
        return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCount, viewDepth);
    }

    float cascadeStart = cascadeIndex == 0
        ? 0.0
        : shadowCascades.splitDepths[cascadeIndex - 1];
    float cascadeEnd = shadowCascades.splitDepths[cascadeIndex];
    float cascadeRange = max(cascadeEnd - cascadeStart, 0.0001);
    float blendWidth = cascadeRange * clamp(shadowCascades.cascadeBlendControls.x, 0.0, 0.25);
    if (blendWidth <= 0.0001) {
        return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCount, viewDepth);
    }

    float blendFactor = smoothstep(cascadeEnd - blendWidth, cascadeEnd, viewDepth);
    if (blendFactor <= 0.0001) {
        return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCount, viewDepth);
    }

    float nextVisibility = SampleShadowCascadeVisibility(
        fragWorldPosition,
        normal,
        lightDir,
        cascadeIndex + 1
    );
    visibility = mix(visibility, nextVisibility, blendFactor);
    return ApplyShadowDistanceFade(visibility, cascadeIndex, activeCount, viewDepth);
}

vec3 ApplyNormalMap(vec3 normal, float normalScale, vec2 materialUv) {
    vec3 tangentNormal = texture(normalSampler, materialUv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= max(normalScale, 0.0);
    tangentNormal = normalize(tangentNormal);

    vec3 tangent = fragTangent.xyz;
    if (dot(tangent, tangent) > 0.000001) {
        tangent = normalize(tangent - normal * dot(normal, tangent));
        vec3 bitangent = normalize(cross(normal, tangent) * fragTangent.w);
        return normalize(mat3(tangent, bitangent, normal) * tangentNormal);
    }

    vec3 dp1 = dFdx(fragWorldPosition);
    vec3 dp2 = dFdy(fragWorldPosition);
    vec2 duv1 = dFdx(materialUv);
    vec2 duv2 = dFdy(materialUv);

    vec3 fallbackTangent = dp1 * duv2.y - dp2 * duv1.y;
    vec3 fallbackBitangent = -dp1 * duv2.x + dp2 * duv1.x;
    if (dot(fallbackTangent, fallbackTangent) < 0.000001 ||
        dot(fallbackBitangent, fallbackBitangent) < 0.000001) {
        return normal;
    }

    fallbackTangent = normalize(fallbackTangent - normal * dot(normal, fallbackTangent));
    fallbackBitangent = normalize(fallbackBitangent);
    return normalize(mat3(fallbackTangent, fallbackBitangent, normal) * tangentNormal);
}

void ApplyBlackHoleOcclusionMask() {
    float eventHorizonRadius = objectData.cameraDirection.w;
    if (eventHorizonRadius <= 0.001) {
        return;
    }

    vec3 cameraPos = objectData.cameraPosition.xyz;
    vec3 toFragment = fragWorldPosition - cameraPos;
    float fragmentDistance = length(toFragment);
    if (fragmentDistance <= 0.0001) {
        return;
    }

    vec3 rayDir = toFragment / fragmentDistance;
    float b = dot(cameraPos, rayDir);
    float c = dot(cameraPos, cameraPos) -
        eventHorizonRadius * eventHorizonRadius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return;
    }

    float hitDistance = -b - sqrt(discriminant);
    float depthBias = max(objectData.cameraPosition.w, 0.0);
    if (hitDistance > 0.0 && hitDistance < fragmentDistance - depthBias) {
        discard;
    }
}

void main() {
    ApplyBlackHoleOcclusionMask();

    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = frame.directionalLight.xyz;
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = vec3(-0.45, -0.82, -0.35);
    }

    vec3 lightDir = normalize(-lightDirection);
    vec3 viewDir = normalize(objectData.cameraPosition.xyz - fragWorldPosition);
    vec3 halfDir = normalize(lightDir + viewDir);

    float ambientStrength = max(frame.ambientLight.x, 0.0);
    float directIntensity = max(frame.directionalLight.w, 0.0);
    float specularStrength = max(frame.ambientLight.y, 0.0);

    float textureMix = clamp(objectData.materialControls.x, 0.0, 1.0);
    float metallic = clamp(objectData.cameraControls.x, 0.0, 1.0);
    float roughness = clamp(objectData.cameraControls.y, 0.04, 1.0);
    float auxTextureMix = clamp(objectData.cameraControls.z, 0.0, 1.0);
    float textureFlags = objectData.cameraControls.w;
    float auxTextureKind = mod(textureFlags, 8.0);
    MaterialRecord materialRecord;
    bool hasMaterialRecord = TryGetMaterialRecord(objectData.materialControls.w, materialRecord);
    vec2 materialUv = TransformMaterialUv(fragTexCoord, materialRecord);
    vec4 sampledBaseColor = texture(texSampler, materialUv);
    vec3 textureColor = sampledBaseColor.rgb;
    vec3 baseColor = mix(fragColor, textureColor, textureMix);
    baseColor *= objectData.materialBaseColorFactor.rgb;
    float materialAlpha = clamp(
        objectData.materialBaseColorFactor.a * mix(1.0, sampledBaseColor.a, textureMix),
        0.0,
        1.0
    );
    vec4 pbrFactors = hasMaterialRecord ? materialRecord.pbrFactors : vec4(1.0, 1.0, 0.0, 0.0);
    vec4 emissiveFactor = hasMaterialRecord ? materialRecord.emissiveFactor : vec4(0.0, 0.0, 0.0, 1.0);
    vec4 specularFactor = hasMaterialRecord ? materialRecord.specularFactor : vec4(1.0);
    float clearcoat = hasMaterialRecord ? clamp(materialRecord.uvControls.w, 0.0, 1.0) : 0.0;
    float clearcoatRoughness = hasMaterialRecord ? clamp(materialRecord.emissiveFactor.w, 0.0, 1.0) : 0.0;
    float transmission = hasMaterialRecord ? clamp(materialRecord.materialCustom.w, 0.0, 1.0) : 0.0;
    float normalScale = clamp(pbrFactors.x, 0.0, 4.0);
    float occlusionStrength = clamp(pbrFactors.y, 0.0, 1.0);
    float alphaMode = pbrFactors.z;
    float alphaCutoff = clamp(pbrFactors.w, 0.0, 1.0);
    bool hasNormalTexture = HasTextureFlag(textureFlags, 8.0);
    bool hasOcclusionTexture = HasTextureFlag(textureFlags, 16.0);
    bool hasEmissiveTexture = HasTextureFlag(textureFlags, 32.0);
    bool hasOpacityTexture = HasTextureFlag(textureFlags, 64.0);
    bool hasSpecularTexture = HasTextureFlag(textureFlags, 128.0);
    bool hasClearcoatTexture = HasTextureFlag(textureFlags, 256.0);
    bool hasTransmissionTexture = HasTextureFlag(textureFlags, 512.0);
    bool hasClearcoatRoughnessTexture = HasTextureFlag(textureFlags, 1024.0);
    if (hasOpacityTexture) {
        materialAlpha *= clamp(texture(opacitySampler, materialUv).r, 0.0, 1.0);
    }
    if (hasClearcoatTexture) {
        clearcoat *= clamp(texture(clearcoatSampler, materialUv).r, 0.0, 1.0);
    }
    if (hasClearcoatRoughnessTexture) {
        clearcoatRoughness *= clamp(texture(clearcoatRoughnessSampler, materialUv).r, 0.0, 1.0);
    }
    if (hasTransmissionTexture) {
        transmission *= clamp(texture(transmissionSampler, materialUv).r, 0.0, 1.0);
    }
    vec3 specularColorFactor = clamp(
        specularFactor.rgb * max(specularFactor.a, 0.0),
        vec3(0.0),
        vec3(2.0)
    );
    if (hasSpecularTexture) {
        vec3 specularTexture = clamp(texture(specularSampler, materialUv).rgb, vec3(0.0), vec3(1.0));
        specularColorFactor *= specularTexture;
    }
    if (alphaMode > 0.5 && alphaMode < 1.5 && materialAlpha < alphaCutoff) {
        discard;
    }
    if (auxTextureMix > 0.001) {
        vec4 auxTexture = texture(materialAuxSampler, materialUv);
        if (auxTextureKind < 1.5) {
            roughness = mix(roughness, roughness * clamp(auxTexture.g, 0.04, 1.0), auxTextureMix);
            metallic = mix(metallic, metallic * clamp(auxTexture.b, 0.0, 1.0), auxTextureMix);
        } else if (auxTextureKind < 2.5) {
            roughness = mix(roughness, roughness * clamp(auxTexture.r, 0.04, 1.0), auxTextureMix);
        } else if (auxTextureKind < 3.5) {
            metallic = mix(metallic, metallic * clamp(auxTexture.r, 0.0, 1.0), auxTextureMix);
        }
    }

    if (hasNormalTexture) {
        normal = ApplyNormalMap(normal, normalScale, materialUv);
    }

    float occlusionTexture = hasOcclusionTexture
        ? clamp(texture(occlusionSampler, materialUv).r, 0.0, 1.0)
        : 1.0;
    float occlusion = mix(1.0, occlusionTexture, occlusionStrength);

    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    float shadowVisibility = ShadowVisibility(normal, lightDir);
    int debugView = ForwardDebugView();
    if (debugView == 1) {
        outColor = DebugColor(baseColor);
        return;
    }
    if (debugView == 2) {
        outColor = DebugColor(normal * 0.5 + 0.5);
        return;
    }
    if (debugView == 3) {
        outColor = DebugColor(vec3(roughness));
        return;
    }
    if (debugView == 4) {
        outColor = DebugColor(vec3(metallic));
        return;
    }
    if (debugView == 5) {
        outColor = DebugColor(vec3(occlusion));
        return;
    }
    if (debugView == 6) {
        outColor = DebugColor(vec3(shadowVisibility));
        return;
    }
    if (debugView == 7) {
        vec3 projectionCoords = fragLightSpacePosition.xyz / fragLightSpacePosition.w;
        outColor = DebugColor(vec3(clamp(projectionCoords.z, 0.0, 1.0)));
        return;
    }

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
    float localLightInfluenceCount = 0.0;
    int localLightCount = clamp(int(lights.lightCounts.z + 0.5), 0, MAX_LOCAL_LIGHTS);
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

            AccumulateFrameLocalLight(
                localLightIndex,
                lights.localLights[localLightIndex],
                baseColor,
                roughness,
                metallic,
                normal,
                viewDir,
                fragWorldPosition,
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

            AccumulateFrameLocalLight(
                localLightIndex,
                lights.localLights[localLightIndex],
                baseColor,
                roughness,
                metallic,
                normal,
                viewDir,
                fragWorldPosition,
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
            AccumulateFrameLocalLight(
                index,
                lights.localLights[index],
                baseColor,
                roughness,
                metallic,
                normal,
                viewDir,
                fragWorldPosition,
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
    if (debugView == 8) {
        float complexityScale = max(min(float(localLightCount), 8.0), 1.0);
        float complexity = clamp(localLightInfluenceCount / complexityScale, 0.0, 1.0);
        vec3 low = vec3(0.03, 0.10, 0.22);
        vec3 mid = vec3(0.95, 0.78, 0.18);
        vec3 high = vec3(1.0, 0.12, 0.08);
        vec3 heat = mix(low, mid, smoothstep(0.0, 0.5, complexity));
        heat = mix(heat, high, smoothstep(0.45, 1.0, complexity));
        outColor = DebugColor(heat + vec3(localLightInfluenceCount) * 0.035);
        return;
    }
    if (debugView == 26) {
        outColor = DebugColor(LocalShadowAtlasDebugColor(fragTexCoord));
        return;
    }

    vec3 reflection = reflect(-viewDir, normal);
    vec3 diffuseEnv = EnvironmentRadiance(normal, lightDir, 1.0);
    vec3 specularEnv = EnvironmentRadiance(reflection, lightDir, roughness);
    vec3 dielectricF0 = clamp(vec3(0.04) * specularColorFactor, vec3(0.0), vec3(1.0));
    f0 = mix(dielectricF0, baseColor, metallic);
    vec3 envFresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 envDiffuse = (vec3(1.0) - envFresnel) * (1.0 - metallic);
    vec2 envBrdf = EnvBrdfApprox(roughness, nDotV);
    vec3 envSpecular = specularEnv * (f0 * envBrdf.x + envBrdf.y);
    float envStrength = ambientStrength * 4.0 + 0.08 + directIntensity * 0.06;
    vec3 ambient = (envDiffuse * baseColor * diffuseEnv +
        envSpecular * (0.55 + specularStrength)) * envStrength * occlusion;
    float ambientShadowStrength = clamp(frame.shadowFiltering.y, 0.0, 1.0);
    ambient *= mix(1.0 - ambientShadowStrength, 1.0, shadowVisibility);
    if (transmission > 0.001) {
        vec3 volumeTransmittance = hasMaterialRecord ? VolumeTransmittance(materialRecord) : vec3(1.0);
        vec3 transmittedEnv =
            EnvironmentRadiance(-normal, lightDir, roughness) *
            baseColor *
            volumeTransmittance *
            envStrength *
            occlusion *
            0.32;
        direct *= mix(1.0, 0.78, transmission);
        ambient = mix(ambient, ambient * 0.88 + transmittedEnv, transmission);
    }

    vec3 litColor = ambient + direct;
    vec3 emissiveFactorColor = max(emissiveFactor.rgb, vec3(0.0));
    float emissiveFactorMax = max(
        max(emissiveFactorColor.r, emissiveFactorColor.g),
        emissiveFactorColor.b
    );
    vec3 emissive = emissiveFactorColor;
    if (hasEmissiveTexture) {
        vec3 emissiveTexture = texture(emissiveSampler, materialUv).rgb;
        emissive = emissiveFactorMax > 0.001
            ? emissiveTexture * emissiveFactorColor
            : emissiveTexture;
    }
    litColor += emissive;
    litColor = ToneMapAces(litColor);
    float tintMix = clamp(objectData.tint.a, 0.0, 1.0);

    outColor = vec4(
        mix(litColor, objectData.tint.rgb, tintMix),
        materialAlpha
    );
}
