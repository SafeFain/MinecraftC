#pragma once
#include "plugins/PluginManager.h"
#include "network/ChunkProtocol.h"
#include "entity/GroundNavigation.h"
namespace BlockShapesIntegration {
inline void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline int run(const std::filesystem::path& assets,const std::filesystem::path& module) {
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-block-shapes";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root/"mods");
    const auto source=assets.parent_path()/"examples"/"plugins";
    std::filesystem::copy(source/"block_shapes_data",root/"mods"/"block_shapes_data",std::filesystem::copy_options::recursive);
    std::filesystem::copy(source/"block_shapes_demo",root/"mods"/"block_shapes_demo",std::filesystem::copy_options::recursive);
    const auto nativeName=Platform::DynamicLibrary::supported()?module.filename().string():std::string{};
    if(!nativeName.empty())std::filesystem::copy_file(module,root/"mods"/"block_shapes_demo"/nativeName);
    // The CMake module name is identical to the installed package entry.
    auto package=[&](const std::string& id,const std::string& data,bool visual=false) {
        const auto path=root/"mods"/id;std::filesystem::create_directories(path);
        std::ofstream(path/"mod.json")<<"{\"id\":\""<<id<<"\",\"api\":1,\"version\":\"1.0.0\",\"data\":\"content.json\",\"visual_only\":"<<(visual?"true":"false")<<"}";
        std::ofstream(path/"content.json")<<data;
    };
    package("shape_bad",R"({"blocks":[{"key":"shape_bad:first","name":"Rollback","materials":"stone"}],"block_geometry":[{"block":"shape_bad:first","collision":[[0,0,0,2,1,1]]}]})");
    package("shape_visual",R"({"block_families":[{"key":"shape_visual:slab","name":"Forbidden","kind":"slab","materials":"stone","full_block":"minecraftc:stone"}]})",true);
    package("shape_missing",R"({"block_families":[{"key":"shape_missing:slab","name":"Missing base","kind":"slab","materials":"stone","full_block":"shape_missing:unknown"}]})");
    Plugins::PluginManager plugins({assets,root});plugins.initialize();
    size_t index=0;for(size_t i=0;i<plugins.plugins().size();++i) {
        const auto& p=plugins.plugins()[i];if(p.manifest.id=="block_shapes_data")index=i;
        if(p.manifest.id.rfind("shape_",0)==0)check(!p.active,"invalid shape package accepted");
        else if(p.manifest.id=="block_shapes_demo"||p.manifest.id=="block_shapes_data")check(p.active,p.status.c_str());
    }
    bool rejected=false;try{Plugins::resolveBlock("shape_bad:first");}catch(const std::exception&){rejected=true;}check(rejected,"geometry registration did not roll back");
    MC_PluginContext ctx;ctx.manager=&plugins;ctx.index=index;const auto* api=static_cast<const MC_BlockShapesV1*>(plugins.extension(&ctx,MC_BLOCK_SHAPES_EXTENSION,1));
    check(api&&api->version==1&&!plugins.extension(&ctx,MC_BLOCK_SHAPES_EXTENSION,2),"shape extension negotiation");
    for(const auto& entry:Plugins::content().blocks)if(entry.second.family) {
        MC_BlockState state{};state.size=sizeof(state);uint16_t output=0;
        check(!api->state(&ctx,entry.first,&state)&&!api->compose(&ctx,&state,&output)&&output==entry.first,"template state roundtrip");
        check(itemForBlock(static_cast<BlockId>(entry.first))!=ItemId::EMPTY,"template state lost drop");
        check(Plugins::content().networkDescription.count("shape/"+entry.second.key),"shape definition absent from handshake");
    }
    const auto slab=Plugins::resolveBlock("block_shapes_data:slab"),stair=Plugins::resolveBlock("block_shapes_data:stair"),door=Plugins::resolveBlock("block_shapes_data:door"),button=Plugins::resolveBlock("block_shapes_data:button"),box=Plugins::resolveBlock("block_shapes_data:box");
    ArchitecturalBlockState a;check(decodeArchitecturalBlock(slab,a),"slab decoding");a.half=BlockHalf::Top;
    check(blockCollisionBoxes(architecturalStateBlock(a)).boxes[0].min.y==.5f&&architecturalFullBlock(a)==BlockId::STONE,"slab collision and merging target");
    check(decodeArchitecturalBlock(stair,a),"stair decoding");a.half=BlockHalf::Top;a.direction=BedDirection::West;
    check(blockCollisionBoxes(architecturalStateBlock(a)).count==2&&!pointInsideBlockCollision(architecturalStateBlock(a),{.75f,.25f,.5f}),"oriented stair empty space");
    check(getLightDampening(button)==0&&getLightDampening(door)==0,"partial templates block all light");
    check(blockCollisionBoxes(box).count==1&&blockCollisionHeight(box)==.5f&&blockSelectionBoxes(box).boxes[0].max.y==1,"independent geometry groups");
    check(blockCollisionBoxes(Plugins::resolveBlock("block_shapes_data:empty")).count==0,"explicit empty collision ignored");
    check(blockSupportsFace(Plugins::resolveBlock("block_shapes_data:union_support"),FaceDir::TOP)&&isFullCollisionBlock(Plugins::resolveBlock("block_shapes_data:union_support")),"full collision union lost cube support/power classification");
    DoorState navigationDoor;decodeDoor(Plugins::resolveBlock("block_shapes_data:locked_door"),navigationDoor);navigationDoor.direction=BedDirection::East;
    GroundNavigation::Terrain terrain;terrain.loaded=[](int,int){return true;};
    terrain.block=[&](int x,int y,int z){if(y==0)return BlockId::STONE;if(x==1&&z==0&&(y==1||y==2)){auto d=navigationDoor;d.upper=y==2;return doorBlock(d);}return BlockId::AIR;};
    check(!GroundNavigation::clear(terrain,{.6f,1.8f,.6f},{1.9,1,.5}),"closed plugin door does not collide with mobs");
    terrain.canOpenWoodDoors=true;check(GroundNavigation::clear(terrain,{.6f,1.8f,.6f},{1.9,1,.5}),"villager cannot navigate configured plugin door");
    uint32_t count=0;check(!api->boxes(&ctx,static_cast<uint16_t>(box),2,nullptr,0,&count)&&count==2,"model box count query");MC_BlockBox buffer[16]{};
    check(api->boxes(&ctx,static_cast<uint16_t>(box),2,buffer,1,&count)!=0,"undersized box output accepted");
    MC_BlockGeometry late{};late.size=sizeof(late);late.block="block_shapes_data:box";check(api->register_geometry(&ctx,&late)!=0,"frozen shape registry changed");ctx.registrationFailed=false;
    MC_BlockState invalid{};invalid.size=sizeof(invalid);invalid.family=static_cast<uint16_t>(slab);invalid.kind=MC_SLAB_FAMILY;invalid.direction=1;uint16_t ignored;
    check(api->compose(&ctx,&invalid,&ignored)!=0,"invalid slab property accepted");
    // Overlapping model boxes must emit only the union's exterior, in the correct layer.
    Chunk chunk(-1,-1);chunk.setBlock(15,211,15,box);int maxima[16][16];chunk.copyColumnMaxY(maxima);ChunkMesh mesh;
    mesh.build(-16,-16,chunk.rawBlocks(),maxima,[](int,int,int){return BlockId::AIR;},[](int,int,int){return LightSample{15,0};});
    check(mesh.opaqueIndexCount==0&&mesh.translucentIndexCount>0&&mesh.shadowCasterIndexCount==0,"box model layer handoff");
    double area=0;for(size_t i=0;i<mesh.vertices.size();i+=4) {
        const auto& p=mesh.vertices[i];const auto& q=mesh.vertices[i+1];const auto& r=mesh.vertices[i+3];
        area+=glm::length(glm::cross(glm::dvec3(q.px-p.px,q.py-p.py,q.pz-p.pz),glm::dvec3(r.px-p.px,r.py-p.py,r.pz-p.pz)));
        check(p.alpha==.5f,"model alpha lost");
    }
    check(std::abs(area-6)<.0001,"box union contains internal or duplicate faces");
    auto altered=Plugins::content().networkDescription;const auto signature=Lan::contentSignature(altered);
    altered["shape/block_shapes_data:button"]="different release policy";check(Lan::contentSignature(altered)!=signature,"behavior mismatch leaves handshake unchanged");
    // Namespaced palettes and canonical wire IDs survive a different local allocation order.
    const auto original=Plugins::content();
    WorldMetadata metadata;metadata.displayName="Shape palette";metadata.generationVersion=WorldGenContext::GENERATION_VERSION;
    SaveStore palette(root/"palette");palette.saveMetadata(metadata);palette.saveChunkOverrides(-1,-1,{{0,architecturalStateBlock(a)}});
    std::vector<uint16_t> raw;chunk.copyRawBlocks(raw);const auto wire=Lan::encodeChunkSnapshot({{DimensionId::Overworld,-1,-1},1,1,raw});
    auto blocks=Plugins::content().blocks;Plugins::content().blocks.clear();
    for(auto& entry:blocks){const auto id=static_cast<uint16_t>(entry.first+500);entry.second.properties.id=static_cast<BlockId>(id);if(entry.second.family)entry.second.family+=500;Plugins::content().blocks.emplace(id,std::move(entry.second));}
    for(auto& entry:Plugins::content().items)if(entry.second.properties.placedBlock&&static_cast<uint16_t>(*entry.second.properties.placedBlock)>=Plugins::FIRST_CONTENT_ID)entry.second.properties.placedBlock=static_cast<BlockId>(static_cast<uint16_t>(*entry.second.properties.placedBlock)+500);
    Plugins::freezeContent();
    check(static_cast<uint16_t>(palette.loadChunkOverrides(-1,-1)[0].block)==static_cast<uint16_t>(architecturalStateBlock(a))+500,"save palette state remapping");
    const auto decoded=Lan::decodeChunkSnapshot(wire);check(decoded.blocks[(211-Config::WORLD_MIN_Y)*256+15+15*16]==static_cast<uint16_t>(box)+500,"wire state remapping");
    MC_BlockState remapped{};remapped.size=sizeof(remapped);check(!api->state(&ctx,static_cast<uint16_t>(stair)+507,&remapped)&&remapped.family==static_cast<uint16_t>(stair)+500,"remapped state family identity");
    const auto revision=Plugins::content().revision;Plugins::content()=original;Plugins::content().revision=revision;Plugins::freezeContent();
    const int oldDistance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=1;
    {
        GameSession session(root/"saves");EntityAiTestRenderer renderer;session.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        const auto path=session.createWorld("Shapes",42,GameMode::Creative,Difficulty::Normal,true,WorldType::Superflat);session.startWorld(path,true,0);
        RuntimeClock clock;bool loaded=false;for(int i=0;i<4000;++i){if(session.advanceLoading(&renderer,clock.now())){loaded=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}check(loaded,"shape world loading");
        auto& world=GameSessionTestAccess::world(session);
        for(int x=0;x<=3;++x)for(int z=0;z<=3;++z)for(int y=210;y<=213;++y)world.setBlock(x,y,z,y==210?BlockId::STONE:BlockId::AIR);
        DoorState d;decodeDoor(door,d);check(world.placeDoor({0,211,1},d)&&world.setDoorOpen({0,212,1},true),"plugin paired door use");
        DoorState upper;check(decodeDoor(world.getBlock(0,212,1),upper)&&upper.family==d.family&&upper.upper&&upper.open,"paired update changed family");
        world.setDoorOpen({0,211,1},false);world.setBlock(1,211,1,BlockId::STONE);
        const auto empty=Plugins::resolveBlock("block_shapes_data:empty");world.setBlock(2,211,0,empty);
        const auto hit=world.raycast({2.5,211.5,-1.5},{0,0,1},4);check(hit&&hit->blockPos==glm::ivec3(2,211,0),"non-colliding selection boxes not selectable");
        ButtonState b;decodeButton(button,b);b.attachment=FaceDir::BACK;
        check(world.placeButton({1,211,2},b)&&world.activateButton({1,211,2}),"plugin button placement/press");
        check(decodeDoor(world.getBlock(0,211,1),d)&&d.powered&&d.open,"plugin button power ignored");
        for(int tick=0;tick<4;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        check(decodeButton(world.getBlock(1,211,2),b)&&b.pressed,"configured duration released early");
        world.tickInteractiveBlocks([](const auto&){return false;});check(decodeDoor(world.getBlock(0,211,1),d)&&!d.open&&!d.powered,"configured button release");
        DoorState locked;decodeDoor(Plugins::resolveBlock("block_shapes_data:locked_door"),locked);
        check(world.placeDoor({3,211,1},locked)&&!world.setDoorOpen({3,211,1},true)&&world.setDoorOpen({3,211,1},true,true),"independent manual/villager permissions");
        ButtonState slow;decodeButton(Plugins::resolveBlock("block_shapes_data:slow_button"),slow);slow.attachment=FaceDir::TOP;
        check(!buttonArrowTrigger(slow)&&buttonReleaseTicks(slow)==80&&world.placeButton({2,211,2},slow)&&world.activateButton({2,211,2}),"configurable long arrow-disabled button");
        plugins.blockInteraction=[&](uint32_t dim,int32_t x,int32_t y,int32_t z,bool press,bool open){return session.pluginBlockInteraction(dim,x,y,z,press,open);};
        check(!api->set_door_open(&ctx,0,0,211,1,1)&&decodeDoor(world.getBlock(0,211,1),d)&&!d.open,"SDK operation was not queued");plugins.flushOperations();
        check(decodeDoor(world.getBlock(0,211,1),d)&&d.open,"queued SDK door operation");
        {Plugins::ActorScope actor({1,0,MC_CLIENT});check(api->press_button(&ctx,0,1,211,2)!=0,"client state mutation accepted");}
        check(!api->press_button(&ctx,0,1,211,2),"SDK button operation");plugins.flushOperations();
        for(int tick=0;tick<2;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        bool failed=false;session.saveNow([&]{failed=true;});check(!failed,"shape state save");session.leaveWorld();session.startWorld(path,false,0);
        loaded=false;for(int i=0;i<4000;++i){if(session.advanceLoading(&renderer,clock.now())){loaded=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}check(loaded,"shape state reload");
        auto& reloaded=GameSessionTestAccess::world(session);check(decodeButton(reloaded.getBlock(1,211,2),b)&&b.pressed&&b.family==static_cast<uint16_t>(button),"saved pressed state lost identity");
        for(int tick=0;tick<2;++tick)reloaded.tickInteractiveBlocks([](const auto&){return false;});
        check(decodeButton(reloaded.getBlock(1,211,2),b)&&b.pressed,"saved remaining timer lost");
        reloaded.tickInteractiveBlocks([](const auto&){return false;});check(decodeButton(reloaded.getBlock(1,211,2),b)&&!b.pressed,"saved timer failed to release");
        check(decodeButton(reloaded.getBlock(2,211,2),slow)&&slow.pressed,"long saved timer was rejected or truncated");
        reloaded.setBlock(0,212,1,BlockId::AIR);check(reloaded.getBlock(0,211,1)==BlockId::AIR,"plugin door partner not removed");
        check(session.openLanRoom(0,2,true),"shape LAN host start");
        GameSession client(root/"client"/"saves");client.configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
        check(client.joinLanRoom("::1",session.lanPort(),0),"shape LAN client start");
        double now=0;auto pump=[&]{now+=.005;session.pollLan(now);client.pollLan(now);GameSessionTestAccess::world(client).processCompletedGenerations();std::this_thread::sleep_for(std::chrono::milliseconds(1));};
        for(int i=0;i<1000&&!client.lanWorldReady();++i)pump();
        check(client.lanWorldReady()&&!client.lanConnectionFailed(),"shape LAN registry handshake");
        check(!client.pluginBlockInteraction(0,1,211,2,true,true),"replica accepted state mutation");
        check(session.pluginBlockInteraction(0,1,211,2,true,true),"host button interaction");
        for(int i=0;i<1000;++i){pump();if(decodeButton(GameSessionTestAccess::world(client).getBlock(1,211,2),b)&&b.pressed)break;}
        check(decodeButton(GameSessionTestAccess::world(client).getBlock(1,211,2),b)&&b.pressed&&b.family==static_cast<uint16_t>(button),"LAN button state replication");
        client.leaveWorld();session.closeLanRoom();
        session.leaveWorld();
    }
    Config::RENDER_DISTANCE=oldDistance;plugins.blockInteraction={};plugins.shutdown();std::filesystem::remove_all(root);
    std::cout<<"Plugin block shape integration passed\n";return 0;
}
}
