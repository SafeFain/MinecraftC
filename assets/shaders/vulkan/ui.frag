#version 450
layout(location=0)in vec2 uv;
layout(location=1)in vec4 color;
layout(set=0,binding=0)uniform sampler2D uiTexture;
layout(push_constant)uniform UiConstants{mat4 projection;vec4 options;}ui;
layout(location=0)out vec4 outColor;
void main(){vec4 result=texture(uiTexture,uv)*color;if(ui.options.x>0.5)
result.rgb=pow(max(result.rgb,vec3(0.0)),vec3(1.0/2.2));outColor=result;}
