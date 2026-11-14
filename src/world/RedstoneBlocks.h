#pragma once
#include "world/Block.h"
#include "world/RedstoneNativeState.h"

namespace Redstone {
inline bool native(BlockId id) {return id>=BlockId::REDSTONE_FIRST && id<=BlockId::REDSTONE_LAST;}
inline BlockId worldBlock(NativeBlock id) {
    if(id>=NativeBlock::Count)throw std::invalid_argument("Invalid native redstone block");
    return static_cast<BlockId>(unsigned(BlockId::REDSTONE_FIRST)+unsigned(id));
}
inline NativeBlock nativeSlot(BlockId id) {
    if(!native(id))throw std::invalid_argument("Not a native redstone block");
    return static_cast<NativeBlock>(unsigned(id)-unsigned(BlockId::REDSTONE_FIRST));
}
inline std::optional<State> blockState(BlockId id) {return native(id)?nativeState(nativeSlot(id)):std::nullopt;}
inline BlockId block(State state) {return worldBlock(nativeBlock(state));}
inline glm::ivec3 vector(Position p) {return {p.x,p.y,p.z};}
inline Position position(glm::ivec3 p) {return {p.x,p.y,p.z};}
inline BlockCollisionBox orient(BlockCollisionBox box,Face face) {
    auto transform=[face](glm::vec3 p) {
        switch(face) {
            case Face::Up:return p;
            case Face::Down:return glm::vec3(p.x,1-p.y,1-p.z);
            case Face::North:return glm::vec3(p.x,p.z,1-p.y);
            case Face::South:return glm::vec3(1-p.x,p.z,p.y);
            case Face::East:return glm::vec3(p.y,p.x,p.z);
            case Face::West:return glm::vec3(1-p.y,1-p.x,p.z);
        }
        return p;
    };
    const auto a=transform(box.min),b=transform(box.max);
    return {glm::min(a,b),glm::max(a,b)};
}
inline BlockCollisionBoxes geometry(BlockId id) {
    BlockCollisionBoxes boxes;
    auto add=[&](BlockCollisionBox b){boxes.boxes[boxes.count++]=b;};
    const auto s=blockState(id);
    if(!s) {
        if(native(id)&&pistonHead(nativeSlot(id))) {
            const auto face=static_cast<Face>((unsigned(nativeSlot(id))-unsigned(NativeBlock::PISTON_HEAD_FIRST))%6);
            add(orient({{0,.75f,0},{1,1,1}},face));
            add(orient({{.375f,0,.375f},{.625f,.75f,.625f}},face));
        } else add({{0,0,0},{1,1,1}});
        return boxes;
    }
    switch(s->kind) {
        case Kind::Dust:add({{0,0,0},{1,.015625f,1}});break;
        case Kind::Lever:
            add(orient({{.25f,0,.3125f},{.75f,.1875f,.6875f}},s->facing));
            add(orient({{s->alternate?.3125f:.5625f,.1875f,.4375f},{s->alternate?.4375f:.6875f,.5f,.5625f}},s->facing));break;
        case Kind::Torch:
            add(orient({{.4375f,0,.4375f},{.5625f,.625f,.5625f}},s->facing));break;
        case Kind::Repeater:case Kind::Comparator: {
            add({{0,0,0},{1,.125f,1}});
            auto rotate=[&](BlockCollisionBox b) {
                // Horizontal rotation from North, preserving plate height.
                const auto turn=s->facing==Face::North?0:s->facing==Face::East?1:s->facing==Face::South?2:3;
                for(int i=0;i<turn;++i) {const auto a=b.min,c=b.max;b.min={1-c.z,a.y,a.x};b.max={1-a.z,c.y,c.x};}
                return b;
            };
            add(rotate({{.4375f,.125f,.1875f},{.5625f,s->kind==Kind::Comparator?(s->alternate?.4375f:.3125f):.375f,.3125f}}));
            const float z=s->kind==Kind::Repeater?.375f+float(s->delay-1)*.125f:.6875f;
            if(s->kind==Kind::Repeater)add(rotate({{s->locked?.25f:.4375f,.125f,z},{s->locked?.75f:.5625f,.375f,z+.125f}}));
            if(s->kind==Kind::Comparator) {
                add(rotate({{.1875f,.125f,.6875f},{.3125f,.375f,.8125f}}));
                add(rotate({{.6875f,.125f,.6875f},{.8125f,.375f,.8125f}}));
            }
            break;
        }
        case Kind::Piston:
            add(orient({{0,0,0},{1,s->power?.75f:1.f,1}},s->facing));
            if(s->power)add(orient({{.375f,.75f,.375f},{.625f,1,.625f}},s->facing));
            break;
        default:add({{0,0,0},{1,1,1}});break;
    }
    return boxes;
}
inline bool litOre(BlockId id) {
    return native(id) && ore(nativeSlot(id)) && unsigned(nativeSlot(id))>=unsigned(NativeBlock::REDSTONE_ORE_LIT);
}
inline bool conductive(BlockId id) {
    if(id==BlockId::CHEST || id==BlockId::BREWING_STAND || id==BlockId::BLAZE_SPAWNER || id==BlockId::MAGMA_SPAWNER)return false;
    if(const auto s=blockState(id))return s->kind==Kind::Lamp;
    if(native(id))return ore(nativeSlot(id));
    return isFullCollisionBlock(id)&&getBlockProps(id).layer==RenderLayer::Opaque;
}
} // namespace Redstone
