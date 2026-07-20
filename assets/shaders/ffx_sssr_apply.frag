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
layout(set = 1, binding = 0) uniform sampler2D gBufferAlbedo;
layout(set = 1, binding = 1) uniform sampler2D gBufferNormalRoughness;
layout(set = 1, binding = 2) uniform sampler2D gBufferMaterial;
layout(set = 1, binding = 4) uniform sampler2D sceneDepth;
layout(set = 1, binding = 5) uniform sampler2D gBufferEmissive;
layout(set = 1, binding = 7) uniform sampler2D gBufferMaterialAux;
layout(set = 1, binding = 17) uniform sampler2D ffxSssrCurrentRadiance;

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

float IblSpecularStability(float roughness) {
    float r = clamp(roughness, 0.0, 1.0);
    return mix(0.48, 1.0, smoothstep(0.18, 0.72, r));
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
    float blendWeight = SsrProbeFallbackBlendWeight(
        clamp(resolved.a, 0.0, 1.0) * clamp(frame.ssrControls.x, 0.0, 1.0),
        roughness
    );
    float ambientStrength = max(lights.ambientLight.x, 0.08);
    float specularScale = floor(frame.reflectionProbeBlendControls.w + 0.5) > 1.5
        ? 1.0
        : 0.36 * ambientStrength;
    vec3 contribution = max(resolved.rgb, vec3(0.0)) *
        envSpecularBrdf *
        max(specularScale, 0.0) *
        IblSpecularStability(roughness) *
        occlusion *
        blendWeight;
    outColor = vec4(contribution, 1.0);
}
