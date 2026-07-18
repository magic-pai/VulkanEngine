#version 450
#extension GL_EXT_control_flow_attributes : require

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
    vec4 lightDepthWorldSpans;
    vec4 cascadeBlendControls;
    vec4 receiverPlaneBiasControls;
    vec4 directionalFilterControls;
    vec4 directionalPcssControls;
    vec4 directionalPcssGeometry;
    vec4 directionalPcssReceiverControls;
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
    vec4 pointFilterControls;
    vec4 spotFilterControls;
    vec4 rectFilterControls;
    vec4 kindSoftShadowControls;
    vec4 pointPcssControls;
    vec4 spotPcssControls;
    vec4 rectPcssControls;
    uvec4 tileRanges[64];
    LocalShadowTileRecord tiles[64];
} localShadows;

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

layout(set = 0, binding = 6) uniform sampler2D brdfLut;
layout(set = 0, binding = 7) uniform samplerCube irradianceMap;
layout(set = 0, binding = 8) uniform samplerCube prefilteredMap;
layout(std430, set = 0, binding = 9) readonly buffer ProbeGridData { vec4 probes[]; } probeGrid;
layout(set = 0, binding = 11) uniform samplerCube localReflectionProbeMaps[4];
layout(set = 0, binding = 13) uniform samplerCube localReflectionProbeDiffuseIrradianceMaps[4];
layout(set = 0, binding = 12) uniform sampler2D visibleSkyboxTexture;

layout(set = 1, binding = 0) uniform sampler2D gBufferAlbedo;
layout(set = 1, binding = 1) uniform sampler2D gBufferNormalRoughness;
layout(set = 1, binding = 2) uniform sampler2D gBufferMaterial;
layout(set = 1, binding = 3) uniform sampler2D gBufferVelocity;
layout(set = 1, binding = 4) uniform sampler2D sceneDepth;
layout(set = 1, binding = 5) uniform sampler2D gBufferEmissive;
layout(set = 1, binding = 6) uniform sampler2DShadow shadowSampler;
layout(set = 1, binding = 13) uniform sampler2D shadowRawDepthSampler;
layout(set = 1, binding = 7) uniform sampler2D gBufferMaterialAux;
layout(set = 1, binding = 12) uniform sampler2DShadow localShadowComparisonSampler;
layout(set = 1, binding = 14) uniform sampler2D localShadowRawDepthSampler;
layout(set = 1, binding = 15) uniform sampler2D ssrDepthPyramid;
layout(set = 1, binding = 16) uniform sampler2D ssrSceneColorHistory;
layout(set = 1, binding = 17) uniform sampler2D ssrResolvedReflection;
layout(set = 1, binding = 18) uniform sampler2D ssrHistoryMetadata;

const float PI = 3.14159265359;
const int MAX_LOCAL_LIGHTS = 64;
const int MAX_LIGHT_TILES = 8192;
const int MAX_LIGHTS_PER_TILE = 16;
const int LIGHT_INDEX_GROUPS_PER_TILE = 4;
const int MAX_LIGHT_TILE_OVERFLOW_INDICES = 65536;
const int MAX_FRAME_MATERIALS = 256;
const int MAX_DIRECTIONAL_SHADOW_CASCADES = 4;
const int MAX_LOCAL_SHADOW_TILES = 64;
const int MAX_LOCAL_SHADOW_TILES_PER_LIGHT = 6;
const int MAX_REFLECTION_PROBES = 4;
const int REFLECTION_PROBE_DIFFUSE_LOBE_COUNT = 6;

float InterleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec2 EquirectUv(vec3 direction) {
    vec3 d = dot(direction, direction) > 0.000001
        ? normalize(direction)
        : vec3(1.0, 0.0, 0.0);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return vec2(u, v);
}

vec3 VisibleSkyboxTextureRadiance(vec3 direction) {
    return max(textureLod(
        visibleSkyboxTexture,
        EquirectUv(direction),
        clamp(frame.environmentControls.z, 0.0, 8.0)
    ).rgb, vec3(0.0));
}

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

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) *
        Pow5(clamp(1.0 - cosTheta, 0.0, 1.0));
}

vec2 SampleEnvironmentBrdf(float roughness, float nDotV) {
    return max(texture(brdfLut, vec2(
        clamp(nDotV, 0.0, 1.0),
        clamp(roughness, 0.0, 1.0)
    )).rg, vec2(0.0));
}

float IblSpecularStability(float roughness) {
    float r = clamp(roughness, 0.0, 1.0);
    return mix(0.48, 1.0, smoothstep(0.18, 0.72, r));
}

struct IblAmbientResult {
    vec3 diffuse;
    vec3 specular;
};

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
    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 diffuseFallback = (1.0 - metallic) * baseColor / PI;
    if (nDotL <= 0.000001 || nDotV <= 0.000001) {
        specularOut = vec3(0.0);
        return diffuseFallback * radiance * nDotL;
    }
    vec3 halfVector = lightDir + viewDir;
    float halfLengthSquared = dot(halfVector, halfVector);
    if (halfLengthSquared <= 0.00000001) {
        specularOut = vec3(0.0);
        return diffuseFallback * radiance * nDotL;
    }
    vec3 halfDir = halfVector * inversesqrt(halfLengthSquared);
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
    vec3 procedural =
        (base + vec3(1.12, 1.08, 1.0) * sunDisk * mix(5.0, 2.2, roughness)) *
        intensity;
    float cubemapSampling = clamp(frame.reflectionProbeBlendControls.z, 0.0, 1.0);
    if (cubemapSampling <= 0.0001) {
        return procedural * enabled;
    }

    vec3 sampleDirection = dot(direction, direction) > 0.0001
        ? normalize(direction)
        : vec3(0.0, 1.0, 0.0);
    vec3 sampledDiffuse =
        max(texture(irradianceMap, sampleDirection).rgb, vec3(0.0)) *
        diffuseIntensity;
    vec3 sampledSpecular =
        max(textureLod(
            prefilteredMap,
            sampleDirection,
            clamp(roughness, 0.0, 1.0) * 4.0
        ).rgb, vec3(0.0)) *
        specularIntensity;
    vec3 sampled = mix(sampledSpecular, sampledDiffuse, smoothstep(0.45, 1.0, roughness));
    return mix(procedural, sampled, 0.65 * cubemapSampling) * enabled;
}

bool ProbeGridAvailable() {
    vec3 gs = frame.probeGridSizeBlend.xyz;
    ivec3 gridSize = ivec3(gs + vec3(0.5));
    return frame.probeGridOriginSpacing.w > 0.0 &&
        clamp(frame.probeGridSizeBlend.w, 0.0, 2.0) > 0.0001 &&
        gridSize.x >= 2 &&
        gridSize.y >= 2 &&
        gridSize.z >= 2;
}

vec3 ProbeGridLocalPosition(vec3 worldPos) {
    return (worldPos - frame.probeGridOriginSpacing.xyz) /
        max(frame.probeGridOriginSpacing.w, 0.0001);
}

vec3 ProbeGridProbeIrradiance(uint probeIndex, vec3 normal) {
    uint baseIndex = probeIndex * 7u;
    vec3 n = dot(normal, normal) > 0.0001
        ? normalize(normal)
        : vec3(0.0, 1.0, 0.0);
    vec3 irradiance = probeGrid.probes[baseIndex].rgb;
    irradiance += probeGrid.probes[baseIndex + 1u].rgb * max(n.x, 0.0);
    irradiance += probeGrid.probes[baseIndex + 2u].rgb * max(-n.x, 0.0);
    irradiance += probeGrid.probes[baseIndex + 3u].rgb * max(n.y, 0.0);
    irradiance += probeGrid.probes[baseIndex + 4u].rgb * max(-n.y, 0.0);
    irradiance += probeGrid.probes[baseIndex + 5u].rgb * max(n.z, 0.0);
    irradiance += probeGrid.probes[baseIndex + 6u].rgb * max(-n.z, 0.0);
    return max(irradiance, vec3(0.0));
}

vec3 SampleProbeGridIrradiance(vec3 worldPos, vec3 normal) {
    vec3 origin = frame.probeGridOriginSpacing.xyz;
    float spacing = frame.probeGridOriginSpacing.w;
    vec3 gs = frame.probeGridSizeBlend.xyz;
    float blendStrength = clamp(frame.probeGridSizeBlend.w, 0.0, 2.0);
    ivec3 gridSize = ivec3(gs + vec3(0.5));
    if (!ProbeGridAvailable()) {
        return vec3(0.0);
    }

    vec3 gridPos = clamp(
        (worldPos - origin) / spacing,
        vec3(0.0),
        vec3(gridSize) - vec3(1.0)
    );
    ivec3 cell = clamp(
        ivec3(floor(gridPos)),
        ivec3(0),
        gridSize - ivec3(2)
    );
    vec3 frac = clamp(gridPos - vec3(cell), vec3(0.0), vec3(1.0));
    int gx = gridSize.x;
    int gy = gridSize.y;

    uint i000 = uint(cell.z * gy * gx + cell.y * gx + cell.x);
    uint i100 = uint(cell.z * gy * gx + cell.y * gx + cell.x + 1);
    uint i010 = uint(cell.z * gy * gx + (cell.y + 1) * gx + cell.x);
    uint i110 = uint(cell.z * gy * gx + (cell.y + 1) * gx + cell.x + 1);
    uint i001 = uint((cell.z + 1) * gy * gx + cell.y * gx + cell.x);
    uint i101 = uint((cell.z + 1) * gy * gx + cell.y * gx + cell.x + 1);
    uint i011 = uint((cell.z + 1) * gy * gx + (cell.y + 1) * gx + cell.x);
    uint i111 = uint((cell.z + 1) * gy * gx + (cell.y + 1) * gx + cell.x + 1);

    vec3 c000 = ProbeGridProbeIrradiance(i000, normal);
    vec3 c100 = ProbeGridProbeIrradiance(i100, normal);
    vec3 c010 = ProbeGridProbeIrradiance(i010, normal);
    vec3 c110 = ProbeGridProbeIrradiance(i110, normal);
    vec3 c001 = ProbeGridProbeIrradiance(i001, normal);
    vec3 c101 = ProbeGridProbeIrradiance(i101, normal);
    vec3 c011 = ProbeGridProbeIrradiance(i011, normal);
    vec3 c111 = ProbeGridProbeIrradiance(i111, normal);

    vec3 c00 = mix(c000, c100, frac.x);
    vec3 c10 = mix(c010, c110, frac.x);
    vec3 c01 = mix(c001, c101, frac.x);
    vec3 c11 = mix(c011, c111, frac.x);
    vec3 c0 = mix(c00, c10, frac.y);
    vec3 c1 = mix(c01, c11, frac.y);
    return mix(c0, c1, frac.z) * blendStrength;
}

vec3 ProbeGridContributionDebugColor(
    vec3 worldPos,
    vec3 normal,
    vec3 baseColor,
    float occlusion,
    float metallic
) {
    if (!ProbeGridAvailable()) {
        return vec3(0.12, 0.02, 0.05);
    }

    vec3 contribution =
        SampleProbeGridIrradiance(worldPos, normal) *
        baseColor *
        clamp(occlusion, 0.0, 1.0) *
        (1.0 - clamp(metallic, 0.0, 1.0));
    vec3 display = contribution / (contribution + vec3(1.0));
    return clamp(display * 1.35 + contribution * 0.35, vec3(0.0), vec3(1.0));
}

vec3 ProbeGridCellDebugColor(vec3 worldPos) {
    if (!ProbeGridAvailable()) {
        return vec3(0.12, 0.02, 0.05);
    }

    ivec3 gridSize = ivec3(frame.probeGridSizeBlend.xyz + vec3(0.5));
    vec3 local = ProbeGridLocalPosition(worldPos);
    vec3 maxProbe = vec3(gridSize) - vec3(1.0);
    bool inside =
        all(greaterThanEqual(local, vec3(0.0))) &&
        all(lessThanEqual(local, maxProbe));
    if (!inside) {
        vec3 nearest = clamp(local, vec3(0.0), maxProbe);
        float edgeDistance = length(local - nearest);
        return mix(
            vec3(0.20, 0.03, 0.035),
            vec3(0.72, 0.07, 0.035),
            smoothstep(0.0, 2.5, edgeDistance)
        );
    }

    ivec3 cell = clamp(
        ivec3(floor(local)),
        ivec3(0),
        gridSize - ivec3(2)
    );
    vec3 frac = clamp(local - vec3(cell), vec3(0.0), vec3(1.0));
    float parity = mod(float(cell.x + cell.y + cell.z), 2.0);
    vec3 cellA = vec3(0.05, 0.28, 0.54);
    vec3 cellB = vec3(0.10, 0.48, 0.30);
    vec3 color = mix(cellA, cellB, parity);
    color += frac * vec3(0.18, 0.12, 0.22);
    float gridLine =
        step(frac.x, 0.035) +
        step(frac.y, 0.035) +
        step(frac.z, 0.035) +
        step(0.965, frac.x) +
        step(0.965, frac.y) +
        step(0.965, frac.z);
    if (gridLine > 0.0) {
        color = mix(color, vec3(1.0, 0.96, 0.72), 0.72);
    }
    return clamp(color, vec3(0.0), vec3(1.0));
}

float LocalReflectionProbeWeightAt(int probeIndex, vec3 worldPosition) {
    vec4 controls = frame.reflectionProbeControlsArray[probeIndex];
    vec4 positionRadius = frame.reflectionProbePositionRadius[probeIndex];
    vec4 boxExtentsProjection =
        frame.reflectionProbeBoxExtentsProjectionArray[probeIndex];
    float enabled = clamp(controls.x, 0.0, 1.0);
    if (enabled <= 0.0001) {
        return 0.0;
    }

    float radius = max(positionRadius.w, 0.001);
    float normalizedDistance =
        length(worldPosition - positionRadius.xyz) / radius;
    float falloff = clamp(controls.w, 0.25, 8.0);
    float influence = pow(clamp(1.0 - normalizedDistance, 0.0, 1.0), falloff);
    if (boxExtentsProjection.w > 0.5) {
        vec3 extents = max(boxExtentsProjection.xyz, vec3(0.001));
        vec3 normalizedBox =
            abs(worldPosition - positionRadius.xyz) / extents;
        float maxAxis = max(max(normalizedBox.x, normalizedBox.y), normalizedBox.z);
        influence *= 1.0 - smoothstep(1.0, 1.25, maxAxis);
    }
    return influence * clamp(controls.z, 0.0, 1.0);
}

float LocalReflectionProbeWeight(vec3 worldPosition) {
    return LocalReflectionProbeWeightAt(0, worldPosition);
}

bool LocalReflectionProbeProductionBlendEnabled() {
    float mode = floor(frame.reflectionProbeBlendControls.w + 0.5);
    return mod(mode, 2.0) > 0.5;
}

bool ReflectionProbeIndependentIblEnergyEnabled() {
    return floor(frame.reflectionProbeBlendControls.w + 0.5) > 1.5;
}

float LocalReflectionProbeShapeCoordinateAt(
    int probeIndex,
    vec3 worldPosition
) {
    vec4 positionRadius = frame.reflectionProbePositionRadius[probeIndex];
    vec4 boxExtentsProjection =
        frame.reflectionProbeBoxExtentsProjectionArray[probeIndex];
    if (boxExtentsProjection.w > 0.5) {
        vec3 normalizedBox = abs(worldPosition - positionRadius.xyz) /
            max(boxExtentsProjection.xyz, vec3(0.001));
        return max(max(normalizedBox.x, normalizedBox.y), normalizedBox.z);
    }
    return length(worldPosition - positionRadius.xyz) /
        max(positionRadius.w, 0.001);
}

float LocalReflectionProbeProductionCoverageAt(
    int probeIndex,
    vec3 worldPosition
) {
    vec4 controls = frame.reflectionProbeControlsArray[probeIndex];
    if (controls.x <= 0.0001 || controls.y <= 0.0001 ||
        controls.z <= 0.0001) {
        return 0.0;
    }
    float edgeWidth = mix(0.08, 0.45, clamp(controls.z, 0.0, 1.0));
    float edgeStart = clamp(1.0 - edgeWidth, 0.05, 0.95);
    return 1.0 - smoothstep(
        edgeStart,
        1.0,
        LocalReflectionProbeShapeCoordinateAt(probeIndex, worldPosition)
    );
}

float LocalReflectionProbeVolumePriorityAt(int probeIndex) {
    vec4 positionRadius = frame.reflectionProbePositionRadius[probeIndex];
    vec4 boxExtentsProjection =
        frame.reflectionProbeBoxExtentsProjectionArray[probeIndex];
    if (boxExtentsProjection.w > 0.5) {
        vec3 extents = max(boxExtentsProjection.xyz, vec3(0.01));
        return 1.0 / max(extents.x * extents.y * extents.z, 0.000001);
    }
    float radius = max(positionRadius.w, 0.01);
    return 1.0 / (radius * radius * radius);
}

vec3 BoxProjectedLocalReflectionDirectionAt(
    int probeIndex,
    vec3 direction,
    vec3 worldPosition
) {
    vec4 positionRadius = frame.reflectionProbePositionRadius[probeIndex];
    vec4 boxExtentsProjection =
        frame.reflectionProbeBoxExtentsProjectionArray[probeIndex];
    vec3 sampleDirection = dot(direction, direction) > 0.0001
        ? normalize(direction)
        : vec3(0.0, 1.0, 0.0);
    if (boxExtentsProjection.w <= 0.5) {
        return sampleDirection;
    }

    vec3 center = positionRadius.xyz;
    vec3 extents = max(boxExtentsProjection.xyz, vec3(0.001));
    vec3 localPosition = worldPosition - center;
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
    vec3 tFar = max(tMin, tMax);
    float hitDistance = min(min(tFar.x, tFar.y), tFar.z);
    if (hitDistance <= 0.0001 || hitDistance > 100000.0) {
        return sampleDirection;
    }

    vec3 hitLocal = localPosition + sampleDirection * hitDistance;
    return dot(hitLocal, hitLocal) > 0.0001
        ? normalize(hitLocal)
        : sampleDirection;
}

