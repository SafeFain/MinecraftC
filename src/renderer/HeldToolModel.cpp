#include "renderer/HeldToolModel.h"
#include "renderer/HeldItemMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {
// Pixel palettes, each with six face shades, avoid new shader layouts.
enum Palette { Handle, Wood, Stone, Iron, Gold, Diamond, Crystal, Dark, String, Count };
constexpr int atlasSide = 8;
void box(MeshData& mesh, glm::vec3 center, glm::vec3 size, int palette) {
    std::array<int, 6> tiles{};
    for (int face = 0; face < 6; ++face) tiles[face] = palette * 6 + face;
    MeshData cube = buildHeldCubeMesh(tiles, atlasSide, false);
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (size_t i=0; i<cube.vertices.size(); ++i) {
        auto v=cube.vertices[i];
        const int tile=tiles[i/4];
        const glm::vec2 lo((tile%atlasSide+.0625f)/atlasSide, (tile/atlasSide+.0625f)/atlasSide);
        const glm::vec2 hi((tile%atlasSide+.9375f)/atlasSide, (tile/atlasSide+.9375f)/atlasSide);
        v.uv=glm::clamp(v.uv,lo,hi);
        v.position = center + v.position * size;
        // Keep every face inside its pixel tile with nearest sampling.
        mesh.vertices.push_back(v);
    }
    for (uint32_t i : cube.indices) mesh.indices.push_back(base + i);
}
int tierPalette(ToolTier tier) {
    switch (tier) {
        case ToolTier::Stone: return Stone;
        case ToolTier::Iron: return Iron;
        case ToolTier::Gold: return Gold;
        case ToolTier::Diamond: return Diamond;
        default: return Wood;
    }
}
void range(HeldToolModel& model, HeldToolPart part, uint32_t start) {
    model.ranges.push_back({part, start,
        static_cast<uint32_t>(model.mesh.indices.size()) - start});
}
// Bow lives in the YZ plane, with arrows aimed along local -Z.
glm::mat4 rod(glm::vec3 a, glm::vec3 b, float thickness) {
    const glm::vec3 d = b - a;
    return glm::translate(glm::mat4(1), (a+b)*0.5f) *
        glm::rotate(glm::mat4(1), std::atan2(d.z,d.y), glm::vec3(1,0,0)) *
        glm::scale(glm::mat4(1), glm::vec3(thickness,glm::length(d),thickness));
}
}

bool hasHeldToolModel(ItemId id, ToolKind tool) {
    return tool != ToolKind::None || id == ItemId::FLINT_AND_STEEL ||
        id == ItemId::STARSTEP_SCEPTER;
}

TextureData buildHeldToolTexture() {
    const std::array<glm::vec3, Count> colors = {{
        {117,78,43}, {158,112,62}, {125,130,137}, {213,221,224},
        {244,193,49}, {61,220,208}, {142,127,244}, {50,54,65}, {227,217,184}}};
    const std::array<float,6> shades = {{0.90f,0.78f,0.70f,0.84f,1.0f,0.60f}};
    TextureData result; result.width=result.height=atlasSide*8;
    result.pixels.resize(result.width*result.height*4,255);
    for (int palette=0; palette<Count; ++palette) for (int face=0; face<6; ++face) {
        const int tile=palette*6+face;
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            const float grain = ((x*3+y*5)%7 == 0) ? 0.91f : 1.0f;
            const glm::vec3 c=colors[palette]*shades[face]*grain;
            const size_t p=((tile/atlasSide*8+y)*result.width+tile%atlasSide*8+x)*4;
            for(int channel=0;channel<3;++channel)
                result.pixels[p+channel]=static_cast<uint8_t>(c[channel]);
        }
    }
    return result;
}

