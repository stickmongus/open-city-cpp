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
    for (int i = 0; i < 5; ++i) {
        v += a * valueNoise(p);
        p = rot * p * 2.03 + vec2(11.3, 5.7);
        a *= 0.49;
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
    float sunsetBand = clamp(1.0 - abs(timeHours - 18.0) * 0.52, 0.0, 1.0)
                     + clamp(1.0 - abs(timeHours - 6.5) * 0.58, 0.0, 1.0);
    sunsetBand = clamp(sunsetBand, 0.0, 1.0);
    float night = 1.0 - clamp(dayFactor * 1.45, 0.0, 1.0);

    // Rich Physical Rayleigh Sky Palette (vivid clear Seoul cobalt-azure daytime, dramatic sunset, velvet night)
    vec3 zenithDay   = vec3(0.038, 0.20, 0.66);
    vec3 midDay      = vec3(0.14,  0.42, 0.84);
    vec3 horizDay    = vec3(0.48,  0.71, 0.92);
    vec3 zenithSun   = vec3(0.09,  0.11, 0.34);
    vec3 horizSun    = vec3(1.18,  0.44, 0.18);
    vec3 zenithNight = vec3(0.005, 0.011, 0.032);
    vec3 horizNight  = vec3(0.024, 0.042, 0.088);

    vec3 dayGrad = mix(horizDay, midDay, pow(h, 0.38));
    dayGrad = mix(dayGrad, zenithDay, pow(h, 0.92));

    vec3 sunGrad = mix(horizSun, zenithSun, pow(h, 0.46));
    vec3 nightGrad = mix(horizNight, zenithNight, pow(h, 0.55));

    vec3 sky = mix(nightGrad, mix(dayGrad, sunGrad, sunsetBand * 0.88), dayFactor);

    // Overcast rain-storm sky modulation
    vec3 stormZenith = mix(vec3(0.012, 0.018, 0.030), vec3(0.15, 0.19, 0.25), dayFactor);
    vec3 stormHoriz  = mix(vec3(0.036, 0.048, 0.072), vec3(0.36, 0.42, 0.48), dayFactor);
    vec3 stormSky    = mix(stormHoriz, stormZenith, pow(h, 0.55));
    sky = mix(sky, stormSky, wetWeather * 0.78);

    // Atmospheric Rayleigh horizon airglow + warm Mie solar forward scattering
    float sunDot = max(dot(dir, S), 0.0);
    float horizGlow = exp(-h * 7.5);
    vec3 mieCol = mix(vec3(0.38, 0.56, 0.96),
                      mix(vec3(1.22, 0.96, 0.68), vec3(1.55, 0.54, 0.18), sunsetBand),
                      dayFactor);
    sky += mieCol * pow(sunDot, 7.0) * (0.24 + 0.52 * sunsetBand + 0.25 * horizGlow) * (1.0 - wetWeather * 0.68);

    if (S.y > -0.05 && wetWeather < 0.75) {
        float sunDisc = smoothstep(0.99935, 0.99982, dot(dir, S));
        float corona  = pow(sunDot, 160.0) * 1.10;
        vec3 discCol  = mix(vec3(0.88, 0.94, 1.0), vec3(3.0, 2.3, 1.45), dayFactor);
        sky += discCol * (sunDisc * 2.4 + corona) * (1.0 - wetWeather * 0.92);
    }

    // Crisp twinkling stars & subtle Seoul urban horizon skyglow at night
    if (night > 0.15 && dir.y > 0.04 && wetWeather < 0.7) {
        vec2 starUv = dir.xz / (dir.y + 0.35) * 105.0;
        vec2 cell = floor(starUv);
        vec2 f = fract(starUv) - 0.5;
        float rnd = hash12(cell);
        if (rnd > 0.966) {
            float d = length(f);
            float twinkle = 0.65 + 0.35 * sin(animTime * 2.6 + rnd * 91.0);
            float star = (1.0 - smoothstep(0.02, 0.21, d)) * twinkle * night * smoothstep(0.04, 0.26, dir.y);
            sky += vec3(0.76, 0.88, 1.0) * star * 1.35 * (1.0 - wetWeather * 0.85);
        }
        sky += vec3(0.036, 0.046, 0.072) * horizGlow * night * 0.85;
    }

    // Sculpted 2-Layer Sunlit Cumulus + High Cirrus Clouds (crisp depth, never muddy grey!)
    if (dir.y > 0.015) {
        vec2 cloudUv = dir.xz / (dir.y + 0.18) * 0.88 + vec2(animTime * 0.0065, animTime * 0.0032);
        float n1 = fbmClouds(cloudUv);
        float n2 = fbmClouds(cloudUv * 2.25 + vec2(4.7, -3.3));
        float highWisps = smoothstep(0.56, 0.86, fbmClouds(cloudUv * 1.7 + vec2(-8.2, 12.1))) * 0.24 * (1.0 - wetWeather);

        float lowThresh = mix(0.49, 0.26, wetWeather);
        float density = smoothstep(lowThresh, mix(0.79, 0.62, wetWeather), n1 * 0.70 + n2 * 0.30);
        float horizonFade = smoothstep(0.015, 0.22, dir.y);
        density = clamp((density + highWisps) * horizonFade * mix(0.86, 0.96, wetWeather), 0.0, 1.0);

        // Directional self-shadowing: sample slightly toward sun direction for bright silver cloud tops!
        vec2 sunStep = normalize(S.xz + vec2(0.001)) * 0.11;
        float nSun = fbmClouds(cloudUv + sunStep);
        float rimLit = clamp(0.55 + (n1 - nSun) * 2.4 + pow(sunDot, 4.0) * 0.58, 0.0, 1.0);

        vec3 cloudLit = mix(vec3(0.08, 0.11, 0.19),
                            mix(vec3(1.16, 1.13, 1.08), vec3(1.42, 0.70, 0.36), sunsetBand), dayFactor);
        vec3 cloudShade = mix(vec3(0.025, 0.035, 0.065),
                              mix(vec3(0.36, 0.46, 0.62), vec3(0.32, 0.22, 0.34), sunsetBand), dayFactor);
        if (wetWeather > 0.1) {
            cloudLit   = mix(cloudLit,   vec3(0.34, 0.38, 0.44), wetWeather * 0.72);
            cloudShade = mix(cloudShade, vec3(0.14, 0.16, 0.20), wetWeather * 0.78);
            cloudLit   = mix(cloudLit,   vec3(0.06, 0.08, 0.12), night);
            cloudShade = mix(cloudShade, vec3(0.02, 0.03, 0.05), night);
        }
        vec3 cloudCol = mix(cloudShade, cloudLit, rimLit);
        sky = mix(sky, cloudCol, density * (0.82 + 0.14 * wetWeather));
    }

    if (dir.y < 0.0) {
        vec3 belowCol = mix(horizNight, horizDay, dayFactor) * 0.42;
        sky = mix(sky, belowCol, clamp(-dir.y * 6.0, 0.0, 1.0));
    }

    outColor = vec4(sky, 1.0);
}
