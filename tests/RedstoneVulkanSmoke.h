#pragma once
#include "entity/EntityModelRegistry.h"
#include "world/PistonMotion.h"
#include "world/World.h"
#include "world/ChunkMeshBuilder.h"
#include "world/RedstoneBlocks.h"

inline void redstoneVulkanSmoke(VulkanRenderer& renderer,const std::filesystem::path& assets,int width,int height) {
    // Existing GPU scenes do not draw signal states or the new moving-block
    // path. Stable controls isolate each changed material/model/translation.
    HeldItemRenderer held;held.initialize(renderer,assets);
    const glm::vec3 camera{3,3,5};
    const auto vp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,30.f)*glm::lookAt(camera,glm::vec3(.5),glm::vec3(0,1,0));
    RenderEnvironment environment;
    const auto capture=[&](const auto& draw) {
        for(int frame=0;frame<4;++frame) {
            if(frame==3)VulkanDiagnostics::requestCapture(renderer);
            renderer.beginFrame();renderer.setEnvironment(environment,camera);draw();renderer.finishScene({});renderer.endFrame();
        }
        renderer.waitIdle();return VulkanDiagnostics::readCapture(renderer);
    };
    const PixelReadback::Region region{width/8,height/8,width*7/8,height*7/8};
    const auto block=[&](BlockId id,glm::vec3 p=glm::vec3(0)){return capture([&]{held.renderMovingBlock(id,vp,p,{1,0});});};
    Redstone::State dust;dust.power=0;const auto off=block(Redstone::block(dust)),offAgain=block(Redstone::block(dust));
    dust.power=15;PixelReadback::requireEffect(off,offAgain,block(Redstone::block(dust)),width,height,region,"wire strength material");
    Redstone::State repeater;repeater.kind=Redstone::Kind::Repeater;
    const auto shortDelay=block(Redstone::block(repeater)),shortAgain=block(Redstone::block(repeater));
    repeater.delay=4;PixelReadback::requireEffect(shortDelay,shortAgain,block(Redstone::block(repeater)),width,height,region,"repeater delay geometry");
    const int oldDistance=Config::RENDER_DISTANCE;
    struct DistanceRestore {int value;~DistanceRestore(){Config::RENDER_DISTANCE=value;}} restore{oldDistance};
    Config::RENDER_DISTANCE=1;
    World world;
    for(int x=0;x<2;++x) {
        auto* chunk=world.getChunk(x,0);chunk->generated=true;chunk->lifecycle=Chunk::LifecycleState::Renderable;
        for(int bx=0;bx<16;++bx)for(int bz=0;bz<16;++bz)chunk->setBlock(bx,0,bz,BlockId::STONE);
    }
    world.update({15.5,1,8.5},0);
    Redstone::State piston;piston.kind=Redstone::Kind::Piston;piston.facing=Redstone::Face::East;
    Redstone::State constant;constant.kind=Redstone::Kind::Block;constant.power=15;
    world.setBlock(15,1,8,Redstone::block(piston));world.setBlock(16,1,8,BlockId::STONE);
    world.queueAtomicChunkMesh(0,0);world.queueAtomicChunkMesh(1,0);world.processCompletedMeshes(&renderer);
    const glm::vec3 sceneCamera{18.5f,4.5f,13.5f};
    const auto sceneVp=glm::perspective(glm::radians(60.f),float(width)/height,.1f,40.f)*glm::lookAt(sceneCamera,glm::vec3(16,1,8.5f),glm::vec3(0,1,0));
    const auto circuitImage=[&](bool movingVisible) {
        for(int frame=0;frame<4;++frame) {
            if(frame==3)VulkanDiagnostics::requestCapture(renderer);
            renderer.beginFrame();renderer.setEnvironment(environment,sceneCamera);renderer.setViewProjection(sceneVp);
            for(int cx=0;cx<2;++cx) {
                const auto& mesh=world.getChunk(cx,0)->getMesh();
                renderer.renderChunk(mesh,glm::translate(glm::mat4(1),glm::vec3(cx*16,0,0)),sceneVp,false);
            }
            if(movingVisible)for(const auto& motion:world.pistonMotions()) {
                const auto direction=glm::vec3(Redstone::vector(Redstone::offset(motion.extending?motion.piston.facing:Redstone::opposite(motion.piston.facing))));
                const float progress=motion.age*.5f;
                for(const auto& move:motion.moves)held.renderMovingBlock(static_cast<BlockId>(move.block),sceneVp,glm::vec3(Redstone::vector(move.from))+direction*progress,{1,0});
                held.renderMovingBlock(motion.headBlock(),sceneVp,glm::vec3(Redstone::vector(motion.extending?motion.base:motion.head()))+direction*progress,{1,0});
            }
            renderer.finishScene({});renderer.endFrame();
        }
        renderer.waitIdle();return VulkanDiagnostics::readCapture(renderer);
    };
    world.setBlock(15,2,8,Redstone::block(constant));world.tickInteractiveBlocks([](const auto&){return false;});
    world.processCompletedMeshes(&renderer);
    if(world.pistonMotions().size()!=1 || !world.getChunk(0,0)->getMesh().gpuReady || !world.getChunk(1,0)->getMesh().gpuReady)throw std::runtime_error("World piston did not publish both chunk meshes");
    const auto referenceMeshes=[&] {
        for(int cx=0;cx<2;++cx) {
            auto* chunk=world.getChunk(cx,0);ChunkMesh reference;
            reference.build(chunk->worldX(),chunk->worldZ(),chunk->rawBlocks(),chunk->getColumnMaxYData(),
                [&](int x,int y,int z){return world.getBlock(x,y,z);},
                [&](int x,int y,int z){return world.getLight(x,y,z);});
            chunk->getMesh().adoptCpuGeometry(reference);renderer.uploadChunkMesh(chunk->getMesh());
        }
    };
    const auto same=[&](const auto& actual,const auto& expected,const char* message) {
        if(actual.size()!=expected.size() || actual.size()!=size_t(width)*height*4)throw std::runtime_error(message);
        for(size_t pixel=0;pixel<actual.size()/4;++pixel)if(PixelReadback::differs(actual,expected,pixel,4))throw std::runtime_error(message);
    };
    const auto absent=circuitImage(false),absentAgain=circuitImage(false),movingStart=circuitImage(true),movingStartAgain=circuitImage(true);
    // An independent public CPU mesh build detects stale source geometry;
    // merely seeing a moving head would not catch a duplicate static stone.
    referenceMeshes();same(absent,circuitImage(false),"Atomic mesh left a stale source block");
    PixelReadback::requireEffect(absent,absentAgain,movingStart,width,height,{0,0,width,height},"World moving blocks over updated static chunks");
    world.tickInteractiveBlocks([](const auto&){return false;});world.processCompletedMeshes(&renderer);
    const auto movingHalf=circuitImage(true);
    PixelReadback::requireEffect(movingStart,movingStartAgain,movingHalf,width,height,{0,0,width,height},"World piston half-step");
    world.tickInteractiveBlocks([](const auto&){return false;});world.processCompletedMeshes(&renderer);
    if(!world.pistonMotions().empty() || world.getBlock(17,1,8)!=BlockId::STONE)throw std::runtime_error("World piston failed final commit");
    const auto final=circuitImage(false),finalAgain=circuitImage(false);
    PixelReadback::requireEffect(absent,absentAgain,final,width,height,{0,0,width,height},"World final blocks without moving overlay");
    same(final,finalAgain,"Unstable final piston scene");
    referenceMeshes();same(final,circuitImage(false),"Atomic mesh missed a cross-chunk destination");
    EntityModelRegistry registry;registry.loadAll(assets);
    const auto& definition=registry.definition(EntityType::Slime);
    if(definition.usesPlaceholder)throw std::runtime_error("Slime GPU scene used placeholder");
    const auto slime=renderer.modelRenderer().upload(definition.asset);
    const auto blank=capture([]{}),blankAgain=capture([]{});
    const auto visible=capture([&]{
        model::ModelDraw draw;draw.model=slime;draw.light={1,0};draw.transform=glm::translate(glm::mat4(1),glm::vec3(.5f,0,.5f));
        renderer.modelRenderer().queue(draw);
        renderer.modelRenderer().flushOpaque(vp,environment,camera,20,30);
        renderer.modelRenderer().flushBlend(vp,environment,camera,20,30);
    });
    PixelReadback::requireEffect(blank,blankAgain,visible,width,height,region,"slime gel and core model");
    held.reset();
    std::cout<<"Redstone signals, piston motion and slime Vulkan readback passed\n";
}
