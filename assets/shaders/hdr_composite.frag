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
    vec4 debugControls;
    vec4 reflectionProbeDiffuseLobes[24];
    mat4 previousView;
    mat4 previousProj;
    vec4 temporalJitter;
    vec4 temporalControls;
    vec4 temporalResolveControls;
    vec4 temporalRejectionControls;
    vec4 environmentControls;
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
layout(set = 1, binding = 5) uniform sampler2D sceneDepth;
layout(set = 0, binding = 11) uniform samplerCube localReflectionProbeMaps[4];
layout(set = 0, binding = 12) uniform sampler2D visibleSkyboxTexture;

const float PI = 3.14159265359;
const int DEBUG_VIEW_BLOOM = 37;
const int DEBUG_VIEW_COLOR_GRADING = 38;
const int DEBUG_VIEW_TONE_MAPPING = 39;
const int DEBUG_VIEW_AUTO_EXPOSURE = 40;
const int DEBUG_VIEW_SHARPENING = 41;
const int DEBUG_VIEW_TAA = 44;
const int DEBUG_VIEW_TAA_REJECTION = 45;
const int DEBUG_VIEW_TAA_HISTORY = 46;
const int DEBUG_VIEW_TAA_REPROJECTION = 47;

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

vec3 ViewRayWorldDirection(vec2 uv) {
    vec2 ndc = uv * 2.0 - 1.0;
    if (frame.temporalControls.z > 0.5) {
        ndc -= frame.temporalJitter.zw * 2.0;
    }

    vec4 clipPosition = vec4(ndc, 1.0, 1.0);
    vec4 viewPosition = frame.invProj * clipPosition;
    vec3 viewDirection = abs(viewPosition.w) > 0.000001
        ? viewPosition.xyz / viewPosition.w
        : viewPosition.xyz;
    if (dot(viewDirection, viewDirection) <= 0.000001) {
        viewDirection = vec3(0.0, 0.0, -1.0);
    }
    return normalize((frame.invView * vec4(normalize(viewDirection), 0.0)).xyz);
}

vec2 EquirectUv(vec3 direction) {
    vec3 d = dot(direction, direction) > 0.000001
        ? normalize(direction)
        : vec3(1.0, 0.0, 0.0);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return vec2(u, v);
}

vec3 VisibleSkyboxTextureRadiance(vec3 direction) {
    return max(textureLod(
        visibleSkyboxTexture,
        EquirectUv(direction),
        clamp(frame.environmentControls.z, 0.0, 8.0)
    ).rgb, vec3(0.0));
}

vec3 ProceduralSkyRadiance(vec3 direction) {
    float up = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyTop = vec3(0.37, 0.50, 0.72);
    vec3 skyHorizon = vec3(0.68, 0.71, 0.76);
    vec3 ground = vec3(0.09, 0.085, 0.08);
    vec3 base = mix(ground, skyTop, smoothstep(0.05, 1.0, up));
    return mix(base, skyHorizon, 1.0 - abs(direction.y));
}

vec3 PostTemporalSkyboxRadiance(vec2 uv) {
    if (frame.environmentControls.x <= 0.5 ||
        frame.environmentControls.w < 1.5) {
        return vec3(0.0);
    }

    vec3 direction = ViewRayWorldDirection(uv);
    vec3 skyRadiance = VisibleSkyboxTextureRadiance(direction);
    return skyRadiance * clamp(frame.environmentControls.y, 0.0, 4.0);
}

bool IsPureSkyNeighborhood(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(max(textureSize(sceneDepth, 0), ivec2(1)));
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUv = clamp(
                uv + vec2(float(x), float(y)) * texelSize,
                vec2(0.0),
                vec2(1.0)
            );
            if (texture(sceneDepth, sampleUv).r < 0.999999) {
                return false;
            }
        }
    }
    return true;
}

// The LUT is stored as a 2D strip; fetch texels manually to avoid filtering across blue slices.
vec3 FetchColorGradingLut(ivec3 coord, int lutSize) {
    coord = clamp(coord, ivec3(0), ivec3(lutSize - 1));
    return texelFetch(
        colorGradingLut,
        ivec2(coord.b * lutSize + coord.r, coord.g),
        0
    ).rgb;
}

