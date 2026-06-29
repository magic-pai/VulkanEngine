#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragTint;

layout(location = 0) out vec4 outColor;

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

layout(set = 1, binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 vertexColor = vec4(fragColor, 1.0);
    vec4 textureColor = texture(texSampler, fragTexCoord);
    float textureMix = clamp(objectData.materialControls.x, 0.0, 1.0);
    float tintMix = clamp(fragTint.a, 0.0, 1.0);

    if (tintMix > 0.0 && textureColor.a <= 0.05) {
        discard;
    }

    vec4 baseColor = mix(vertexColor, textureColor, textureMix) * objectData.materialBaseColorFactor;
    outColor = mix(baseColor, vec4(fragTint.rgb, textureColor.a), tintMix);
}
