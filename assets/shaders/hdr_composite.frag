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
} frame;

layout(set = 1, binding = 0) uniform sampler2D hdrSceneColor;

vec3 ToneMapAces(vec3 value) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

void main() {
    float exposure = max(frame.shadowFiltering.w, 0.001);
    vec3 hdrColor = texture(hdrSceneColor, fragUv).rgb * exposure;
    outColor = vec4(ToneMapAces(hdrColor), 1.0);
}
