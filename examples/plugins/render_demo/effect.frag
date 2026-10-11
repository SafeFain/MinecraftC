#version 450
layout(location=0) in vec2 texcoord;
layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform sampler2D scene;
layout(push_constant) uniform Push {mat4 mvp;vec4 tint;} push;
void main(){vec3 linear=texture(scene,texcoord).rgb;color=vec4(linear*vec3(0.92,1.03,1.08),1)*push.tint;}
