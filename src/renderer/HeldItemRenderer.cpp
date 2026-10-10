#include "plugins/ContentRegistry.h"
#include "renderer/HeldItemRenderer.h"

#include "core/AssetStore.h"
#include "player/PlayerVisual.h"
#include "renderer/GameRenderer.h"
#include "renderer/HeldItemMesh.h"
#include "renderer/RenderDevice.h"
#include "world/Block.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
TextureData decode(const std::filesystem::path& path) {
    const auto bytes = AssetStore::readPath(path);
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        throw std::runtime_error("Could not decode held-item texture: " + path.string());
    }
    TextureData result;
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4u);
    stbi_image_free(pixels);
    return result;
}

std::string itemKey(ItemId id) {
    std::string key = getItemProps(id).name;
    for (char& c : key) c = std::isalnum(static_cast<unsigned char>(c))
        ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    return key;
}

void quad(MeshData& mesh, const glm::vec3& a, const glm::vec3& b,
          const glm::vec3& c, const glm::vec3& d,
          const glm::vec2& ta, const glm::vec2& tb,
          const glm::vec2& tc, const glm::vec2& td) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {{a,ta},{b,tb},{c,tc},{d,td}});
    mesh.indices.insert(mesh.indices.end(), {base,base+1,base+2,base,base+2,base+3});
}

glm::mat4 droppedMeshTransform(const MeshData& mesh,
                               const std::vector<HeldToolRange>& ranges, float size) {
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    const auto include = [&](glm::vec3 p) {
        minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);
    };
    if (ranges.empty()) {
        for (const auto& vertex : mesh.vertices) include(vertex.position);
    } else {
        // Bow geometry contains unit boxes and a drawn arrow. Bound only the
        // visible rest pose, so dropped models never inherit player use state.
        for (const auto& range : ranges) {
            if (range.part==HeldToolPart::Arrow) continue;
            const glm::mat4 part=heldToolPartTransform(range.part,0.0f);
            for (uint32_t i=range.firstIndex;i<range.firstIndex+range.indexCount;++i)
                include(glm::vec3(part*glm::vec4(mesh.vertices[mesh.indices[i]].position,1)));
        }
    }
    if (minimum.x>maximum.x) return glm::mat4(1);
    const glm::vec3 extent=maximum-minimum;
    const float scale=size/std::max({extent.x,extent.y,extent.z,0.001f});
    return glm::scale(glm::mat4(1),glm::vec3(scale))*
        glm::translate(glm::mat4(1),-(minimum+maximum)*0.5f);
}
// Shared by the view model draw and world-space fishing-line attachment.
glm::mat4 toolFirstPersonPose(float swing, float strength, float aspect,
                             const glm::mat4& movement) {
    const float lowered = (1.0f-std::clamp(strength,0.0f,1.0f))*.28f;
    const float horizontal = std::min(1.0f,std::max(aspect,.1f)/(4.0f/3.0f));
    return movement * glm::translate(glm::mat4(1),glm::vec3(0,-lowered,0)) *
        firstPersonSwingTransform(swing) *
        glm::translate(glm::mat4(1),glm::vec3(.32f*horizontal,-.39f,-.72f)) *
        glm::rotate(glm::mat4(1),glm::radians(-28.0f*horizontal),glm::vec3(0,0,1)) *
        glm::rotate(glm::mat4(1),glm::radians(25.0f),glm::vec3(0,1,0)) *
        glm::scale(glm::mat4(1),glm::vec3(.46f*std::sqrt(horizontal)));
}

}

HeldItemRenderer::~HeldItemRenderer() { reset(); }

