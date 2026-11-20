#version 450

layout(location = 0) in vec2 fragUv;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform PostPush {
    vec4 params0; // x=invW, y=invH, z=bloomStrength, w=exposure
    vec4 params1; // x=nitroStrength, y=enableBloom, z=0, w=0
} pc;

layout(location = 0) out vec4 outColor;

vec3 extractBright(vec3 linearCol) {
    float peak = max(linearCol.r, max(linearCol.g, linearCol.b));
    float w = smoothstep(1.05, 1.65, peak);
    return linearCol * w;
}

vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = fragUv;
    vec2 texel = pc.params0.xy;
    float bloomStrength = pc.params0.z;
    float exposure = pc.params0.w;
    float nitroStrength = pc.params1.x;
    float enableBloom = pc.params1.y;

    vec2 centered = uv - vec2(0.5);
    float edgeDist = length(centered);

    // True linear-HDR scene sample (with subtle peripheral speed aberration during nitro)
    vec3 baseCol = texture(sceneTex, uv).rgb;
    if (nitroStrength > 0.02) {
        vec2 caOff = centered * (0.0042 * nitroStrength * edgeDist);
        baseCol.r = texture(sceneTex, clamp(uv + caOff, vec2(0.001), vec2(0.999))).r;
        baseCol.b = texture(sceneTex, clamp(uv - caOff, vec2(0.001), vec2(0.999))).b;
    }

    vec3 bloom = vec3(0.0);
    if (enableBloom > 0.5 && bloomStrength > 0.005) {
        vec2 o1 = texel * 2.5;
        vec2 o2 = texel * 5.8;
        bloom += extractBright(texture(sceneTex, uv + vec2( o1.x,  0.0 )).rgb) * 0.13;
        bloom += extractBright(texture(sceneTex, uv + vec2(-o1.x,  0.0 )).rgb) * 0.13;
        bloom += extractBright(texture(sceneTex, uv + vec2( 0.0,  o1.y )).rgb) * 0.13;
        bloom += extractBright(texture(sceneTex, uv + vec2( 0.0, -o1.y )).rgb) * 0.13;
        bloom += extractBright(texture(sceneTex, uv + vec2( o2.x,  o2.y)).rgb) * 0.08;
        bloom += extractBright(texture(sceneTex, uv + vec2(-o2.x,  o2.y)).rgb) * 0.08;
        bloom += extractBright(texture(sceneTex, uv + vec2( o2.x, -o2.y)).rgb) * 0.08;
        bloom += extractBright(texture(sceneTex, uv + vec2(-o2.x, -o2.y)).rgb) * 0.08;
        bloom += extractBright(texture(sceneTex, uv + vec2( o2.x * 1.6,  0.0)).rgb) * 0.04;
        bloom += extractBright(texture(sceneTex, uv + vec2(-o2.x * 1.6,  0.0)).rgb) * 0.04;
        bloom += extractBright(texture(sceneTex, uv + vec2( 0.0,  o2.y * 1.6)).rgb) * 0.04;
        bloom += extractBright(texture(sceneTex, uv + vec2( 0.0, -o2.y * 1.6)).rgb) * 0.04;
    }

    vec3 hdr = (baseCol + bloom * bloomStrength * 0.65) * exposure;
    vec3 mapped = acesFilmic(hdr);

    // Subtle filmic split-tone + micro-vignette
    float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped *= mix(vec3(0.985, 0.992, 1.015), vec3(1.015, 1.004, 0.985), smoothstep(0.15, 0.85, luma));
    float vignette = 1.0 - smoothstep(0.48, 0.98, edgeDist) * 0.10;
    mapped *= vignette;

    // Encode linear -> sRGB for UNORM swapchain presentation
    vec3 srgb = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
    outColor = vec4(srgb, 1.0);
}
