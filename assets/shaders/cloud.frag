#version 330 core
out vec4 FragColor;

uniform vec3 uColor;
uniform int uManualGamma;

void main() {
    vec3 color = uColor;
    if (uManualGamma != 0) color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