void HeldItemRenderer::initialize(IGameRenderer& renderer,
                                  const std::filesystem::path& root) {
    reset();
    m_renderer = &renderer;
    const auto atlasJson = nlohmann::json::parse(AssetStore::readTextPath(
        root / "textures/generated/items_atlas.json"));
    m_itemColumns = atlasJson.at("columns").get<int>();
    m_itemRows = atlasJson.at("rows").get<int>();
    for (auto entry = atlasJson.at("items").begin();
         entry != atlasJson.at("items").end(); ++entry)
        m_itemIndices.emplace(entry.key(), entry.value().at("index").get<int>());
    TextureData items = decode(root / "textures/generated/items_atlas.png");
    m_itemWidth = items.width; m_itemHeight = items.height; m_itemPixels = items.pixels;
    TextureSamplerDesc sampler; sampler.addressU=TextureAddressMode::ClampToEdge;
    sampler.addressV=TextureAddressMode::ClampToEdge;
    m_itemTexture = renderer.createTexture(items, sampler);
    TextureData white; white.width=white.height=1; white.pixels={255,255,255,255};
    m_fishingTexture=renderer.createTexture(white,sampler);
    MaterialDesc fishingMaterial; fishingMaterial.baseColorTexture=m_fishingTexture;
    m_fishingMaterial=renderer.createMaterial(fishingMaterial);
    m_fishingCube=renderer.createMesh(buildHeldCubeMesh({0,0,0,0,0,0},1,false));
    m_toolTexture = renderer.createTexture(buildHeldToolTexture(), sampler);
    TextureData arm = decode(root / "textures/generated/entity_skins/player.png");
    m_armTexture = renderer.createTexture(arm, sampler);
    MaterialDesc material; material.pipeline=MaterialPipeline::UnlitTextured;
    material.backfaceCull=false; material.baseColorTexture=m_itemTexture;
    m_itemMaterial=renderer.createMaterial(material);
    material.baseColorTexture=m_toolTexture;
    m_toolMaterial=renderer.createMaterial(material);
    material.baseColorTexture=renderer.getBlockAtlasTexture();
    m_blockMaterial=renderer.createMaterial(material);
    material.baseColorTexture=m_armTexture;
    m_armMaterial=renderer.createMaterial(material);
    // Limb-primary is tile 12 of the semantic 4x4 player skin.
    m_armMesh=renderer.createMesh(buildHeldCubeMesh(
        {12,12,12,12,12,12},4,false));
    m_blockTiles=static_cast<int>(renderer.blockAtlasTilesPerSide());
}

void HeldItemRenderer::reset() {
    if (!m_renderer) return;
    for (const auto& entry : m_meshes) if(entry.second.handle)m_renderer->destroyMesh(entry.second.handle);
    m_meshes.clear();
    if(m_fishingCube)m_renderer->destroyMesh(m_fishingCube);
    if(m_fishingMaterial)m_renderer->destroyMaterial(m_fishingMaterial);
    if(m_fishingTexture)m_renderer->destroyTexture(m_fishingTexture);
    m_fishingCube={};m_fishingMaterial={};m_fishingTexture={};
    if(m_toolMaterial)m_renderer->destroyMaterial(m_toolMaterial);
    if(m_toolTexture)m_renderer->destroyTexture(m_toolTexture);
    m_toolMaterial={};m_toolTexture={};m_use={};
    if(m_armMesh)m_renderer->destroyMesh(m_armMesh);
    if(m_armMaterial)m_renderer->destroyMaterial(m_armMaterial);
    if(m_blockMaterial)m_renderer->destroyMaterial(m_blockMaterial);
    if(m_itemMaterial)m_renderer->destroyMaterial(m_itemMaterial);
    if(m_armTexture)m_renderer->destroyTexture(m_armTexture);
    if(m_itemTexture)m_renderer->destroyTexture(m_itemTexture);
    m_renderer=nullptr;m_armMesh={};m_armMaterial={};m_blockMaterial={};m_itemMaterial={};
    m_armTexture={};m_itemTexture={};m_itemPixels.clear();m_itemIndices.clear();
}

