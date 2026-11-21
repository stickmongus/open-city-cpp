#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;
layout(set = 0, binding = 1) uniform sampler2D mapTex;

layout(location = 0) out vec4 outColor;

void main() {
    if (fragUv.x < 0.0) {
        outColor = fragColor;
    } else if (fragUv.x >= 1.5) {
        vec4 m = texture(mapTex, vec2(fragUv.x - 2.0, fragUv.y));
        outColor = vec4(m.rgb * fragColor.rgb, m.a * fragColor.a);
    } else {
        float cov = texture(fontAtlas, fragUv).r;
        outColor = vec4(fragColor.rgb, fragColor.a * cov);
    }
}
