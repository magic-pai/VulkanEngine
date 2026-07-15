#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec4 inInstanceModel0;
layout(location = 6) in vec4 inInstanceModel1;
layout(location = 7) in vec4 inInstanceModel2;
layout(location = 8) in vec4 inInstanceModel3;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out vec4 fragTangent;
layout(location = 5) out vec4 fragLightSpacePosition;

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

void main() {
    mat4 instanceModel = mat4(
        inInstanceModel0,
        inInstanceModel1,
        inInstanceModel2,
        inInstanceModel3
    );
    mat3 modelLinear = mat3(instanceModel);
    mat3 normalMatrix = mat3(transpose(inverse(instanceModel)));
    vec4 worldPosition = instanceModel * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inTexCoord;
    fragWorldPosition = worldPosition.xyz;
    fragTangent = vec4(normalize(modelLinear * inTangent.xyz), inTangent.w);
    fragLightSpacePosition = frame.lightViewProj * worldPosition;
    gl_Position = frame.proj * frame.view * worldPosition;
}