HeldItemRenderer::CachedMesh HeldItemRenderer::meshFor(ItemId id) {
    const uint16_t key=static_cast<uint16_t>(id);
    if(const auto found=m_meshes.find(key);found!=m_meshes.end())return found->second;
    const auto& props=getItemProps(id);
    MeshData mesh;bool block=false;
    if(hasHeldToolModel(id,props.tool)) {
        HeldToolModel model=buildHeldToolModel(id,props.tool,props.tier);
        CachedMesh result{m_renderer->createMesh(model.mesh),false,true,std::move(model.ranges)};
        result.droppedTransform=droppedMeshTransform(model.mesh,result.ranges,.4f);
        m_meshes.emplace(key,result);
        return result;
    }
    if(props.placedBlock&&getBlockProps(*props.placedBlock).shape==RenderShape::Cube){
        const BlockId b=*props.placedBlock;
        mesh=buildHeldCubeMesh({static_cast<int>(getFaceTextureIndex(b,FaceDir::FRONT)),
                       static_cast<int>(getFaceTextureIndex(b,FaceDir::BACK)),
                       static_cast<int>(getFaceTextureIndex(b,FaceDir::LEFT)),
                       static_cast<int>(getFaceTextureIndex(b,FaceDir::RIGHT)),
                       static_cast<int>(getFaceTextureIndex(b,FaceDir::TOP)),
                       static_cast<int>(getFaceTextureIndex(b,FaceDir::BOTTOM))},
                       m_blockTiles,true);
        block=true;
    }else if(const auto* item=Plugins::pluginItem(id)) {
        const float s=static_cast<float>(m_blockTiles);const int tile=item->tile;
        const float u0=(tile%m_blockTiles+.5f/16)/s,u1=(tile%m_blockTiles+1-.5f/16)/s;
        const float v0=(tile/m_blockTiles+.5f/16)/s,v1=(tile/m_blockTiles+1-.5f/16)/s;
        quad(mesh,{-.5f,-.5f,.04f},{.5f,-.5f,.04f},{.5f,.5f,.04f},{-.5f,.5f,.04f},{u0,v0},{u1,v0},{u1,v1},{u0,v1});
        quad(mesh,{.5f,-.5f,-.04f},{-.5f,-.5f,-.04f},{-.5f,.5f,-.04f},{.5f,.5f,-.04f},{u1,v0},{u0,v0},{u0,v1},{u1,v1});
        block=true;
    }else{
        const auto found=m_itemIndices.find(itemKey(id));
        if(found==m_itemIndices.end())return {};
        const int tile=found->second,tx=tile%m_itemColumns,ty=tile/m_itemColumns;
        auto opaque=[&](int x,int y){if(x<0||x>=16||y<0||y>=16)return false;
            const size_t pixel=(static_cast<size_t>(ty*16+y)*m_itemWidth+tx*16+x)*4u;
            return pixel+3<m_itemPixels.size()&&m_itemPixels[pixel+3]>=26;};
        const float z=.045f;
        auto uv=[&](float x,float y){return glm::vec2((tx*16+x)/m_itemWidth,
                                                      (ty*16+y)/m_itemHeight);};
        for(int y=0;y<16;++y)for(int x=0;x<16;++x)if(opaque(x,y)){
            const float x0=x/16.0f-.5f,x1=(x+1)/16.0f-.5f;
            const float y1=.5f-y/16.0f,y0=.5f-(y+1)/16.0f;
            quad(mesh,{x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z},uv(x,y+1),uv(x+1,y+1),uv(x+1,y),uv(x,y));
            quad(mesh,{x1,y0,-z},{x0,y0,-z},{x0,y1,-z},{x1,y1,-z},uv(x+1,y+1),uv(x,y+1),uv(x,y),uv(x+1,y));
            if(!opaque(x-1,y))quad(mesh,{x0,y0,-z},{x0,y0,z},{x0,y1,z},{x0,y1,-z},uv(x,y+1),uv(x,y+1),uv(x,y),uv(x,y));
            if(!opaque(x+1,y))quad(mesh,{x1,y0,z},{x1,y0,-z},{x1,y1,-z},{x1,y1,z},uv(x+1,y+1),uv(x+1,y+1),uv(x+1,y),uv(x+1,y));
            if(!opaque(x,y-1))quad(mesh,{x0,y1,z},{x1,y1,z},{x1,y1,-z},{x0,y1,-z},uv(x,y),uv(x+1,y),uv(x+1,y),uv(x,y));
            if(!opaque(x,y+1))quad(mesh,{x0,y0,-z},{x1,y0,-z},{x1,y0,z},{x0,y0,z},uv(x,y+1),uv(x+1,y+1),uv(x+1,y+1),uv(x,y+1));
        }
    }
    CachedMesh result{m_renderer->createMesh(mesh),block,false,{}};
    result.droppedTransform=droppedMeshTransform(mesh,{},block ? .3f : .4f);
    m_meshes.emplace(key,result);return result;
}