vec3 BoxProjectedLocalReflectionDirection(vec3 direction, vec3 worldPosition) {
    return BoxProjectedLocalReflectionDirectionAt(0, direction, worldPosition);
}

vec3 SampleLocalReflectionProbeMap(int slotIndex, vec3 direction, float lod) {
    if (slotIndex == 0) {
        return textureLod(localReflectionProbeMaps[0], direction, lod).rgb;
    }
    if (slotIndex == 1) {
        return textureLod(localReflectionProbeMaps[1], direction, lod).rgb;
    }
    if (slotIndex == 2) {
        return textureLod(localReflectionProbeMaps[2], direction, lod).rgb;
    }
    return textureLod(localReflectionProbeMaps[3], direction, lod).rgb;
}

vec3 SampleLocalReflectionProbeDiffuseIrradianceMap(int slotIndex, vec3 direction) {
    if (slotIndex == 0) {
        return texture(localReflectionProbeDiffuseIrradianceMaps[0], direction).rgb;
    }
    if (slotIndex == 1) {
        return texture(localReflectionProbeDiffuseIrradianceMaps[1], direction).rgb;
    }
    if (slotIndex == 2) {
        return texture(localReflectionProbeDiffuseIrradianceMaps[2], direction).rgb;
    }
    return texture(localReflectionProbeDiffuseIrradianceMaps[3], direction).rgb;
}

vec3 LocalReflectionProbeDiffuseRadianceAt(
    int probeIndex,
    vec3 normal,
    vec3 fallbackRadiance
) {
    int baseIndex = probeIndex * REFLECTION_PROBE_DIFFUSE_LOBE_COUNT;
    vec3 n = dot(normal, normal) > 0.0001
        ? normalize(normal)
        : vec3(0.0, 1.0, 0.0);
    if (frame.reflectionProbeMipControls[probeIndex].w > 0.5) {
        int slotIndex = clamp(
            int(frame.reflectionProbeColorArray[probeIndex].a + 0.5) - 1,
            0,
            MAX_REFLECTION_PROBES - 1
        );
        return max(
            SampleLocalReflectionProbeDiffuseIrradianceMap(slotIndex, n),
            vec3(0.0)
        ) * max(frame.reflectionProbeColorArray[probeIndex].rgb, vec3(0.0));
    }
    if (frame.reflectionProbeDiffuseLobes[baseIndex].a <= 0.5) {
        return fallbackRadiance;
    }

    vec3 irradiance = max(frame.reflectionProbeColorArray[probeIndex].rgb, vec3(0.0));
    irradiance += frame.reflectionProbeDiffuseLobes[baseIndex + 0].rgb * max(n.x, 0.0);
    irradiance += frame.reflectionProbeDiffuseLobes[baseIndex + 1].rgb * max(-n.x, 0.0);
    irradiance += frame.reflectionProbeDiffuseLobes[baseIndex + 2].rgb * max(n.y, 0.0);
    irradiance += frame.reflectionProbeDiffuseLobes[baseIndex + 3].rgb * max(-n.y, 0.0);
    irradiance += frame.reflectionProbeDiffuseLobes[baseIndex + 4].rgb * max(n.z, 0.0);
    irradiance += frame.reflectionProbeDiffuseLobes[baseIndex + 5].rgb * max(-n.z, 0.0);
    return max(irradiance, vec3(0.0));
}

vec3 EnvironmentRadiance(vec3 direction, vec3 sunDirection, float roughness) {
    return GlobalEnvironmentRadiance(direction, sunDirection, roughness);
}

struct EnvironmentRadianceResult {
    vec3 globalRadiance;
    vec3 localRadiance;
    vec3 resolvedRadiance;
    float localCoverage;
};

EnvironmentRadianceResult ResolveEnvironmentRadiance(
    vec3 direction,
    vec3 sunDirection,
    float roughness,
    vec3 worldPosition
) {
    EnvironmentRadianceResult result;
    result.globalRadiance = GlobalEnvironmentRadiance(
        direction,
        sunDirection,
        roughness
    );
    result.localRadiance = result.globalRadiance;
    result.resolvedRadiance = result.globalRadiance;
    result.localCoverage = 0.0;
    int probeCount = clamp(
        int(frame.reflectionProbeBlendControls.x + 0.5),
        0,
        MAX_REFLECTION_PROBES
    );
    if (frame.reflectionProbeBlendControls.y <= 0.5 || probeCount <= 0) {
        return result;
    }

    float weights[MAX_REFLECTION_PROBES];
    float coverages[MAX_REFLECTION_PROBES];
    float priorities[MAX_REFLECTION_PROBES];
    bool productionBlend = LocalReflectionProbeProductionBlendEnabled();
    float bestPriority = 0.0;
    for (int probeIndex = 0; probeIndex < MAX_REFLECTION_PROBES; ++probeIndex) {
        float coverage = probeIndex < probeCount
            ? LocalReflectionProbeProductionCoverageAt(probeIndex, worldPosition)
            : 0.0;
        float priority = probeIndex < probeCount
            ? LocalReflectionProbeVolumePriorityAt(probeIndex)
            : 0.0;
        coverages[probeIndex] = coverage;
        priorities[probeIndex] = priority;
        if (coverage > 0.0001) {
            bestPriority = max(bestPriority, priority);
        }
    }

    float totalWeight = 0.0;
    for (int probeIndex = 0; probeIndex < MAX_REFLECTION_PROBES; ++probeIndex) {
        float weight = 0.0;
        if (probeIndex < probeCount) {
            weight = productionBlend
                ? coverages[probeIndex] * smoothstep(
                    0.35,
                    0.95,
                    priorities[probeIndex] / max(bestPriority, 0.000001)
                )
                : LocalReflectionProbeWeightAt(probeIndex, worldPosition);
        }
        weights[probeIndex] = weight;
        totalWeight += weight;
        result.localCoverage = productionBlend
            ? max(result.localCoverage, coverages[probeIndex])
            : clamp(totalWeight, 0.0, 1.0);
    }
    if (totalWeight <= 0.0001) {
        return result;
    }

    vec3 localBlend = vec3(0.0);
    float roughnessClamped = clamp(roughness, 0.0, 1.0);
    float glossBoost = IblSpecularStability(roughnessClamped);
    for (int probeIndex = 0; probeIndex < MAX_REFLECTION_PROBES; ++probeIndex) {
        float rawWeight = weights[probeIndex];
        if (probeIndex >= probeCount || rawWeight <= 0.0001) {
            continue;
        }

        vec4 color = frame.reflectionProbeColorArray[probeIndex];
        vec4 controls = frame.reflectionProbeControlsArray[probeIndex];
        vec3 localTint = max(color.rgb, vec3(0.0));
        float localIntensity = clamp(controls.y, 0.0, 4.0);
        vec3 sampleDirection =
            BoxProjectedLocalReflectionDirectionAt(
                probeIndex,
                direction,
                worldPosition
            );
        vec3 sampledRadiance = result.globalRadiance * localTint;
        if (color.a > 0.5) {
            int slotIndex = clamp(int(color.a + 0.5) - 1, 0, MAX_REFLECTION_PROBES - 1);
            float localCubemapWeight = 1.0 - smoothstep(0.88, 1.0, roughnessClamped);
            vec3 cubemapRadiance =
                max(SampleLocalReflectionProbeMap(
                    slotIndex,
                    sampleDirection,
                    roughnessClamped * max(
                        frame.reflectionProbeMipControls[probeIndex].x,
                        0.0
                    )
                ), vec3(0.0)) *
                localTint;
            sampledRadiance =
                mix(sampledRadiance, cubemapRadiance, localCubemapWeight);
        }
        vec3 diffuseRadiance = LocalReflectionProbeDiffuseRadianceAt(
            probeIndex,
            direction,
            sampledRadiance
        );
        vec3 localRadiance =
            mix(
                sampledRadiance,
                diffuseRadiance,
                smoothstep(0.72, 1.0, roughnessClamped)
            ) *
            localIntensity *
            glossBoost;
        localBlend += localRadiance * (rawWeight / totalWeight);
    }

    result.localRadiance = localBlend;
    result.resolvedRadiance = mix(
        result.globalRadiance,
        result.localRadiance,
        result.localCoverage
    );
    return result;
}

vec3 EnvironmentRadiance(
    vec3 direction,
    vec3 sunDirection,
    float roughness,
    vec3 worldPosition
) {
    return ResolveEnvironmentRadiance(
        direction,
        sunDirection,
        roughness,
        worldPosition
    ).resolvedRadiance;
}

IblAmbientResult BuildIblAmbient(
    vec3 baseColor,
    float roughness,
    float metallic,
    vec3 normal,
    vec3 viewDir,
    vec3 lightDir,
    vec3 worldPosition,
    vec3 f0,
    float occlusion,
    float diffuseScale,
    float specularScale,
    vec3 specularRadiance
) {
    IblAmbientResult result;
    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 diffuseEnv = EnvironmentRadiance(normal, lightDir, 1.0, worldPosition);
    vec3 envFresnel = FresnelSchlickRoughness(nDotV, f0, roughness);
    vec3 envDiffuse = (vec3(1.0) - envFresnel) * (1.0 - metallic);
    vec2 envBrdf = SampleEnvironmentBrdf(roughness, nDotV);
    vec3 envSpecularBrdf = max(f0 * envBrdf.x + envBrdf.y, vec3(0.0));

    result.diffuse =
        envDiffuse *
        baseColor *
        diffuseEnv *
        max(diffuseScale, 0.0) *
        occlusion;
    result.specular =
        specularRadiance *
        envSpecularBrdf *
        max(specularScale, 0.0) *
        IblSpecularStability(roughness) *
        occlusion;
    return result;
}

vec3 ReflectionProbeDebugColor(
    vec3 normal,
    vec3 reflection,
    vec3 lightDir,
    float roughness,
    vec3 worldPosition
) {
    if (frame.reflectionProbeControls.x <= 0.0001) {
        return vec3(0.12, 0.025, 0.04);
    }

    vec3 diffuseProbe = EnvironmentRadiance(normal, lightDir, 1.0, worldPosition);
    vec3 specularProbe = EnvironmentRadiance(reflection, lightDir, roughness, worldPosition);
    vec3 horizon = vec3(frame.reflectionProbeControls.w);
    int probeCount = clamp(
        int(frame.reflectionProbeBlendControls.x + 0.5),
        0,
        MAX_REFLECTION_PROBES
    );
    float totalLocalWeight = 0.0;
    for (int probeIndex = 0; probeIndex < MAX_REFLECTION_PROBES; ++probeIndex) {
        if (probeIndex < probeCount) {
            totalLocalWeight +=
                LocalReflectionProbeWeightAt(probeIndex, worldPosition);
        }
    }
    vec3 localDebug = vec3(clamp(totalLocalWeight, 0.0, 1.0));
    return clamp(
        diffuseProbe * 0.45 +
            specularProbe * 0.65 +
            horizon * 0.08 +
            localDebug * vec3(0.12, 0.08, 0.02),
        vec3(0.0),
        vec3(8.0)
    );
}

vec3 ReflectionProbeContrastDebugColor(
    vec3 normal,
    vec3 reflection,
    vec3 lightDir,
    float roughness,
    vec3 worldPosition
) {
    vec3 probeColor =
        ReflectionProbeDebugColor(normal, reflection, lightDir, roughness, worldPosition);
    float luminance = dot(probeColor, vec3(0.2126, 0.7152, 0.0722));
    float darkSignal = 1.0 - smoothstep(0.34, 0.92, luminance);
    float localChange = clamp(
        (abs(dFdx(luminance)) + abs(dFdy(luminance))) * 56.0,
        0.0,
        1.0
    );
    float signal = clamp(max(darkSignal, localChange), 0.0, 1.0);
    return mix(vec3(1.0), vec3(0.0), signal);
}

vec3 AmbientComponentDebugColor(vec3 component, float gain) {
    vec3 amplified = clamp(max(component, vec3(0.0)) * gain, vec3(0.0), vec3(1.0));
    return pow(amplified, vec3(0.42));
}

float DebugLuminance(vec3 color) {
    return max(dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.0);
}

vec3 LightingEnergyBalanceDebugColor(
    vec3 directDiffuse,
    vec3 directSpecular,
    vec3 ambientDiffuse,
    vec3 ambientSpecular,
    vec3 ambientProbe,
    vec3 emissiveColor,
    float shadowVisibility,
    float localShadowVisibility,
    float ssaoVisibility
) {
    float directDiffuseEnergy = DebugLuminance(directDiffuse);
    float directSpecularEnergy = DebugLuminance(directSpecular) * 1.18;
    float ambientDiffuseEnergy = DebugLuminance(ambientDiffuse);
    float ambientSpecularEnergy = DebugLuminance(ambientSpecular) * 1.22;
    float ambientProbeEnergy = DebugLuminance(ambientProbe) * 1.12;
    float emissiveEnergy = DebugLuminance(emissiveColor) * 1.35;
    float totalEnergy =
        directDiffuseEnergy +
        directSpecularEnergy +
        ambientDiffuseEnergy +
        ambientSpecularEnergy +
        ambientProbeEnergy +
        emissiveEnergy;

    if (totalEnergy <= 0.00001) {
        return vec3(0.025, 0.025, 0.032);
    }

    vec3 color =
        vec3(1.00, 0.30, 0.08) * (directDiffuseEnergy / totalEnergy) +
        vec3(1.00, 0.13, 0.74) * (directSpecularEnergy / totalEnergy) +
        vec3(0.10, 0.36, 1.00) * (ambientDiffuseEnergy / totalEnergy) +
        vec3(0.06, 0.88, 1.00) * (ambientSpecularEnergy / totalEnergy) +
        vec3(0.18, 1.00, 0.36) * (ambientProbeEnergy / totalEnergy) +
        vec3(1.00, 0.92, 0.18) * (emissiveEnergy / totalEnergy);

    float signal = clamp(0.34 + sqrt(totalEnergy) * 0.92, 0.34, 1.0);
    color *= signal;

    float shadowSuppression = 1.0 - min(
        clamp(shadowVisibility, 0.0, 1.0),
        clamp(localShadowVisibility, 0.0, 1.0)
    );
    float ambientSuppression = 1.0 - clamp(ssaoVisibility, 0.0, 1.0);
    float suppression = clamp(max(shadowSuppression, ambientSuppression) * 0.56, 0.0, 0.56);
    color = mix(color, vec3(0.035, 0.025, 0.105), suppression);

    return clamp(color, vec3(0.0), vec3(1.0));
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
    float atlasDepth = texture(localShadowRawDepthSampler, uv).r;
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

int SecondaryPointShadowFace(vec3 fromLight) {
    vec3 absDir = abs(fromLight);
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        return absDir.y >= absDir.z
            ? (fromLight.y >= 0.0 ? 2 : 3)
            : (fromLight.z >= 0.0 ? 4 : 5);
    }
    if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        return absDir.x >= absDir.z
            ? (fromLight.x >= 0.0 ? 0 : 1)
            : (fromLight.z >= 0.0 ? 4 : 5);
    }
    return absDir.x >= absDir.y
        ? (fromLight.x >= 0.0 ? 0 : 1)
        : (fromLight.y >= 0.0 ? 2 : 3);
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

bool GetLocalShadowTileRange(
    int assignedTileCount,
    int localLightIndex,
    out int firstTileIndex,
    out int tileCount
) {
    firstTileIndex = 0;
    tileCount = 0;
    if (localShadows.atlasInfo2.w < 3u ||
        localLightIndex < 0 || localLightIndex >= MAX_LOCAL_LIGHTS) {
        return false;
    }

    uvec4 range = localShadows.tileRanges[localLightIndex];
    firstTileIndex = int(range.x);
    tileCount = int(range.y);
    return range.w != 0u && tileCount > 0 &&
        tileCount <= MAX_LOCAL_SHADOW_TILES_PER_LIGHT &&
        firstTileIndex >= 0 && firstTileIndex < assignedTileCount &&
        firstTileIndex + tileCount <= assignedTileCount;
}

