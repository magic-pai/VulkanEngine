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
    mat4 previousView;
    mat4 previousProj;
    vec4 temporalJitter;
    vec4 temporalControls;
    vec4 temporalResolveControls;
} frame;

layout(set = 0, binding = 10) readonly buffer AutoExposureState {
    vec4 exposure;
    uvec4 histogram[16];
} autoExposure;

layout(set = 1, binding = 0) uniform sampler2D hdrSceneColor;
layout(set = 1, binding = 1) uniform sampler2D bloomTexture;
layout(set = 1, binding = 2) uniform sampler2D colorGradingLut;
layout(set = 1, binding = 3) uniform sampler2D temporalHistoryColor;
layout(set = 1, binding = 4) uniform sampler2D gBufferVelocity;

const int DEBUG_VIEW_BLOOM = 37;
const int DEBUG_VIEW_COLOR_GRADING = 38;
const int DEBUG_VIEW_TONE_MAPPING = 39;
const int DEBUG_VIEW_AUTO_EXPOSURE = 40;
const int DEBUG_VIEW_SHARPENING = 41;
const int DEBUG_VIEW_TAA = 44;

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

float SceneAverageLuminance() {
    vec2 texelSize = 1.0 / vec2(max(textureSize(hdrSceneColor, 0), ivec2(1)));
    float luminanceSum = 0.0;
    float sampleCount = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texelSize * 32.0;
            vec3 sampleColor = max(texture(hdrSceneColor, clamp(vec2(0.5) + offset, vec2(0.0), vec2(1.0))).rgb, vec3(0.0));
            luminanceSum += dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
            sampleCount += 1.0;
        }
    }
    return luminanceSum / max(sampleCount, 1.0);
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

    float targetLuminance = max(frame.autoExposureControls.x, 0.001);
    float minExposure = max(frame.autoExposureControls.y, 0.001);
    float maxExposure = max(frame.autoExposureControls.z, minExposure);
    float adaptation = clamp(frame.autoExposureControls.w, 0.0, 1.0);
    float measuredLuminance = max(SceneAverageLuminance(), 0.001);
    float adaptedExposure = clamp(targetLuminance / measuredLuminance, minExposure, maxExposure);
    return mix(manualExposure, adaptedExposure, adaptation);
}

vec3 BloomContribution() {
    float enabled = clamp(frame.postProcessControls.x, 0.0, 1.0);
    float intensity = clamp(frame.postProcessControls.y, 0.0, 4.0);
    if (enabled <= 0.0001 || intensity <= 0.0001) {
        return vec3(0.0);
    }
    return max(texture(bloomTexture, fragUv).rgb, vec3(0.0)) * intensity;
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

    float lutStrength = clamp(frame.colorGradingLutControls.x, 0.0, 1.0);
    float lutSize = max(frame.colorGradingLutControls.y, 2.0);
    if (lutStrength > 0.0001) {
        vec3 lutCoord = clamp(graded, vec3(0.0), vec3(1.0)) * (lutSize - 1.0);
        float blueSlice = floor(lutCoord.b);
        float blueBlend = fract(lutCoord.b);
        float stripWidth = lutSize * lutSize;
        vec2 lutUv0 = vec2(
            (blueSlice * lutSize + lutCoord.r + 0.5) / stripWidth,
            (lutCoord.g + 0.5) / lutSize
        );
        vec2 lutUv1 = vec2(
            (min(blueSlice + 1.0, lutSize - 1.0) * lutSize + lutCoord.r + 0.5) / stripWidth,
            lutUv0.y
        );
        vec3 lutColor = mix(
            texture(colorGradingLut, lutUv0).rgb,
            texture(colorGradingLut, lutUv1).rgb,
            blueBlend
        );
        graded = mix(graded, lutColor, lutStrength);
    }

    return clamp(graded, 0.0, 1.0);
}

vec3 CompositeLdrAt(vec2 uv, float exposure) {
    vec3 hdrColor = texture(hdrSceneColor, clamp(uv, vec2(0.0), vec2(1.0))).rgb * exposure;
    return ApplyColorGrading(ToneMapHdr(hdrColor));
}

