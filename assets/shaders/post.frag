#version 330 core

in vec2 vUv;
uniform sampler2D uSceneColor;
uniform vec4 uExposureBloom;
uniform vec4 uEffects;
uniform vec4 uTexelTime;
uniform vec4 uEnvironment;
out vec4 outColor;

vec3 pbrNeutral(vec3 color) {
    const float startCompression = 0.76;
    const float desaturation = 0.10;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return max(color, vec3(0.0));
    float newPeak = 1.0 - (1.0 - startCompression) *
        (1.0 - startCompression) / (peak + 1.0 - 2.0 * startCompression);
    color *= newPeak / peak;
    float amount = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), amount);
}

vec3 brightSample(vec2 uv) {
    vec3 color = texture(uSceneColor, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
    float peak = max(color.r, max(color.g, color.b));
    return color * smoothstep(0.78, 1.35, peak);
}

void main() {
    vec2 uv = vUv;
    float underwater = uEffects.x;
    if (underwater > 0.001) {
        vec2 wave = vec2(sin(uv.y * 48.0 + uTexelTime.z * 1.7),
                         cos(uv.x * 41.0 - uTexelTime.z * 1.3));
        uv += wave * uTexelTime.xy * 2.2 * underwater;
    }
    vec3 hdr = texture(uSceneColor, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
    if (uExposureBloom.y > 0.0) {
        vec2 texel = uTexelTime.xy * uExposureBloom.z;
        vec3 bloom = brightSample(uv) * 0.18;
        const vec2 directions[12] = vec2[12](
            vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1),
            vec2(.707,.707), vec2(-.707,.707),
            vec2(.707,-.707), vec2(-.707,-.707),
            vec2(2,0), vec2(-2,0), vec2(0,2), vec2(0,-2));
        int taps = int(uExposureBloom.w + 0.5);
        for (int i = 0; i < 12; ++i)
            if (i < taps)
                bloom += brightSample(uv + directions[i] * texel) /
                         (float(taps) + 5.0);
        hdr += bloom * uExposureBloom.y;
    }
    vec3 color = hdr * uExposureBloom.x;
    if (underwater > 0.001) {
        float center = 1.0 - smoothstep(0.28, 0.72, length(vUv - 0.5));
        vec3 waterTint = vec3(0.055, 0.31, 0.42);
        color = mix(color,
            waterTint * (0.55 + dot(color, vec3(.2126,.7152,.0722))),
            underwater * (0.30 + 0.22 * (1.0 - center)));
    }
    float hurt = uEffects.y;
    if (hurt > 0.001) {
        float edge = smoothstep(0.24, 0.72, length(vUv - 0.5));
        color = mix(color, vec3(max(color.r, 0.28), color.g * 0.72,
                    color.b * 0.72), edge * hurt * 0.32);
    }
    float luminance = dot(color, vec3(.2126,.7152,.0722));
    color = mix(color, vec3(luminance), uEnvironment.x * 0.06);
    color = pbrNeutral(max(color, vec3(0.0)));
    if (uEffects.z > 0.5)
        color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