bool FindLocalShadowTile(
    int assignedTileCount,
    int localLightIndex,
    int targetFace,
    out LocalShadowTileRecord foundTile
) {
    int firstTileIndex = 0;
    int tileCount = 0;
    if (!GetLocalShadowTileRange(
            assignedTileCount,
            localLightIndex,
            firstTileIndex,
            tileCount
        )) {
        return false;
    }

    [[dont_unroll]] for (int rangeOffset = 0;
         rangeOffset < MAX_LOCAL_SHADOW_TILES_PER_LIGHT;
         ++rangeOffset) {
        if (rangeOffset >= tileCount) {
            break;
        }
        int tileIndex = firstTileIndex + rangeOffset;
        LocalShadowTileRecord tile = localShadows.tiles[tileIndex];
        if (int(tile.tileInfo.y) != localLightIndex) {
            continue;
        }
        if (int(tile.tileInfo.z) != targetFace) {
            continue;
        }

        foundTile = tile;
        return true;
    }

    return false;
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

vec4 LocalShadowFilterControls(uint lightKind) {
    if (lightKind == 0u) {
        return localShadows.pointFilterControls;
    }
    if (lightKind == 1u) {
        return localShadows.spotFilterControls;
    }
    if (lightKind == 2u) {
        return localShadows.rectFilterControls;
    }
    return localShadows.filterControls;
}

float LocalShadowPcssStrength(uint lightKind) {
    if (lightKind == 0u) {
        return clamp(localShadows.kindSoftShadowControls.x, 0.0, 1.0);
    }
    if (lightKind == 1u) {
        return clamp(localShadows.kindSoftShadowControls.y, 0.0, 1.0);
    }
    if (lightKind == 2u) {
        return clamp(localShadows.kindSoftShadowControls.z, 0.0, 1.0);
    }
    return clamp(localShadows.softShadowControls.x, 0.0, 1.0);
}

vec4 LocalShadowPcssControls(uint lightKind) {
    if (lightKind == 0u) {
        return localShadows.pointPcssControls;
    }
    if (lightKind == 1u) {
        return localShadows.spotPcssControls;
    }
    if (lightKind == 2u) {
        return localShadows.rectPcssControls;
    }
    return vec4(0.0);
}

float LinearizeLocalShadowDepth(float depth, vec4 lightInfo) {
    float nearPlane = max(lightInfo.x, 0.0001);
    float farPlane = max(lightInfo.y, nearPlane + 0.0001);
    float denominator = farPlane - clamp(depth, 0.0, 1.0) * (farPlane - nearPlane);
    return nearPlane * farPlane / max(denominator, 0.000001);
}

float LocalShadowStableDiskAngle(uvec4 tileInfo) {
    uint value = tileInfo.y * 747796405u +
        tileInfo.z * 2891336453u + tileInfo.w * 277803737u + 0x9e3779b9u;
    value ^= value >> 16u;
    value *= 2246822519u;
    value ^= value >> 13u;
    return float(value & 65535u) * (6.28318530718 / 65536.0);
}

const vec2 LOCAL_SHADOW_POISSON_DISK[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

mat2 LocalShadowDiskRotation(float angle) {
    float cosine = cos(angle);
    float sine = sin(angle);
    return mat2(cosine, sine, -sine, cosine);
}

vec2 LocalShadowDiskOffset(int sampleIndex, mat2 rotation) {
    return rotation * LOCAL_SHADOW_POISSON_DISK[sampleIndex];
}

float RectAreaShadowSoftness(LocalLightRecord localLight, vec3 worldPosition) {
    vec3 rectNormal = localLight.directionType.xyz;
    if (dot(rectNormal, rectNormal) < 0.0001) {
        rectNormal = vec3(0.0, -1.0, 0.0);
    }
    rectNormal = normalize(rectNormal);

    vec3 fromRect = worldPosition - localLight.positionRadius.xyz;
    float receiverDistance = max(dot(fromRect, rectNormal), 0.001);
    vec2 halfSize = max(localLight.parameters.zw * 0.5, vec2(0.001));
    float areaRadius = max(length(halfSize), 0.001);
    float angularSize = areaRadius / max(receiverDistance, 0.25);
    float distanceFade = 1.0 - smoothstep(
        localLight.positionRadius.w * 0.72,
        localLight.positionRadius.w,
        length(fromRect)
    );
    return clamp(smoothstep(0.08, 0.45, angularSize) * distanceFade, 0.0, 1.0);
}

float SampleLocalShadowTileVisibility(
    LocalShadowTileRecord tile,
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    vec4 filterControls,
    float pcssStrength,
    float biasScale,
    float areaSoftness
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
    vec2 texelSize = 1.0 / vec2(textureSize(localShadowRawDepthSampler, 0));
    float rectAreaSoftness = clamp(areaSoftness, 0.0, 1.0);
    float shadowTileEdgeDistance = min(
        min(shadowUv.x, 1.0 - shadowUv.x),
        min(shadowUv.y, 1.0 - shadowUv.y)
    );
    float rectTileEdgeFade = mix(
        1.0,
        smoothstep(0.018, 0.14, shadowTileEdgeDistance),
        rectAreaSoftness
    );
    float biasMin = max(filterControls.x, 0.0);
    float biasSlope = max(filterControls.y, 0.0);
    float nDotL = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = max(biasSlope * (1.0 - nDotL), biasMin) * max(biasScale, 0.0);
    bool productionFilterReady = localShadows.atlasInfo2.w >= 3u &&
        localShadows.softShadowControls.w > 0.5 &&
        tile.lightInfo.x > 0.0 && tile.lightInfo.y > tile.lightInfo.x &&
        tile.lightInfo.w > 0.0;
    if (!productionFilterReady) {
        int kernelRadius = clamp(int(filterControls.w + 0.5), 0, 2);
        if (kernelRadius <= 0) {
            float closestDepth = texture(localShadowRawDepthSampler, atlasUv).r;
            float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
            float occlusion = shadow *
                mix(1.0, 0.42, rectAreaSoftness) * rectTileEdgeFade;
            return 1.0 - occlusion * clamp(frame.shadowControls.y, 0.0, 1.0);
        }

        float shadow = 0.0;
        int sampleCount = 0;
        float filterRadius = max(filterControls.z, 0.0) *
            (1.0 + rectAreaSoftness * 5.5);
        float legacyPcssStrength = max(
            clamp(pcssStrength, 0.0, 1.0),
            rectAreaSoftness * 0.55
        );
        if (legacyPcssStrength > 0.0001) {
            float blockerDepthSum = 0.0;
            int blockerCount = 0;
            float searchRadius = max(filterRadius, 1.0) *
                (1.0 + legacyPcssStrength);
            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    vec2 blockerUv = clamp(
                        atlasUv + vec2(x, y) * texelSize * searchRadius,
                        tileOrigin + texelSize,
                        tileMax - texelSize
                    );
                    float blockerDepth = texture(
                        localShadowRawDepthSampler,
                        blockerUv
                    ).r;
                    if (currentDepth - bias > blockerDepth) {
                        blockerDepthSum += blockerDepth;
                        ++blockerCount;
                    }
                }
            }
            if (blockerCount > 0) {
                float averageBlockerDepth = blockerDepthSum / float(blockerCount);
                float penumbra = clamp(
                    (currentDepth - averageBlockerDepth) /
                        max(averageBlockerDepth, 0.0001),
                    0.0,
                    1.0
                );
                filterRadius *= 1.0 + penumbra * legacyPcssStrength * 3.0;
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
                float closestDepth = texture(localShadowRawDepthSampler, sampleUv).r;
                shadow += currentDepth - bias > closestDepth ? 1.0 : 0.0;
                ++sampleCount;
            }
        }

        float occlusion = shadow / max(float(sampleCount), 1.0);
        occlusion *= mix(1.0, 0.42, rectAreaSoftness) * rectTileEdgeFade;
        return 1.0 - occlusion * clamp(frame.shadowControls.y, 0.0, 1.0);
    }

    uint lightKind = tile.tileInfo.w;
    vec4 pcssControls = LocalShadowPcssControls(lightKind);
    int blockerSampleCount = clamp(int(pcssControls.x + 0.5), 0, 16);
    int filterSampleCount = clamp(int(pcssControls.y + 0.5), 1, 16);
    float searchRadiusLimit = clamp(pcssControls.z, 0.0, 16.0);
    float maxPenumbraTexels = clamp(pcssControls.w, 0.5, 16.0);
    float receiverDistance = LinearizeLocalShadowDepth(currentDepth, tile.lightInfo);
    float sourceRadius = max(tile.lightInfo.z, 0.0);
    float tanHalfFov = max(tile.lightInfo.w, 0.0001);
    float worldTexelSize = max(
        2.0 * receiverDistance * tanHalfFov / float(tileSize),
        0.000001
    );
    float baseFilterRadius = max(filterControls.z, 0.5);
    float filterRadiusTexels = min(baseFilterRadius, maxPenumbraTexels);
    float stableAngle = LocalShadowStableDiskAngle(tile.tileInfo);
    mat2 diskRotation = LocalShadowDiskRotation(stableAngle);
    vec2 sampleGuard = texelSize * 1.5;
    float comparisonDepth = clamp(currentDepth - bias, 0.0, 1.0);

    if (pcssStrength > 0.0001 && sourceRadius > 0.000001 &&
        blockerSampleCount > 0 && searchRadiusLimit > 0.0) {
        float projectedSourceTexels = sourceRadius / worldTexelSize;
        float geometricSearchRadius = projectedSourceTexels *
            max(receiverDistance - tile.lightInfo.x, 0.0) /
            max(receiverDistance, tile.lightInfo.x);
        float searchRadiusTexels = clamp(
            max(baseFilterRadius, geometricSearchRadius),
            0.5,
            searchRadiusLimit
        );
        float blockerDistanceSum = 0.0;
        int blockerCount = 0;
        [[dont_unroll]] for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
            if (sampleIndex >= blockerSampleCount) {
                break;
            }
            vec2 sampleOffset = LocalShadowDiskOffset(
                sampleIndex,
                diskRotation
            );
            vec2 blockerUv = clamp(
                atlasUv + sampleOffset * texelSize * searchRadiusTexels,
                tileOrigin + sampleGuard,
                tileMax - sampleGuard
            );
            float blockerDepth = textureLod(
                localShadowRawDepthSampler,
                blockerUv,
                0.0
            ).r;
            if (comparisonDepth > blockerDepth + 0.000001) {
                blockerDistanceSum += LinearizeLocalShadowDepth(
                    blockerDepth,
                    tile.lightInfo
                );
                ++blockerCount;
            }
        }
        if (blockerCount > 0) {
            float averageBlockerDistance =
                blockerDistanceSum / float(blockerCount);
            float blockerSeparationWorld = max(
                receiverDistance - averageBlockerDistance,
                0.0
            );
            float penumbraWorld = sourceRadius * blockerSeparationWorld /
                max(averageBlockerDistance, tile.lightInfo.x);
            filterRadiusTexels = clamp(
                baseFilterRadius +
                    penumbraWorld * clamp(pcssStrength, 0.0, 1.0) /
                    worldTexelSize,
                baseFilterRadius,
                maxPenumbraTexels
            );
        }
    }

    float visibilitySum = 0.0;
    [[dont_unroll]] for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
        if (sampleIndex >= filterSampleCount) {
            break;
        }
        vec2 sampleOffset = LocalShadowDiskOffset(
            sampleIndex,
            diskRotation
        );
        vec2 sampleUv = clamp(
            atlasUv + sampleOffset * texelSize * filterRadiusTexels,
            tileOrigin + sampleGuard,
            tileMax - sampleGuard
        );
        visibilitySum += texture(
            localShadowComparisonSampler,
            vec3(sampleUv, comparisonDepth)
        );
    }
    float visibility = visibilitySum / float(filterSampleCount);
    return mix(1.0, visibility, clamp(frame.shadowControls.y, 0.0, 1.0));
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
    int blendFace = -1;
    float faceBlend = 0.0;
    uint lightKind = uint(clamp(int(localLight.directionType.w + 0.5), 0, 3));
    if (lightKind == 0u) {
        vec3 fromLight = worldPosition - localLight.positionRadius.xyz;
        targetFace = DominantPointShadowFace(fromLight);
        blendFace = SecondaryPointShadowFace(fromLight);
        float seamRisk = PointShadowFaceSeamRisk(fromLight);
        float blendStrength = clamp(localShadows.softShadowControls.y, 0.0, 1.0);
        faceBlend = seamRisk * blendStrength;
    } else if (lightKind != 1u && lightKind != 2u) {
        return 1.0;
    }
    float shadowBiasScale =
        lightKind == 2u ? max(localShadows.softShadowControls.z, 0.0) : 1.0;
    vec4 filterControls = LocalShadowFilterControls(lightKind);
    float pcssStrength = LocalShadowPcssStrength(lightKind);
    float areaSoftness =
        lightKind == 2u ? RectAreaShadowSoftness(localLight, worldPosition) : 0.0;
    if (lightKind == 2u) {
        int firstTileIndex = 0;
        int tileCount = 0;
        if (!GetLocalShadowTileRange(
                assignedTileCount,
                localLightIndex,
                firstTileIndex,
                tileCount
            )) {
            return 1.0;
        }
        float visibilitySum = 0.0;
        int visibilityCount = 0;
        [[dont_unroll]] for (int rangeOffset = 0;
             rangeOffset < MAX_LOCAL_SHADOW_TILES_PER_LIGHT;
             ++rangeOffset) {
            if (rangeOffset >= tileCount) {
                break;
            }
            int tileIndex = firstTileIndex + rangeOffset;
            LocalShadowTileRecord rectTile = localShadows.tiles[tileIndex];
            if (int(rectTile.tileInfo.y) != localLightIndex ||
                rectTile.tileInfo.w != 2u) {
                continue;
            }
            visibilitySum += SampleLocalShadowTileVisibility(
                rectTile,
                worldPosition,
                normal,
                lightDir,
                filterControls,
                pcssStrength,
                shadowBiasScale,
                areaSoftness
            );
            ++visibilityCount;
        }
        if (visibilityCount <= 0) {
            return 1.0;
        }
        return visibilitySum / float(visibilityCount);
    }

    LocalShadowTileRecord primaryTile;
    if (!FindLocalShadowTile(assignedTileCount, localLightIndex, targetFace, primaryTile)) {
        return 1.0;
    }

    float primaryVisibility = SampleLocalShadowTileVisibility(
        primaryTile,
        worldPosition,
        normal,
        lightDir,
        filterControls,
        pcssStrength,
        shadowBiasScale,
        areaSoftness
    );
    if (faceBlend <= 0.0001 || blendFace == targetFace) {
        return primaryVisibility;
    }

    LocalShadowTileRecord secondaryTile;
    if (!FindLocalShadowTile(assignedTileCount, localLightIndex, blendFace, secondaryTile)) {
        return primaryVisibility;
    }

    float secondaryVisibility = SampleLocalShadowTileVisibility(
        secondaryTile,
        worldPosition,
        normal,
        lightDir,
        filterControls,
        pcssStrength,
        shadowBiasScale,
        areaSoftness
    );
    return mix(primaryVisibility, secondaryVisibility, clamp(faceBlend, 0.0, 1.0));
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

vec3 ReconstructViewPosition(vec2 uv, float depth) {
    vec4 clipPosition = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPosition = frame.invProj * clipPosition;
    if (abs(viewPosition.w) <= 0.000001) {
        return vec3(0.0);
    }
    return viewPosition.xyz / viewPosition.w;
}

vec2 SsrOctEncode(vec3 value) {
    value /= max(abs(value.x) + abs(value.y) + abs(value.z), 0.000001);
    vec2 encoded = value.xy;
    if (value.z < 0.0) {
        encoded = (1.0 - abs(encoded.yx)) * sign(encoded.xy);
    }
    return encoded;
}

vec3 SsrOctDecode(vec2 encoded) {
    vec3 value = vec3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
    if (value.z < 0.0) {
        value.xy = (1.0 - abs(value.yx)) * sign(value.xy);
    }
    return normalize(value);
}

bool SsrDeferredReceiverReprojectionEnabled() {
    float encodedControl = floor(abs(frame.ssrControls.w) / 1024.0);
    return mod(encodedControl, 2.0) > 0.5;
}

float SsrDeferredReceiverHistoryConfidence(
    vec2 historyUv,
    vec3 receiverNormal,
    float receiverRoughness,
    float receiverPreviousViewDepth
) {
    vec2 texelSize = 1.0 / vec2(textureSize(ssrHistoryMetadata, 0));
    if (any(lessThan(historyUv, texelSize * 0.5)) ||
        any(greaterThan(historyUv, vec2(1.0) - texelSize * 0.5))) {
        return 0.0;
    }

    vec4 historyMetadata = texture(ssrHistoryMetadata, historyUv);
    if (historyMetadata.x <= 0.0001 || receiverPreviousViewDepth <= 0.0001) {
        return 0.0;
    }

    float depthTolerance = max(0.035, receiverPreviousViewDepth * 0.015);
    float depthConfidence = 1.0 - smoothstep(
        depthTolerance,
        depthTolerance * 2.5,
        abs(historyMetadata.x - receiverPreviousViewDepth)
    );
    vec3 historyNormal = SsrOctDecode(historyMetadata.yz);
    float normalConfidence = smoothstep(
        0.80,
        0.96,
        dot(receiverNormal, historyNormal)
    );
    float roughnessConfidence = 1.0 - smoothstep(
        0.08,
        0.22,
        abs(historyMetadata.w - receiverRoughness)
    );
    return min(depthConfidence, min(normalConfidence, roughnessConfidence));
}

vec3 ViewRayWorldDirection(vec2 uv) {
    vec2 ndc = uv * 2.0 - 1.0;
    if (frame.temporalControls.z > 0.5) {
        ndc -= frame.temporalJitter.zw * 2.0;
    }
    vec4 clipPosition = vec4(ndc, 1.0, 1.0);
    vec4 viewPosition = frame.invProj * clipPosition;
    vec3 viewDirection = abs(viewPosition.w) > 0.000001
        ? viewPosition.xyz / viewPosition.w
        : viewPosition.xyz;
    if (dot(viewDirection, viewDirection) <= 0.000001) {
        viewDirection = vec3(0.0, 0.0, -1.0);
    }
    return normalize((frame.invView * vec4(normalize(viewDirection), 0.0)).xyz);
}

vec3 SkyLightDirection() {
    vec3 lightDirection = lights.directionalLight.xyz;
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = frame.directionalLight.xyz;
    }
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = vec3(-0.45, -0.82, -0.35);
    }
    return normalize(-lightDirection);
}

vec3 VisibleSkyboxRadiance(vec2 uv) {
    if (frame.environmentControls.x <= 0.5 ||
        frame.environmentControls.w < 0.5) {
        return vec3(0.0);
    }

    vec3 direction = ViewRayWorldDirection(uv);
    vec3 skyRadiance = VisibleSkyboxTextureRadiance(direction);

    return skyRadiance * clamp(frame.environmentControls.y, 0.0, 4.0);
}