vec3 SampleColorGradingLut(vec3 color, float lutSizeFloat) {
    int lutSize = max(int(lutSizeFloat + 0.5), 2);
    int maxIndex = lutSize - 1;
    vec3 lutCoord = clamp(color, vec3(0.0), vec3(1.0)) * float(maxIndex);
    ivec3 lo = ivec3(floor(lutCoord));
    ivec3 hi = min(lo + ivec3(1), ivec3(maxIndex));
    vec3 blend = fract(lutCoord);

    vec3 c000 = FetchColorGradingLut(ivec3(lo.r, lo.g, lo.b), lutSize);
    vec3 c100 = FetchColorGradingLut(ivec3(hi.r, lo.g, lo.b), lutSize);
    vec3 c010 = FetchColorGradingLut(ivec3(lo.r, hi.g, lo.b), lutSize);
    vec3 c110 = FetchColorGradingLut(ivec3(hi.r, hi.g, lo.b), lutSize);
    vec3 c001 = FetchColorGradingLut(ivec3(lo.r, lo.g, hi.b), lutSize);
    vec3 c101 = FetchColorGradingLut(ivec3(hi.r, lo.g, hi.b), lutSize);
    vec3 c011 = FetchColorGradingLut(ivec3(lo.r, hi.g, hi.b), lutSize);
    vec3 c111 = FetchColorGradingLut(ivec3(hi.r, hi.g, hi.b), lutSize);

    vec3 c00 = mix(c000, c100, blend.r);
    vec3 c10 = mix(c010, c110, blend.r);
    vec3 c01 = mix(c001, c101, blend.r);
    vec3 c11 = mix(c011, c111, blend.r);
    vec3 c0 = mix(c00, c10, blend.g);
    vec3 c1 = mix(c01, c11, blend.g);
    return mix(c0, c1, blend.b);
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
        vec3 lutColor = SampleColorGradingLut(graded, lutSize);
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
    vec2 reprojectionVelocity = velocity;
    if (frame.temporalControls.z > 0.5) {
        reprojectionVelocity -= frame.temporalJitter.zw;
    }
    vec2 historyUv = clamp(fragUv - reprojectionVelocity, vec2(0.0), vec2(1.0));
    vec3 historyHdrColor = texture(temporalHistoryColor, historyUv).rgb;
    float taaEnabled = clamp(frame.temporalResolveControls.x, 0.0, 1.0);
    float historyWeight = clamp(frame.temporalResolveControls.y, 0.0, 0.95);
    float historyReady = clamp(frame.temporalResolveControls.z, 0.0, 1.0);
    float reprojectionEnabled = clamp(frame.temporalResolveControls.w, 0.0, 1.0);
    float rejectionEnabled = clamp(frame.temporalRejectionControls.x, 0.0, 1.0);
    float velocityRejectThreshold = max(frame.temporalRejectionControls.y, 0.0);
    float depthRejectThreshold = max(frame.temporalRejectionControls.z, 0.0);
    float neighborhoodClampEnabled = clamp(frame.temporalRejectionControls.w, 0.0, 1.0);
    float currentDepth = texture(sceneDepth, fragUv).r;
    float historyDepth = texture(sceneDepth, historyUv).r;
    bool postTemporalSkyboxPixel =
        currentDepth >= 0.999999 &&
        frame.environmentControls.x > 0.5 &&
        frame.environmentControls.w >= 1.5 &&
        IsPureSkyNeighborhood(fragUv);
    if (postTemporalSkyboxPixel) {
        currentHdrColor = PostTemporalSkyboxRadiance(fragUv);
        historyHdrColor = currentHdrColor;
    }
    bool historyRejected = false;
    if (rejectionEnabled > 0.5) {
        historyRejected =
            length(reprojectionVelocity) > velocityRejectThreshold ||
            abs(currentDepth - historyDepth) > depthRejectThreshold;
    }
    historyRejected = historyRejected || postTemporalSkyboxPixel;
    if (neighborhoodClampEnabled > 0.5) {
        vec2 texelSize = 1.0 / vec2(max(textureSize(hdrSceneColor, 0), ivec2(1)));
        vec3 neighborhoodMin = vec3(65504.0);
        vec3 neighborhoodMax = vec3(0.0);
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec2 sampleUv = clamp(
                    fragUv + vec2(float(x), float(y)) * texelSize,
                    vec2(0.0),
                    vec2(1.0)
                );
                vec3 sampleColor = texture(hdrSceneColor, sampleUv).rgb;
                neighborhoodMin = min(neighborhoodMin, sampleColor);
                neighborhoodMax = max(neighborhoodMax, sampleColor);
            }
        }
        historyHdrColor = clamp(historyHdrColor, neighborhoodMin, neighborhoodMax);
    }
    vec3 resolvedHdrColor = currentHdrColor;
    if (taaEnabled > 0.5 &&
        historyReady > 0.5 &&
        reprojectionEnabled > 0.5 &&
        !historyRejected) {
        resolvedHdrColor = mix(currentHdrColor, historyHdrColor, historyWeight);
    }
    vec3 hdrColor = resolvedHdrColor * exposure;
    vec3 bloom = BloomContribution();
    int debugView = int(frame.shadowFiltering.z + 0.5);
    if (debugView == DEBUG_VIEW_TAA_HISTORY) {
        outColor = vec4(ToneMapHdr(historyHdrColor * exposure), 1.0);
        return;
    }
    if (debugView == DEBUG_VIEW_TAA_REPROJECTION) {
        float velocityMagnitude = clamp(length(reprojectionVelocity) * 24.0, 0.0, 1.0);
        outColor = vec4(historyUv, velocityMagnitude, 1.0);
        return;
    }
    if (debugView == DEBUG_VIEW_TAA_REJECTION) {
        if (taaEnabled <= 0.5 || historyReady <= 0.5) {
            outColor = vec4(0.0, 0.0, 0.25, 1.0);
        } else {
            outColor = historyRejected
                ? vec4(1.0, 0.05, 0.02, 1.0)
                : vec4(0.05, 0.85, 0.18, 1.0);
        }
        return;
    }
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
