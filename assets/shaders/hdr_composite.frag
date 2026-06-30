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
    vec4 heightFogControls;
    vec4 heightFogColor;
    vec4 postProcessControls;
    vec4 colorGradingControls;
    vec4 toneMappingControls;
} frame;

layout(set = 1, binding = 0) uniform sampler2D hdrSceneColor;

const int DEBUG_VIEW_BLOOM = 37;
const int DEBUG_VIEW_COLOR_GRADING = 38;
const int DEBUG_VIEW_TONE_MAPPING = 39;

vec3 ToneMapAces(vec3 value) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

vec3 ToneMapReinhard(vec3 value, float whitePoint) {
    float whitePointSquared = max(whitePoint * whitePoint, 0.0001);
    vec3 mapped = value * (vec3(1.0) + value / whitePointSquared) / (vec3(1.0) + value);
    return clamp(mapped, 0.0, 1.0);
}

vec3 ToneMapHdr(vec3 value) {
    int mode = int(frame.toneMappingControls.x + 0.5);
    float whitePoint = max(frame.toneMappingControls.z, 0.001);
    if (mode == 1) {
        return ToneMapReinhard(value, whitePoint);
    }
    if (mode == 2) {
        return clamp(value, 0.0, 1.0);
    }
    return ToneMapAces(value);
}

vec3 BloomPrefilter(vec3 color, float threshold) {
    float brightness = max(max(color.r, color.g), color.b);
    float contribution = max(brightness - threshold, 0.0) / max(brightness, 0.0001);
    return color * contribution;
}

vec3 BloomContribution(float exposure) {
    float enabled = clamp(frame.postProcessControls.x, 0.0, 1.0);
    float intensity = clamp(frame.postProcessControls.y, 0.0, 4.0);
    float threshold = max(frame.postProcessControls.z, 0.0);
    float radiusPixels = clamp(frame.postProcessControls.w, 0.0, 24.0);
    if (enabled <= 0.0001 || intensity <= 0.0001 || radiusPixels <= 0.0001) {
        return vec3(0.0);
    }

    vec2 texelSize = 1.0 / vec2(max(textureSize(hdrSceneColor, 0), ivec2(1)));
    vec2 radius = texelSize * radiusPixels;
    vec3 bloom = BloomPrefilter(texture(hdrSceneColor, fragUv).rgb * exposure, threshold) * 0.24;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv + vec2(radius.x, 0.0)).rgb * exposure, threshold) * 0.12;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv - vec2(radius.x, 0.0)).rgb * exposure, threshold) * 0.12;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv + vec2(0.0, radius.y)).rgb * exposure, threshold) * 0.12;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv - vec2(0.0, radius.y)).rgb * exposure, threshold) * 0.12;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv + radius).rgb * exposure, threshold) * 0.07;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv - radius).rgb * exposure, threshold) * 0.07;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv + vec2(radius.x, -radius.y)).rgb * exposure, threshold) * 0.07;
    bloom += BloomPrefilter(texture(hdrSceneColor, fragUv + vec2(-radius.x, radius.y)).rgb * exposure, threshold) * 0.07;
    return bloom * intensity;
}

vec3 ApplyColorGrading(vec3 color) {
    float enabled = clamp(frame.colorGradingControls.x, 0.0, 1.0);
    if (enabled <= 0.0001) {
        return color;
    }

    float saturation = clamp(frame.colorGradingControls.y, 0.0, 2.5);
    float contrast = clamp(frame.colorGradingControls.z, 0.0, 2.5);
    float gamma = clamp(frame.colorGradingControls.w, 0.25, 4.0);

    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 graded = mix(vec3(luminance), color, saturation);
    graded = (graded - vec3(0.5)) * contrast + vec3(0.5);
    graded = clamp(graded, 0.0, 1.0);
    graded = pow(graded, vec3(1.0 / gamma));
    return clamp(graded, 0.0, 1.0);
}

void main() {
    float exposure = max(frame.toneMappingControls.y, 0.001);
    vec3 hdrColor = texture(hdrSceneColor, fragUv).rgb * exposure;
    vec3 bloom = BloomContribution(exposure);
    int debugView = int(frame.shadowFiltering.z + 0.5);
    if (debugView == DEBUG_VIEW_BLOOM) {
        outColor = vec4(ToneMapHdr(bloom), 1.0);
        return;
    }
    hdrColor += bloom;
    vec3 ldrColor = ToneMapHdr(hdrColor);
    if (debugView == DEBUG_VIEW_TONE_MAPPING) {
        outColor = vec4(ldrColor, 1.0);
        return;
    }
    vec3 gradedColor = ApplyColorGrading(ldrColor);
    if (debugView == DEBUG_VIEW_COLOR_GRADING) {
        outColor = vec4(gradedColor, 1.0);
        return;
    }
    outColor = vec4(gradedColor, 1.0);
}
