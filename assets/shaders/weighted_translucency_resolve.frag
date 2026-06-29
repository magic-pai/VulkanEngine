#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D weightedTranslucencyAccum;
layout(set = 1, binding = 2) uniform sampler2D weightedTranslucencyRevealage;

void main() {
    vec4 accum = texture(weightedTranslucencyAccum, fragUv);
    float revealage = clamp(texture(weightedTranslucencyRevealage, fragUv).r, 0.0, 1.0);

    vec3 translucentColor = vec3(0.0);
    if (accum.a > 0.00001) {
        translucentColor = accum.rgb / accum.a;
    }

    float coverage = 1.0 - revealage;
    outColor = vec4(translucentColor, coverage);
}
