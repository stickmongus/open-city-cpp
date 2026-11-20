#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform UiPush {
    vec4 screenScale; // x = 2/W, y = 2/H
} pc;

layout(location = 0) out vec2 fragUv;
layout(location = 1) out vec4 fragColor;

void main() {
    fragUv = inUv;
    fragColor = inColor;
    // Pixel (0,0) top-left -> Vulkan NDC (-1,-1) top-left, (+1,+1) bottom-right
    vec2 ndc = vec2(inPos.x * pc.screenScale.x - 1.0,
                    inPos.y * pc.screenScale.y - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