float HeightFogFactor(vec3 worldPosition, vec3 cameraPosition) {
    float enabled = clamp(frame.heightFogControls.x, 0.0, 1.0);
    float density = clamp(frame.heightFogControls.y, 0.0, 1.0);
    float heightFalloff = clamp(frame.heightFogControls.z, 0.0, 2.0);
    float startDistance = max(frame.heightFogControls.w, 0.0);
    float maxOpacity = clamp(frame.heightFogColor.a, 0.0, 1.0);
    if (enabled <= 0.0001 || density <= 0.0001 || maxOpacity <= 0.0001) {
        return 0.0;
    }

    float viewDistance = length(worldPosition - cameraPosition);
    float fogDistance = max(viewDistance - startDistance, 0.0);
    float heightWeight = exp(-max(worldPosition.y, 0.0) * heightFalloff);
    float fog = 1.0 - exp(-fogDistance * density * max(heightWeight, 0.08));
    return clamp(fog, 0.0, maxOpacity);
}

vec3 HeightFogColor() {
    return max(frame.heightFogColor.rgb, vec3(0.0));
}

vec3 ApplyHeightFog(vec3 color, vec3 worldPosition, vec3 cameraPosition) {
    return mix(color, HeightFogColor(), HeightFogFactor(worldPosition, cameraPosition));
}

float SsaoVisibility(vec2 uv, float depth, vec3 normal) {
    float strength = clamp(frame.ssaoControls.x, 0.0, 1.0);
    float radius = clamp(frame.ssaoControls.y, 0.0, 8.0);
    float bias = clamp(frame.ssaoControls.z, 0.0, 0.5);
    int sampleCount = clamp(int(frame.ssaoControls.w + 0.5), 0, 16);
    if (strength <= 0.0001 || radius <= 0.0001 || sampleCount <= 0 ||
        depth >= 0.999999) {
        return 1.0;
    }

    const vec2 directions[16] = vec2[](
        vec2(1.0, 0.0),
        vec2(-1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, -1.0),
        vec2(0.7071, 0.7071),
        vec2(-0.7071, 0.7071),
        vec2(0.7071, -0.7071),
        vec2(-0.7071, -0.7071),
        vec2(0.9239, 0.3827),
        vec2(-0.3827, 0.9239),
        vec2(0.3827, -0.9239),
        vec2(-0.9239, -0.3827),
        vec2(0.3827, 0.9239),
        vec2(-0.9239, 0.3827),
        vec2(0.9239, -0.3827),
        vec2(-0.3827, -0.9239)
    );

    vec2 texelSize = 1.0 / vec2(textureSize(sceneDepth, 0));
    vec3 viewPosition = ReconstructViewPosition(uv, depth);
    float viewDepth = max(abs(viewPosition.z), 0.0001);
    float pixelRadius = clamp((radius / viewDepth) * 180.0, 1.0, 32.0);
    float noise = InterleavedGradientNoise(gl_FragCoord.xy);
    float angle = noise * 6.2831853;
    mat2 rotation = mat2(
        cos(angle),
        -sin(angle),
        sin(angle),
        cos(angle)
    );
    vec3 viewNormal = normalize((frame.view * vec4(normal, 0.0)).xyz);

    float occlusion = 0.0;
    float weightSum = 0.0;
    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
        if (sampleIndex >= sampleCount) {
            break;
        }

        float sampleScale = (float(sampleIndex) + 1.0) / float(sampleCount);
        vec2 offset = rotation * directions[sampleIndex] *
            texelSize *
            pixelRadius *
            mix(0.35, 1.0, sampleScale * sampleScale);
        vec2 sampleUv = clamp(uv + offset, texelSize, vec2(1.0) - texelSize);
        float sampleDepth = texture(sceneDepth, sampleUv).r;
        if (sampleDepth >= 0.999999) {
            continue;
        }

        vec3 sampleViewPosition = ReconstructViewPosition(sampleUv, sampleDepth);
        vec3 delta = sampleViewPosition - viewPosition;
        float distanceToSample = length(delta);
        if (distanceToSample <= 0.0001 || distanceToSample > radius) {
            continue;
        }

        vec3 sampleDirection = delta / distanceToSample;
        float hemisphere = max(dot(viewNormal, sampleDirection), 0.0);
        float depthOcclusion = step(bias, delta.z);
        float rangeWeight = 1.0 - smoothstep(0.0, radius, distanceToSample);
        occlusion += depthOcclusion * hemisphere * rangeWeight;
        weightSum += rangeWeight;
    }

    float normalizedOcclusion = weightSum > 0.0001
        ? clamp(occlusion / weightSum, 0.0, 1.0)
        : 0.0;
    return clamp(1.0 - normalizedOcclusion * strength, 0.0, 1.0);
}

struct SsrTraceResult {
    vec2 hitUv;
    float confidence;
    float validRay;
    float travel;
    float facing;
    float refinementUsed;
    float hitFacing;
    float footprintConfidence;
    float receiverFootprintConfidence;
    float depthConfidence;
    float validationConfidence;
};

struct SsrProjectedSample {
    vec2 uv;
    vec3 rayView;
    vec3 sceneView;
    float depthDelta;
    float valid;
    float hasDepth;
};

ivec2 SsrNearestTexel(vec2 uv) {
    ivec2 extent = textureSize(sceneDepth, 0);
    return clamp(
        ivec2(uv * vec2(extent)),
        ivec2(0),
        extent - ivec2(1)
    );
}

float SsrFetchDepthNearest(vec2 uv) {
    return texelFetch(sceneDepth, SsrNearestTexel(uv), 0).r;
}

vec4 SsrFetchAlbedoNearest(vec2 uv) {
    return texelFetch(gBufferAlbedo, SsrNearestTexel(uv), 0);
}

vec4 SsrFetchNormalRoughnessNearest(vec2 uv) {
    return texelFetch(gBufferNormalRoughness, SsrNearestTexel(uv), 0);
}

vec4 SsrFetchMaterialNearest(vec2 uv) {
    return texelFetch(gBufferMaterial, SsrNearestTexel(uv), 0);
}

vec4 SsrFetchEmissiveNearest(vec2 uv) {
    return texelFetch(gBufferEmissive, SsrNearestTexel(uv), 0);
}

vec2 SsrFetchVelocityNearest(vec2 uv) {
    return texelFetch(gBufferVelocity, SsrNearestTexel(uv), 0).rg;
}

SsrProjectedSample ProjectSsrRaySample(
    vec3 viewPosition,
    vec3 reflectionDir,
    float rayLength,
    float t,
    vec2 texelSize
) {
    SsrProjectedSample traceSample;
    traceSample.uv = vec2(0.0);
    traceSample.rayView = viewPosition + reflectionDir * rayLength * t;
    traceSample.sceneView = vec3(0.0);
    traceSample.depthDelta = 0.0;
    traceSample.valid = 0.0;
    traceSample.hasDepth = 0.0;

    vec4 clip = frame.proj * vec4(traceSample.rayView, 1.0);
    if (abs(clip.w) <= 0.000001) {
        return traceSample;
    }

    vec3 ndc = clip.xyz / clip.w;
    vec2 sampleUv = ndc.xy * 0.5 + 0.5;
    if (sampleUv.x <= texelSize.x || sampleUv.x >= 1.0 - texelSize.x ||
        sampleUv.y <= texelSize.y || sampleUv.y >= 1.0 - texelSize.y ||
        ndc.z <= 0.0 || ndc.z >= 1.0) {
        return traceSample;
    }

    traceSample.uv = sampleUv;
    traceSample.valid = 1.0;
    float sampleDepth = SsrFetchDepthNearest(sampleUv);
    if (sampleDepth >= 0.999999 || sampleDepth <= 0.0) {
        return traceSample;
    }

    traceSample.sceneView = ReconstructViewPosition(sampleUv, sampleDepth);
    traceSample.depthDelta =
        traceSample.rayView.z - traceSample.sceneView.z;
    traceSample.hasDepth = 1.0;
    return traceSample;
}

float SsrTraceControl() {
    return mod(floor(abs(frame.ssrControls.w)), 128.0);
}

int SsrTraceStepCount() {
    return clamp(int(mod(floor(abs(frame.ssrControls.w)), 64.0) + 0.5), 0, 32);
}

bool SsrHierarchicalTraceEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 64.0), 2.0) > 0.5;
}

bool SsrSceneColorHistoryEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 128.0), 2.0) > 0.5;
}

bool SsrCurrentHdrSourceEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 2048.0), 2.0) > 0.5;
}

bool SsrProbeFallbackBlendEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 16384.0), 2.0) > 0.5;
}

float SsrProbeFallbackBlendWeight(float confidence, float roughness) {
    float resolvedConfidence = clamp(confidence, 0.0, 1.0);
    if (!SsrProbeFallbackBlendEnabled()) {
        return resolvedConfidence;
    }

    float confidenceStability =
        resolvedConfidence * mix(
            0.65,
            1.0,
            smoothstep(0.08, 0.78, resolvedConfidence)
        );
    float roughnessReliability =
        1.0 - smoothstep(0.38, 0.88, clamp(roughness, 0.0, 1.0));
    float roughnessCeiling = mix(0.35, 1.0, roughnessReliability);
    return clamp(confidenceStability * roughnessCeiling, 0.0, 1.0);
}

float SsrCurrentHdrRadianceTrust(float traceConfidence) {
    float confidence = clamp(traceConfidence, 0.0, 1.0);
    if (confidence < 0.45) {
        return 0.0;
    }
    return smoothstep(0.45, 0.85, confidence);
}

bool SsrHitValidationEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 256.0), 2.0) > 0.5;
}

bool SsrReconstructionEnabled() {
    return mod(floor(abs(frame.ssrControls.w) / 512.0), 2.0) > 0.5;
}

float SsrSignedDepthConfidence(float depthDelta, float thickness) {
    float safeThickness = max(thickness, 0.0001);
    float frontError = max(depthDelta, 0.0);
    float behindError = max(-depthDelta, 0.0);
    float frontConfidence = 1.0 - smoothstep(
        safeThickness * 0.5,
        safeThickness,
        frontError
    );
    float behindConfidence = 1.0 - smoothstep(
        safeThickness,
        safeThickness * 2.0,
        behindError
    );
    return frontConfidence * behindConfidence;
}

float SsrHitFootprintConfidence(
    vec2 hitUv,
    vec3 hitSceneView,
    vec3 hitNormalView,
    float thickness,
    vec2 texelSize
) {
    const vec2 offsets[4] = vec2[](
        vec2(1.0, 0.0),
        vec2(-1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, -1.0)
    );
    float stableWeight = 1.0;
    const float totalWeight = 5.0;
    float planeTolerance = max(thickness * 1.5, abs(hitSceneView.z) * 0.0015);
    for (int index = 0; index < 4; ++index) {
        vec2 sampleUv = hitUv + offsets[index] * texelSize;
        if (any(lessThanEqual(sampleUv, vec2(0.0))) ||
            any(greaterThanEqual(sampleUv, vec2(1.0)))) {
            continue;
        }

        float sampleDepth = SsrFetchDepthNearest(sampleUv);
        vec4 sampleAlbedo = SsrFetchAlbedoNearest(sampleUv);
        if (sampleDepth <= 0.0 || sampleDepth >= 0.999999 ||
            sampleAlbedo.a <= 0.001) {
            continue;
        }

        vec3 sampleSceneView = ReconstructViewPosition(sampleUv, sampleDepth);
        vec3 sampleNormal = normalize(
            SsrFetchNormalRoughnessNearest(sampleUv).xyz * 2.0 - 1.0
        );
        vec3 sampleNormalView = normalize((frame.view * vec4(sampleNormal, 0.0)).xyz);
        float planeSeparation = abs(dot(
            sampleSceneView - hitSceneView,
            hitNormalView
        ));
        float planeConfidence = 1.0 - smoothstep(
            planeTolerance,
            planeTolerance * 2.5,
            planeSeparation
        );
        float normalConfidence = smoothstep(
            0.45,
            0.90,
            dot(hitNormalView, sampleNormalView)
        );
        stableWeight += planeConfidence * normalConfidence;
    }
    return smoothstep(0.55, 0.95, stableWeight / totalWeight);
}

