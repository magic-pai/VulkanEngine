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
} frame;

layout(set = 0, binding = 10) readonly buffer AutoExposureState {
    vec4 exposure;
    uvec4 histogram[16];
} autoExposure;

layout(set = 1, binding = 0) uniform sampler2D sourceTexture;

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

vec3 BloomPrefilter(vec3 color, float threshold) {
    float brightness = max(max(color.r, color.g), color.b);
    float contribution = max(brightness - threshold, 0.0) / max(brightness, 0.0001);
    return color * contribution;
}

float EffectiveExposure() {
    float manualExposure = max(frame.toneMappingControls.y, 0.001);
    float autoEnabled = clamp(frame.toneMappingControls.w, 0.0, 1.0);
    if (autoEnabled <= 0.0001) {
        return manualExposure;
    }
    if (autoExposure.exposure.w > 0.5) {
        return max(autoExposure.exposure.x, 0.001);
    }
    return manualExposure;
}

void main() {
    vec2 texelSize = 1.0 / vec2(max(textureSize(sourceTexture, 0), ivec2(1)));
    vec3 color = texture(sourceTexture, fragUv).rgb * 0.36;
    color += texture(sourceTexture, fragUv + vec2(texelSize.x, 0.0)).rgb * 0.16;
    color += texture(sourceTexture, fragUv - vec2(texelSize.x, 0.0)).rgb * 0.16;
    color += texture(sourceTexture, fragUv + vec2(0.0, texelSize.y)).rgb * 0.16;
    color += texture(sourceTexture, fragUv - vec2(0.0, texelSize.y)).rgb * 0.16;

    int mipIndex = int(objectData.materialControls.x + 0.5);
    vec3 bloom = max(color, vec3(0.0));
    if (mipIndex == 0) {
        float threshold = max(frame.postProcessControls.z, 0.0);
        bloom = BloomPrefilter(bloom * EffectiveExposure(), threshold);
    }
    outColor = vec4(bloom, 1.0);
}
