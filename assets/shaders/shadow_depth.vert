#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in uvec4 inBoneIndices;
layout(location = 6) in vec4 inBoneWeights;

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

layout(push_constant) uniform ShadowPushConstants {
    mat4 model;
    mat4 lightViewProjection;
    vec4 skinningControls;
} shadowData;

layout(set = 2, binding = 0, std430) readonly buffer BonePaletteData {
    mat4 bonePalette[];
} bonePaletteData;

mat4 WeightedSkinMatrix(uint currentPaletteOffset, vec4 weights) {
    return
        bonePaletteData.bonePalette[currentPaletteOffset + inBoneIndices.x] * weights.x +
        bonePaletteData.bonePalette[currentPaletteOffset + inBoneIndices.y] * weights.y +
        bonePaletteData.bonePalette[currentPaletteOffset + inBoneIndices.z] * weights.z +
        bonePaletteData.bonePalette[currentPaletteOffset + inBoneIndices.w] * weights.w;
}

void main() {
    float boneWeightSum = dot(inBoneWeights, vec4(1.0));
    uint currentPaletteOffset = uint(max(shadowData.skinningControls.x, 0.0) + 0.5);
    bool skinningEnabled = boneWeightSum > 0.00001 && currentPaletteOffset > 0u;
    vec4 normalizedBoneWeights = skinningEnabled
        ? inBoneWeights / boneWeightSum
        : vec4(1.0, 0.0, 0.0, 0.0);
    mat4 skinMatrix = skinningEnabled
        ? WeightedSkinMatrix(currentPaletteOffset, normalizedBoneWeights)
        : mat4(1.0);
    vec4 localPosition = skinMatrix * vec4(inPosition, 1.0);
    gl_Position = shadowData.lightViewProjection *
        shadowData.model *
        localPosition;
}
