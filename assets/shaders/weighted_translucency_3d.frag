#version 450
#extension GL_EXT_control_flow_attributes : require

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec4 fragTangent;
layout(location = 5) in vec4 fragLightSpacePosition;

layout(location = 0) out vec4 outAccum;
layout(location = 1) out float outRevealage;

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

layout(set = 0, binding = 6) uniform sampler2D brdfLut;
layout(set = 0, binding = 7) uniform samplerCube irradianceMap;
layout(set = 0, binding = 8) uniform samplerCube prefilteredMap;
layout(std430, set = 0, binding = 9) readonly buffer ProbeGridData { vec4 probes[]; } probeGrid;
layout(set = 0, binding = 11) uniform samplerCube localReflectionProbeMaps[4];
layout(set = 0, binding = 13) uniform samplerCube localReflectionProbeDiffuseIrradianceMaps[4];

layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform sampler2D materialAuxSampler;
layout(set = 1, binding = 3) uniform sampler2D normalSampler;
layout(set = 1, binding = 4) uniform sampler2D occlusionSampler;
layout(set = 1, binding = 5) uniform sampler2D emissiveSampler;
layout(set = 1, binding = 6) uniform sampler2DShadow shadowSampler;
layout(set = 1, binding = 13) uniform sampler2D shadowRawDepthSampler;
layout(set = 1, binding = 7) uniform sampler2D opacitySampler;
layout(set = 1, binding = 8) uniform sampler2D specularSampler;
layout(set = 1, binding = 9) uniform sampler2D clearcoatSampler;
layout(set = 1, binding = 10) uniform sampler2D transmissionSampler;
layout(set = 1, binding = 11) uniform sampler2D clearcoatRoughnessSampler;
layout(set = 1, binding = 12) uniform sampler2DShadow localShadowComparisonSampler;
layout(set = 1, binding = 14) uniform sampler2D localShadowRawDepthSampler;

const float PI = 3.14159265359;
const int MAX_LOCAL_LIGHTS = 64;
const int MAX_LIGHT_TILES = 8192;
const int MAX_LIGHTS_PER_TILE = 16;
const int MAX_LIGHT_TILE_OVERFLOW_INDICES = 65536;
const int MAX_DIRECTIONAL_SHADOW_CASCADES = 4;
const int MAX_LOCAL_SHADOW_TILES = 64;
const int MAX_LOCAL_SHADOW_TILES_PER_LIGHT = 6;
const int MAX_REFLECTION_PROBES = 4;
const int REFLECTION_PROBE_DIFFUSE_LOBE_COUNT = 6;

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

