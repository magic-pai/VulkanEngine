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
} frame;

layout(set = 1, binding = 0) uniform sampler2D hdrSceneColor;
layout(set = 1, binding = 3) uniform sampler2D temporalHistoryColor;
layout(set = 1, binding = 4) uniform sampler2D gBufferVelocity;
layout(set = 1, binding = 5) uniform sampler2D sceneDepth;

void main() {
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
    bool historyRejected = false;
    if (rejectionEnabled > 0.5) {
        historyRejected =
            length(reprojectionVelocity) > velocityRejectThreshold ||
            abs(currentDepth - historyDepth) > depthRejectThreshold;
    }

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

    outColor = vec4(resolvedHdrColor, 1.0);
}
