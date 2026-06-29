#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D weightedTranslucencyAccum;
layout(set = 1, binding = 2) uniform sampler2D weightedTranslucencyRevealage;

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

void main() {
    vec4 accum = texture(weightedTranslucencyAccum, fragUv);
    float revealage = clamp(texture(weightedTranslucencyRevealage, fragUv).r, 0.0, 1.0);
    int debugView = int(objectData.materialControls.w + 0.5);

    vec3 translucentColor = vec3(0.0);
    if (accum.a > 0.00001) {
        translucentColor = accum.rgb / accum.a;
    }

    float coverage = 1.0 - revealage;
    if (debugView == 1) {
        vec3 accumDebug = accum.rgb / max(accum.a, 1.0);
        outColor = vec4(clamp(accumDebug, vec3(0.0), vec3(1.0)), 1.0);
        return;
    }
    if (debugView == 2) {
        outColor = vec4(vec3(revealage), 1.0);
        return;
    }
    if (debugView == 3) {
        float weight = clamp(accum.a / max(coverage, 0.0001), 0.0, 1.0);
        outColor = vec4(coverage, weight, revealage, 1.0);
        return;
    }

    outColor = vec4(translucentColor, coverage);
}
