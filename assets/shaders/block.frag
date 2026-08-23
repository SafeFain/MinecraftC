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
uniform sampler2D uNormalAtlas;
uniform sampler2D uPropertyAtlas;
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
uniform int uSmoothLighting;
uniform float uAtlasTiles;
uniform float uLavaTile;
uniform float uWaterTile;
uniform float uTime;
uniform float uRainIntensity;
uniform float uThunderIntensity;
uniform float uCloudShadowStrength;
uniform float uNormalStrength;
uniform float uAoStrength;
uniform vec4 uTint;
uniform sampler2D uShadowMap;
uniform mat4 uShadowMatrices[4];
uniform vec4 uShadowSplits;
uniform int uShadowCascadeCount;
uniform float uShadowResolution;
uniform int uShadowAtlasColumns;

out vec4 outColor;

float sampleShadowCascade(vec3 worldPosition, vec3 normal, int cascade) {
    vec4 lightClip = uShadowMatrices[cascade] * vec4(worldPosition, 1.0);
    vec3 projected = lightClip.xyz / lightClip.w * 0.5 + 0.5;
    if (projected.z <= 0.0 || projected.z >= 1.0 ||
        any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0))))
        return 1.0;
    int row = cascade / uShadowAtlasColumns;
    int column = cascade - row * uShadowAtlasColumns;
    int rows = (uShadowCascadeCount + uShadowAtlasColumns - 1) / uShadowAtlasColumns;
    vec2 atlasSize = vec2(float(uShadowAtlasColumns), float(rows));
    vec2 atlasUv = (projected.xy + vec2(column, row)) / atlasSize;
    vec2 texel = 1.0 / (uShadowResolution * atlasSize);
    float bias = 0.0008 + 0.002 * (1.0 - max(dot(normal, normalize(uLightDirection)), 0.0));
    int taps = uShadowCascadeCount == 3 ? (cascade < 2 ? 4 : 1) :
               uShadowCascadeCount == 2 && cascade == 0 ? 4 : 1;
    float visible = 0.0;
    if (taps == 1) {
        visible = projected.z - bias <= texture(uShadowMap, atlasUv).r ? 1.0 : 0.0;
    } else if (taps == 4) {
        for (int y = -1; y <= 1; y += 2) for (int x = -1; x <= 1; x += 2)
            visible += projected.z - bias <=
                texture(uShadowMap, atlasUv + vec2(x, y) * texel * 0.5).r ? 1.0 : 0.0;
        visible *= 0.25;
    } else {
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
            visible += projected.z - bias <=
                texture(uShadowMap, atlasUv + vec2(x, y) * texel).r ? 1.0 : 0.0;
        visible /= 9.0;
    }
    return mix(0.35, 1.0, visible);
}

float shadowVisibility(vec3 worldPosition, vec3 normal) {
    if (uShadowCascadeCount == 0) return 1.0;
    float cameraDistance = length(worldPosition - uCameraPosition);
    int cascade = 0;
    if (uShadowCascadeCount > 1 && cameraDistance > uShadowSplits.x) cascade = 1;
    if (uShadowCascadeCount > 2 && cameraDistance > uShadowSplits.y) cascade = 2;
    if (uShadowCascadeCount > 3 && cameraDistance > uShadowSplits.z) cascade = 3;
    float visibility = sampleShadowCascade(worldPosition, normal, cascade);
    float split = cascade == 0 ? uShadowSplits.x :
                  cascade == 1 ? uShadowSplits.y :
                  cascade == 2 ? uShadowSplits.z : uShadowSplits.w;
    float fade = smoothstep(split * 0.88, split, cameraDistance);
    if (cascade + 1 < uShadowCascadeCount)
        visibility = mix(visibility,
            sampleShadowCascade(worldPosition, normal, cascade + 1), fade);
    else
        visibility = mix(visibility, 1.0, fade);
    return visibility;
}

vec3 faceNormal(float face) {
    if (face < 0.5) return vec3( 0.0, 1.0, 0.0);
    if (face < 1.5) return vec3( 0.0,-1.0, 0.0);
    if (face < 2.5) return vec3( 0.0, 0.0,-1.0);
    if (face < 3.5) return vec3( 0.0, 0.0, 1.0);
    if (face < 4.5) return vec3( 1.0, 0.0, 0.0);
    if (face < 5.5) return vec3(-1.0, 0.0, 0.0);
    return vec3(0.0, 0.55, 0.0);
}

vec3 detailNormal(vec3 baseNormal, vec3 tangentNormal, float strength) {
    vec3 tangent = abs(baseNormal.y) > 0.5 ? vec3(1.0, 0.0, 0.0) :
                   abs(baseNormal.x) > 0.5 ? vec3(0.0, 0.0, 1.0) :
                                             vec3(1.0, 0.0, 0.0);
    vec3 bitangent = normalize(cross(baseNormal, tangent));
    tangentNormal.xy *= strength;
    tangentNormal = normalize(tangentNormal);
    return normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y +
                     normalize(baseNormal) * tangentNormal.z);
}

