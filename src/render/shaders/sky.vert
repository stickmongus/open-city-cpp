#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 viewProj;
    mat4 lightVP;
    vec4 lightDirAndShadows;
    vec4 sunColorAndTime;
    vec4 skyAmbientAndNight;
    vec4 groundAmbientAndWet;
    vec4 fogColorAndTexel;
    vec4 horizonColorAndDay;
    vec4 viewPosAndHour;
} ubo;

layout(push_constant) uniform DrawPush {
    mat4 model;
    vec4 tint;
    vec4 pbrParams;
} pc;

layout(location = 0) out vec3 fragDir;

void main() {
    fragDir = inPos;
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position = ubo.viewProj * vec4(worldPos.xyz - ubo.viewPosAndHour.xyz, 1.0);
}
