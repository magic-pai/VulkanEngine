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
    vec4 reflectionProbeDiffuseLobes[24];
} frame;

layout(set = 1, binding = 2) uniform sampler2D lowerBloomMip;

void main() {
    float radiusPixels = clamp(frame.postProcessControls.w, 0.0, 24.0);
    vec2 texelSize = 1.0 / vec2(max(textureSize(lowerBloomMip, 0), ivec2(1)));
    vec2 radius = texelSize * max(radiusPixels * 0.25, 0.5);

    vec3 bloom = texture(lowerBloomMip, fragUv).rgb * 0.40;
    bloom += texture(lowerBloomMip, fragUv + vec2(radius.x, 0.0)).rgb * 0.15;
    bloom += texture(lowerBloomMip, fragUv - vec2(radius.x, 0.0)).rgb * 0.15;
    bloom += texture(lowerBloomMip, fragUv + vec2(0.0, radius.y)).rgb * 0.15;
    bloom += texture(lowerBloomMip, fragUv - vec2(0.0, radius.y)).rgb * 0.15;

    outColor = vec4(max(bloom, vec3(0.0)), 1.0);
}
