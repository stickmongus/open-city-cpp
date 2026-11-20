#version 450

layout(location = 0) in vec3 fragDir;

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

layout(location = 0) out vec4 outColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbmClouds(vec2 p) {
    float v = 0.0;
    float a = 0.52;
    mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
    for (int i = 0; i < 4; ++i) {
        v += a * valueNoise(p);
        p = rot * p * 2.05 + vec2(11.3, 5.7);
        a *= 0.50;
    }
    return v;
}

void main() {
    vec3 dir = normalize(fragDir);
    vec3 S = normalize(-ubo.lightDirAndShadows.xyz);
    float dayFactor = ubo.horizonColorAndDay.w;
    float timeHours = ubo.viewPosAndHour.w;
    float animTime = ubo.sunColorAndTime.w;
    float wetWeather = ubo.groundAmbientAndWet.w;

    float h = clamp(dir.y, 0.0, 1.0);
    float sunsetBand = clamp(1.0 - abs(timeHours - 18.0) * 0.55, 0.0, 1.0)
                     + clamp(1.0 - abs(timeHours - 6.5) * 0.60, 0.0, 1.0);
    sunsetBand = clamp(sunsetBand, 0.0, 1.0);
    float night = 1.0 - clamp(dayFactor * 1.45, 0.0, 1.0);

    vec3 zenithDay   = vec3(0.08, 0.28, 0.72);
    vec3 horizDay    = vec3(0.56, 0.74, 0.92);
    vec3 zenithSun   = vec3(0.14, 0.14, 0.36);
    vec3 horizSun    = vec3(1.05, 0.44, 0.22);
    vec3 zenithNight = vec3(0.008, 0.014, 0.038);
    vec3 horizNight  = vec3(0.032, 0.052, 0.105);

    vec3 zenithCol = mix(zenithNight, mix(zenithDay, zenithSun, sunsetBand * 0.78), dayFactor);
    vec3 horizCol  = mix(horizNight,  mix(horizDay,  horizSun,  sunsetBand * 0.92), dayFactor);

    vec3 stormZenith = mix(vec3(0.015, 0.022, 0.036), vec3(0.16, 0.20, 0.26), dayFactor);
    vec3 stormHoriz  = mix(vec3(0.04, 0.055, 0.085), vec3(0.38, 0.43, 0.49), dayFactor);
    zenithCol = mix(zenithCol, stormZenith, wetWeather * 0.78);
    horizCol  = mix(horizCol,  stormHoriz,  wetWeather * 0.78);

    vec3 sky = mix(horizCol, zenithCol, pow(h, 0.52));

    // Mie forward scattering solar/lunar halo
    float sunDot = max(dot(dir, S), 0.0);
    vec3 mieCol = mix(vec3(0.45, 0.62, 1.0), mix(vec3(1.15, 0.96, 0.72), vec3(1.45, 0.56, 0.20), sunsetBand), dayFactor);
    sky += mieCol * pow(sunDot, 10.0) * (0.28 + 0.55 * sunsetBand) * (1.0 - wetWeather * 0.65);

    if (S.y > -0.05 && wetWeather < 0.75) {
        float sunDisc = smoothstep(0.99920, 0.99975, dot(dir, S));
        float corona  = pow(sunDot, 180.0) * 1.15;
        vec3 discCol  = mix(vec3(0.90, 0.94, 1.0), vec3(2.8, 2.2, 1.4), dayFactor);
        sky += discCol * (sunDisc * 2.2 + corona) * (1.0 - wetWeather * 0.9);
    }

    // Subtle twinkling stars at night
    if (night > 0.15 && dir.y > 0.05 && wetWeather < 0.7) {
        vec2 starUv = dir.xz / (dir.y + 0.35) * 95.0;
        vec2 cell = floor(starUv);
        vec2 f = fract(starUv) - 0.5;
        float rnd = hash12(cell);
        if (rnd > 0.968) {
            float d = length(f);
            float twinkle = 0.65 + 0.35 * sin(animTime * 2.7 + rnd * 91.0);
            float star = (1.0 - smoothstep(0.02, 0.22, d)) * twinkle * night * smoothstep(0.04, 0.28, dir.y);
            sky += vec3(0.75, 0.86, 1.0) * star * 1.3 * (1.0 - wetWeather * 0.85);
        }
    }

    // Layered volumetric-style FBM clouds
    if (dir.y > 0.02) {
        vec2 cloudUv = dir.xz / (dir.y + 0.16) * 0.92 + vec2(animTime * 0.0085, animTime * 0.0042);
        float n1 = fbmClouds(cloudUv);
        float n2 = fbmClouds(cloudUv * 2.1 + vec2(4.2, -3.1));
        float lowThresh = mix(0.46, 0.26, wetWeather);
        float density = smoothstep(lowThresh, mix(0.80, 0.66, wetWeather), n1 * 0.72 + n2 * 0.28);
        float horizonFade = smoothstep(0.015, 0.20, dir.y);
        density *= horizonFade * mix(0.84, 0.96, wetWeather);

        float detailLight = 0.50 + 0.50 * (n1 - n2) * 1.6 + pow(sunDot, 5.0) * 0.55;
        vec3 cloudLit = mix(vec3(0.11, 0.14, 0.22),
                            mix(vec3(1.06, 1.04, 1.00), vec3(1.32, 0.68, 0.38), sunsetBand), dayFactor);
        vec3 cloudShade = mix(vec3(0.03, 0.04, 0.07),
                              mix(vec3(0.46, 0.52, 0.64), vec3(0.36, 0.25, 0.34), sunsetBand), dayFactor);
        if (wetWeather > 0.1) {
            cloudLit   = mix(cloudLit,   vec3(0.34, 0.38, 0.44), wetWeather * 0.72);
            cloudShade = mix(cloudShade, vec3(0.15, 0.17, 0.21), wetWeather * 0.78);
            cloudLit   = mix(cloudLit,   vec3(0.06, 0.08, 0.12), night);
            cloudShade = mix(cloudShade, vec3(0.02, 0.03, 0.05), night);
        }
        vec3 cloudCol = mix(cloudShade, cloudLit, clamp(detailLight, 0.0, 1.0));
        sky = mix(sky, cloudCol, density * (0.80 + 0.16 * wetWeather));
    }

    // Distant Seoul Bukhansan / Gwanaksan mountain ring silhouette on horizon
    float azimuth = atan(dir.z, dir.x);
    float mtnHeight = 0.018 + 0.024 * sin(azimuth * 2.0 + 1.1)
                    + 0.014 * sin(azimuth * 5.0 - 0.8)
                    + 0.007 * sin(azimuth * 13.0);
    if (dir.y > -0.04 && dir.y < mtnHeight) {
        float ridge = 1.0 - smoothstep(mtnHeight - 0.004, mtnHeight, dir.y);
        vec3 mtnCol = mix(horizCol * 0.46, zenithCol * 0.52, 0.36);
        sky = mix(sky, mtnCol, ridge * 0.76);
    }

    if (dir.y < 0.0) {
        sky = mix(sky, horizCol * 0.46, clamp(-dir.y * 6.0, 0.0, 1.0));
    }

    // Output linear HDR directly into our 16-bit HDR scene framebuffer!
    outColor = vec4(sky, 1.0);
}