float SsrReceiverFootprintConfidence(
    vec2 receiverUv,
    vec3 receiverSceneView,
    vec3 receiverNormalView,
    float thickness,
    vec2 texelSize
) {
    const vec2 offsets[4] = vec2[](
        vec2(1.0, 0.0),
        vec2(-1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(0.0, -1.0)
    );
    float stableWeight = 1.0;
    const float totalWeight = 5.0;
    float planeTolerance = max(
        thickness * 1.5,
        abs(receiverSceneView.z) * 0.0015
    );
    for (int index = 0; index < 4; ++index) {
        vec2 sampleUv = receiverUv + offsets[index] * texelSize;
        if (any(lessThanEqual(sampleUv, vec2(0.0))) ||
            any(greaterThanEqual(sampleUv, vec2(1.0)))) {
            continue;
        }

        float sampleDepth = SsrFetchDepthNearest(sampleUv);
        vec4 sampleAlbedo = SsrFetchAlbedoNearest(sampleUv);
        if (sampleDepth <= 0.0 || sampleDepth >= 0.999999 ||
            sampleAlbedo.a <= 0.001) {
            continue;
        }

        vec3 sampleSceneView = ReconstructViewPosition(sampleUv, sampleDepth);
        vec3 sampleNormal = normalize(
            SsrFetchNormalRoughnessNearest(sampleUv).xyz * 2.0 - 1.0
        );
        vec3 sampleNormalView = normalize((frame.view * vec4(sampleNormal, 0.0)).xyz);
        float planeSeparation = abs(dot(
            sampleSceneView - receiverSceneView,
            receiverNormalView
        ));
        float planeConfidence = 1.0 - smoothstep(
            planeTolerance,
            planeTolerance * 2.5,
            planeSeparation
        );
        float normalConfidence = smoothstep(
            0.45,
            0.90,
            dot(receiverNormalView, sampleNormalView)
        );
        stableWeight += planeConfidence * normalConfidence;
    }
    return smoothstep(0.75, 0.98, stableWeight / totalWeight);
}

float SsrHitValidationConfidence(
    vec2 receiverUv,
    SsrProjectedSample hitSample,
    vec3 reflectionDir,
    float receiverFacing,
    vec3 receiverSceneView,
    vec3 receiverNormalView,
    float thickness,
    vec2 texelSize,
    vec2 depthExtent,
    out float hitFacing,
    out float footprintConfidence,
    out float receiverFootprintConfidence,
    out float depthConfidence
) {
    hitFacing = 1.0;
    footprintConfidence = 1.0;
    receiverFootprintConfidence = 1.0;
    depthConfidence = 1.0;
    if (!SsrHitValidationEnabled()) {
        return 1.0;
    }

    receiverFootprintConfidence = SsrReceiverFootprintConfidence(
        receiverUv,
        receiverSceneView,
        receiverNormalView,
        thickness,
        texelSize
    );
    vec3 hitNormal = normalize(
        SsrFetchNormalRoughnessNearest(hitSample.uv).xyz * 2.0 - 1.0
    );
    vec3 hitNormalView = normalize((frame.view * vec4(hitNormal, 0.0)).xyz);
    hitFacing = smoothstep(
        0.05,
        0.25,
        dot(hitNormalView, -reflectionDir)
    );
    depthConfidence = SsrSignedDepthConfidence(
        hitSample.depthDelta,
        thickness
    );
    footprintConfidence = SsrHitFootprintConfidence(
        hitSample.uv,
        hitSample.sceneView,
        hitNormalView,
        thickness,
        texelSize
    );
    float travelPixels = length((hitSample.uv - receiverUv) * depthExtent);
    float minimumTravelPixels = mix(6.0, 2.0, receiverFacing);
    float travelConfidence = smoothstep(
        minimumTravelPixels,
        minimumTravelPixels + 2.0,
        travelPixels
    );
    return receiverFacing * receiverFootprintConfidence * hitFacing *
        depthConfidence * footprintConfidence * travelConfidence;
}

SsrTraceResult TraceFixedStepScreenSpaceReflection(
    vec2 uv,
    float depth,
    vec3 normal,
    float roughness
) {
    SsrTraceResult result;
    result.hitUv = uv;
    result.confidence = 0.0;
    result.validRay = 0.0;
    result.travel = 0.0;
    result.facing = 0.0;
    result.refinementUsed = 0.0;
    result.hitFacing = 0.0;
    result.footprintConfidence = 0.0;
    result.receiverFootprintConfidence = 0.0;
    result.depthConfidence = 0.0;
    result.validationConfidence = 0.0;

    float strength = clamp(frame.ssrControls.x, 0.0, 1.0);
    float rayLength = clamp(frame.ssrControls.y, 0.0, 64.0);
    float thickness = clamp(frame.ssrControls.z, 0.0, 0.5);
    int stepCount = SsrTraceStepCount();
    bool refinementEnabled = frame.ssrControls.w >= 0.0;
    if (strength <= 0.0001 || rayLength <= 0.0001 || stepCount <= 0 ||
        depth >= 0.999999) {
        return result;
    }

    vec3 viewPosition = ReconstructViewPosition(uv, depth);
    vec3 viewNormal = normalize((frame.view * vec4(normal, 0.0)).xyz);
    vec3 viewDirFromCamera = normalize(viewPosition);
    vec3 reflectionDir = normalize(reflect(viewDirFromCamera, viewNormal));
    result.facing = smoothstep(
        0.06,
        0.30,
        max(dot(-viewDirFromCamera, viewNormal), 0.0)
    );
    if (reflectionDir.z >= -0.0001) {
        return result;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(sceneDepth, 0));
    vec2 depthExtent = vec2(textureSize(sceneDepth, 0));
    // SSR has no dedicated hit-history accumulation yet. A per-pixel random
    // step therefore turns hit acceptance into temporal noise; keep the
    // traversal deterministic until the production temporal resolve exists.
    const float jitter = 0.5;
    float roughnessMask = 1.0 - smoothstep(0.45, 0.95, roughness);
    float edgeFadePixels = 24.0;
    float bestConfidence = 0.0;
    float bestHitFacing = 0.0;
    float bestFootprintConfidence = 0.0;
    float bestReceiverFootprintConfidence = 0.0;
    float bestDepthConfidence = 0.0;
    float bestValidationConfidence = 0.0;
    vec2 bestUv = uv;
    float validRay = 0.0;
    float previousT = 0.0;
    float previousDepthDelta = 0.0;
    bool previousSampleValid = false;

    for (int index = 1; index <= 32; ++index) {
        if (index > stepCount) {
            break;
        }

        float t = (float(index) - 0.35 + jitter * 0.7) / float(stepCount);
        t = clamp(t, 0.001, 1.0);
        SsrProjectedSample traceSample = ProjectSsrRaySample(
            viewPosition,
            reflectionDir,
            rayLength,
            t,
            texelSize
        );
        if (traceSample.valid < 0.5) {
            continue;
        }
        validRay = 1.0;
        if (traceSample.hasDepth < 0.5) {
            continue;
        }

        float adaptiveThickness = max(
            thickness,
            abs(traceSample.rayView.z) * 0.006
        );
        if (!refinementEnabled) {
            float depthDelta = abs(traceSample.depthDelta);
            float hit = 1.0 - smoothstep(
                adaptiveThickness,
                adaptiveThickness * 2.5,
                depthDelta
            );
            if (traceSample.depthDelta > 0.0) {
                hit *= 0.45;
            }
            vec2 edgePixels =
                min(traceSample.uv, 1.0 - traceSample.uv) * depthExtent;
            float edgeFade = smoothstep(
                0.0,
                edgeFadePixels,
                min(edgePixels.x, edgePixels.y)
            );
            float distanceFade = 1.0 - smoothstep(0.45, 1.0, t);
            float hitFacing = 1.0;
            float footprintConfidence = 1.0;
            float receiverFootprintConfidence = 1.0;
            float depthConfidence = 1.0;
            float validationConfidence = SsrHitValidationConfidence(
                uv,
                traceSample,
                reflectionDir,
                result.facing,
                viewPosition,
                viewNormal,
                adaptiveThickness,
                texelSize,
                depthExtent,
                hitFacing,
                footprintConfidence,
                receiverFootprintConfidence,
                depthConfidence
            );
            float confidence = hit * edgeFade * distanceFade * roughnessMask *
                strength * validationConfidence;
            if (confidence > bestConfidence) {
                bestConfidence = confidence;
                bestUv = traceSample.uv;
                bestHitFacing = hitFacing;
                bestFootprintConfidence = footprintConfidence;
                bestReceiverFootprintConfidence = receiverFootprintConfidence;
                bestDepthConfidence = depthConfidence;
                bestValidationConfidence = validationConfidence;
            }
            continue;
        }

        bool crossedDepth = previousSampleValid &&
            previousDepthDelta > adaptiveThickness &&
            traceSample.depthDelta <= adaptiveThickness;
        bool firstSampleHit = !previousSampleValid &&
            traceSample.depthDelta <= adaptiveThickness;
        if (crossedDepth || firstSampleHit) {
            SsrProjectedSample hitSample = traceSample;
            float lowerT = previousSampleValid ? previousT : max(0.001, t - 1.0 / float(stepCount));
            float upperT = t;
            int refinementIterations = crossedDepth ? 4 : 0;
            for (int refineIndex = 0; refineIndex < 4; ++refineIndex) {
                if (refineIndex >= refinementIterations) {
                    break;
                }
                float middleT = 0.5 * (lowerT + upperT);
                SsrProjectedSample middleSample = ProjectSsrRaySample(
                    viewPosition,
                    reflectionDir,
                    rayLength,
                    middleT,
                    texelSize
                );
                if (middleSample.valid < 0.5 || middleSample.hasDepth < 0.5) {
                    lowerT = middleT;
                    continue;
                }
                float middleThickness = max(
                    thickness,
                    abs(middleSample.rayView.z) * 0.006
                );
                if (middleSample.depthDelta > middleThickness) {
                    lowerT = middleT;
                } else {
                    upperT = middleT;
                    hitSample = middleSample;
                }
            }

            float hitThickness = max(thickness, abs(hitSample.rayView.z) * 0.006);
            float hit = 1.0 - smoothstep(
                hitThickness,
                hitThickness * 2.5,
                abs(hitSample.depthDelta)
            );
            vec2 edgePixels = min(hitSample.uv, 1.0 - hitSample.uv) * depthExtent;
            float edgeFade = smoothstep(
                0.0,
                edgeFadePixels,
                min(edgePixels.x, edgePixels.y)
            );
            float distanceFade = 1.0 - smoothstep(0.45, 1.0, upperT);
            bestValidationConfidence = SsrHitValidationConfidence(
                uv,
                hitSample,
                reflectionDir,
                result.facing,
                viewPosition,
                viewNormal,
                hitThickness,
                texelSize,
                depthExtent,
                bestHitFacing,
                bestFootprintConfidence,
                bestReceiverFootprintConfidence,
                bestDepthConfidence
            );
            bestConfidence = hit * edgeFade * distanceFade * roughnessMask *
                strength * bestValidationConfidence;
            bestUv = hitSample.uv;
            result.refinementUsed = float(refinementIterations);
            break;
        }

        previousT = t;
        previousDepthDelta = traceSample.depthDelta;
        previousSampleValid = true;
    }

    result.confidence = clamp(bestConfidence, 0.0, 1.0);
    result.validRay = validRay;
    result.hitUv = bestUv;
    result.travel = clamp(length(bestUv - uv) * 3.5, 0.0, 1.0);
    result.hitFacing = bestHitFacing;
    result.footprintConfidence = bestFootprintConfidence;
    result.receiverFootprintConfidence = bestReceiverFootprintConfidence;
    result.depthConfidence = bestDepthConfidence;
    result.validationConfidence = bestValidationConfidence;
    return result;
}

float SsrScreenRayExitT(vec3 origin, vec3 direction) {
    float exitT = 1.0;
    for (int axis = 0; axis < 3; ++axis) {
        float component = direction[axis];
        if (abs(component) <= 0.000001) {
            continue;
        }
        float boundary = component > 0.0 ? 0.999999 : 0.000001;
        float axisT = (boundary - origin[axis]) / component;
        if (axisT > 0.0) {
            exitT = min(exitT, axisT);
        }
    }
    return max(exitT, 0.0);
}

float SsrNextCellBoundaryT(
    vec2 uv,
    vec2 screenDirection,
    ivec2 cellCount
) {
    vec2 cells = vec2(max(cellCount, ivec2(1)));
    vec2 cell = floor(clamp(uv, vec2(0.0), vec2(0.999999)) * cells);
    vec2 boundaryCell = cell + step(vec2(0.0), screenDirection);
    vec2 boundaryUv = boundaryCell / cells;
    vec2 boundaryDelta = boundaryUv - uv;
    vec2 boundaryT = vec2(1e20);
    if (abs(screenDirection.x) > 0.000001) {
        boundaryT.x = boundaryDelta.x / screenDirection.x;
    }
    if (abs(screenDirection.y) > 0.000001) {
        boundaryT.y = boundaryDelta.y / screenDirection.y;
    }
    if (boundaryT.x <= 0.000001) {
        boundaryT.x = 1e20;
    }
    if (boundaryT.y <= 0.000001) {
        boundaryT.y = 1e20;
    }
    return min(boundaryT.x, boundaryT.y);
}

SsrProjectedSample EvaluateSsrScreenSample(
    vec3 screenOrigin,
    vec3 screenDirection,
    float t,
    vec2 texelSize
) {
    SsrProjectedSample traceSample;
    traceSample.uv = vec2(0.0);
    traceSample.rayView = vec3(0.0);
    traceSample.sceneView = vec3(0.0);
    traceSample.depthDelta = 0.0;
    traceSample.valid = 0.0;
    traceSample.hasDepth = 0.0;

    vec3 screenPoint = screenOrigin + screenDirection * t;
    if (screenPoint.x <= texelSize.x ||
        screenPoint.x >= 1.0 - texelSize.x ||
        screenPoint.y <= texelSize.y ||
        screenPoint.y >= 1.0 - texelSize.y ||
        screenPoint.z <= 0.0 || screenPoint.z >= 1.0) {
        return traceSample;
    }

    traceSample.uv = screenPoint.xy;
    traceSample.valid = 1.0;
    traceSample.rayView = ReconstructViewPosition(
        screenPoint.xy,
        screenPoint.z
    );
    float sceneDepthValue = SsrFetchDepthNearest(screenPoint.xy);
    if (sceneDepthValue >= 0.999999 || sceneDepthValue <= 0.0) {
        return traceSample;
    }
    traceSample.sceneView = ReconstructViewPosition(
        screenPoint.xy,
        sceneDepthValue
    );
    traceSample.depthDelta = traceSample.rayView.z - traceSample.sceneView.z;
    traceSample.hasDepth = 1.0;
    return traceSample;
}

SsrTraceResult TraceHierarchicalScreenSpaceReflection(
    vec2 uv,
    float depth,
    vec3 normal,
    float roughness
) {
    SsrTraceResult result;
    result.hitUv = uv;
    result.confidence = 0.0;
    result.validRay = 0.0;
    result.travel = 0.0;
    result.facing = 0.0;
    result.refinementUsed = 0.0;
    result.hitFacing = 0.0;
    result.footprintConfidence = 0.0;
    result.receiverFootprintConfidence = 0.0;
    result.depthConfidence = 0.0;
    result.validationConfidence = 0.0;

    float strength = clamp(frame.ssrControls.x, 0.0, 1.0);
    float rayLength = clamp(frame.ssrControls.y, 0.0, 64.0);
    float thickness = clamp(frame.ssrControls.z, 0.0, 0.5);
    int stepCount = SsrTraceStepCount();
    if (strength <= 0.0001 || rayLength <= 0.0001 || stepCount <= 0 ||
        depth >= 0.999999) {
        return result;
    }

    vec3 viewPosition = ReconstructViewPosition(uv, depth);
    vec3 viewNormal = normalize((frame.view * vec4(normal, 0.0)).xyz);
    vec3 viewDirFromCamera = normalize(viewPosition);
    vec3 reflectionDir = normalize(reflect(viewDirFromCamera, viewNormal));
    result.facing = smoothstep(
        0.06,
        0.30,
        max(dot(-viewDirFromCamera, viewNormal), 0.0)
    );
    if (reflectionDir.z >= -0.0001) {
        return result;
    }

    vec4 endClip = frame.proj * vec4(
        viewPosition + reflectionDir * rayLength,
        1.0
    );
    if (abs(endClip.w) <= 0.000001) {
        return result;
    }
    vec3 endNdc = endClip.xyz / endClip.w;
    vec3 screenOrigin = vec3(uv, depth);
    vec3 screenEnd = vec3(endNdc.xy * 0.5 + 0.5, endNdc.z);
    vec3 screenDirection = screenEnd - screenOrigin;
    vec2 depthExtent = vec2(textureSize(sceneDepth, 0));
    vec2 texelSize = 1.0 / depthExtent;
    float projectedLengthPixels = length(screenDirection.xy * depthExtent);
    if (projectedLengthPixels <= 0.5) {
        return result;
    }

    int maxMip = max(textureQueryLevels(ssrDepthPyramid) - 1, 0);
    int mip = min(2, maxMip);
    float maxT = SsrScreenRayExitT(screenOrigin, screenDirection);
    float originBiasPixels = SsrHitValidationEnabled()
        ? mix(6.0, 2.0, result.facing)
        : 2.0;
    float currentT = min(
        max(originBiasPixels / projectedLengthPixels, 0.0005),
        maxT
    );
    float previousFrontT = 0.0;
    bool hasFrontSample = false;
    bool refinementEnabled = frame.ssrControls.w >= 0.0;
    float roughnessMask = 1.0 - smoothstep(0.45, 0.95, roughness);
    const float edgeFadePixels = 24.0;

    for (int iteration = 0; iteration < 32; ++iteration) {
        if (iteration >= stepCount || currentT >= maxT) {
            break;
        }
        vec3 screenPoint = screenOrigin + screenDirection * currentT;
        if (screenPoint.x <= texelSize.x ||
            screenPoint.x >= 1.0 - texelSize.x ||
            screenPoint.y <= texelSize.y ||
            screenPoint.y >= 1.0 - texelSize.y ||
            screenPoint.z <= 0.0 || screenPoint.z >= 1.0) {
            break;
        }
        result.validRay = 1.0;

        ivec2 mipSize = textureSize(ssrDepthPyramid, mip);
        ivec2 mipPixel = clamp(
            ivec2(screenPoint.xy * vec2(mipSize)),
            ivec2(0),
            mipSize - ivec2(1)
        );
        float nearestDepth = texelFetch(
            ssrDepthPyramid,
            mipPixel,
            mip
        ).r;
        bool emptyCell = nearestDepth >= 0.999999 || nearestDepth <= 0.0;
        vec3 rayView = ReconstructViewPosition(screenPoint.xy, screenPoint.z);
        float depthDelta = 1e20;
        float adaptiveThickness = max(thickness, abs(rayView.z) * 0.006);
        if (!emptyCell) {
            vec3 nearestSceneView = ReconstructViewPosition(
                screenPoint.xy,
                nearestDepth
            );
            depthDelta = rayView.z - nearestSceneView.z;
        }

        bool rayInFrontOfCell = emptyCell || depthDelta > adaptiveThickness;
        if (rayInFrontOfCell) {
            previousFrontT = currentT;
            hasFrontSample = true;
            ivec2 cellCount = textureSize(ssrDepthPyramid, mip);
            float boundaryAdvance = SsrNextCellBoundaryT(
                screenPoint.xy,
                screenDirection.xy,
                cellCount
            );
            if (boundaryAdvance >= 1e19) {
                break;
            }
            currentT += boundaryAdvance + max(0.25 / projectedLengthPixels, 0.00001);
            mip = min(mip + 1, maxMip);
            continue;
        }

        if (mip > 0) {
            --mip;
            continue;
        }

        if (!hasFrontSample) {
            currentT += max(1.0 / projectedLengthPixels, 0.00005);
            continue;
        }

        float lowerT = previousFrontT;
        float upperT = currentT;
        SsrProjectedSample hitSample = EvaluateSsrScreenSample(
            screenOrigin,
            screenDirection,
            upperT,
            texelSize
        );
        int refinementIterations = refinementEnabled ? 4 : 0;
        for (int refineIndex = 0; refineIndex < 4; ++refineIndex) {
            if (refineIndex >= refinementIterations) {
                break;
            }
            float middleT = 0.5 * (lowerT + upperT);
            SsrProjectedSample middleSample = EvaluateSsrScreenSample(
                screenOrigin,
                screenDirection,
                middleT,
                texelSize
            );
            if (middleSample.valid < 0.5 || middleSample.hasDepth < 0.5) {
                lowerT = middleT;
                continue;
            }
            float middleThickness = max(
                thickness,
                abs(middleSample.rayView.z) * 0.006
            );
            if (middleSample.depthDelta > middleThickness) {
                lowerT = middleT;
            } else {
                upperT = middleT;
                hitSample = middleSample;
            }
        }

        if (hitSample.valid > 0.5 && hitSample.hasDepth > 0.5) {
            float hitThickness = max(
                thickness,
                abs(hitSample.rayView.z) * 0.006
            );
            float hit = 1.0 - smoothstep(
                hitThickness,
                hitThickness * 2.5,
                abs(hitSample.depthDelta)
            );
            vec2 edgePixels = min(
                hitSample.uv,
                1.0 - hitSample.uv
            ) * depthExtent;
            float edgeFade = smoothstep(
                0.0,
                edgeFadePixels,
                min(edgePixels.x, edgePixels.y)
            );
            float distanceFade = 1.0 - smoothstep(0.45, 1.0, upperT);
            result.validationConfidence = SsrHitValidationConfidence(
                uv,
                hitSample,
                reflectionDir,
                result.facing,
                viewPosition,
                viewNormal,
                hitThickness,
                texelSize,
                depthExtent,
                result.hitFacing,
                result.footprintConfidence,
                result.receiverFootprintConfidence,
                result.depthConfidence
            );
            result.confidence = clamp(
                hit * edgeFade * distanceFade * roughnessMask * strength *
                    result.validationConfidence,
                0.0,
                1.0
            );
            result.hitUv = hitSample.uv;
            result.travel = clamp(
                length(hitSample.uv - uv) * 3.5,
                0.0,
                1.0
            );
            result.refinementUsed = float(refinementIterations);
        }
        break;
    }
    return result;
}

SsrTraceResult TraceScreenSpaceReflection(
    vec2 uv,
    float depth,
    vec3 normal,
    float roughness
) {
    if (SsrHierarchicalTraceEnabled()) {
        return TraceHierarchicalScreenSpaceReflection(
            uv,
            depth,
            normal,
            roughness
        );
    }
    return TraceFixedStepScreenSpaceReflection(uv, depth, normal, roughness);
}

vec3 ScreenSpaceReflectionDebug(
    SsrTraceResult trace,
    float roughness
) {
    vec3 miss = trace.validRay > 0.5
        ? vec3(0.03, 0.09, 0.16)
        : mix(vec3(0.02, 0.025, 0.035), vec3(0.28, 0.10, 0.055), trace.facing);
    if (SsrHitValidationEnabled() && trace.validRay > 0.5) {
        vec3 rejection = vec3(
            1.0 - trace.hitFacing,
            1.0 - trace.footprintConfidence,
            1.0 - trace.depthConfidence
        );
        miss = mix(miss, rejection, 0.35 * (1.0 - trace.validationConfidence));
    }
    vec3 rough = vec3(0.18, 0.11, 0.035);
    vec3 hit = mix(vec3(0.08, 0.46, 0.72), vec3(0.85, 0.98, 1.0), trace.confidence);
    hit = mix(hit, vec3(0.26, 0.96, 0.78), trace.travel * 0.35);
    hit = mix(hit, vec3(0.36, 0.98, 0.52), step(0.5, trace.refinementUsed));
    return mix(mix(miss, rough, smoothstep(0.45, 0.95, roughness)), hit, trace.confidence);
}

vec3 ScreenSpaceReflectionRadiance(
    SsrTraceResult trace,
    vec3 fallbackRadiance,
    float receiverRoughness,
    vec3 lightDir,
    float ambientStrength,
    float directIntensity
) {
    if (trace.confidence <= 0.0001) {
        return fallbackRadiance;
    }

    float hitDepth = SsrFetchDepthNearest(trace.hitUv);
    if (hitDepth >= 0.999999 || hitDepth <= 0.0) {
        return fallbackRadiance;
    }

    vec4 hitAlbedo = SsrFetchAlbedoNearest(trace.hitUv);
    if (hitAlbedo.a <= 0.001) {
        return fallbackRadiance;
    }

    vec4 hitNormalRoughness = SsrFetchNormalRoughnessNearest(trace.hitUv);
    vec4 hitMaterial = SsrFetchMaterialNearest(trace.hitUv);
    vec3 hitNormal = normalize(hitNormalRoughness.xyz * 2.0 - 1.0);
    float hitRoughness = clamp(hitNormalRoughness.w, 0.04, 1.0);
    float hitOcclusion = clamp(hitMaterial.g, 0.0, 1.0);
    vec3 hitEmissive = max(SsrFetchEmissiveNearest(trace.hitUv).rgb, vec3(0.0));
    vec3 hitBaseColor = hitAlbedo.rgb;
    float hitNDotL = max(dot(hitNormal, lightDir), 0.0);
    vec3 diffuseApprox =
        hitBaseColor *
        (
            EnvironmentRadiance(hitNormal, lightDir, 1.0) * 0.38 * ambientStrength * hitOcclusion +
            vec3(hitNDotL * directIntensity * 0.22)
        );
    vec3 glossyApprox =
        EnvironmentRadiance(reflect(-lightDir, hitNormal), lightDir, hitRoughness) *
        mix(0.04, 0.18, 1.0 - hitRoughness);
    vec3 hitRadiance = max(diffuseApprox + glossyApprox + hitEmissive, vec3(0.0));

    float historyReady = clamp(frame.temporalResolveControls.z, 0.0, 1.0);
    float velocityReady = clamp(frame.temporalResolveControls.w, 0.0, 1.0);
    float currentHdrTrust = SsrCurrentHdrSourceEnabled()
        ? SsrCurrentHdrRadianceTrust(trace.confidence)
        : 0.0;
    if (!SsrCurrentHdrSourceEnabled() || !SsrSceneColorHistoryEnabled() ||
        historyReady <= 0.5 || velocityReady <= 0.5) {
        return mix(
            fallbackRadiance,
            hitRadiance,
            SsrProbeFallbackBlendWeight(trace.confidence, receiverRoughness)
        );
    }

    vec2 reprojectionVelocity = SsrFetchVelocityNearest(trace.hitUv);
    if (frame.temporalControls.z > 0.5) {
        reprojectionVelocity -= frame.temporalJitter.zw;
    }
    vec2 historyUv = trace.hitUv - reprojectionVelocity;
    if (any(lessThanEqual(historyUv, vec2(0.0))) ||
        any(greaterThanEqual(historyUv, vec2(1.0)))) {
        return mix(
            fallbackRadiance,
            hitRadiance,
            SsrProbeFallbackBlendWeight(trace.confidence, receiverRoughness)
        );
    }

    vec2 historyExtent = vec2(max(textureSize(ssrSceneColorHistory, 0), ivec2(1)));
    vec2 historyTexel = 1.0 / historyExtent;
    float filterRadiusPixels = mix(
        0.5,
        3.0,
        clamp(receiverRoughness * receiverRoughness, 0.0, 1.0)
    );
    vec2 filterRadius = historyTexel * filterRadiusPixels;
    vec3 historyRadiance =
        texture(ssrSceneColorHistory, historyUv).rgb * 0.40 +
        texture(ssrSceneColorHistory, historyUv + vec2(filterRadius.x, 0.0)).rgb * 0.15 +
        texture(ssrSceneColorHistory, historyUv - vec2(filterRadius.x, 0.0)).rgb * 0.15 +
        texture(ssrSceneColorHistory, historyUv + vec2(0.0, filterRadius.y)).rgb * 0.15 +
        texture(ssrSceneColorHistory, historyUv - vec2(0.0, filterRadius.y)).rgb * 0.15;
    historyRadiance = clamp(historyRadiance, vec3(0.0), vec3(64.0));

    float velocityThreshold = max(frame.temporalRejectionControls.y, 0.01);
    float motionConfidence = 1.0 - smoothstep(
        velocityThreshold * 0.5,
        velocityThreshold * 2.0,
        length(reprojectionVelocity)
    );
    vec2 edgePixels = min(historyUv, 1.0 - historyUv) * historyExtent;
    float historyEdgeConfidence = smoothstep(
        0.0,
        8.0,
        min(edgePixels.x, edgePixels.y)
    );
    float historyConfidence = motionConfidence * historyEdgeConfidence;
    historyConfidence *= currentHdrTrust;
    vec3 resolvedHitRadiance = mix(hitRadiance, historyRadiance, historyConfidence);
    float blendWeight = SsrProbeFallbackBlendWeight(
        currentHdrTrust * mix(0.65, 1.0, historyConfidence),
        receiverRoughness
    );
    return mix(fallbackRadiance, resolvedHitRadiance, blendWeight);
}

int ActiveShadowCascadeCount() {
    return clamp(
        int(shadowCascades.cascadeInfo.x + 0.5),
        0,
        MAX_DIRECTIONAL_SHADOW_CASCADES
    );
}

int SelectShadowCascade(vec3 worldPosition, out float viewDepth) {
    int activeCascadeCount = ActiveShadowCascadeCount();
    viewDepth = abs((frame.view * vec4(worldPosition, 1.0)).z);
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

    vec4 lightSpacePosition = shadowCascades.viewProjections[cascadeIndex] *
        vec4(worldPosition, 1.0);
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

vec3 DirectionalShadowReceiverPosition(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int cascadeIndex
) {
    float normalOffsetTexels = clamp(
        shadowCascades.receiverPlaneBiasControls.y,
        0.0,
        4.0
    );
    float slopeOffsetTexels = clamp(
        shadowCascades.receiverPlaneBiasControls.z,
        0.0,
        2.0
    );
    if ((normalOffsetTexels <= 0.0001 && slopeOffsetTexels <= 0.0001) ||
        cascadeIndex < 0 || cascadeIndex >= ActiveShadowCascadeCount()) {
        return worldPosition;
    }

    float texelWorldSize = max(shadowCascades.texelWorldSizes[cascadeIndex], 0.0);
    float normalLengthSquared = dot(normal, normal);
    float lightLengthSquared = dot(lightDir, lightDir);
    if (texelWorldSize <= 0.0 ||
        normalLengthSquared <= 0.0000001 || lightLengthSquared <= 0.0000001) {
        return worldPosition;
    }

    vec3 unitNormal = normal * inversesqrt(normalLengthSquared);
    vec3 unitLightDir = lightDir * inversesqrt(lightLengthSquared);
    float cosAlpha = clamp(dot(unitNormal, unitLightDir), 0.0, 1.0);
    float sinAlpha = sqrt(max(1.0 - cosAlpha * cosAlpha, 0.0));
    float tanAlpha = sinAlpha / max(cosAlpha, 0.0001);
    return worldPosition +
        unitNormal * (normalOffsetTexels * texelWorldSize * sinAlpha) +
        unitLightDir * (slopeOffsetTexels * texelWorldSize * min(tanAlpha, 2.0));
}

const int MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES = 16;

float DirectionalShadowHash12(vec2 value) {
    vec3 hash = fract(vec3(value.xyx) * 0.1031);
    hash += dot(hash, hash.yzx + 33.33);
    return fract((hash.x + hash.y) * hash.z);
}

vec2 DirectionalShadowPoissonOffset(int sampleIndex) {
    const vec2 offsets[MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES] = vec2[](
        vec2(0.14383161, -0.14100790), vec2(-0.81544232, -0.87912464),
        vec2(0.97484398, 0.75648379), vec2(-0.81409955, 0.91437590),
        vec2(0.44323325, -0.97511554), vec2(-0.94201624, -0.39906216),
        vec2(0.79197514, 0.19090188), vec2(-0.24188840, 0.99706507),
        vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
        vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023),
        vec2(-0.38277543, 0.27676845), vec2(-0.91588581, 0.45771432),
        vec2(0.94558609, -0.76890725), vec2(0.19984126, 0.78641367)
    );
    return offsets[clamp(sampleIndex, 0, MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES - 1)];
}

float DirectionalShadowStablePoissonAngle(
    vec2 shadowUv,
    vec2 cascadeTexelSize,
    int cascadeIndex
) {
    return mod(0.754877666 + float(cascadeIndex) * 2.39996323, 6.28318530718);
}

vec2 RotateDirectionalShadowPoissonOffset(vec2 offset, float angle) {
    float sinAngle = sin(angle);
    float cosAngle = cos(angle);
    return vec2(
        offset.x * cosAngle - offset.y * sinAngle,
        offset.x * sinAngle + offset.y * cosAngle
    );
}

bool SampleShadowCascadeVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int cascadeIndex,
    out float visibility
);

