#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) out vec4 outColor;

void main() {
    if (fragUv.x < 0.0) {
        outColor = fragColor;
    } else {
        float cov = texture(fontAtlas, fragUv).r;
        outColor = vec4(fragColor.rgb, fragColor.a * cov);
    }
}
