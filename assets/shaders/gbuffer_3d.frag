#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec4 fragTangent;
layout(location = 5) in vec4 fragCurrentClip;
layout(location = 6) in vec4 fragPreviousClip;
layout(location = 7) in float fragBonePaletteDiagnostic;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec2 outVelocity;
layout(location = 5) out vec2 outMaterialAux;

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
layout(set = 1, binding = 1) uniform sampler2D materialAuxSampler;
layout(set = 1, binding = 3) uniform sampler2D normalSampler;
layout(set = 1, binding = 4) uniform sampler2D occlusionSampler;
layout(set = 1, binding = 5) uniform sampler2D emissiveSampler;
layout(set = 1, binding = 7) uniform sampler2D opacitySampler;
layout(set = 1, binding = 8) uniform sampler2D specularSampler;
layout(set = 1, binding = 9) uniform sampler2D clearcoatSampler;
layout(set = 1, binding = 10) uniform sampler2D transmissionSampler;
layout(set = 1, binding = 11) uniform sampler2D clearcoatRoughnessSampler;

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

void main() {
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

    vec3 normal = normalize(fragNormal);
    if (hasNormalTexture) {
        normal = ApplyNormalMap(normal, normalScale, materialUv);
    }

    float occlusionTexture = hasOcclusionTexture
        ? clamp(texture(occlusionSampler, materialUv).r, 0.0, 1.0)
        : 1.0;
    float occlusion = mix(1.0, occlusionTexture, occlusionStrength);
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
    float specularTextureFactor = 1.0;
    if (hasSpecularTexture) {
        vec3 specularTexture = clamp(texture(specularSampler, materialUv).rgb, vec3(0.0), vec3(1.0));
        specularTextureFactor = dot(specularTexture, vec3(0.2126, 0.7152, 0.0722));
    }

    float tintMix = clamp(objectData.tint.a, 0.0, 1.0);
    baseColor = mix(baseColor, objectData.tint.rgb, tintMix);

    outAlbedo = vec4(baseColor, materialAlpha);
    outNormalRoughness = vec4(normal * 0.5 + 0.5, roughness);
    outMaterial = vec4(metallic, occlusion, specularTextureFactor, max(objectData.materialControls.w, 0.0));
    outEmissive = vec4(emissive, clearcoat);
    vec2 currentNdc = fragCurrentClip.xy / max(abs(fragCurrentClip.w), 0.000001);
    vec2 previousNdc = fragPreviousClip.xy / max(abs(fragPreviousClip.w), 0.000001);
    vec2 currentUv = currentNdc * 0.5 + 0.5;
    vec2 previousUv = previousNdc * 0.5 + 0.5;
    outVelocity = currentUv - previousUv;
    float bonePaletteDiagnosticAux =
        clamp(fragBonePaletteDiagnostic, 0.0, 1.0) * 0.000001;
    outMaterialAux = vec2(
        max(hasClearcoatRoughnessTexture ? clearcoatRoughness : 0.0, bonePaletteDiagnosticAux),
        hasTransmissionTexture ? transmission : 0.0
    );
}
