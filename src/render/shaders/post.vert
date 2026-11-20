#version 450

layout(location = 0) out vec2 fragUv;

void main() {
    // Full-screen triangle (3 vertices, no vertex buffer required!)
    vec2 pos = vec2((gl_VertexIndex == 2) ? 3.0 : -1.0,
                    (gl_VertexIndex == 1) ? 3.0 : -1.0);
    fragUv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
