#include "core/Window.h"
#include "player/PlayerRenderer.h"
#include "renderer/HeldItemRenderer.h"
#include "renderer/backend/vulkan/VulkanRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// Opt-in display-dependent scene harness. Capture mode pauses after each scene
// so an external screen grab can inspect actual Vulkan output.
int main(int argc,char** argv) {
    if(argc<2 || argc>5)return 2;
    try {
        bool capture=false,portrait=false,fishingOnly=false,droppedOnly=false;
        for(int i=2;i<argc;++i) {
            if(std::string(argv[i])=="--capture")capture=true;
            else if(std::string(argv[i])=="--portrait")portrait=true;
            else if(std::string(argv[i])=="--fishing-only")fishingOnly=true;
            else if(std::string(argv[i])=="--dropped-only")droppedOnly=true;
            else return 2;
        }
        const int width=portrait ? 480 : 960;
        const float aspect=static_cast<float>(width)/720;
        const auto root=std::filesystem::absolute(argv[1]);
        Window window(width,720,"MinecraftC held tool smoke",Window::SurfaceMode::Vulkan,false,false);
        VulkanRenderer renderer;renderer.initialize(window,root);
        renderer.setVisualQuality(VisualQuality::Low);renderer.setEnhancedVisuals(false);
        HeldItemRenderer held;held.initialize(renderer,root);
        PlayerRenderer player;player.initialize(root,renderer);
        const glm::mat4 projection=glm::perspective(glm::radians(55.0f),aspect,.05f,20.0f);
        if (droppedOnly) {
            const std::vector<ItemId> samples={ItemId::GRASS_BLOCK,ItemId::OAK_LOG,
                ItemId::GLASS,ItemId::OAK_LEAVES,ItemId::FLOWER,ItemId::BREAD,
                ItemId::WOODEN_PICKAXE,ItemId::STONE_AXE,ItemId::IRON_SHOVEL,
                ItemId::GOLDEN_HOE,ItemId::DIAMOND_SWORD,ItemId::SHIELD,
                ItemId::BOW,ItemId::FISHING_ROD,ItemId::STARSTEP_SCEPTER,
                ItemId::FLINT_AND_STEEL,ItemId::DIAMOND,ItemId::COAL,
                ItemId::RAW_COD,ItemId::RAW_SALMON,ItemId::COOKED_COD,
                ItemId::COOKED_SALMON,ItemId::WHEAT,ItemId::STICK};
            const glm::mat4 vp=projection*glm::lookAt(glm::vec3(0,1.1f,portrait ? 6.0f : 3.4f),
                glm::vec3(0,1.1f,0),glm::vec3(0,1,0));
            uint32_t expected=0;
            for (int angle=0;angle<3;++angle) {
                for (int frame=0;frame<12;++frame) {
                    held.updateUseState(angle==1,1,angle==1,1);
                    FrameData data;data.clearColor={.16f,.21f,.28f,1};renderer.beginFrame(data);
                    for (size_t i=0;i<samples.size();++i) {
                        const glm::vec3 p((static_cast<int>(i)%6-2.5f)*.65f,
                            (3-static_cast<int>(i)/6)*.65f,0);
                        held.renderDropped({samples[i],1,0},vp,p,0,angle*157u,{1,0});
                    }
                    renderer.endFrame();
                    const uint32_t draws=renderer.performanceStats().drawCalls;
                    if (frame>2 && expected && draws!=expected)
                        throw std::runtime_error("dropped draw count changed with player use state");
                    expected=draws;
                }
                renderer.waitIdle();std::cout<<"READY dropped-"<<angle<<std::endl;
                if(capture){std::string line;std::getline(std::cin,line);}
            }
            held.reset();renderer.waitIdle();std::cout<<"PASS dropped item Vulkan scenes"<<std::endl;
            return 0;
        }
        const auto scene=[&](std::string name,ItemId id,bool third,bool front,
                             bool charging,float charge,bool shield,bool blocking,float swing=1.0f,float pitch=0.0f) {
            uint32_t previous=0;
            for(int frame=0;frame<12;++frame) {
                const ItemStack item{id,1,0},offhand{shield ? ItemId::SHIELD : ItemId::EMPTY,
                                                   static_cast<uint8_t>(shield ? 1 : 0),0};
                PlayerVisualState visual;visual.grounded=true;visual.swingProgress=swing;
                visual.bowCharging=charging;visual.bowCharge=charge;visual.blocking=blocking;
                if(swing<1)visual.swingSequence=1;
                player.update(visual,1.0f/60);
                held.updateUseState(charging,charge,blocking,1.0f/60);
                FrameData frameData;frameData.clearColor={.16f,.21f,.28f,1};
                renderer.beginFrame(frameData);
                if(third) {
                    const auto view=glm::lookAt(glm::vec3(front ? 1.9f : -1.9f,1.35f,
                                                         front ? 3.0f : -3.0f),
                                                glm::vec3(0,.9f,0),glm::vec3(0,1,0));
                    const glm::mat4 vp=projection*view;
                    const auto hands=player.renderThirdPerson(renderer,{0,0,0},{0,0,0},0,pitch,vp,{1,0});
                    held.renderThirdPerson(item,vp,hands.right,offhand,hands.left);
                    if(id==ItemId::FISHING_ROD) {
                        FishingView fishing;fishing.phase=FishingPhase::Waiting;fishing.position={0,.7,2};
                        held.renderFishing(fishing,{0,0,0},HeldItemRenderer::thirdPersonFishingTip(hands.right),vp);
                    }
                } else {
                    if(id==ItemId::FISHING_ROD) {
                        const glm::mat4 vp=projection;
                        FishingView fishing;fishing.phase=FishingPhase::Waiting;fishing.position={.1,-.4,-3};
                        held.renderFishing(fishing,{0,0,0},held.firstPersonFishingTip(swing,1,aspect,glm::mat4(1),vp),vp);
                    }
                    held.renderFirstPerson(item,offhand,swing,1,aspect,glm::mat4(1));
                }
                renderer.endFrame();
                const uint32_t count=renderer.performanceStats().drawCalls;
                if(frame>2 && count!=previous)throw std::runtime_error("draw count changed across fixed scene");
                previous=count;
            }
            renderer.waitIdle();
            std::cout<<"READY "<<name<<std::endl;
            if(capture){std::string line;std::getline(std::cin,line);}
        };
        scene("fishing-first",ItemId::FISHING_ROD,false,false,false,0,false,false);
        scene("fishing-reel",ItemId::FISHING_ROD,false,false,false,0,false,false,.4f);
        scene("fishing-third-rear",ItemId::FISHING_ROD,true,false,false,0,false,false);
        scene("fishing-third-front",ItemId::FISHING_ROD,true,true,false,0,false,false);
        if(fishingOnly) { held.reset();renderer.waitIdle();std::cout<<"PASS fishing Vulkan scenes"<<std::endl;return 0; }
        for(int i=static_cast<int>(ItemId::WOODEN_PICKAXE);i<=static_cast<int>(ItemId::DIAMOND_SWORD);++i)
            scene("first-"+itemCommandName(static_cast<ItemId>(i)),static_cast<ItemId>(i),false,false,false,0,false,false);
        for(const auto id:{ItemId::FLINT_AND_STEEL,ItemId::STARSTEP_SCEPTER,ItemId::SHIELD})
            scene("first-"+itemCommandName(id),id,false,false,false,0,false,false);
        scene("first-bow-idle",ItemId::BOW,false,false,false,0,false,false);
        for(float charge:{0.0f,.5f,1.0f})
            scene("first-bow-"+std::to_string(charge),ItemId::BOW,false,false,true,charge,false,false);
        scene("first-bow-with-shield",ItemId::BOW,false,false,true,1,true,false);
        scene("first-bow-cancelled",ItemId::BOW,false,false,false,0,false,false);
        scene("first-shield-idle",ItemId::IRON_SWORD,false,false,false,0,true,false);
        scene("first-shield-raised",ItemId::IRON_SWORD,false,false,false,0,true,true);
        scene("first-shield-only",ItemId::EMPTY,false,false,false,0,true,true);
        scene("first-shield-removed",ItemId::IRON_SWORD,false,false,false,0,false,false);
        scene("first-shield-lowered",ItemId::IRON_SWORD,false,false,false,0,true,false);
        scene("first-sword-swing",ItemId::DIAMOND_SWORD,false,false,false,0,false,false,.5f);
        scene("first-block",ItemId::GRASS_BLOCK,false,false,false,0,false,false);
        scene("first-food",ItemId::BREAD,false,false,false,0,false,false);
        for(bool front:{false,true}) {
            const std::string prefix=front ? "front-" : "back-";
            for(const auto id:{ItemId::DIAMOND_SWORD,ItemId::IRON_PICKAXE,ItemId::STARSTEP_SCEPTER})
                scene(prefix+itemCommandName(id),id,true,front,false,0,false,false);
            scene(prefix+"bow-idle",ItemId::BOW,true,front,false,0,false,false);
            scene(prefix+"bow-half",ItemId::BOW,true,front,true,.5f,false,false);
            scene(prefix+"bow-full",ItemId::BOW,true,front,true,1,false,false);
            scene(prefix+"bow-up",ItemId::BOW,true,front,true,1,false,false,1,45);
            scene(prefix+"bow-down",ItemId::BOW,true,front,true,1,false,false,1,-45);
            scene(prefix+"bow-with-shield",ItemId::BOW,true,front,true,1,true,false);
            scene(prefix+"bow-cancelled",ItemId::BOW,true,front,false,0,false,false);
            scene(prefix+"shield-idle",ItemId::IRON_SWORD,true,front,false,0,true,false);
            scene(prefix+"shield-raised",ItemId::IRON_SWORD,true,front,false,0,true,true);
            scene(prefix+"shield-lowered",ItemId::IRON_SWORD,true,front,false,0,true,false);
        }
        held.reset();held.initialize(renderer,root);
        scene("reinitialized-model-cache",ItemId::DIAMOND_SWORD,false,false,false,0,false,false);
        held.reset();renderer.waitIdle();
        std::cout<<"PASS held tool Vulkan scenes"<<std::endl;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