HeldToolModel buildHeldToolModel(ItemId id, ToolKind tool, ToolTier tier) {
    HeldToolModel m;
    if (!hasHeldToolModel(id,tool)) return m;
    const int metal=tierPalette(tier);
    auto add=[&](glm::vec3 c,glm::vec3 s,int p){box(m.mesh,c,s,p);};
    if(tool == ToolKind::Bow) {
        add({0,0,0},{.12f,.28f,.14f},Handle);
        range(m,HeldToolPart::Body,0);
        for(HeldToolPart p : {HeldToolPart::BowUpperInner,HeldToolPart::BowUpperOuter,
            HeldToolPart::BowLowerInner,HeldToolPart::BowLowerOuter,
            HeldToolPart::StringUpper,HeldToolPart::StringLower}) {
            const uint32_t start=static_cast<uint32_t>(m.mesh.indices.size());
            add({0,0,0},{1,1,1}, p==HeldToolPart::StringUpper ||
                p==HeldToolPart::StringLower ? String : Wood);
            range(m,p,start);
        }
        const uint32_t start=static_cast<uint32_t>(m.mesh.indices.size());
        add({0,0,-.40f},{.025f,.025f,.85f},Handle);
        add({0,0,-.87f},{.08f,.06f,.12f},Iron);
        add({0,0,-.03f},{.11f,.025f,.13f},String);
        add({0,0,-.03f},{.025f,.11f,.13f},String);
        range(m,HeldToolPart::Arrow,start);
        return m;
    }
    if(tool==ToolKind::FishingRod) {
        add({0,.05f,0},{.10f,.40f,.10f},Handle);
        add({0,.52f,0},{.055f,.65f,.055f},Wood);
        add({0,1.02f,0},{.035f,.40f,.035f},Wood);
        add({0,.03f,.09f},{.18f,.16f,.08f},Iron);
        add({.13f,.03f,.09f},{.10f,.025f,.025f},Dark);
        add({0,1.23f,0},{.07f,.04f,.07f},Iron);
    } else if(tool==ToolKind::Shield) {
        add({0,.12f,0},{.68f,.84f,.12f},Wood);
        add({0,.56f,0},{.76f,.08f,.16f},Iron);
        add({0,-.32f,0},{.76f,.08f,.16f},Iron);
        add({-.34f,.12f,0},{.08f,.80f,.16f},Iron);
        add({.34f,.12f,0},{.08f,.80f,.16f},Iron);
        add({0,.12f,-.10f},{.16f,.18f,.12f},Iron);
        add({0,0,.12f},{.10f,.30f,.10f},Handle);
    } else if(id==ItemId::FLINT_AND_STEEL) {
        add({-.13f,0,0},{.12f,.32f,.16f},Dark);
        add({.08f,.14f,0},{.34f,.08f,.10f},Iron);
        add({.21f,.01f,0},{.08f,.26f,.10f},Iron);
        add({.08f,-.10f,0},{.26f,.08f,.10f},Iron);
    } else if(id==ItemId::STARSTEP_SCEPTER) {
        add({0,.24f,0},{.10f,1.05f,.10f},Handle);
        add({0,.72f,0},{.26f,.10f,.26f},Gold);
        add({0,.90f,0},{.20f,.28f,.20f},Crystal);
        add({0,1.07f,0},{.12f,.10f,.12f},Diamond);
        add({0,-.20f,0},{.14f,.10f,.14f},Gold);
    } else {
        add(tool==ToolKind::Sword ? glm::vec3(0,-.01f,0) : glm::vec3(0,.14f,0),
            tool==ToolKind::Sword ? glm::vec3(.10f,.58f,.10f) : glm::vec3(.10f,.78f,.10f),Handle);
        if(tool==ToolKind::Sword) {
            add({0,.66f,0},{.16f,.72f,.08f},metal);
            add({0,1.06f,0},{.08f,.10f,.06f},metal);
            add({0,.28f,0},{.38f,.09f,.14f},metal);
            add({0,-.23f,0},{.16f,.10f,.14f},metal);
        } else if(tool==ToolKind::Pickaxe) {
            add({0,.53f,0},{.62f,.12f,.15f},metal);
            add({-.28f,.43f,0},{.12f,.16f,.13f},metal);
            add({.28f,.43f,0},{.12f,.16f,.13f},metal);
        } else if(tool==ToolKind::Axe) {
            add({-.16f,.52f,0},{.42f,.18f,.16f},metal);
            add({-.33f,.48f,0},{.14f,.38f,.12f},metal);
            add({-.40f,.48f,0},{.06f,.28f,.08f},metal);
        } else if(tool==ToolKind::Shovel) {
            add({0,.64f,0},{.28f,.32f,.10f},metal);
            add({0,.84f,0},{.18f,.08f,.08f},metal);
        } else if(tool==ToolKind::Hoe) {
            add({-.14f,.53f,0},{.40f,.10f,.14f},metal);
            add({-.30f,.41f,0},{.10f,.20f,.12f},metal);
        }
    }
    if(tool==ToolKind::Shield)
        for(auto& vertex:m.mesh.vertices)vertex.position.z-=.12f;
    range(m,HeldToolPart::Body,0);
    return m;
}

glm::mat4 heldToolPartTransform(HeldToolPart part, float charge) {
    const float c=std::clamp(charge,0.0f,1.0f);
    const glm::vec3 root(0,.14f,0), elbow(0,.43f,-.14f+.16f*c);
    const glm::vec3 tip(0,.70f-.09f*c,.02f+.24f*c), pull(0,0,.03f+.38f*c);
    auto lower=[](glm::vec3 p){p.y=-p.y;return p;};
    switch(part) {
        case HeldToolPart::BowUpperInner: return rod(root,elbow,.09f);
        case HeldToolPart::BowUpperOuter: return rod(elbow,tip,.075f);
        case HeldToolPart::BowLowerInner: return rod(lower(elbow),lower(root),.09f);
        case HeldToolPart::BowLowerOuter: return rod(lower(tip),lower(elbow),.075f);
        case HeldToolPart::StringUpper: return rod(pull,tip,.014f);
        case HeldToolPart::StringLower: return rod(lower(tip),pull,.014f);
        case HeldToolPart::Arrow: return glm::translate(glm::mat4(1),pull);
        default: return glm::mat4(1);
    }
}

HeldItemUseState advanceHeldItemUseState(HeldItemUseState state,
    bool charging, float charge, bool blocking, float dt) {
    state.bowCharging=charging;
    state.bowCharge=charging ? std::clamp(charge,0.0f,1.0f) : 0.0f;
    const float step=std::max(dt,0.0f)*8.0f;
    state.shieldRaise+=std::clamp((blocking ? 1.0f : 0.0f)-state.shieldRaise,-step,step);
    return state;
}

glm::mat4 heldToolGripTransform(ItemId, ToolKind tool, bool firstPerson) {
    glm::mat4 result(1);
    if(!firstPerson)
        result=glm::rotate(result,glm::radians(-120.0f),glm::vec3(1,0,0));
    if(!firstPerson && tool==ToolKind::FishingRod)
        result=glm::rotate(glm::mat4(1),glm::radians(-40.0f),glm::vec3(1,0,0));
    if(tool==ToolKind::Shield)result=glm::mat4(1);
    return result;
}