void HeldItemRenderer::renderDropped(const ItemStack& item, const glm::mat4& vp,
    const glm::vec3& position, float ageSeconds, uint32_t phaseSeed, SmoothLightSample light) {
    if (!m_renderer || item.empty()) return;
    const CachedMesh cached=meshFor(item.id);
    if (!cached.handle) return;
    const float phase=static_cast<float>(phaseSeed%628u)*.01f;
    const glm::mat4 pose=glm::translate(glm::mat4(1),position+
        glm::vec3(0,.25f+.035f*std::sin(ageSeconds*2.5f+phase),0))*
        glm::rotate(glm::mat4(1),ageSeconds*1.5f+phase,glm::vec3(0,1,0))*cached.droppedTransform;
    const float sky=std::pow(std::clamp(light.sky,0.0f,1.0f),1.20f);
    const float block=std::pow(std::clamp(light.block,0.0f,1.0f),1.35f);
    DrawCommand command;command.mesh=cached.handle;
    command.material=cached.toolModel ? m_toolMaterial :
        (cached.blockAtlas ? m_blockMaterial : m_itemMaterial);
    command.viewProjection=vp;command.useCustomViewProjection=true;
    command.tint=glm::vec4(glm::max(glm::max(glm::vec3(sky),
        glm::vec3(1,.72f,.38f)*block),glm::vec3(.025f)),1);
    command.model=pose;
    if (!cached.toolModel) {m_renderer->draw(command);return;}
    for (const auto& range : cached.ranges) {
        if (range.part==HeldToolPart::Arrow) continue;
        command.firstIndex=range.firstIndex;command.indexCount=range.indexCount;
        command.model=pose*heldToolPartTransform(range.part,0.0f);
        m_renderer->draw(command);
    }
}

void HeldItemRenderer::updateUseState(bool charging, float charge, bool blocking, float dt) {
    m_use=advanceHeldItemUseState(m_use,charging,charge,blocking,dt);
}

void HeldItemRenderer::drawItem(const ItemStack& item, const glm::mat4& vp,
                                const glm::mat4& transform, bool firstPerson) {
    if(item.empty())return;
    const CachedMesh cached=meshFor(item.id);
    if(!cached.handle)return;
    DrawCommand held;
    held.mesh=cached.handle;
    held.material=cached.toolModel ? m_toolMaterial :
        (cached.blockAtlas ? m_blockMaterial : m_itemMaterial);
    held.viewProjection=vp;held.useCustomViewProjection=true;
    held.model=transform;
    if(!cached.toolModel) {m_renderer->draw(held);return;}
    const glm::mat4 grip=(!firstPerson && item.id==ItemId::BOW && m_use.bowCharging) ?
        glm::mat4(1) : heldToolGripTransform(item.id,getItemProps(item.id).tool,firstPerson);
    for(const auto& range:cached.ranges) {
        if(range.part==HeldToolPart::Arrow && !m_use.bowCharging)continue;
        held.firstIndex=range.firstIndex;held.indexCount=range.indexCount;
        held.model=transform*grip*heldToolPartTransform(range.part,
            item.id==ItemId::BOW ? m_use.bowCharge : 0.0f);
        m_renderer->draw(held);
    }
}