float valueNoise(vec2 point) {
    vec2 cell = floor(point);
    vec2 local = fract(point);
    local = local * local * (3.0 - 2.0 * local);
    vec2 hash = fract(sin(vec2(dot(cell, vec2(127.1, 311.7)),
        dot(cell + vec2(1.0), vec2(269.5, 183.3)))) * 43758.5453);
    float a = hash.x;
    float d = hash.y;
    float b = fract(sin(dot(cell + vec2(1.0, 0.0),
        vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(cell + vec2(0.0, 1.0),
        vec2(127.1, 311.7))) * 43758.5453);
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

void main() {
    float atlasTiles = uAtlasTiles;
    float tile = floor(vTile + 0.5);
    float packedLight = floor(fract(vTile) * 512.0 + 0.5);
    float flatSkyLight = floor(packedLight / 16.0) / 15.0;
    float flatBlockLight = mod(packedLight, 16.0) / 15.0;
    vec2 tileOrigin = vec2(mod(tile, atlasTiles), floor(tile / atlasTiles));
    vec2 localPixel = mix(vec2(0.5), vec2(15.5), fract(vTileUV));
    vec2 atlasUV = (tileOrigin + localPixel / 16.0) / atlasTiles;
    vec2 dx = dFdx(vTileUV) * (15.0 / 16.0) / atlasTiles;
    vec2 dy = dFdy(vTileUV) * (15.0 / 16.0) / atlasTiles;
    vec4 texel = textureGrad(uBlockAtlas, atlasUV, dx, dy);
    vec3 tangentNormal = textureGrad(uNormalAtlas, atlasUV, dx, dy).xyz * 2.0 - 1.0;
    vec4 properties = textureGrad(uPropertyAtlas, atlasUV, dx, dy);
    texel.a *= vAlpha;
    if (texel.a < 0.1) discard;

    bool isLava = abs(tile - uLavaTile) < 0.25;
    bool isWater = abs(tile - uWaterTile) < 0.25;
    vec3 geometricNormal = faceNormal(vFace);
    float detailStrength = vFace > 5.5 ? 0.30 :
                           isWater ? 0.0 : isLava ? 0.46 : 1.0;
    vec3 normal = detailNormal(geometricNormal, tangentNormal,
                               detailStrength * uNormalStrength);
    if (isWater && vFace < 0.5) {
        vec2 wave = vec2(sin(vWorldPosition.x * 0.72 + uTime * 1.35) +
                         sin(vWorldPosition.z * 1.31 - uTime * 0.84),
                         cos(vWorldPosition.z * 0.67 - uTime * 1.08) +
                         cos(vWorldPosition.x * 1.17 + uTime * 0.73));
        normal = normalize(geometricNormal + vec3(wave.x, 0.0, wave.y) * 0.055);
    }
    float diffuse = vFace > 5.5
        ? 0.32
        : max(dot(normal, normalize(uLightDirection)), 0.0);
    float smoothWeight = uSmoothLighting != 0 ? 1.0 : 0.0;
    float ao = mix(1.0, mix(0.52, 1.0, clamp(vAO, 0.0, 1.0)),
                   smoothWeight * uAoStrength);
    float sampledSky = mix(flatSkyLight, vSkyLight, smoothWeight);
    float sampledBlock = mix(flatBlockLight, vBlockLight, smoothWeight);
    float skyLight = pow(clamp(sampledSky, 0.0, 1.0), 1.20);
    float blockLight = pow(clamp(sampledBlock, 0.0, 1.0), 1.35);
    float topSurface = max(geometricNormal.y, 0.0);
    float wetness = uRainIntensity * skyLight * topSurface;
    float cloudNoise = valueNoise(vWorldPosition.xz * 0.010 +
                                  vec2(uTime * 0.004, 0.0));
    cloudNoise = 0.58 + 0.42 * cloudNoise;
    float cloudShadow = mix(1.0, cloudNoise,
        uCloudShadowStrength * (0.45 + 0.55 * uRainIntensity));
    float visibility = shadowVisibility(vWorldPosition, normal) * cloudShadow;
    vec3 lighting = uAmbientColor * uAmbientIntensity * skyLight * 0.72;
    lighting += uDirectColor * uDirectIntensity * diffuse * skyLight * 0.52 *
                visibility;
    lighting = max(lighting, vec3(1.0, 0.72, 0.38) * blockLight * 1.15);
    lighting = max(lighting * ao, vec3(0.025));

    // Atlas tile 19 is lava: keep it readable and warm independent of the sky.
    if (isLava || properties.b > 0.01)
        lighting = max(lighting,
            vec3(2.15, 0.72, 0.16) * max(properties.b, isLava ? 1.0 : 0.0));

    float roughness = isWater ? 0.08 : mix(properties.r, 0.82,
                                           step(5.5, vFace));
    roughness = mix(roughness, 0.16, wetness * 0.82);
    if (vFace < 5.5) {
        vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
        vec3 halfDir = normalize(viewDir + normalize(uLightDirection));
        float exponent = mix(128.0, 10.0, roughness * roughness);
        float fresnel = 0.04 + 0.96 * pow(1.0 -
            max(dot(normal, viewDir), 0.0), 5.0);
        float specular = pow(max(dot(normal, halfDir), 0.0), exponent) *
            mix(0.16, 1.0, fresnel) * (1.0 - roughness * 0.45);
        lighting += uDirectColor * specular * skyLight * visibility *
                    (isWater ? 0.82 : 0.28);
    }

    vec3 albedo = texel.rgb * mix(1.0, 0.78, wetness);
    if (isWater) albedo = mix(albedo, vec3(0.055, 0.26, 0.34), 0.16);
    vec3 color = albedo * uTint.rgb * lighting;
    float distanceToCamera = length(vWorldPosition - uCameraPosition);
    float fogStart = uFogEnd * uFogStartFraction;
    float fogCoordinate = max(distanceToCamera - fogStart, 0.0) /
                          max(uFogEnd - fogStart, 1.0);
    float fog = 1.0 - exp(-3.0 * fogCoordinate * fogCoordinate);
    vec3 localFog = mix(uAmbientColor * uAmbientIntensity * 0.34,
                        uFogColor, skyLight);
    color = mix(color, localFog, fog);

    if (uManualGamma != 0)
        color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, texel.a * uTint.a);
}
