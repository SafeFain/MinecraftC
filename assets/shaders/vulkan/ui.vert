#version 450
layout(location=0)in vec2 inPosition;
layout(location=1)in vec2 inUv;
layout(location=2)in vec4 inColor;
layout(push_constant)uniform UiConstants{mat4 projection;vec4 options;}ui;
layout(location=0)out vec2 uv;
layout(location=1)out vec4 color;
void main(){gl_Position=ui.projection*vec4(inPosition,0,1);uv=inUv;color=inColor;}