void HeldItemRenderer::renderFirstPerson(const ItemStack& item, const ItemStack& offhand,
                                         float swing, float attackStrength, float aspect,
                                         const glm::mat4& movementTransform) {
    if(!m_renderer)return;
    FrameData frame;frame.projection=glm::perspective(glm::radians(70.0f),aspect,.05f,8.0f);
    m_renderer->beginViewModel(frame.projection);
    const float lowered=(1.0f-std::clamp(attackStrength,0.0f,1.0f))*.28f;
    const glm::mat4 motion=movementTransform *
        glm::translate(glm::mat4(1),glm::vec3(0,-lowered,0)) *
        firstPersonSwingTransform(swing);
    DrawCommand arm;arm.mesh=m_armMesh;arm.material=m_armMaterial;
    arm.viewProjection=frame.projection;arm.useCustomViewProjection=true;
    arm.model=motion*glm::translate(glm::mat4(1),{.58f,-.66f,-.82f})*
        glm::rotate(glm::mat4(1),glm::radians(-18.0f),glm::vec3(0,0,1))*
        glm::scale(glm::mat4(1),{.20f,.72f,.20f});
    const float horizontal=std::min(1.0f,std::max(aspect,.1f)/(4.0f/3.0f));
    const auto screenPoint=[&](glm::vec3 p){p.x*=horizontal;return p;};
    const bool drawingBow=item.id==ItemId::BOW && m_use.bowCharging;
    const auto armBetween=[&](const glm::mat4& base,glm::vec3 elbow,glm::vec3 grip) {
        elbow=screenPoint(elbow);grip=screenPoint(grip);
        const glm::vec3 delta=grip-elbow;
        const glm::vec3 direction=glm::normalize(delta);
        const glm::quat rotation=glm::normalize(
            glm::quat(1.0f+direction.y,direction.z,0.0f,-direction.x));
        return base*glm::translate(glm::mat4(1),(elbow+grip)*.5f)*
            glm::mat4_cast(rotation)*
            glm::scale(glm::mat4(1),glm::vec3(drawingBow ? .14f : .18f,glm::length(delta),drawingBow ? .14f : .18f));
    };
    const glm::vec3 bowGrip(.20f,-.18f,-.93f);
    const glm::mat4 bowFacing=glm::rotate(glm::mat4(1),glm::radians(35.0f),glm::vec3(0,1,0));
    if(drawingBow)arm.model=armBetween(movementTransform,{.54f,-.60f,-.50f},bowGrip);
    else if(!item.empty() && meshFor(item.id).toolModel)
        arm.model=armBetween(motion,{.58f,-.85f,-.65f},{.32f,-.39f,-.72f});
    m_renderer->draw(arm);
    if(drawingBow) {
        const glm::vec3 pull=glm::vec3(bowFacing*glm::vec4(0,0,
            .46f*std::sqrt(horizontal)*(.03f+.38f*m_use.bowCharge),0));
        // Preserve the same 3D nock used by the bow, including portrait scaling.
        glm::vec3 nock=bowGrip+pull;
        nock.x=bowGrip.x+pull.x/horizontal;
        arm.model=armBetween(movementTransform,{-.42f,-.55f,-.48f},nock);
        m_renderer->draw(arm);
    }
    if(!item.empty()) {
        const CachedMesh cached=meshFor(item.id);
        const bool bow=item.id==ItemId::BOW && m_use.bowCharging;
        const float itemHorizontal=cached.toolModel ? horizontal : 1.0f;
        glm::vec3 grip=bow ? bowGrip :
            glm::vec3(cached.toolModel ? .32f : .40f,-.39f,-.72f);
        grip.x*=itemHorizontal;
        const glm::mat4 pose=(bow ? movementTransform : motion)*glm::translate(glm::mat4(1),grip)*
            glm::rotate(glm::mat4(1),glm::radians(bow ? 0.0f : -28.0f*itemHorizontal),glm::vec3(0,0,1))*
            glm::rotate(glm::mat4(1),glm::radians(bow ? 35.0f : 25.0f),glm::vec3(0,1,0))*
            glm::scale(glm::mat4(1),glm::vec3(cached.blockAtlas ? .32f : .46f*std::sqrt(itemHorizontal)));
        drawItem(item,frame.projection,
            !bow && cached.toolModel ? toolFirstPersonPose(swing,attackStrength,aspect,movementTransform) : pose,true);
    }
    if(offhand.id==ItemId::SHIELD && !offhand.empty() && !drawingBow) {
        const float r=m_use.shieldRaise;
        const glm::mat4 left=movementTransform*glm::translate(glm::mat4(1),
            screenPoint(glm::vec3(-.48f+.22f*r,-.43f+.25f*r,-.78f-.05f*r)));
        arm.model=left*glm::translate(glm::mat4(1),glm::vec3(0,-.24f,.13f))*
            glm::scale(glm::mat4(1),glm::vec3(.18f,.54f,.18f));
        m_renderer->draw(arm);
        drawItem(offhand,frame.projection,left*
            glm::rotate(glm::mat4(1),glm::radians(25.0f*(1-r)),glm::vec3(0,1,0))*
            glm::scale(glm::mat4(1),glm::vec3(.66f*horizontal)),true);
    }
}