bool ResolveShadowCascadeVisibility(
    vec3 worldPosition,
    vec3 normal,
    vec3 lightDir,
    int preferredCascadeIndex,
    int activeCascadeCount,
    out float visibility
);

float ApplyShadowDistanceFade(
    float visibility,
    int cascadeIndex,
    int activeCount,
    float viewDepth
);

float ShadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightDir) {
    if (frame.shadowControls.x < 0.5 ||
        frame.shadowControls.y <= 0.001 ||
        shadowCascades.cascadeInfo.y <= 0.5) {
        return 1.0;
    }

    int activeCascadeCount = ActiveShadowCascadeCount();
    if (activeCascadeCount <= 0) {
        return 1.0;
    }

    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(worldPosition, viewDepth);

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
    float thickness = clamp(frame.contactShadowControls.w, 0.0, 0.5);
    float jitterStrength = clamp(frame.contactShadowStabilityControls.x, 0.0, 1.0);
    float edgeFadePixels = clamp(frame.contactShadowStabilityControls.y, 0.0, 96.0);
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
    float receiverBias = max(0.0015, thickness * 0.08);
    float maxThickness = max(thickness, rayLength * 0.08);
    float rayPixels = clamp(
        (rayLength / viewDepth) * 220.0,
        1.0,
        48.0
    );
    vec3 viewLightDir = normalize(mat3(frame.view) * lightDir);
    vec4 lightClipA = frame.proj * (viewPosition + vec4(viewLightDir * rayLength, 0.0));
    vec2 rayDir = vec2(0.0);
    if (abs(lightClipA.w) > 0.000001) {
        vec2 lightUv = lightClipA.xy / lightClipA.w * 0.5 + 0.5;
        rayDir = lightUv - uv;
    }
    if (dot(rayDir, rayDir) <= 0.000001) {
        rayDir = normalize(lightDir.xy + vec2(0.37, 0.19));
    } else {
        rayDir = normalize(rayDir);
    }

    float occlusion = 0.0;
    vec2 depthExtent = vec2(textureSize(sceneDepth, 0));
    float jitter = (InterleavedGradientNoise(floor(uv * depthExtent)) - 0.5) * jitterStrength;
    for (int index = 1; index <= 12; ++index) {
        if (index > stepCount) {
            break;
        }

        float t = (float(index) + jitter) / float(stepCount);
        t = clamp(t, 0.0001, 1.0);
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
            normalSeparation > receiverBias &&
            normalSeparation < maxThickness) {
            float distanceFade = 1.0 - smoothstep(0.0, rayLength, alongLight);
            float thicknessFade = 1.0 - smoothstep(receiverBias, maxThickness, normalSeparation);
            float stepFade = 1.0 - smoothstep(0.55, 1.0, t);
            vec2 edgePixels = min(sampleUv, 1.0 - sampleUv) * depthExtent;
            float edgeFade = edgeFadePixels > 0.0001
                ? smoothstep(0.0, edgeFadePixels, min(edgePixels.x, edgePixels.y))
                : 1.0;
            occlusion = max(occlusion, distanceFade * thicknessFade * stepFade * edgeFade);
        }
    }

    float grazingBoost = mix(1.0, 0.35, nDotL);
    return 1.0 - clamp(occlusion * strength * grazingBoost, 0.0, 1.0);
}

