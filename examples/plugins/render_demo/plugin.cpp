#include "minecraftc/plugin.hpp"

namespace {
const MC_RenderV1* render;
MC_RenderHandle mesh, pipeline, target, targetPipeline, overlayPipeline, uniforms;

int32_t draw(MC_PluginContext* context, const MC_RenderFrame* frame, void*) {
    const float values[4]={static_cast<float>(frame->time),1,0,0};
    if(render->update_buffer(context,uniforms,values,sizeof(values)))return -1;
    MC_RenderDraw command{};
    command.size=sizeof(command);command.mesh=mesh;command.pipeline=targetPipeline;
    command.target=target;command.uniforms=uniforms;command.clip_space=1;
    for(int i=0;i<4;++i){command.transform[i*5]=1;command.tint[i]=1;}
    if(render->draw(context,&command))return -1;
    command.target=0;command.pipeline=pipeline;
    if(render->draw(context,&command))return -1;
    // Sample the offscreen result in a small corner overlay.
    command.pipeline=overlayPipeline;command.transform[0]=command.transform[5]=.12f;
    command.transform[12]=.84f;command.transform[13]=.84f;
    return render->draw(context,&command);
}
int32_t load(const MC_Host* api,MC_PluginContext* context) {
    minecraftc::Host host(api,context);render=host.render();if(!render)return -1;
    MC_RenderResource resource{};resource.size=sizeof(resource);resource.kind=MC_RENDER_SHADER;
    resource.key="render_demo:vertex";resource.path="effect.vert.spv";
    MC_RenderHandle vertex,fragment,texture;
    if(render->register_resource(context,&resource,&vertex))return -1;
    resource.key="render_demo:fragment";resource.path="effect.frag.spv";
    if(render->register_resource(context,&resource,&fragment))return -1;
    const MC_RenderVertex vertices[]={{{-1,-1,0},{0,0}},{{1,-1,0},{1,0}},{{1,1,0},{1,1}},{{-1,1,0},{0,1}}};
    const uint32_t indices[]={0,1,2,0,2,3};
    resource={};resource.size=sizeof(resource);resource.kind=MC_RENDER_MESH;resource.key="render_demo:screen";
    resource.data=vertices;resource.data_bytes=sizeof(vertices);resource.indices=indices;resource.index_count=6;
    if(render->register_resource(context,&resource,&mesh))return -1;
    const uint8_t pixel[]={48,160,224,255};
    resource={};resource.size=sizeof(resource);resource.kind=MC_RENDER_TEXTURE;resource.key="render_demo:blue";
    resource.width=resource.height=1;resource.data=pixel;resource.data_bytes=sizeof(pixel);
    if(render->register_resource(context,&resource,&texture))return -1;
    resource={};resource.size=sizeof(resource);resource.kind=MC_RENDER_TARGET;resource.key="render_demo:target";
    resource.width=resource.height=64;
    if(render->register_resource(context,&resource,&target))return -1;
    const float values[4]={0,1,0,0};
    resource={};resource.size=sizeof(resource);resource.kind=MC_RENDER_BUFFER;resource.key="render_demo:uniforms";
    resource.data=values;resource.data_bytes=sizeof(values);
    if(render->register_resource(context,&resource,&uniforms))return -1;
    resource={};resource.size=sizeof(resource);resource.kind=MC_RENDER_PIPELINE;resource.vertex_shader=vertex;resource.fragment_shader=fragment;
    resource.key="render_demo:effect";resource.texture=MC_RENDER_SCENE_COLOR;
    if(render->register_resource(context,&resource,&pipeline))return -1;
    resource.key="render_demo:offscreen";resource.texture=texture;
    if(render->register_resource(context,&resource,&targetPipeline))return -1;
    resource.key="render_demo:overlay";resource.texture=target;
    if(render->register_resource(context,&resource,&overlayPipeline))return -1;
    MC_RenderPass pass{sizeof(MC_RenderPass),MC_RENDER_POST,1,0,"render_demo:color_grade",draw,nullptr};
    return render->register_pass(context,&pass);
}
const MC_Plugin plugin{sizeof(MC_Plugin),MC_PLUGIN_ABI,"render_demo","1.0.0",load,nullptr};
}
extern "C" MC_PLUGIN_EXPORT const MC_Plugin* MCPlugin_Query(uint32_t abi) {
    return abi==MC_PLUGIN_ABI?&plugin:nullptr;
}