vec3 ApplySharpening(vec3 color, float exposure, out vec3 sharpeningDelta) {
    float enabled = clamp(frame.sharpeningControls.x, 0.0, 1.0);
    float strength = clamp(frame.sharpeningControls.y, 0.0, 2.0);
    float radiusPixels = clamp(frame.sharpeningControls.z, 0.0, 4.0);
    sharpeningDelta = vec3(0.0);
    if (enabled <= 0.0001 || strength <= 0.0001 || radiusPixels <= 0.0001) {
        return color;
    }

    vec2 texelSize = 1.0 / vec2(max(textureSize(hdrSceneColor, 0), ivec2(1)));
    vec2 radius = texelSize * radiusPixels;
    vec3 neighborAverage =
        CompositeLdrAt(fragUv + vec2(radius.x, 0.0), exposure) +
        CompositeLdrAt(fragUv - vec2(radius.x, 0.0), exposure) +
        CompositeLdrAt(fragUv + vec2(0.0, radius.y), exposure) +
        CompositeLdrAt(fragUv - vec2(0.0, radius.y), exposure);
    neighborAverage *= 0.25;
    sharpeningDelta = (color - neighborAverage) * strength;
    return clamp(color + sharpeningDelta, 0.0, 1.0);
}

void main() {
    float exposure = EffectiveExposure();
    vec3 currentHdrColor = texture(hdrSceneColor, fragUv).rgb;
    vec2 velocity = texture(gBufferVelocity, fragUv).rg;
    vec2 historyUv = clamp(fragUv - velocity, vec2(0.0), vec2(1.0));
    vec3 historyHdrColor = texture(temporalHistoryColor, historyUv).rgb;
    float taaEnabled = clamp(frame.temporalResolveControls.x, 0.0, 1.0);
    float historyWeight = clamp(frame.temporalResolveControls.y, 0.0, 0.95);
    float historyReady = clamp(frame.temporalResolveControls.z, 0.0, 1.0);
    float reprojectionEnabled = clamp(frame.temporalResolveControls.w, 0.0, 1.0);
    vec3 resolvedHdrColor = currentHdrColor;
    if (taaEnabled > 0.5 && historyReady > 0.5 && reprojectionEnabled > 0.5) {
        resolvedHdrColor = mix(currentHdrColor, historyHdrColor, historyWeight);
    }
    vec3 hdrColor = resolvedHdrColor * exposure;
    vec3 bloom = BloomContribution();
    int debugView = int(frame.shadowFiltering.z + 0.5);
    if (debugView == DEBUG_VIEW_TAA) {
        vec3 delta = abs(currentHdrColor - historyHdrColor) * exposure;
        outColor = vec4(ToneMapHdr(delta), 1.0);
        return;
    }
    if (debugView == DEBUG_VIEW_BLOOM) {
        outColor = vec4(ToneMapHdr(bloom), 1.0);
        return;
    }
    hdrColor += bloom;
    vec3 ldrColor = ToneMapHdr(hdrColor);
    if (debugView == DEBUG_VIEW_AUTO_EXPOSURE) {
        float normalizedExposure = clamp(exposure / max(frame.autoExposureControls.z, 0.001), 0.0, 1.0);
        outColor = vec4(vec3(normalizedExposure), 1.0);
        return;
    }
    if (debugView == DEBUG_VIEW_TONE_MAPPING) {
        outColor = vec4(ldrColor, 1.0);
        return;
    }
    vec3 gradedColor = ApplyColorGrading(ldrColor);
    if (debugView == DEBUG_VIEW_COLOR_GRADING) {
        outColor = vec4(gradedColor, 1.0);
        return;
    }
    vec3 sharpeningDelta = vec3(0.0);
    vec3 sharpenedColor = ApplySharpening(gradedColor, exposure, sharpeningDelta);
    if (debugView == DEBUG_VIEW_SHARPENING) {
        outColor = vec4(clamp(abs(sharpeningDelta) * 4.0, 0.0, 1.0), 1.0);
        return;
    }
    outColor = vec4(sharpenedColor, 1.0);
}
