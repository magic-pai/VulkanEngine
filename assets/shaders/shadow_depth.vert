#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;

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
    vec4 heightFogControls;
    vec4 heightFogColor;
    vec4 postProcessControls;
} frame;

layout(push_constant) uniform ShadowPushConstants {
    mat4 model;
    mat4 lightViewProjection;
} shadowData;

void main() {
    gl_Position = shadowData.lightViewProjection *
        shadowData.model *
        vec4(inPosition, 1.0);
}