#if 0 // Replaced by hardware-comparison optimized tent PCF below.
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

    bool singleShadowMap = shadowCascades.cascadeInfo.w < -0.5;
    vec2 tileOrigin = singleShadowMap
        ? vec2(0.0)
        : vec2(float(cascadeIndex % 2), float(cascadeIndex / 2)) * 0.5;
    vec2 atlasUv = singleShadowMap
        ? shadowUv
        : tileOrigin + shadowUv * 0.5;
    vec2 tileMax = singleShadowMap ? vec2(1.0) : tileOrigin + vec2(0.5);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowSampler, 0));
    float pcfRadius = clamp(frame.shadowFiltering.x, 0.0, 3.0);
    float biasMin = max(frame.shadowControls.z, 0.0);
    float biasSlope = max(frame.shadowControls.w, 0.0);
    float bias = max(biasSlope * (1.0 - dot(normal, lightDir)), biasMin);
    float receiverPlaneBiasScale = clamp(
        shadowCascades.receiverPlaneBiasControls.x,
        0.0,
        4.0
    );
    vec2 shadowUvDx = dFdx(shadowUv);
    vec2 shadowUvDy = dFdy(shadowUv);
    float depthDx = dFdx(currentDepth);
    float depthDy = dFdy(currentDepth);
    float receiverPlaneDeterminant =
        shadowUvDx.x * shadowUvDy.y - shadowUvDx.y * shadowUvDy.x;
    vec2 receiverPlaneGradient = vec2(0.0);
    if (abs(receiverPlaneDeterminant) > 0.0000001) {
        receiverPlaneGradient = vec2(
            (shadowUvDy.y * depthDx - shadowUvDx.y * depthDy) /
                receiverPlaneDeterminant,
            (shadowUvDx.x * depthDy - shadowUvDy.x * depthDx) /
                receiverPlaneDeterminant
        );
    }
    float cascadeUvPerAtlasUv = singleShadowMap ? 1.0 : 2.0;
    vec2 cascadeTexelSize = texelSize * cascadeUvPerAtlasUv;
    int kernelRadius = clamp(int(shadowCascades.cascadeBlendControls.z + 0.5), 0, 2);
    float pcssStrength = clamp(shadowCascades.cascadeBlendControls.w, 0.0, 1.0);
    int filterMode = clamp(
        int(shadowCascades.directionalFilterControls.x + 0.5),
        0,
        1
    );
    bool useStablePoisson = filterMode == 1;
    int poissonSampleCount = clamp(
        int(shadowCascades.directionalFilterControls.y + 0.5),
        4,
        MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES
    );
    int blockerSampleCount = clamp(
        int(shadowCascades.directionalFilterControls.z + 0.5),
        4,
        MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES
    );
    float poissonAngle = DirectionalShadowStablePoissonAngle(
        shadowUv,
        cascadeTexelSize,
        cascadeIndex
    );
    float filterRadius = pcfRadius;
    if (pcssStrength > 0.0001 && kernelRadius > 0) {
        float blockerDepthSum = 0.0;
        int blockerCount = 0;
        float searchRadius = max(pcfRadius, 1.0) * (1.0 + pcssStrength);
        if (useStablePoisson) {
            for (int sampleIndex = 0;
                sampleIndex < MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES;
                ++sampleIndex) {
                if (sampleIndex >= blockerSampleCount) {
                    break;
                }
                vec2 sampleOffset = RotateDirectionalShadowPoissonOffset(
                    DirectionalShadowPoissonOffset(sampleIndex),
                    poissonAngle
                );
                vec2 blockerUv = clamp(
                    atlasUv + sampleOffset * texelSize * searchRadius,
                    tileOrigin + texelSize,
                    tileMax - texelSize
                );
                float blockerDepth = texture(shadowSampler, blockerUv).r;
                vec2 cascadeOffset =
                    sampleOffset * texelSize * searchRadius * cascadeUvPerAtlasUv;
                float receiverPlaneBias = receiverPlaneBiasScale * dot(
                    abs(receiverPlaneGradient),
                    max(abs(cascadeOffset), cascadeTexelSize * 0.5)
                );
                if (currentDepth - bias - receiverPlaneBias > blockerDepth) {
                    blockerDepthSum += blockerDepth;
                    ++blockerCount;
                }
            }
        } else {
            for (int x = -1; x <= 1; ++x) {
                for (int y = -1; y <= 1; ++y) {
                    vec2 blockerUv = clamp(
                        atlasUv + vec2(x, y) * texelSize * searchRadius,
                        tileOrigin + texelSize,
                        tileMax - texelSize
                    );
                    float blockerDepth = texture(shadowSampler, blockerUv).r;
                    vec2 cascadeOffset =
                        vec2(x, y) * texelSize * searchRadius * cascadeUvPerAtlasUv;
                    float receiverPlaneBias = receiverPlaneBiasScale * dot(
                        abs(receiverPlaneGradient),
                        max(abs(cascadeOffset), cascadeTexelSize * 0.5)
                    );
                    if (currentDepth - bias - receiverPlaneBias > blockerDepth) {
                        blockerDepthSum += blockerDepth;
                        ++blockerCount;
                    }
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
    float filterWeightSum = 0.0;
    if (useStablePoisson) {
        float filterExtent = filterRadius * float(max(kernelRadius, 1));
        for (int sampleIndex = 0;
            sampleIndex < MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES;
            ++sampleIndex) {
            if (sampleIndex >= poissonSampleCount) {
                break;
            }
            vec2 sampleOffset = RotateDirectionalShadowPoissonOffset(
                DirectionalShadowPoissonOffset(sampleIndex),
                poissonAngle
            );
            vec2 sampleUv = clamp(
                atlasUv + sampleOffset * texelSize * filterExtent,
                tileOrigin + texelSize,
                tileMax - texelSize
            );
            float closestDepth = texture(shadowSampler, sampleUv).r;
            vec2 cascadeOffset =
                sampleOffset * texelSize * filterExtent * cascadeUvPerAtlasUv;
            float receiverPlaneBias = receiverPlaneBiasScale * dot(
                abs(receiverPlaneGradient),
                max(abs(cascadeOffset), cascadeTexelSize * 0.5)
            );
            shadow += (currentDepth - bias - receiverPlaneBias > closestDepth ? 1.0 : 0.0);
            filterWeightSum += 1.0;
        }
    } else {
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
                float closestDepth = texture(shadowSampler, sampleUv).r;
                vec2 cascadeOffset =
                    vec2(x, y) * texelSize * filterRadius * cascadeUvPerAtlasUv;
                float receiverPlaneBias = receiverPlaneBiasScale * dot(
                    abs(receiverPlaneGradient),
                    max(abs(cascadeOffset), cascadeTexelSize * 0.5)
                );
                float filterWeight = float(kernelRadius + 1 - abs(x)) *
                    float(kernelRadius + 1 - abs(y));
                shadow += (currentDepth - bias - receiverPlaneBias > closestDepth ? 1.0 : 0.0) *
                    filterWeight;
                filterWeightSum += filterWeight;
            }
        }
    }

    float occlusion = shadow / max(filterWeightSum, 1.0);
    visibility = 1.0 - occlusion * clamp(frame.shadowControls.y, 0.0, 1.0);
    return true;
}

#endif

vec2 ClampShadowAtlasSampleUv(vec2 atlasUv, int cascadeIndex, vec2 texelSize) {
    if (shadowCascades.cascadeInfo.w < -0.5) {
        return clamp(atlasUv, texelSize, vec2(1.0) - texelSize);
    }
    vec2 tileMin = vec2(float(cascadeIndex % 2), float(cascadeIndex / 2)) * 0.5;
    vec2 tileMax = tileMin + vec2(0.5);
    return clamp(atlasUv, tileMin + texelSize, tileMax - texelSize);
}

bool SampleDirectionalPcssVisibility(
    vec2 atlasUv,
    vec2 texelSize,
    vec2 cascadeTexelSize,
    float currentDepth,
    float baseBias,
    vec2 receiverPlaneGradient,
    float receiverPlaneBiasScale,
    float receiverNdotL,
    int cascadeIndex,
    out float pcssVisibility,
    out float pcssBlend
) {
    pcssVisibility = 1.0;
    pcssBlend = 0.0;
    // Grazing self-receivers make blocker search unreliable; retain Tent PCF there.
    vec4 receiverControls = shadowCascades.directionalPcssReceiverControls;
    float receiverConfidence = receiverControls.z > 0.5
        ? smoothstep(
            clamp(receiverControls.x, 0.0, 0.95),
            max(receiverControls.y, receiverControls.x + 0.001),
            clamp(receiverNdotL, 0.0, 1.0)
        )
        : 1.0;
    if (receiverConfidence <= 0.0001) {
        return false;
    }
    float strength = clamp(shadowCascades.directionalPcssControls.x, 0.0, 1.0);
    int blockerSampleCount = clamp(
        int(shadowCascades.directionalPcssControls.y + 0.5),
        0,
        MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES
    );
    int filterSampleCount = clamp(
        int(shadowCascades.directionalPcssControls.z + 0.5),
        0,
        MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES
    );
    float maxPenumbraTexels = clamp(
        shadowCascades.directionalPcssControls.w,
        0.0,
        16.0
    );
    float searchRadiusTexels = clamp(
        shadowCascades.directionalPcssGeometry.x,
        0.0,
        16.0
    );
    float lightAngularRadius = clamp(
        shadowCascades.directionalPcssGeometry.y,
        0.0,
        0.05
    );
    if (strength <= 0.0001 || blockerSampleCount <= 0 ||
        filterSampleCount <= 0 || maxPenumbraTexels <= 2.0 ||
        searchRadiusTexels <= 0.0 || lightAngularRadius <= 0.0 ||
        shadowCascades.directionalPcssGeometry.z < 0.5 ||
        shadowCascades.directionalPcssGeometry.w > 0.5) {
        return false;
    }

    float depthWorldSpan = max(shadowCascades.lightDepthWorldSpans[cascadeIndex], 0.0);
    float texelWorldSize = max(shadowCascades.texelWorldSizes[cascadeIndex], 0.0);
    if (depthWorldSpan <= 0.0 || texelWorldSize <= 0.0) {
        return false;
    }

    float poissonAngle = DirectionalShadowStablePoissonAngle(
        atlasUv,
        cascadeTexelSize,
        cascadeIndex
    );
    float blockerDepthSum = 0.0;
    int blockerCount = 0;
    for (int sampleIndex = 0;
        sampleIndex < MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES;
        ++sampleIndex) {
        if (sampleIndex >= blockerSampleCount) {
            break;
        }
        vec2 sampleOffset = RotateDirectionalShadowPoissonOffset(
            DirectionalShadowPoissonOffset(sampleIndex),
            poissonAngle
        );
        vec2 sampleUv = ClampShadowAtlasSampleUv(
            atlasUv + sampleOffset * texelSize * searchRadiusTexels,
            cascadeIndex,
            texelSize
        );
        vec2 cascadeOffset = sampleOffset * cascadeTexelSize * searchRadiusTexels;
        float sampleReceiverBias = receiverPlaneBiasScale * dot(
            abs(receiverPlaneGradient),
            max(abs(cascadeOffset), cascadeTexelSize * 0.5)
        );
        float sampleComparisonDepth = clamp(
            currentDepth - baseBias - sampleReceiverBias,
            0.0,
            1.0
        );
        float blockerDepth = textureLod(shadowRawDepthSampler, sampleUv, 0.0).r;
        if (sampleComparisonDepth > blockerDepth + 0.000001) {
            blockerDepthSum += blockerDepth;
            ++blockerCount;
        }
    }
    if (blockerCount == 0) {
        return false;
    }

    float averageBlockerDepth = blockerDepthSum / float(blockerCount);
    float receiverDepth = clamp(currentDepth - baseBias, 0.0, 1.0);
    float blockerSeparationWorld = max(
        receiverDepth - averageBlockerDepth,
        0.0
    ) * depthWorldSpan;
    float penumbraWorld = blockerSeparationWorld * tan(lightAngularRadius) * strength;
    float filterRadiusTexels = clamp(
        2.0 + penumbraWorld / texelWorldSize,
        2.0,
        maxPenumbraTexels
    );
    pcssBlend = smoothstep(
        2.0,
        min(3.0, maxPenumbraTexels),
        filterRadiusTexels
    ) * receiverConfidence;

    float visibilitySum = 0.0;
    for (int sampleIndex = 0;
        sampleIndex < MAX_DIRECTIONAL_SHADOW_POISSON_SAMPLES;
        ++sampleIndex) {
        if (sampleIndex >= filterSampleCount) {
            break;
        }
        vec2 sampleOffset = RotateDirectionalShadowPoissonOffset(
            DirectionalShadowPoissonOffset(sampleIndex),
            poissonAngle
        );
        vec2 sampleUv = ClampShadowAtlasSampleUv(
            atlasUv + sampleOffset * texelSize * filterRadiusTexels,
            cascadeIndex,
            texelSize
        );
        vec2 cascadeOffset = sampleOffset * cascadeTexelSize * filterRadiusTexels;
        float sampleReceiverBias = receiverPlaneBiasScale * dot(
            abs(receiverPlaneGradient),
            max(abs(cascadeOffset), cascadeTexelSize * 0.5)
        );
        float sampleComparisonDepth = clamp(
            currentDepth - baseBias - sampleReceiverBias,
            0.0,
            1.0
        );
        visibilitySum += texture(
            shadowSampler,
            vec3(sampleUv, sampleComparisonDepth)
        );
    }
    pcssVisibility = visibilitySum / float(filterSampleCount);
    return true;
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
    vec3 shadowReceiverPosition = DirectionalShadowReceiverPosition(
        worldPosition,
        normal,
        lightDir,
        cascadeIndex
    );
    if (ProjectShadowCascade(
            shadowReceiverPosition,
            cascadeIndex,
            shadowUv,
            currentDepth) != 0) {
        return false;
    }

    bool singleShadowMap = shadowCascades.cascadeInfo.w < -0.5;
    vec2 tileOrigin = singleShadowMap
        ? vec2(0.0)
        : vec2(float(cascadeIndex % 2), float(cascadeIndex / 2)) * 0.5;
    vec2 tileMax = singleShadowMap ? vec2(1.0) : tileOrigin + vec2(0.5);
    vec2 atlasUv = singleShadowMap ? shadowUv : tileOrigin + shadowUv * 0.5;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowSampler, 0));
    float bias = max(
        max(frame.shadowControls.w, 0.0) * (1.0 - dot(normal, lightDir)),
        max(frame.shadowControls.z, 0.0)
    );
    vec2 shadowUvDx = dFdx(shadowUv);
    vec2 shadowUvDy = dFdy(shadowUv);
    float depthDx = dFdx(currentDepth);
    float depthDy = dFdy(currentDepth);
    float determinant = shadowUvDx.x * shadowUvDy.y - shadowUvDx.y * shadowUvDy.x;
    vec2 receiverPlaneGradient = vec2(0.0);
    if (abs(determinant) > 0.0000001) {
        receiverPlaneGradient = vec2(
            (shadowUvDy.y * depthDx - shadowUvDx.y * depthDy) / determinant,
            (shadowUvDx.x * depthDy - shadowUvDy.x * depthDx) / determinant
        );
    }
    float cascadeUvPerAtlasUv = singleShadowMap ? 1.0 : 2.0;
    vec2 cascadeTexelSize = texelSize * cascadeUvPerAtlasUv;
    int filterMode = clamp(int(shadowCascades.directionalFilterControls.x + 0.5), 0, 1);
    int kernelWidth = clamp(int(shadowCascades.directionalFilterControls.z + 0.5), 3, 5);
    kernelWidth = kernelWidth >= 5 ? 5 : 3;
    float receiverBiasExtent = clamp(shadowCascades.directionalFilterControls.w, 0.0, 4.0);
    float receiverPlaneBiasScale = clamp(
        shadowCascades.receiverPlaneBiasControls.x,
        0.0,
        4.0
    );
    float receiverPlaneBias = receiverPlaneBiasScale * dot(
        abs(receiverPlaneGradient),
        cascadeTexelSize * receiverBiasExtent
    );
    float comparisonDepth = clamp(currentDepth - bias - receiverPlaneBias, 0.0, 1.0);
    float pcssVisibility = 1.0;
    float pcssBlend = 0.0;
    bool pcssResolved = SampleDirectionalPcssVisibility(
        atlasUv,
        texelSize,
        cascadeTexelSize,
        currentDepth,
        bias,
        receiverPlaneGradient,
        receiverPlaneBiasScale,
        clamp(dot(normal, lightDir), 0.0, 1.0),
        cascadeIndex,
        pcssVisibility,
        pcssBlend
    );
    float filteredVisibility = 0.0;

    if (filterMode == 1 && kernelWidth == 5) {
        // The optimized offsets are relative to the lower texel center, not
        // the continuous lookup coordinate. Aligning here preserves the
        // symmetric 1-3-4-3-1 tent footprint at texel centers.
        vec2 texelPosition = atlasUv / texelSize - vec2(0.5);
        vec2 baseTexel = floor(texelPosition);
        vec2 subTexel = fract(texelPosition);
        vec2 baseAtlasUv = (baseTexel + vec2(0.5)) * texelSize;
        float uw[3] = float[](4.0 - 3.0 * subTexel.x, 7.0, 1.0 + 3.0 * subTexel.x);
        float vw[3] = float[](4.0 - 3.0 * subTexel.y, 7.0, 1.0 + 3.0 * subTexel.y);
        float u[3] = float[](
            (3.0 - 2.0 * subTexel.x) / uw[0] - 2.0,
            (3.0 + subTexel.x) / uw[1],
            subTexel.x / uw[2] + 2.0
        );
        float v[3] = float[](
            (3.0 - 2.0 * subTexel.y) / vw[0] - 2.0,
            (3.0 + subTexel.y) / vw[1],
            subTexel.y / vw[2] + 2.0
        );
        for (int x = 0; x < 3; ++x) {
            for (int y = 0; y < 3; ++y) {
                vec2 sampleUv = clamp(
                    baseAtlasUv + vec2(u[x], v[y]) * texelSize,
                    tileOrigin + texelSize,
                    tileMax - texelSize
                );
                filteredVisibility += texture(
                    shadowSampler,
                    vec3(sampleUv, comparisonDepth)
                ) * uw[x] * vw[y];
            }
        }
        filteredVisibility /= 144.0;
    } else {
        float weightSum = 0.0;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                float weight = filterMode == 1
                    ? float(2 - abs(x)) * float(2 - abs(y))
                    : 1.0;
                vec2 sampleUv = clamp(
                    atlasUv + vec2(x, y) * texelSize,
                    tileOrigin + texelSize,
                    tileMax - texelSize
                );
                filteredVisibility += texture(
                    shadowSampler,
                    vec3(sampleUv, comparisonDepth)
                ) * weight;
                weightSum += weight;
            }
        }
        filteredVisibility /= max(weightSum, 1.0);
    }

    if (pcssResolved) {
        filteredVisibility = mix(filteredVisibility, pcssVisibility, pcssBlend);
    }

    visibility = mix(
        1.0 - clamp(frame.shadowControls.y, 0.0, 1.0),
        1.0,
        filteredVisibility
    );
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
        float shadowVisibility = LocalShadowVisibility(
            localLightIndex,
            localLight,
            worldPosition,
            normal,
            localLightDir
        );
        vec3 radiance =
            max(colorIntensity.rgb, vec3(0.0)) *
            colorIntensity.w *
            attenuation *
            shadowVisibility *
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
            0.0,
            specularColorFactor,
            0.0,
            1.0,
            sampleSpecular
        );
        accumulatedSpecular += sampleSpecular;
    }

    vec3 mirrorDir = reflect(-viewDir, normal);
    float planeDenom = dot(mirrorDir, rectNormal);
    if (abs(planeDenom) > 0.0001) {
        float hitDistance = dot(positionRadius.xyz - worldPosition, rectNormal) / planeDenom;
        if (hitDistance > 0.0 && hitDistance < radius) {
            vec3 hitPosition = worldPosition + mirrorDir * hitDistance;
            vec3 hitOffset = hitPosition - positionRadius.xyz;
            vec2 hitLocal = vec2(dot(hitOffset, rectTangent), dot(hitOffset, rectBitangent));
            vec2 normalizedHit = abs(hitLocal) / halfSize;
            float outsideDistance = length(max(normalizedHit - vec2(1.0), vec2(0.0)));
            float rectMask =
                1.0 - smoothstep(0.0, mix(0.035, 0.72, clamp(roughness, 0.0, 1.0)), outsideDistance);
            float facing = max(dot(normalize(worldPosition - hitPosition), rectNormal), 0.0);
            float nDotL = max(dot(normal, mirrorDir), 0.0);
            if (rectMask > 0.001 && facing > 0.001 && nDotL > 0.001) {
                float normalizedDistance = clamp(hitDistance / radius, 0.0, 1.0);
                float attenuation = pow(1.0 - normalizedDistance * normalizedDistance, 2.0);
                float shadowVisibility = LocalShadowVisibility(
                    localLightIndex,
                    localLight,
                    worldPosition,
                    normal,
                    mirrorDir
                );
                vec3 dielectricF0 = clamp(vec3(0.04) * specularColorFactor, vec3(0.0), vec3(1.0));
                vec3 f0 = mix(dielectricF0, baseColor, metallic);
                vec3 fresnel = FresnelSchlick(max(dot(normal, viewDir), 0.0), f0);
                vec3 coatFresnel =
                    FresnelSchlick(max(dot(normal, viewDir), 0.0), vec3(0.04)) *
                    clamp(clearcoat, 0.0, 1.0) *
                    mix(1.0, 0.25, clamp(clearcoatRoughness, 0.0, 1.0));
                float roughnessEnergy = mix(
                    0.95,
                    0.22,
                    smoothstep(0.05, 0.85, clamp(roughness, 0.0, 1.0))
                );
                vec3 rectSpecular =
                    max(colorIntensity.rgb, vec3(0.0)) *
                    colorIntensity.w *
                    attenuation *
                    facing *
                    nDotL *
                    shadowVisibility *
                    rectMask *
                    max(specularStrength, 0.0) *
                    roughnessEnergy *
                    (fresnel + coatFresnel);
                accumulatedDirect += rectSpecular;
                accumulatedSpecular += rectSpecular;
                influence = max(influence, 1.0);
            }
        }
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

float LocalLightDebugCoverage(
    LocalLightRecord localLight,
    vec3 worldPosition,
    vec3 normal,
    out vec3 lightDir
) {
    lightDir = DirectionToLocalLight(localLight, worldPosition);
    vec4 positionRadius = localLight.positionRadius;
    vec4 colorIntensity = localLight.colorIntensity;
    if (colorIntensity.w <= 0.0) {
        return 0.0;
    }

    uint lightKind = uint(clamp(int(localLight.directionType.w + 0.5), 0, 3));
    if (lightKind == 2u) {
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

        float bestCoverage = 0.0;
        vec3 weightedLightDir = vec3(0.0);
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

            vec3 sampleLightDir = toLight / max(distanceToLight, 0.001);
            float normalizedDistance = clamp(distanceToLight / radius, 0.0, 1.0);
            float attenuation = pow(1.0 - normalizedDistance * normalizedDistance, 2.0);
            float emitterFacing = max(dot(normalize(worldPosition - samplePosition), rectNormal), 0.0);
            float receiverFacing = max(dot(normal, sampleLightDir), 0.0);
            float coverage = attenuation * emitterFacing * receiverFacing;
            bestCoverage = max(bestCoverage, coverage);
            weightedLightDir += sampleLightDir * coverage;
        }

        if (dot(weightedLightDir, weightedLightDir) > 0.000001) {
            lightDir = normalize(weightedLightDir);
        }
        return clamp(bestCoverage, 0.0, 1.0);
    }

    vec3 toLight = positionRadius.xyz - worldPosition;
    float distanceToLight = length(toLight);
    float radius = max(positionRadius.w, 0.001);
    if (distanceToLight >= radius) {
        return 0.0;
    }

    lightDir = toLight / max(distanceToLight, 0.001);
    float normalizedDistance = clamp(distanceToLight / radius, 0.0, 1.0);
    float coverage = pow(1.0 - normalizedDistance * normalizedDistance, 2.0);
    if (lightKind == 1u) {
        vec3 spotDirection = localLight.directionType.xyz;
        if (dot(spotDirection, spotDirection) < 0.0001) {
            spotDirection = vec3(0.0, -1.0, 0.0);
        }
        spotDirection = normalize(spotDirection);
        float innerCone = max(localLight.parameters.x, localLight.parameters.y);
        float outerCone = min(localLight.parameters.x, localLight.parameters.y);
        float coneWidth = max(innerCone - outerCone, 0.0001);
        float coneCos = dot(normalize(worldPosition - positionRadius.xyz), spotDirection);
        coverage *= clamp((coneCos - outerCone) / coneWidth, 0.0, 1.0);
    }

    coverage *= max(dot(normal, lightDir), 0.0);
    return clamp(coverage, 0.0, 1.0);
}

