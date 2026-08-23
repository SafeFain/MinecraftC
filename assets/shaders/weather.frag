#version 330 core

in float vKind;
in vec2 vUv;
in float vTexture;
in float vPhase;

uniform float uIntensity;
uniform int uManualGamma;
uniform sampler2D uBlockAtlas;
uniform float uAtlasTiles;

out vec4 outColor;

vec3 heavenPalette(float index) {
    vec3 color = vec3(1.0, 0.86, 0.55);
    if (index < 0.5) color = vec3(1.0, 0.86, 0.55);      // Dawn Meadow
    else if (index < 1.5) color = vec3(0.62, 0.95, 0.55); // Skyroot Grove
    else if (index < 2.5) color = vec3(1.0, 0.72, 0.30);  // Sunstone Heights
    else if (index < 3.5) color = vec3(0.45, 0.85, 1.0);  // Starcrystal Garden
    else if (index < 4.5) color = vec3(0.95, 0.97, 1.0);  // Cloudbloom Fields
    else if (index < 5.5) color = vec3(0.78, 0.80, 0.86); // Skystone Barrens
    else if (index < 6.5) color = vec3(0.40, 1.0, 0.80);  // Glimmer Fen
    else color = vec3(0.85, 0.75, 1.0);                    // Moonpearl Terrace
    return color;
}

void main() {
    vec4 color;
    if (vKind < 0.5) {
        float tile = floor(vTexture + 0.5);
        vec2 tileOrigin = vec2(mod(tile, uAtlasTiles),
                               floor(tile / uAtlasTiles)) / uAtlasTiles;
        vec2 fragmentOffset =
            vec2(fract(vPhase * 7.13), fract(vPhase * 13.71)) * 0.55;
        vec2 localUv = fragmentOffset + vUv * 0.42;
        color = texture(uBlockAtlas, tileOrigin + localUv / uAtlasTiles);
        color.rgb = mix(color.rgb, vec3(0.58, 0.72, 0.90), 0.32);
        color.a *= 0.86 * uIntensity;
    } else if (vKind < 1.5) {
        vec2 centered = vUv - vec2(0.5);
        float flake = 1.0 - smoothstep(0.30, 0.52, length(centered));
        color = vec4(0.90, 0.94, 1.0, flake * 0.84 * uIntensity);
    } else if (vKind < 2.5) {
        float edge = smoothstep(0.0, 0.18, vUv.x) *
                     (1.0 - smoothstep(0.82, 1.0, vUv.x));
        color = vec4(0.78, 0.87, 1.0, edge * 0.96);
    } else if (vKind < 3.5) {
        float tile = floor(vTexture + 0.5);
        vec2 tileOrigin = vec2(mod(tile, uAtlasTiles),
                               floor(tile / uAtlasTiles)) / uAtlasTiles;
        vec2 fragmentOffset = vec2(fract(vPhase * 7.13), fract(vPhase * 13.71)) * 0.55;
        vec2 localUv = fragmentOffset + vUv * 0.42;
        color = texture(uBlockAtlas, tileOrigin + localUv / uAtlasTiles);
        color.a *= 0.92;
    } else if (vKind < 5.5) {
        vec2 centered = (vUv - vec2(0.5)) * vec2(2.0, 2.8);
        float radius = length(centered);
        float inner = 0.26 + vPhase * 0.34;
        float ring = smoothstep(inner - 0.09, inner, radius) *
                     (1.0 - smoothstep(inner + 0.02, inner + 0.13, radius));
        float crown = (1.0 - smoothstep(0.18, 0.46, abs(centered.x))) *
                      smoothstep(-0.20, 0.46, centered.y) *
                      (1.0 - smoothstep(0.46, 0.82, centered.y));
        color = vec4(0.52, 0.76, 0.94,
                     (ring * 0.72 + crown * 0.25) * (1.0 - vPhase));
    } else if (vKind < 6.5) {
        vec2 centered = vUv - vec2(0.5);
        float radius = length(centered * vec2(2.0, 2.0));
        float glow = 1.0 - smoothstep(0.05, 0.72, radius);
        float pulse = 0.72 + 0.28 * sin(uIntensity * 4.0 + vPhase * 6.283);
        vec3 dayColor = vec3(1.0, 0.84, 0.38);
        vec3 nightColor = vec3(0.40, 0.58, 1.0);
        vec3 tint = mix(nightColor, dayColor, clamp(vTexture, 0.0, 1.0));
        tint = mix(tint, vec3(0.78, 0.42, 1.0),
                   0.5 + 0.5 * sin(vPhase * 9.0));
        color = vec4(tint,
                     glow * pulse * 0.72 * uIntensity);
    } else if (vKind < 7.5) {
        // Heaven pollen: a soft biome-tinted tuft drifting on the air.
        vec2 centered = vUv - vec2(0.5);
        float radius = length(centered * vec2(1.6, 1.6));
        float blob = 1.0 - smoothstep(0.10, 0.62, radius);
        float pulse = 0.82 + 0.18 * sin(uIntensity * 3.0 + vPhase * 6.283);
        color = vec4(heavenPalette(vTexture),
                     blob * pulse * 0.55 * uIntensity);
    } else {
        // Heaven sparkle: a narrow four-point star with a sharp twinkle.
        vec2 centered = vUv - vec2(0.5);
        float diamond = abs(centered.x) + abs(centered.y);
        float square = max(abs(centered.x), abs(centered.y));
        float star = 1.0 - smoothstep(0.06, 0.34, diamond);
        star *= smoothstep(0.12, 0.42, square);
        float twinkle = 0.5 + 0.5 * sin(uIntensity * 6.0 + vPhase * 6.283);
        color = vec4(heavenPalette(vTexture) * 1.2,
                     star * twinkle * 0.85 * uIntensity);
    }
    if (color.a < 0.01) discard;
    if (uManualGamma != 0)
        color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = color;
}
