#include "plugins/PluginManager.h"
#include <cstring>
#include <cmath>
#include <stb_image.h>

namespace Plugins {
void PluginManager::loadRenderData(MC_PluginContext& ctx,const Json& data) {
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto number=[&](const Json& value,const char* key,uint32_t fallback){const double n=value.numeric(key,fallback);require(std::isfinite(n)&&n>=0&&n<=UINT32_MAX&&std::floor(n)==n,"Invalid render integer");return static_cast<uint32_t>(n);};
    auto handle=[&](const Json& value,const char* field)->MC_RenderHandle {
        const auto key=value.text(field);if(key.empty())return 0;if(key=="minecraftc:scene_color")return MC_RENDER_SCENE_COLOR;
        for(const auto& r:content().extensions.renderResources)if(r.second.owner==ctx.index&&r.second.key==key)return r.first;
        throw std::runtime_error("Unknown render resource: "+key);
    };
    if(data.has("render_resources")) {
        const auto& array=data.at("render_resources");require(array.type==Json::Type::Array,"Expected render resource array");
        for(const auto& value:array.array) {
            MC_RenderResource resource{};resource.size=sizeof(resource);const auto key=value.at("key").text(),kind=value.at("kind").text(),path=value.text("path");resource.key=key.c_str();resource.path=path.c_str();
            std::vector<uint8_t> bytes;std::vector<uint32_t> indices;
            if(kind=="shader")resource.kind=MC_RENDER_SHADER;
            else if(kind=="texture") {
                resource.kind=MC_RENDER_TEXTURE;const auto image=containedPath(m_info.at(ctx.index).root,path);
                require(std::filesystem::file_size(image)<=16*1024*1024,"Render texture file too large");
                int w=0,h=0,channels=0;uint8_t* pixels=stbi_load(image.string().c_str(),&w,&h,&channels,4);
                std::unique_ptr<uint8_t,decltype(&stbi_image_free)> ownedPixels(pixels,stbi_image_free);
                require(pixels&&w>0&&h>0&&w<=2048&&h<=2048,"Invalid render texture");bytes.assign(pixels,pixels+static_cast<size_t>(w)*h*4);resource.width=w;resource.height=h;
            } else if(kind=="mesh") {
                resource.kind=MC_RENDER_MESH;const auto& vertices=value.at("vertices");require(vertices.type==Json::Type::Array&&vertices.array.size()<=100000,"Invalid vertex array");
                for(const auto& vertex:vertices.array){require(vertex.type==Json::Type::Array&&vertex.array.size()==5,"Vertices require XYZ/UV");MC_RenderVertex v{};float components[5];for(size_t i=0;i<5;++i){require(vertex.array[i].type==Json::Type::Number,"Expected vertex number");components[i]=static_cast<float>(vertex.array[i].number);}std::memcpy(&v,components,sizeof(v));const auto* raw=reinterpret_cast<const uint8_t*>(&v);bytes.insert(bytes.end(),raw,raw+sizeof(v));}
                const auto& index=value.at("indices");require(index.type==Json::Type::Array&&index.array.size()<=1000000,"Invalid indices");for(const auto& i:index.array){require(i.type==Json::Type::Number&&i.number>=0&&i.number<=UINT32_MAX&&std::floor(i.number)==i.number,"Invalid mesh index");indices.push_back(static_cast<uint32_t>(i.number));}
            } else if(kind=="buffer") {
                resource.kind=MC_RENDER_BUFFER;const auto& values=value.at("values");require(values.type==Json::Type::Array&&values.array.size()<=4096,"Invalid buffer data");for(const auto& n:values.array){require(n.type==Json::Type::Number,"Expected buffer number");const float f=static_cast<float>(n.number);require(std::isfinite(f),"Invalid buffer value");const auto* raw=reinterpret_cast<const uint8_t*>(&f);bytes.insert(bytes.end(),raw,raw+4);}
            } else if(kind=="target") {resource.kind=MC_RENDER_TARGET;resource.width=number(value,"width",512);resource.height=number(value,"height",512);}
            else if(kind=="pipeline") {resource.kind=MC_RENDER_PIPELINE;resource.vertex_shader=handle(value,"vertex_shader");resource.fragment_shader=handle(value,"fragment_shader");resource.texture=handle(value,"texture");resource.depth_test=value.flag("depth_test",true);resource.blend=value.flag("blend",false);}
            else throw std::runtime_error("Unknown render resource kind");
            resource.data=bytes.data();resource.data_bytes=static_cast<uint32_t>(bytes.size());resource.indices=indices.data();resource.index_count=static_cast<uint32_t>(indices.size());MC_RenderHandle id;renderResource(ctx,resource,id);
        }
    }
    if(data.has("render_passes")) {
        const auto& array=data.at("render_passes");require(array.type==Json::Type::Array,"Expected render pass array");
        for(const auto& value:array.array) {
            auto pass=std::make_shared<DeclarativeRenderPass>();pass->scenePipeline=handle(value,"scene_pipeline");
            if(pass->scenePipeline)require(content().extensions.renderResources.at(pass->scenePipeline).descriptor.kind==MC_RENDER_PIPELINE,"Invalid scene pipeline");
            if(value.has("draws")) {const auto& draws=value.at("draws");require(draws.type==Json::Type::Array&&draws.array.size()<=512,"Invalid draw array");
                for(const auto& command:draws.array){MC_RenderDraw d{};d.size=sizeof(d);d.mesh=handle(command,"mesh");d.pipeline=handle(command,"pipeline");d.target=handle(command,"target");d.uniforms=handle(command,"uniforms");d.clip_space=command.flag("clip_space",false);for(int i=0;i<4;++i){d.transform[i*5]=1;d.tint[i]=1;}
                    auto floats=[&](const char* field,float* out,size_t count){if(!command.has(field))return;const auto& values=command.at(field);require(values.type==Json::Type::Array&&values.array.size()==count,"Invalid draw vector");for(size_t i=0;i<count;++i){require(values.array[i].type==Json::Type::Number,"Expected draw number");out[i]=static_cast<float>(values.array[i].number);require(std::isfinite(out[i]),"Invalid draw float");}};
                    floats("transform",d.transform,16);floats("tint",d.tint,4);pass->draws.push_back(d);}}
            MC_RenderPass descriptor{};descriptor.size=sizeof(descriptor);const auto key=value.at("key").text();descriptor.key=key.c_str();descriptor.stage=number(value,"stage",MC_RENDER_POST);descriptor.replace=value.flag("replace",false);
            const auto priority=value.numeric("priority",0);require(priority>=INT32_MIN&&priority<=INT32_MAX&&std::floor(priority)==priority,"Invalid pass priority");descriptor.priority=static_cast<int32_t>(priority);descriptor.userdata=pass.get();
            descriptor.callback=[](MC_PluginContext* c,const MC_RenderFrame* frame,void* data)->int32_t {
                auto& manager=*static_cast<PluginManager*>(c->manager);const auto& pass=*static_cast<DeclarativeRenderPass*>(data);
                if(pass.scenePipeline)for(uint64_t i=0;i<frame->scene_count;++i)if(!manager.renderScene||!manager.renderScene(c->index,i,pass.scenePipeline))return -1;
                for(const auto& draw:pass.draws)if(!manager.renderDraw||!manager.renderDraw(c->index,draw))return -1;
                return 0;
            };
            renderPass(ctx,descriptor);content().extensions.dataPasses.push_back(std::move(pass));
        }
    }
}
}