void HeldItemRenderer::renderThirdPerson(const ItemStack& item,
                                         const glm::mat4& vp, const glm::mat4& hand,
                                         const ItemStack& offhand,const glm::mat4& leftHand, const HeldItemUseState* useState) {
    if(!m_renderer)return;
    struct Restore { HeldItemUseState& state; HeldItemUseState previous; ~Restore() { state = previous; } } restore{m_use, m_use};
    if (useState) m_use = *useState;
    if(!item.empty()) {
        const auto cached=meshFor(item.id);
        drawItem(item,vp,hand*glm::scale(glm::mat4(1),
            glm::vec3(cached.blockAtlas ? .28f : (cached.toolModel ? .70f : .38f))),false);
    }
    if(offhand.id==ItemId::SHIELD && !offhand.empty() &&
       !(item.id==ItemId::BOW && m_use.bowCharging))
        drawItem(offhand,vp,leftHand*
            glm::rotate(glm::mat4(1),glm::radians(-80.0f*m_use.shieldRaise),glm::vec3(1,0,0))*
            glm::scale(glm::mat4(1),glm::vec3(.85f)),false);
}

glm::vec3 HeldItemRenderer::firstPersonFishingTip(float swing, float strength, float aspect,
    const glm::mat4& movement, const glm::mat4& worldVp) const {
    const auto projection=glm::perspective(glm::radians(70.0f),aspect,.05f,8.0f);
    const glm::vec4 world=glm::inverse(worldVp)*projection*
        toolFirstPersonPose(swing,strength,aspect,movement)*glm::vec4(0,1.25f,0,1);
    return glm::vec3(world)/world.w;
}
glm::vec3 HeldItemRenderer::thirdPersonFishingTip(const glm::mat4& hand) {
    return glm::vec3(hand*glm::scale(glm::mat4(1),glm::vec3(.70f))*
        heldToolGripTransform(ItemId::FISHING_ROD,ToolKind::FishingRod,false)*glm::vec4(0,1.25f,0,1));
}
void HeldItemRenderer::renderFishing(const FishingView& fishing, const glm::dvec3& origin,
    const glm::vec3& tip, const glm::mat4& vp) {
    if (!m_renderer || !fishing.active()) return;
    const glm::vec3 bobber=glm::vec3(fishing.position-origin);
    DrawCommand command;command.mesh=m_fishingCube;command.material=m_fishingMaterial;
    command.viewProjection=vp;command.useCustomViewProjection=true;
    auto box=[&](glm::vec3 position,glm::vec3 size,glm::vec4 tint) {
        command.model=glm::translate(glm::mat4(1),position)*glm::scale(glm::mat4(1),size);
        command.tint=tint;m_renderer->draw(command);
    };
    box(bobber+glm::vec3(0,.04f,0),{.15f,.08f,.15f},{.95f,.95f,.90f,1});
    box(bobber+glm::vec3(0,.12f,0),{.15f,.08f,.15f},{.85f,.08f,.06f,1});
    box(bobber+glm::vec3(0,.20f,0),{.025f,.10f,.025f},{.25f,.22f,.19f,1});
    const glm::vec3 end=bobber+glm::vec3(0,.25f,0);
    const float sag=std::min(1.0f,glm::length(end-tip)*.08f);
    glm::vec3 previous=tip;
    for(int i=1;i<=24;++i) {
        const float t=i/24.0f;
        const glm::vec3 p=glm::mix(tip,end,t)-glm::vec3(0,4*t*(1-t)*sag,0);
        const glm::vec3 delta=p-previous;
        const float length=glm::length(delta);
        if(length>1e-6f) {
            const glm::vec3 direction=delta/length;
            const glm::quat rotation=direction.y < -.99999f ? glm::quat(0,1,0,0) :
                glm::normalize(glm::quat(1.0f+direction.y,direction.z,0,-direction.x));
            command.model=glm::translate(glm::mat4(1),(p+previous)*.5f)*
                glm::mat4_cast(rotation)*glm::scale(glm::mat4(1),glm::vec3(.008f,length,.008f));
            command.tint={.13f,.12f,.11f,1};m_renderer->draw(command);
        }
        previous=p;
    }
}