void WriteWeightedOutput(vec3 color, float alpha) {
    float viewDistance = length(objectData.cameraPosition.xyz - fragWorldPosition);
    float depthWeight = clamp(1.0 / (1.0 + viewDistance * 0.015), 0.02, 1.0);
    float alphaWeight = clamp(alpha * 8.0 + 0.01, 0.01, 1.0);
    float weight = depthWeight * alphaWeight;
    vec3 premultipliedColor = max(color, vec3(0.0)) * alpha;

    outAccum = vec4(premultipliedColor * weight, alpha * weight);
    outRevealage = clamp(1.0 - alpha, 0.0, 1.0);
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
    float sunPower = mix(1024.0, 24.0, roughness);
    float sunDisk = pow(sunAmount, sunPower);
    vec3 sunTint = vec3(1.12, 1.08, 1.0);
    vec3 sun = sunTint * sunDisk * mix(5.0, 2.2, roughness);
    float intensity = mix(specularIntensity, diffuseIntensity, smoothstep(0.45, 1.0, roughness));
    vec3 procedural = (base + sun) * intensity;
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
    if (spacing <= 0.0 ||
        blendStrength <= 0.0001 ||
        gridSize.x < 2 ||
        gridSize.y < 2 ||
        gridSize.z < 2) {
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

bool ReflectionProbeDominantMirrorEnabled() {
    float mode = floor(frame.reflectionProbeBlendControls.w + 0.5);
    return mod(floor(mode / 4.0), 2.0) > 0.5;
}

bool ReflectionProbeDominantMirrorHardSwitchEnabled() {
    float mode = floor(frame.reflectionProbeBlendControls.w + 0.5);
    return mod(floor(mode / 8.0), 2.0) > 0.5;
}

bool ReflectionProbeObjectStableEnabled() {
    float mode = floor(frame.reflectionProbeBlendControls.w + 0.5);
    return mod(floor(mode / 16.0), 2.0) > 0.5;
}

float ReflectionProbeMirrorFactor(float roughness) {
    return 1.0 - smoothstep(0.30, 0.60, clamp(roughness, 0.0, 1.0));
}

int ObjectProbeAssignmentCode() {
    uint packedObjectMetadata = uint(max(objectData.viewport.w, 0.0) + 0.5);
    return int(packedObjectMetadata % 8u);
}

float ReflectionProbeDominantMirrorFactor(
    float roughness,
    float dominantWeight,
    float runnerUpWeight,
    float totalWeight
) {
    float roughnessFactor = ReflectionProbeMirrorFactor(roughness);
    if (ReflectionProbeDominantMirrorHardSwitchEnabled()) {
        return roughnessFactor;
    }
    float dominanceMargin = clamp(
        (dominantWeight - runnerUpWeight) / max(totalWeight, 0.000001),
        0.0,
        1.0
    );
    return roughnessFactor * smoothstep(0.02, 0.18, dominanceMargin);
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
    vec3 worldPosition,
    int objectProbeAssignmentCode
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
    int assignedProbeIndex = objectProbeAssignmentCode - 1;
    bool assignedProbeValid = assignedProbeIndex >= 0 &&
        assignedProbeIndex < probeCount;
    float objectStableFactor = productionBlend &&
        ReflectionProbeDominantMirrorEnabled() &&
        ReflectionProbeObjectStableEnabled()
        ? ReflectionProbeMirrorFactor(roughness)
        : 0.0;
    if (totalWeight <= 0.0001 &&
        !(assignedProbeValid && objectStableFactor > 0.0001)) {
        return result;
    }

    int dominantProbeIndex = -1;
    float dominantRawWeight = 0.0;
    float runnerUpRawWeight = 0.0;
    for (int probeIndex = 0; probeIndex < MAX_REFLECTION_PROBES; ++probeIndex) {
        if (probeIndex < probeCount && weights[probeIndex] > dominantRawWeight) {
            runnerUpRawWeight = dominantRawWeight;
            dominantRawWeight = weights[probeIndex];
            dominantProbeIndex = probeIndex;
        } else if (probeIndex < probeCount) {
            runnerUpRawWeight = max(runnerUpRawWeight, weights[probeIndex]);
        }
    }
    float dominantMirrorFactor = productionBlend &&
        ReflectionProbeDominantMirrorEnabled() &&
        !ReflectionProbeObjectStableEnabled() &&
        dominantProbeIndex >= 0
        ? ReflectionProbeDominantMirrorFactor(
            roughness,
            dominantRawWeight,
            runnerUpRawWeight,
            totalWeight
        )
        : 0.0;
    int resolvedMirrorProbeIndex = ReflectionProbeObjectStableEnabled()
        ? assignedProbeIndex
        : dominantProbeIndex;
    float resolvedMirrorFactor = ReflectionProbeObjectStableEnabled()
        ? objectStableFactor
        : dominantMirrorFactor;
    float localWeightDenominator = totalWeight > 0.0001
        ? totalWeight
        : 1.0;
    result.localCoverage = mix(
        result.localCoverage,
        assignedProbeValid ? 1.0 : 0.0,
        objectStableFactor
    );

    vec3 localBlend = vec3(0.0);
    float roughnessClamped = clamp(roughness, 0.0, 1.0);
    float glossBoost = IblSpecularStability(roughnessClamped);
    for (int probeIndex = 0; probeIndex < MAX_REFLECTION_PROBES; ++probeIndex) {
        float rawWeight = weights[probeIndex];
        bool stableAssignedProbe = objectStableFactor > 0.0001 &&
            assignedProbeValid && probeIndex == assignedProbeIndex;
        if (probeIndex >= probeCount ||
            (rawWeight <= 0.0001 && !stableAssignedProbe)) {
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
        float effectiveWeight = mix(
            rawWeight,
            probeIndex == resolvedMirrorProbeIndex
                ? localWeightDenominator
                : 0.0,
            resolvedMirrorFactor
        );
        if (effectiveWeight <= 0.0001) {
            continue;
        }
        localBlend += localRadiance *
            (effectiveWeight / localWeightDenominator);
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
        worldPosition,
        ObjectProbeAssignmentCode()
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
    float specularScale
) {
    IblAmbientResult result;
    float nDotV = max(dot(normal, viewDir), 0.0);
    vec3 reflection = reflect(-viewDir, normal);
    vec3 diffuseEnv = EnvironmentRadiance(normal, lightDir, 1.0, worldPosition);
    vec3 specularEnv =
        EnvironmentRadiance(reflection, lightDir, roughness, worldPosition);
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
        specularEnv *
        envSpecularBrdf *
        max(specularScale, 0.0) *
        IblSpecularStability(roughness) *
        occlusion;
    return result;
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

vec3 ApplyHeightFog(vec3 color, vec3 worldPosition, vec3 cameraPosition) {
    vec3 fogColor = max(frame.heightFogColor.rgb, vec3(0.0));
    return mix(color, fogColor, HeightFogFactor(worldPosition, cameraPosition));
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
    float localSpecularStrength =
        specularStrength * clamp(parameters.w, 0.0, 1.0);
    vec3 localDirect = DirectPbrContribution(
        baseColor,
        roughness,
        metallic,
        normal,
        viewDir,
        localLightDir,
        radiance,
        localSpecularStrength,
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
    float analyticSpecular = clamp(localLight.parameters.x, 0.0, 1.0);
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
                    analyticSpecular *
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
    if (shadowCascades.cascadeInfo.w < -0.5) {
        return vec2(0.0);
    }
    return vec2(float(cascadeIndex % 2), float(cascadeIndex / 2)) * 0.5;
}

vec2 ShadowAtlasUv(vec2 shadowUv, int cascadeIndex) {
    if (shadowCascades.cascadeInfo.w < -0.5) {
        return shadowUv;
    }
    return ShadowCascadeTileOrigin(cascadeIndex) + shadowUv * 0.5;
}

vec2 ClampShadowAtlasSampleUv(vec2 atlasUv, int cascadeIndex, vec2 texelSize) {
    if (shadowCascades.cascadeInfo.w < -0.5) {
        return clamp(atlasUv, texelSize, vec2(1.0) - texelSize);
    }
    vec2 tileMin = ShadowCascadeTileOrigin(cascadeIndex);
    vec2 tileMax = tileMin + vec2(0.5);
    return clamp(atlasUv, tileMin + texelSize, tileMax - texelSize);
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

vec3 ShadowCascadeDebugColor(vec3 worldPosition) {
    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(worldPosition, viewDepth);
    if (cascadeIndex < 0) {
        return vec3(1.0, 0.1, 0.1);
    }

    vec4 lightSpacePosition =
        shadowCascades.viewProjections[cascadeIndex] * vec4(worldPosition, 1.0);
    if (abs(lightSpacePosition.w) <= 0.000001) {
        return vec3(0.04);
    }

    vec3 projectionCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    vec2 shadowUv = projectionCoords.xy * 0.5 + 0.5;
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        projectionCoords.z < 0.0 || projectionCoords.z > 1.0) {
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

    vec2 atlasUv = ShadowAtlasUv(shadowUv, cascadeIndex);
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
    float cascadeUvPerAtlasUv =
        shadowCascades.cascadeInfo.w < -0.5 ? 1.0 : 2.0;
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
                vec2 blockerUv = ClampShadowAtlasSampleUv(
                    atlasUv + sampleOffset * texelSize * searchRadius,
                    cascadeIndex,
                    texelSize
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
                    vec2 blockerUv = ClampShadowAtlasSampleUv(
                        atlasUv + vec2(x, y) * texelSize * searchRadius,
                        cascadeIndex,
                        texelSize
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
            vec2 sampleUv = ClampShadowAtlasSampleUv(
                atlasUv + sampleOffset * texelSize * filterExtent,
                cascadeIndex,
                texelSize
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
                vec2 sampleUv = ClampShadowAtlasSampleUv(
                    atlasUv + vec2(x, y) * texelSize * filterRadius,
                    cascadeIndex,
                    texelSize
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

    vec2 atlasUv = ShadowAtlasUv(shadowUv, cascadeIndex);
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
    float cascadeUvPerAtlasUv = shadowCascades.cascadeInfo.w < -0.5 ? 1.0 : 2.0;
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
                vec2 sampleUv = ClampShadowAtlasSampleUv(
                    baseAtlasUv + vec2(u[x], v[y]) * texelSize,
                    cascadeIndex,
                    texelSize
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
                vec2 sampleUv = ClampShadowAtlasSampleUv(
                    atlasUv + vec2(x, y) * texelSize,
                    cascadeIndex,
                    texelSize
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

float ShadowVisibility(vec3 normal, vec3 lightDir) {
    if (frame.shadowControls.x < 0.5 ||
        frame.shadowControls.y <= 0.001 ||
        !DirectionalShadowReceiveEnabled()) {
        return 1.0;
    }

    float viewDepth = 0.0;
    int cascadeIndex = SelectShadowCascade(fragWorldPosition, viewDepth);
    if (cascadeIndex < 0) {
        return 1.0;
    }

    int activeCount = ActiveShadowCascadeCount();
    float visibility = 1.0;
    bool visibilityValid = ResolveShadowCascadeVisibility(
        fragWorldPosition,
        normal,
        lightDir,
        cascadeIndex,
        activeCount,
        visibility
    );
    if (!visibilityValid) {
        return 1.0;
    }
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

    float nextVisibility = visibility;
    if (SampleShadowCascadeVisibility(
        fragWorldPosition,
        normal,
        lightDir,
        cascadeIndex + 1,
        nextVisibility)) {
        visibility = mix(visibility, nextVisibility, blendFactor);
    }
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
    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = lights.lightDirectionalLight.xyz;
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = frame.directionalLight.xyz;
    }
    if (dot(lightDirection, lightDirection) < 0.0001) {
        lightDirection = vec3(-0.45, -0.82, -0.35);
    }

    vec3 lightDir = normalize(-lightDirection);
    vec3 viewDir = normalize(objectData.cameraPosition.xyz - fragWorldPosition);

    float ambientStrength = max(lights.lightAmbientLight.x, frame.ambientLight.x);
    float directIntensity = max(lights.lightDirectionalLight.w, frame.directionalLight.w);
    float specularStrength = max(lights.lightAmbientLight.y, frame.ambientLight.y);

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
    if (hasMaterialRecord) {
        metallic = clamp(materialRecord.cameraControls.x, 0.0, 1.0);
        roughness = clamp(materialRecord.cameraControls.y, 0.04, 1.0);
        auxTextureMix = clamp(materialRecord.cameraControls.z, 0.0, 1.0);
        textureFlags = materialRecord.materialFlags.y;
        auxTextureKind = mod(max(materialRecord.cameraControls.w, 0.0), 8.0);
        specularStrength = max(specularStrength, materialRecord.materialControls.z);
    }

    bool hasNormalTexture = HasTextureFlag(textureFlags, 8.0);
    bool hasOcclusionTexture = HasTextureFlag(textureFlags, 16.0);
    bool hasEmissiveTexture = HasTextureFlag(textureFlags, 32.0);
    bool hasOpacityTexture = HasTextureFlag(textureFlags, 64.0);
    bool hasSpecularTexture = HasTextureFlag(textureFlags, 128.0);
    bool hasClearcoatTexture = HasTextureFlag(textureFlags, 256.0);
    bool hasTransmissionTexture = HasTextureFlag(textureFlags, 512.0);
    bool hasClearcoatRoughnessTexture = HasTextureFlag(textureFlags, 1024.0);
    if (hasOpacityTexture && alphaMode > 0.5) {
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
    if (materialAlpha <= 0.001) {
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
    float nDotV = max(dot(normal, viewDir), 0.0);
    float shadowVisibility = ShadowVisibility(normal, lightDir);
    int debugView = ForwardDebugView();
    if (debugView == 25) {
        WriteWeightedOutput(
            clamp(ShadowCascadeDebugColor(fragWorldPosition) * DebugExposure(), 0.0, 1.0),
            materialAlpha
        );
        return;
    }

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

    vec3 dielectricF0 = clamp(vec3(0.04) * specularColorFactor, vec3(0.0), vec3(1.0));
    vec3 f0 = mix(dielectricF0, baseColor, metallic);
    float envStrength = ambientStrength * 4.0 + 0.08 + directIntensity * 0.06;
    IblAmbientResult iblAmbient = BuildIblAmbient(
        baseColor,
        roughness,
        metallic,
        normal,
        viewDir,
        lightDir,
        fragWorldPosition,
        f0,
        occlusion,
        ReflectionProbeIndependentIblEnergyEnabled() ? 1.0 : envStrength,
        ReflectionProbeIndependentIblEnergyEnabled()
            ? 1.0
            : envStrength * (0.55 + specularStrength)
    );
    vec3 ambient = iblAmbient.diffuse + iblAmbient.specular;
    float ambientShadowStrength = clamp(frame.shadowFiltering.y, 0.0, 1.0);
    ambient *= mix(1.0 - ambientShadowStrength, 1.0, shadowVisibility);
    ambient += SampleProbeGridIrradiance(fragWorldPosition, normal) *
        baseColor *
        occlusion *
        (1.0 - metallic);
    if (transmission > 0.001) {
        vec3 volumeTransmittance = hasMaterialRecord ? VolumeTransmittance(materialRecord) : vec3(1.0);
        vec3 transmittedEnv =
            EnvironmentRadiance(-normal, lightDir, roughness, fragWorldPosition) *
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
    litColor = ApplyHeightFog(
        litColor,
        fragWorldPosition,
        objectData.cameraPosition.xyz
    );
    float tintMix = clamp(objectData.tint.a, 0.0, 1.0);
    litColor = mix(litColor, objectData.tint.rgb, tintMix);

    WriteWeightedOutput(litColor, materialAlpha);
}
