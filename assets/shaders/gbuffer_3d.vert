#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in uvec4 inBoneIndices;
layout(location = 6) in vec4 inBoneWeights;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out vec4 fragTangent;
layout(location = 5) out vec4 fragCurrentClip;
layout(location = 6) out vec4 fragPreviousClip;
layout(location = 7) out float fragBonePaletteDiagnostic;

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
    vec4 reflectionProbeDiffuseLobes[24];
    mat4 previousView;
    mat4 previousProj;
    vec4 temporalJitter;
    vec4 temporalControls;
} frame;

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

layout(set = 2, binding = 0, std430) readonly buffer BonePaletteData {
    mat4 bonePalette[];
} bonePaletteData;

void main() {
    mat3 modelLinear = mat3(objectData.model);
    mat3 normalMatrix = mat3(transpose(inverse(objectData.model)));
    vec4 worldPosition = objectData.model * vec4(inPosition, 1.0);
    mat4 diagnosticBonePalette = bonePaletteData.bonePalette[0];
    float boneWeightDiagnostic = dot(inBoneWeights, vec4(1.0));
    float boneIndexDiagnostic = float(
        inBoneIndices.x +
        inBoneIndices.y +
        inBoneIndices.z +
        inBoneIndices.w
    ) * 0.000001;
    fragBonePaletteDiagnostic = clamp(
        abs(diagnosticBonePalette[0][0]) +
            boneWeightDiagnostic +
            boneIndexDiagnostic,
        0.0,
        1.0
    );
    fragColor = inColor;
    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inTexCoord;
    fragWorldPosition = worldPosition.xyz;
    fragTangent = vec4(normalize(modelLinear * inTangent.xyz), inTangent.w);
    gl_Position = frame.proj * frame.view * worldPosition;
    fragCurrentClip = gl_Position;
    vec4 previousWorldPosition = objectData.previousModel * vec4(inPosition, 1.0);
    fragPreviousClip = frame.temporalControls.x > 0.5
        ? frame.previousProj * frame.previousView * previousWorldPosition
        : gl_Position;
}
