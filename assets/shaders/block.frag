#version 330 core

in float vAO;
in float vSkyLight;
in float vAlpha;
in float vBlockLight;
in vec2 vTileUV;
in vec3 vWorldPosition;
flat in float vTile;
flat in float vFace;

uniform sampler2D uBlockAtlas;
uniform vec3 uCameraPosition;
uniform vec3 uLightDirection;
uniform vec3 uDirectColor;
uniform vec3 uAmbientColor;
uniform vec3 uFogColor;
uniform float uDirectIntensity;
uniform float uAmbientIntensity;
uniform float uFogEnd;
uniform float uFogStartFraction;
uniform int uManualGamma;

out vec4 outColor;

vec3 faceNormal(float face) {
    if (face < 0.5) return vec3( 0.0, 1.0, 0.0);
    if (face < 1.5) return vec3( 0.0,-1.0, 0.0);
    if (face < 2.5) return vec3( 0.0, 0.0,-1.0);
    if (face < 3.5) return vec3( 0.0, 0.0, 1.0);
    if (face < 4.5) return vec3( 1.0, 0.0, 0.0);
    if (face < 5.5) return vec3(-1.0, 0.0, 0.0);
    return vec3(0.0, 0.55, 0.0);
}

void main() {
    const float atlasTiles = 8.0;
    float tile = floor(vTile + 0.5);
    vec2 tileOrigin = vec2(mod(tile, atlasTiles), floor(tile / atlasTiles));
    vec2 local = mix(vec2(0.5 / 16.0), vec2(15.5 / 16.0), fract(vTileUV));
    vec2 atlasUV = (tileOrigin + local) / atlasTiles;
    vec2 dx = dFdx(vTileUV) * (15.0 / 16.0) / atlasTiles;
    vec2 dy = dFdy(vTileUV) * (15.0 / 16.0) / atlasTiles;
    vec4 texel = textureGrad(uBlockAtlas, atlasUV, dx, dy);
    texel.a *= vAlpha;
    if (texel.a < 0.1) discard;

    vec3 normal = faceNormal(vFace);
    float diffuse = vFace > 5.5
        ? 0.32
        : max(dot(normal, normalize(uLightDirection)), 0.0);
    float ao = mix(0.55, 1.0, clamp(vAO, 0.0, 1.0));
    float caveAmbient = mix(0.72, 1.0, vSkyLight);
    vec3 lighting = uAmbientColor * uAmbientIntensity * caveAmbient;
    lighting += uDirectColor * uDirectIntensity * diffuse * vSkyLight * 0.58;
    lighting *= ao;
    lighting = max(lighting, vec3(1.0, 0.72, 0.38) * vBlockLight * 1.15);

    // Atlas tile 19 is lava: keep it readable and warm independent of the sky.
    if (abs(tile - 19.0) < 0.25)
        lighting = max(lighting, vec3(1.15, 0.52, 0.16));

    // Atlas tile 9 is water: add a restrained celestial highlight.
    if (abs(tile - 9.0) < 0.25 && vFace < 0.5) {
        vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
        vec3 halfDir = normalize(viewDir + normalize(uLightDirection));
        float sparkle = pow(max(dot(normal, halfDir), 0.0), 48.0);
        lighting += uDirectColor * sparkle * vSkyLight * 0.24;
    }

    vec3 color = texel.rgb * lighting;
    float distanceToCamera = length(vWorldPosition - uCameraPosition);
    float fog = smoothstep(uFogEnd * uFogStartFraction, uFogEnd,
                           distanceToCamera);
    vec3 localFog = mix(uAmbientColor * uAmbientIntensity * 0.34,
                        uFogColor, vSkyLight);
    color = mix(color, localFog, fog);

    if (uManualGamma != 0)
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, texel.a);
}
