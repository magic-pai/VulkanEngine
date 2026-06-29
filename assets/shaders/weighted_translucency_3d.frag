#version 450

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
    vec4 tint;
    vec4 materialBaseColorFactor;
    vec4 materialControls;
    vec4 materialCustom;
    vec4 viewport;
    vec4 cameraControls;
    vec4 cameraPosition;
    vec4 cameraDirection;
} objectData;

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

layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 1) uniform sampler2D materialAuxSampler;
layout(set = 1, binding = 5) uniform sampler2D emissiveSampler;
layout(set = 1, binding = 7) uniform sampler2D opacitySampler;

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

void main() {
    float textureMix = clamp(objectData.materialControls.x, 0.0, 1.0);
    MaterialRecord materialRecord;
    bool hasMaterialRecord = TryGetMaterialRecord(objectData.materialControls.w, materialRecord);
    vec2 materialUv = TransformMaterialUv(fragTexCoord, materialRecord);

    vec4 sampledBaseColor = texture(texSampler, materialUv);
    vec3 baseColor = mix(fragColor, sampledBaseColor.rgb, textureMix);
    baseColor *= objectData.materialBaseColorFactor.rgb;
    float alpha = clamp(
        objectData.materialBaseColorFactor.a * mix(1.0, sampledBaseColor.a, textureMix),
        0.0,
        1.0
    );

    float textureFlags = hasMaterialRecord ? materialRecord.materialFlags.y : 0.0;
    if (HasTextureFlag(textureFlags, 64.0)) {
        alpha *= clamp(texture(opacitySampler, materialUv).r, 0.0, 1.0);
    }

    vec3 emissive = vec3(0.0);
    if (hasMaterialRecord) {
        emissive += max(materialRecord.emissiveFactor.rgb, vec3(0.0)) *
            max(materialRecord.emissiveFactor.a, 0.0);
    }
    if (HasTextureFlag(textureFlags, 32.0)) {
        emissive += texture(emissiveSampler, materialUv).rgb;
    }

    if (alpha <= 0.001) {
        discard;
    }

    float viewDistance = length(objectData.cameraPosition.xyz - fragWorldPosition);
    float depthWeight = clamp(1.0 / (1.0 + viewDistance * 0.015), 0.02, 1.0);
    float alphaWeight = clamp(alpha * 8.0 + 0.01, 0.01, 1.0);
    float weight = depthWeight * alphaWeight;
    vec3 premultipliedColor = (baseColor + emissive) * alpha;

    outAccum = vec4(premultipliedColor * weight, alpha * weight);
    outRevealage = clamp(1.0 - alpha, 0.0, 1.0);
}