bool LocalLightHasActiveShadowTile(int localLightIndex) {
    int assignedTileCount = clamp(int(localShadows.atlasInfo.x), 0, MAX_LOCAL_SHADOW_TILES);
    if (assignedTileCount <= 0 || localLightIndex < 0) {
        return false;
    }

    for (int tileIndex = 0; tileIndex < MAX_LOCAL_SHADOW_TILES; ++tileIndex) {
        if (tileIndex >= assignedTileCount) {
            break;
        }

        LocalShadowTileRecord tile = localShadows.tiles[tileIndex];
        if (int(tile.tileInfo.y) == localLightIndex) {
            return true;
        }
    }

    return false;
}

vec3 LocalLightDebugTint(LocalLightRecord localLight) {
    vec3 tint = max(localLight.colorIntensity.rgb, vec3(0.04));
    float peak = max(max(tint.r, tint.g), tint.b);
    return tint / max(peak, 0.04);
}

bool SelectedLocalShadowDebugColor(
    vec3 worldPosition,
    vec3 normal,
    out vec3 debugColor
) {
    int localLightCount = clamp(int(lights.lightCounts.z + 0.5), 0, MAX_LOCAL_LIGHTS);
    if (localLightCount <= 0) {
        debugColor = vec3(0.08, 0.02, 0.05);
        return false;
    }

    int requestedLightIndex =
        frame.debugControls.x < -0.5 ? -1 : int(floor(frame.debugControls.x + 0.5));
    if (requestedLightIndex >= localLightCount) {
        debugColor = vec3(0.26, 0.02, 0.34);
        return false;
    }

    int selectedLightIndex = requestedLightIndex;
    float selectedCoverage = 0.0;
    vec3 selectedLightDir = vec3(0.0, 1.0, 0.0);
    if (selectedLightIndex < 0) {
        float bestWeightedCoverage = 0.0;
        for (int localLightIndex = 0; localLightIndex < MAX_LOCAL_LIGHTS; ++localLightIndex) {
            if (localLightIndex >= localLightCount) {
                break;
            }

            vec3 candidateLightDir = vec3(0.0);
            LocalLightRecord candidateLight = lights.localLights[localLightIndex];
            float coverage = LocalLightDebugCoverage(
                candidateLight,
                worldPosition,
                normal,
                candidateLightDir
            );
            float weightedCoverage =
                coverage *
                max(candidateLight.colorIntensity.w, 0.0) *
                max(max(candidateLight.colorIntensity.r, candidateLight.colorIntensity.g),
                    candidateLight.colorIntensity.b);
            if (weightedCoverage > bestWeightedCoverage) {
                bestWeightedCoverage = weightedCoverage;
                selectedCoverage = coverage;
                selectedLightIndex = localLightIndex;
                selectedLightDir = candidateLightDir;
            }
        }
    } else {
        LocalLightRecord selectedLight = lights.localLights[selectedLightIndex];
        selectedCoverage = LocalLightDebugCoverage(
            selectedLight,
            worldPosition,
            normal,
            selectedLightDir
        );
    }

    const vec3 noInfluence = vec3(0.015, 0.035, 0.085);
    if (selectedLightIndex < 0 || selectedCoverage <= 0.001) {
        debugColor = noInfluence;
        return true;
    }

    LocalLightRecord selectedLight = lights.localLights[selectedLightIndex];
    bool hasShadowTile = LocalLightHasActiveShadowTile(selectedLightIndex);
    if (!hasShadowTile) {
        vec3 noTile = vec3(0.08, 0.62, 0.86);
        debugColor = mix(noInfluence, noTile, smoothstep(0.0, 0.65, selectedCoverage));
        return true;
    }

    float visibility = clamp(LocalShadowVisibility(
        selectedLightIndex,
        selectedLight,
        worldPosition,
        normal,
        selectedLightDir
    ), 0.0, 1.0);
    vec3 shadowed = vec3(0.32, 0.035, 0.018);
    vec3 partial = vec3(0.96, 0.56, 0.12);
    vec3 lit = mix(vec3(0.88, 0.94, 1.0), LocalLightDebugTint(selectedLight), 0.42);
    vec3 visibilityColor = mix(shadowed, partial, smoothstep(0.0, 0.72, visibility));
    visibilityColor = mix(visibilityColor, lit, smoothstep(0.55, 1.0, visibility));
    float coverageWeight = clamp(0.22 + selectedCoverage * 0.78, 0.0, 1.0);
    debugColor = mix(noInfluence, visibilityColor, coverageWeight);
    return true;
}

void main() {
    vec4 albedo = texture(gBufferAlbedo, fragUv);
    vec4 normalRoughness = texture(gBufferNormalRoughness, fragUv);
    vec4 material = texture(gBufferMaterial, fragUv);
    vec4 emissive = texture(gBufferEmissive, fragUv);
    vec2 materialAux = texture(gBufferMaterialAux, fragUv).rg;
    float depth = texture(sceneDepth, fragUv).r;
    int deferredDebugView = int(objectData.materialControls.w + 0.5);

    if (depth >= 0.999999 || albedo.a <= 0.001) {
        vec3 background = deferredDebugView == 0
            ? VisibleSkyboxRadiance(fragUv)
            : vec3(0.0);
        outColor = vec4(background, 1.0);
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
        ? clamp(materialAux.x, 0.0, 1.0)
        : (hasMaterialRecord ? clamp(materialRecord.emissiveFactor.w, 0.0, 1.0) : 0.0);
    float transmission = hasTransmissionTexture
        ? clamp(materialAux.y, 0.0, 1.0)
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
    vec3 environmentReflection =
        EnvironmentRadiance(reflection, lightDir, roughness, worldPosition);
    SsrTraceResult ssrTrace;
    ssrTrace.hitUv = fragUv;
    ssrTrace.confidence = 0.0;
    ssrTrace.validRay = 0.0;
    ssrTrace.travel = 0.0;
    ssrTrace.facing = 0.0;
    ssrTrace.refinementUsed = 0.0;
    ssrTrace.hitFacing = 0.0;
    ssrTrace.footprintConfidence = 0.0;
    ssrTrace.receiverFootprintConfidence = 0.0;
    ssrTrace.depthConfidence = 0.0;
    ssrTrace.validationConfidence = 0.0;
    vec3 ssrReflection = environmentReflection;
    if (SsrReconstructionEnabled()) {
        vec2 ssrHistoryUv = fragUv;
        float receiverHistoryConfidence = 1.0;
        if (SsrDeferredReceiverReprojectionEnabled()) {
            vec2 velocity = texture(gBufferVelocity, fragUv).rg;
            ssrHistoryUv = fragUv - velocity;
            vec3 previousReceiverViewPosition =
                (frame.previousView * vec4(worldPosition, 1.0)).xyz;
            receiverHistoryConfidence =
                frame.temporalControls.x > 0.5 &&
                frame.temporalControls.y > 0.5
                ? SsrDeferredReceiverHistoryConfidence(
                    ssrHistoryUv,
                    normal,
                    roughness,
                    abs(previousReceiverViewPosition.z)
                )
                : 0.0;
        }
        vec4 resolvedSsr = texture(ssrResolvedReflection, ssrHistoryUv);
        float resolvedConfidence = clamp(resolvedSsr.a, 0.0, 1.0) *
            receiverHistoryConfidence;
        ssrReflection = mix(
            environmentReflection,
            max(resolvedSsr.rgb, vec3(0.0)),
            resolvedConfidence
        );
        ssrTrace.validRay = step(0.0001, resolvedConfidence);
        ssrTrace.confidence = resolvedConfidence;
        ssrTrace.validationConfidence = receiverHistoryConfidence;
    } else {
        ssrTrace = TraceScreenSpaceReflection(
            fragUv,
            depth,
            normal,
            roughness
        );
        ssrReflection = ScreenSpaceReflectionRadiance(
            ssrTrace,
            environmentReflection,
            roughness,
            lightDir,
            ambientStrength,
            directIntensity
        );
    }
    IblAmbientResult iblAmbient = BuildIblAmbient(
        baseColor,
        roughness,
        metallic,
        normal,
        viewDir,
        lightDir,
        worldPosition,
        f0,
        occlusion,
        ReflectionProbeIndependentIblEnergyEnabled()
            ? 1.0
            : 0.45 * ambientStrength,
        ReflectionProbeIndependentIblEnergyEnabled()
            ? 1.0
            : 0.36 * ambientStrength,
        ssrReflection
    );
    vec3 ambientDiffuse = iblAmbient.diffuse;
    vec3 ambientSpecular = iblAmbient.specular;
    vec3 ambient = ambientDiffuse + ambientSpecular;
    float ambientShadowStrength = clamp(frame.shadowFiltering.y, 0.0, 1.0);
    float ambientVisibility = mix(1.0 - ambientShadowStrength, 1.0, shadowVisibility);
    ambient *= ambientVisibility;
    ambientDiffuse *= ambientVisibility;
    ambientSpecular *= ambientVisibility;
    float ssaoVisibility = SsaoVisibility(fragUv, depth, normal);
    ambient *= ssaoVisibility;
    ambientDiffuse *= ssaoVisibility;
    ambientSpecular *= ssaoVisibility;
    vec3 ambientProbe =
        SampleProbeGridIrradiance(worldPosition, normal) *
        baseColor *
        occlusion *
        (1.0 - metallic);
    ambient += ambientProbe;
    vec3 ssrDebugColor = ScreenSpaceReflectionDebug(ssrTrace, roughness);
    if (transmission > 0.001) {
        vec3 volumeTransmittance = hasMaterialRecord ? VolumeTransmittance(materialRecord) : vec3(1.0);
        vec3 transmittedEnv =
            EnvironmentRadiance(-normal, lightDir, roughness, worldPosition) *
            baseColor *
            volumeTransmittance *
            ambientStrength *
            occlusion *
            0.32;
        direct *= mix(1.0, 0.78, transmission);
        ambient = mix(ambient, ambient * 0.88 + transmittedEnv, transmission);
    }

    if (deferredDebugView == 22) {
        float averageLocalShadowVisibility = localShadowVisibilityCount > 0.0
            ? clamp(localShadowVisibilitySum / localShadowVisibilityCount, 0.0, 1.0)
            : 1.0;
        vec3 directDiffuse = max(direct - directSpecular, vec3(0.0));
        outColor = vec4(
            LightingEnergyBalanceDebugColor(
                directDiffuse,
                directSpecular,
                ambientDiffuse,
                ambientSpecular,
                ambientProbe,
                emissiveColor,
                shadowVisibility,
                averageLocalShadowVisibility,
                ssaoVisibility
            ),
            1.0
        );
        return;
    }
    if (deferredDebugView == 1) {
        outColor = vec4(direct, 1.0);
        return;
    }
    if (deferredDebugView == 2) {
        outColor = vec4(ambient, 1.0);
        return;
    }
    if (deferredDebugView == 18) {
        outColor = vec4(AmbientComponentDebugColor(ambientDiffuse, 24.0), 1.0);
        return;
    }
    if (deferredDebugView == 19) {
        outColor = vec4(AmbientComponentDebugColor(ambientSpecular, 64.0), 1.0);
        return;
    }
    if (deferredDebugView == 20) {
        outColor = vec4(AmbientComponentDebugColor(ambientProbe, 32.0), 1.0);
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
            ? clamp(materialAux.x, 0.0, 1.0)
            : clamp(materialRecord.emissiveFactor.w, 0.0, 1.0);
        float tableTransmission = transmissionTextured
            ? clamp(materialAux.y, 0.0, 1.0)
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
    if (deferredDebugView == 21) {
        vec3 selectedShadowColor = vec3(0.0);
        if (!SelectedLocalShadowDebugColor(worldPosition, normal, selectedShadowColor)) {
            outColor = vec4(selectedShadowColor, 1.0);
            return;
        }

        outColor = vec4(selectedShadowColor, 1.0);
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
    if (deferredDebugView == 11) {
        vec3 occluded = vec3(0.03, 0.055, 0.08);
        vec3 mid = vec3(0.55, 0.72, 0.82);
        vec3 open = vec3(0.96, 0.98, 1.0);
        vec3 ssaoColor = mix(occluded, mid, smoothstep(0.0, 0.75, ssaoVisibility));
        ssaoColor = mix(ssaoColor, open, smoothstep(0.55, 1.0, ssaoVisibility));
        outColor = vec4(ssaoColor, 1.0);
        return;
    }
    if (deferredDebugView == 12) {
        outColor = vec4(ssrDebugColor + vec3(ssrTrace.confidence) * 0.08, 1.0);
        return;
    }
    if (deferredDebugView == 13) {
        outColor = vec4(
            ReflectionProbeDebugColor(normal, reflection, lightDir, roughness, worldPosition),
            1.0
        );
        return;
    }
    if (deferredDebugView == 17) {
        outColor = vec4(
            ReflectionProbeContrastDebugColor(
                normal,
                reflection,
                lightDir,
                roughness,
                worldPosition
            ),
            1.0
        );
        return;
    }
    if (deferredDebugView == 23) {
        EnvironmentRadianceResult probeRadiance = ResolveEnvironmentRadiance(
            reflection,
            lightDir,
            roughness,
            worldPosition
        );
        vec3 displayRadiance = probeRadiance.localRadiance /
            (probeRadiance.localRadiance + vec3(1.0));
        outColor = vec4(
            mix(
                vec3(0.08, 0.01, 0.04),
                displayRadiance,
                probeRadiance.localCoverage
            ),
            1.0
        );
        return;
    }
    if (deferredDebugView == 14) {
        float fogFactor = HeightFogFactor(worldPosition, cameraPosition);
        vec3 thin = vec3(0.02, 0.05, 0.08);
        vec3 mid = mix(vec3(0.42, 0.60, 0.72), HeightFogColor(), 0.35);
        vec3 fogDebug = mix(thin, mid, smoothstep(0.0, 0.55, fogFactor));
        fogDebug = mix(fogDebug, HeightFogColor(), smoothstep(0.45, 1.0, fogFactor));
        outColor = vec4(fogDebug + vec3(fogFactor) * 0.08, 1.0);
        return;
    }
    if (deferredDebugView == 15) {
        outColor = vec4(
            ProbeGridContributionDebugColor(
                worldPosition,
                normal,
                baseColor,
                occlusion,
                metallic
            ),
            1.0
        );
        return;
    }
    if (deferredDebugView == 16) {
        outColor = vec4(ProbeGridCellDebugColor(worldPosition), 1.0);
        return;
    }

    vec3 finalColor = ApplyHeightFog(
        ambient + direct + emissiveColor,
        worldPosition,
        cameraPosition
    );
    outColor = vec4(finalColor, 1.0);
}
