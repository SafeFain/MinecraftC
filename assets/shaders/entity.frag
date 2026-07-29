#version 330 core
out vec4 FragColor;

uniform vec3 uColor;
uniform sampler2D uEntityAtlas;
uniform int uUseTexture;
uniform int uManualGamma;
uniform float uSkyLight;
uniform float uBlockLight;
in vec2 vUV;

void main() {
    vec3 color = uUseTexture != 0 ? texture(uEntityAtlas, vUV).rgb : uColor;
    float sky = pow(clamp(uSkyLight, 0.0, 1.0), 1.35);
    float block = pow(clamp(uBlockLight, 0.0, 1.0), 1.35);
    vec3 lighting = max(vec3(sky), vec3(1.0, 0.72, 0.38) * block);
    color *= max(lighting, vec3(0.025));
    if (uManualGamma != 0) color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
