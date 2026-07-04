#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec4 fragTangent;
layout(location = 5) in vec4 fragCurrentClip;
layout(location = 6) in vec4 fragPreviousClip;

layout(location = 0) out vec2 outVelocity;

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

layout(set = 1, binding = 0) uniform sampler2D texSampler;
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
    float textureFlags = objectData.cameraControls.w;

    MaterialRecord materialRecord;
    bool hasMaterialRecord = TryGetMaterialRecord(
        objectData.materialControls.w,
        materialRecord
    );
    vec2 materialUv = TransformMaterialUv(fragTexCoord, materialRecord);

    vec4 sampledBaseColor = texture(texSampler, materialUv);
    float materialAlpha = clamp(
        objectData.materialBaseColorFactor.a * mix(1.0, sampledBaseColor.a, textureMix),
        0.0,
        1.0
    );
    vec4 pbrFactors = hasMaterialRecord ? materialRecord.pbrFactors : vec4(1.0, 1.0, 0.0, 0.0);
    float alphaMode = pbrFactors.z;
    float alphaCutoff = clamp(pbrFactors.w, 0.0, 1.0);
    if (HasTextureFlag(textureFlags, 64.0)) {
        materialAlpha *= clamp(texture(opacitySampler, materialUv).r, 0.0, 1.0);
    }
    if (alphaMode > 0.5 && alphaMode < 1.5 && materialAlpha < alphaCutoff) {
        discard;
    }
    if (materialAlpha <= 0.001) {
        discard;
    }

    vec2 currentNdc = fragCurrentClip.xy / max(abs(fragCurrentClip.w), 0.000001);
    vec2 previousNdc = fragPreviousClip.xy / max(abs(fragPreviousClip.w), 0.000001);
    outVelocity = currentNdc * 0.5 + 0.5 - (previousNdc * 0.5 + 0.5);
}
