#pragma once
#include "world/ChunkMesh.h"
#include "world/ChunkMeshBuilder.h"

inline int naturalTextureVulkanSmoke(Window& window,VulkanRenderer& renderer,
                                     const std::filesystem::path& assets,bool natural) {
    const int width=window.width(),height=window.height();
    UIRenderer ui;ui.initialize(renderer,renderer.getBlockAtlasTexture(),assets);
    HeldItemRenderer held;held.initialize(renderer,assets);
    std::vector<uint16_t> blocks(Config::CHUNK_VOLUME,0);
    int maxima[16][16]{};
    const std::array<BlockId,16> samples{{BlockId::GRASS,BlockId::DIRT,BlockId::STONE,BlockId::WOOD,
        BlockId::PLANKS,BlockId::COBBLESTONE,BlockId::IRON_ORE,BlockId::DIAMOND_ORE,
        BlockId::NETHERRACK,BlockId::BASALT,BlockId::SAND,BlockId::CRAFTING_TABLE,
        BlockId::LEAVES,BlockId::GLASS,BlockId::WATER,BlockId::LAVA}};
    auto put=[&](int x,int y,int z,BlockId id){blocks[x+z*16+Config::worldYToStorageY(y)*256]=uint16_t(id);maxima[x][z]=std::max(maxima[x][z],y);};
    for(int z=0;z<16;++z)for(int x=0;x<16;++x)put(x,0,z,BlockId::STONE);
    for(size_t i=0;i<samples.size();++i)for(int z=0;z<2;++z)for(int x=0;x<2;++x)
        put(1+int(i%4)*4+x,1,1+int(i/4)*4+z,samples[i]);
    ChunkMesh mesh;mesh.build(0,0,blocks.data(),maxima,[](int,int,int){return BlockId::AIR;},[](int,int,int){return LightSample{15,0};});
    if(!mesh.opaqueIndexCount||!mesh.translucentIndexCount||mesh.indexCount!=mesh.indices.size()||mesh.translucentIndexOffset!=mesh.opaqueIndexCount)
        throw std::runtime_error("Natural scene lost index-layer metadata");
    renderer.uploadChunkMesh(mesh);
    const std::vector<ItemId> items{ItemId::GRASS_BLOCK,ItemId::OAK_LOG,ItemId::STONE,ItemId::OAK_PLANKS,
        ItemId::COBBLESTONE,ItemId::DIAMOND_ORE,ItemId::NETHERRACK,ItemId::OAK_LEAVES,ItemId::GLASS,
        ItemId::DIAMOND,ItemId::IRON_INGOT,ItemId::DIAMOND_SWORD,ItemId::BREAD,ItemId::WHEAT,
        ItemId::BOW,ItemId::FISHING_ROD,ItemId::STAR_CRYSTAL,ItemId::CLOUDSTONE};
    auto render=[&](int flags,float phase,bool distant){
        const glm::vec3 camera=distant?glm::vec3(29,24,38):glm::vec3(20,16,25);
        const auto vp=glm::perspective(glm::radians(55.f),window.aspectRatio(),.05f,128.f)*glm::lookAt(camera,glm::vec3(8,0,8),glm::vec3(0,1,0));
        DayNightCycle cycle;cycle.setPhase(phase);const auto env=cycle.evaluate();
        for(int frame=0;frame<5;++frame) {
            renderer.beginFrame();renderer.setEnvironment(env,camera);renderer.setViewProjection(vp);
            renderer.renderSky(env,glm::inverse(vp),camera,false);
            if(flags&1)renderer.renderChunk(mesh,glm::mat4(1),vp);
            if(flags&16){renderer.beginTranslucent();renderer.renderChunk(mesh,glm::mat4(1),vp,true);renderer.endTranslucent();}
            if(flags&2)for(size_t i=0;i<items.size();++i)
                held.renderDropped({items[i],1,0},vp,{2+float(i%6)*2.1f,3.0f,2+float(i/6)*3},.2f,uint32_t(i),{1,0});
            if(flags&4)held.renderFirstPerson({ItemId::DIAMOND_SWORD,1,0},{},0,1,window.aspectRatio(),glm::mat4(1));
            PostProcessState post;post.environment=env;post.inverseViewProjection=glm::inverse(vp);post.cameraPosition=camera;renderer.finishScene(post);
            if(flags&8){ui.beginUIFrame(width,height);ui.drawRect(16,16,float(width-32),110,{.08f,.09f,.10f,.94f});
                for(size_t i=0;i<items.size();++i)ui.drawItemIcon(24+float(i%18)*49,36,42,42,{items[i],1,0});ui.endUIFrame();}
            if(frame==4)VulkanGiSmokeProbe::requestCapture(renderer);
            renderer.endFrame();window.finishEventFrame();
        }
        renderer.waitIdle();auto pixels=VulkanGiSmokeProbe::readCapture(renderer);
        if(pixels.size()!=size_t(width)*height*4)throw std::runtime_error("Natural scene readback missing");
        return pixels;
    };
    const auto empty=render(0,.125f,false),opaque=render(1,.125f,false),terrain=render(17,.125f,false);
    if(empty==opaque)throw std::runtime_error("Natural terrain was not drawn");
    if(opaque==terrain)throw std::runtime_error("Natural translucent layer was not drawn");
    if(terrain==render(19,.125f,false))throw std::runtime_error("Natural dropped items were not drawn");
    if(terrain==render(21,.125f,false))throw std::runtime_error("Natural held tool was not drawn");
    auto save=[&](const std::string& name,const std::vector<uint8_t>& pixels){
        std::ofstream out(std::filesystem::temp_directory_path()/((natural?"natural-":"default-")+name+".ppm"),std::ios::binary);
        out<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(size_t i=0;i<pixels.size();i+=4)out.write(reinterpret_cast<const char*>(pixels.data()+i),3);
    };
    const auto day=render(31,.125f,false),night=render(31,.75f,false);
    if(day==night)throw std::runtime_error("Natural scene day/night identical");
    save("day",day);save("night",night);save("far",render(31,.125f,true));
    renderer.resize(width,height);renderer.suspendPresentation();renderer.resumePresentation();render(31,.125f,false);
    renderer.releaseChunkMesh(mesh);held.reset();
    std::cout<<"Natural textures Vulkan terrain/layers, cutout/fluid, UI, held/dropped and day/night readback passed\n";return 0;
}
