#pragma once

#include "app/GameSession.h"
#include "EntityAiTestRenderer.h"
#include "entity/EntityPresenter.h"
#include "game/Command.h"
#include "game/Localization.h"
#include "world/Chunk.h"
#include <cmath>
#include <chrono>
#include <map>
#include <thread>

// The fixture supplies real collaborators through the composition boundary.
// It never reaches into GameSession's flags, private maps or worker queues.
struct SessionScenarioContext : std::enable_shared_from_this<SessionScenarioContext> {
    ThreadPool workers{2};
    EntityAiTestRenderer renderer;
    Lan::Host host;
    Lan::Client client;
    std::map<DimensionId,DimensionSimulation*> dimensions;
    std::map<Player*,LanPlayerRuntime*> guests;
    std::filesystem::path saves;
    std::vector<std::unique_ptr<Lan::Client>> extraClients;
    std::optional<glm::dvec3> loadingOrigin;
    double networkTime=0;
    static inline std::map<const GameSession*,std::weak_ptr<SessionScenarioContext>> sessions;
    GameSessionServices services() {
        auto self=shared_from_this();GameSessionServices result;
        result.workers=&workers;result.host=&host;result.client=&client;
        result.fishingRandom=[]{return 0.0f;};
        result.dimension=[self](DimensionId id) {
            auto runtime=std::make_unique<DimensionSimulation>();self->dimensions[id]=runtime.get();return runtime;
        };
        result.guest=[self](World& world,EntityManager& entities,Lan::PlayerProfile profile) {
            auto runtime=std::make_unique<LanPlayerRuntime>(world,entities,std::move(profile));
            self->guests[&runtime->player]=runtime.get();return runtime;
        };
        return result;
    }
    static std::shared_ptr<SessionScenarioContext> get(const GameSession& session) {
        auto context=sessions.at(&session).lock();
        if(!context)throw std::runtime_error("session scenario collaborator lifetime ended");return context;
    }
};

class ScenarioSession final : public GameSession {
    ScenarioSession(const std::filesystem::path& saves,std::shared_ptr<SessionScenarioContext> context)
        :GameSession(saves,context->services()) {
        context->saves=saves;SessionScenarioContext::sessions[this]=context;
        configureLod({false,16,LodAggressiveness::PowerSaver,LodPrecision::Low});
    }
public:
    explicit ScenarioSession(const std::filesystem::path& saves)
        :ScenarioSession(saves,std::make_shared<SessionScenarioContext>()) {}
};

struct SessionScenario {
    static auto context(const GameSession& s) {return SessionScenarioContext::get(s);}
    static DimensionSimulation& runtime(GameSession& s,DimensionId id) {
        s.prepareDimension(id);return *context(s)->dimensions.at(id);
    }
    static World& world(GameSession& s) {return context(s)->dimensions.at(s.activeDimension())->world;}
    static Player& player(GameSession& s) {
        if(s.worldLoadingPending()&&s.hasWorldStore())complete(s);
        uint32_t dimension=0;auto* actor=s.pluginActor(s.joiningLan()?s.playerState().roomPlayerId():0,dimension);
        if(!actor)throw std::runtime_error("actor is not ready; finish the public loading gate first");return *actor;
    }
    static EntityManager& entities(GameSession& s) {return runtime(s,s.activeDimension()).entities;}
    static LanPlayerRuntime& guest(GameSession& s,uint64_t id) {
        uint32_t dimension=0;auto* actor=s.pluginActor(id,dimension);
        if(!actor)throw std::runtime_error("requested guest is not admitted");
        return *context(s)->guests.at(actor);
    }
    static void complete(GameSession& s) {
        if(!s.hasWorldStore()&&!s.joiningLan())return;
        if(!s.worldLoadingPending())return;
        const int distance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=0;
        world(s).setThreadPool(&context(s)->workers);
        world(s).update(s.playerState().getPosition(),16);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        RuntimeClock clock;bool ready=false;
        do {ready=s.advanceLoading(&context(s)->renderer,clock.now());
            if(!ready)std::this_thread::yield();
        }while(!ready&&std::chrono::steady_clock::now()<deadline);
        Config::RENDER_DISTANCE=distance;
        if(!ready){const auto snapshot=s.loadingSnapshot();
            throw std::runtime_error("scenario public loading gate timed out: dimension="+std::to_string(uint32_t(s.activeDimension()))+
                " phase="+std::to_string(int(snapshot.phase)));}
        context(s)->workers.waitIdle();
    }
    static GameSession::CommandResult command(GameSession& s,const std::string& text) {
        const auto parsed=parseCommand(text);if(!parsed.command)throw std::runtime_error("invalid scenario command: "+text);
        Localization localization;return s.executeCommand(*parsed.command,localization);
    }
    static void ensureWorld(GameSession& s,uint64_t seed=42,GameMode mode=GameMode::Survival) {
        if(!s.hasWorldStore()) {
            const auto id=s.createWorld("Scenario",seed,mode,Difficulty::Normal,true,WorldType::Superflat);
            s.startWorld(id,true,RuntimeClock{}.now());
        }
        complete(s);
    }
    template<class Change>static void metadata(GameSession& s,Change change) {
        ensureWorld(s);auto ctx=context(s);ctx->workers.waitIdle();
        std::map<DimensionId,std::vector<std::pair<glm::ivec2,std::vector<uint16_t>>>> scenes;
        for(const auto& pair:ctx->dimensions)for(const auto* chunk:pair.second->world.getSimulationChunks())if(chunk->generated) {
            std::vector<uint16_t> blocks;chunk->copyRawBlocks(blocks);scenes[pair.first].push_back({{chunk->cx,chunk->cz},std::move(blocks)});
        }
        bool failed=false;s.saveNow([&]{failed=true;});if(failed)throw std::runtime_error("scenario metadata checkpoint failed");
        const auto path=s.pluginWorldDirectory();s.leaveWorld();SaveStore store(path);
        auto value=store.loadMetadata();change(value);store.saveMetadata(value);
        s.startWorld(path.filename().string(),false,RuntimeClock{}.now());ctx->workers.waitIdle();
        for(const auto& dimension:scenes) {
            auto& model=runtime(s,dimension.first);ctx->workers.waitIdle();
            for(const auto& saved:dimension.second){auto* chunk=model.world.getChunk(saved.first.x,saved.first.y);
                chunk->loadRawBlocks(saved.second);chunk->generated=true;chunk->lifecycle=Chunk::LifecycleState::Renderable;}
        }
        complete(s);
    }
    static void setCheats(GameSession& s,bool enabled) {metadata(s,[&](auto& value){value.cheatsEnabled=enabled;});}
    static void setRules(GameSession& s,const GameRuleSet& rules) {
        for(const auto& definition:GAME_RULES)if(definition.id!=GameRuleId::DayNightDuration&&s.metadata().gameRules.get(definition.id)!=rules.get(definition.id))
            command(s,"/gamerule "+std::string(definition.name)+" "+gameRuleValueText(rules.get(definition.id)));
    }
    static void setDay(GameSession& s) {runtime(s,s.activeDimension()).daylight.setDay();}
    static void setNight(GameSession& s) {runtime(s,s.activeDimension()).daylight.setNight();}
    static void restTime(GameSession& s,bool night) {if(night)setNight(s);else setDay(s);}
    static void tickDaylight(GameSession& s,float dt) {s.updateDaylight(dt,true);}
    static void tickWeather(GameSession& s) {runtime(s,s.activeDimension()).weather.tick(s.metadata().gameRules.boolean(GameRuleId::AdvanceWeather));}
    static void tickLanPlayers(GameSession& s,float dt) {s.updatePlaying(dt,nullptr,{},false);}
    static void tickLanSleep(GameSession& s,float dt) {tickLanPlayers(s,dt);}
    static void waitLanWorkers(GameSession& s) {context(s)->workers.waitIdle();}
    static void processCompletedGenerations(GameSession& s) {world(s).processCompletedGenerations();}
    static bool backgroundWorkIdle(const GameSession& s) {return context(s)->workers.idle();}
    static bool terrainReady(const GameSession& s) {return !s.worldLoadingPending();}
    static bool newWorldLoading(const GameSession& s) {return s.loadingSnapshot().newWorld;}
    static bool hasDimensionStore(GameSession& s) {return runtime(s,s.activeDimension()).store!=nullptr;}
    static void markTerrainReady(GameSession& s) {complete(s);}
    static void netherReady(GameSession& s) {complete(s);}
    static void setPlayerPosition(GameSession& s,glm::dvec3 p) {player(s).setPosition(p);}
    static uint64_t replicaPeer(GameSession& s) {return s.playerState().roomPlayerId();}
    static void disconnectReplica(GameSession& s) {s.leaveWorld();}
    static size_t particleCount(const GameSession& s) {return s.particleState().size();}
    static void dimensionBed(GameSession& s,uint32_t id,std::optional<glm::ivec3> bed) {
        if(uint32_t(s.activeDimension())!=id)throw std::runtime_error("bed fixture needs its active dimension");
        if(!bed)return; // The caller removed the bed; normal respawn rejects the stale record.
        const auto phase=s.daylightState().phase();setNight(s);
        if(s.interactRestBlock(*bed)!=GameSession::RestInteraction::Sleeping)throw std::runtime_error("bed fixture could not register through sleep");
        s.cancelSleep({});s.updatePlaying(.4f,nullptr,{},false);runtime(s,s.activeDimension()).daylight.setPhase(phase);
    }
    static glm::dvec3 setHeavenSafePosition(GameSession& s,glm::ivec3 p) {
        auto& actor=player(s);actor.setPosition(glm::dvec3(p)+glm::dvec3(.5,.01,.5));actor.update(.05f);
        bool failed=false;s.saveNow([&]{failed=true;});
        if(failed||!s.metadata().heaven.hasSafePosition)throw std::runtime_error("grounded Heaven checkpoint failed");
        return actor.getPosition();
    }
    static std::optional<glm::dvec3> portalLanding(GameSession& s,DimensionId d,glm::dvec3 p) {return s.preparePortalLanding(d,p);}
    static bool tickPortals(GameSession& s,float dt) {return s.updatePortals(dt,{});}
    static void portalRule(GameSession& s,bool enabled) {command(s,std::string("/gamerule allow_entering_nether_using_portals ")+(enabled?"true":"false"));}
    static void restoreNetherWorkers(GameSession& s) {runtime(s,DimensionId::Nether).world.setThreadPool(&context(s)->workers);}
    static void travelLan(GameSession& s,LanPlayerRuntime& guest,DimensionId id) {if(!s.pluginTravel(guest.player.roomPlayerId(),uint32_t(id)))throw std::runtime_error("guest public travel failed");}
    static void die(GameSession& s) {
        ensureWorld(s);if(s.isPlayerDead()){s.respawn(RuntimeClock{}.now());complete(s);}
        player(s).configureRules(GameMode::Survival,s.metadata().difficulty);
        player(s).survivalStats().damage(10000);s.updatePlaying(0,nullptr,{});
        if(!s.isPlayerDead()&&!s.metadata().gameRules.boolean(GameRuleId::ImmediateRespawn))throw std::runtime_error("lethal damage did not enter death state");
    }
    static World& netherFlat(GameSession& s,DimensionId d) {
        auto& model=runtime(s,d);context(s)->workers.waitIdle();model.world.setThreadPool(nullptr);model.entities.setNaturalSpawningEnabled(false);
        for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x) {
            auto* c=model.world.getChunk(x,z);
            for(int bz=0;bz<16;++bz)for(int bx=0;bx<16;++bx)for(int y=64;y<74;++y)c->setBlock(bx,y,bz,y==64?BlockId::OBSIDIAN:BlockId::AIR);
            c->generated=true;c->lifecycle=Chunk::LifecycleState::Renderable;
        }
        model.world.setAdditionalStreamingInterests({{0,0,2}});model.world.update({.5,65.01,.5},0);return model.world;
    }
    static void flatRuleScene(GameSession& s) {
        ensureWorld(s);metadata(s,[](auto& value){value.worldSpawn={0,0,0};});
        context(s)->workers.waitIdle();world(s).setThreadPool(nullptr);
        for(int cx=-1;cx<=1;++cx)for(int cz=-1;cz<=1;++cz) {
            auto* chunk=world(s).getChunk(cx,cz);
            for(int x=0;x<16;++x)for(int z=0;z<16;++z)chunk->setBlock(x,0,z,BlockId::STONE);
            chunk->generated=true;chunk->lifecycle=Chunk::LifecycleState::Renderable;
        }
        world(s).update({.5,1.01,.5},0);player(s).configureRules(GameMode::Survival,Difficulty::Normal);
        player(s).setPosition({.5,1.01,.5});entities(s).setNaturalSpawningEnabled(false);
    }
    static void prepareLanScene(GameSession& s) {flatRuleScene(s);}
    static void prepareActionScene(GameSession& s,DimensionId d,uint64_t seed) {
        ensureWorld(s,seed);s.prepareDimension(d);
        if(d!=s.activeDimension()&&!s.switchDimension(d,RuntimeClock{}.now()))throw std::runtime_error("public scene dimension entry failed");
        complete(s);context(s)->workers.waitIdle();world(s).setThreadPool(nullptr);
        player(s).configureRules(GameMode::Survival,Difficulty::Normal);entities(s).setNaturalSpawningEnabled(false);
    }
    static World& pluginDimension(GameSession& s,DimensionId id) {
        auto& model=runtime(s,id);context(s)->workers.waitIdle();model.world.setThreadPool(nullptr);
        for(int x=-1;x<=1;++x)for(int z=-1;z<=1;++z){auto* c=model.world.getChunk(x,z);c->generated=true;c->lifecycle=Chunk::LifecycleState::Renderable;}
        return model.world;
    }
    static void setTerrainReady(GameSession& s,bool ready) {
        if(!ready){context(s)->loadingOrigin=s.playerState().getPosition();const auto p=*context(s)->loadingOrigin;
            command(s,"/tp "+std::to_string(p.x+1024)+" "+std::to_string(p.y)+" "+std::to_string(p.z));}
        else {complete(s);if(context(s)->loadingOrigin){player(s).setPosition(*context(s)->loadingOrigin);context(s)->loadingOrigin.reset();}}
    }
    static void pluginHostLoading(GameSession& s,bool loading) {setTerrainReady(s,!loading);}
    static void fishingPond(GameSession& s,GameMode mode) {
        ensureWorld(s);context(s)->workers.waitIdle();world(s).setThreadPool(nullptr);world(s).update({8.5,65,1.5},1);
        auto* c=world(s).getChunk(0,0);c->generated=true;c->lifecycle=Chunk::LifecycleState::Renderable;
        for(int z=0;z<16;++z)for(int x=0;x<16;++x){c->setBlock(x,62,z,BlockId::STONE);c->setBlock(x,63,z,BlockId::WATER);c->setBlock(x,64,z,BlockId::WATER);}
        player(s).setPosition({8.5,65,1.5});player(s).configureRules(mode,Difficulty::Peaceful);
        s.inventory().slot(0)={ItemId::FISHING_ROD,1,63};s.setSelectedSlot(0);
    }
    static void tickFishing(GameSession& s,float dt) {s.updateFishing(dt);}
    static void tickFishingDrops(GameSession& s,float dt) {entities(s).update(player(s),dt,true,true,true,true,false,false,0);}
    static void retireFishingPond(GameSession& s) {world(s).getChunk(0,0)->lifecycle=Chunk::LifecycleState::Warm;}
    static std::string replicaLoadingDiagnostic(const GameSession& s) {const auto p=s.loadingSnapshot();return "phase="+std::to_string(int(p.phase))+" "+s.lanError();}
    static void emitLanEvent(GameSession& s,Lan::GameEvent event) {
        for(const auto& peer:context(s)->host.peers()) {
            auto& runtime=guest(s,peer.first);event.epoch=runtime.chunkEpoch;
            context(s)->host.send(peer.first,{Lan::MessageType::Event,0,Lan::encodeGameEvent(event)});
        }
    }
    static void corruptReplicaRevision(GameSession& client) {
        for(const auto& entry:SessionScenarioContext::sessions)if(auto ctx=entry.second.lock())if(entry.first->hostingLan()) {
            const auto peer=replicaPeer(client);if(!ctx->host.peers().count(peer))continue;
            Lan::ChunkDelta delta;delta.epoch=client.lanStreamEpoch();delta.address={client.activeDimension(),0,0};
            delta.base=std::numeric_limits<uint64_t>::max()-1;delta.revision=std::numeric_limits<uint64_t>::max();
            delta.edits={{0,uint16_t(BlockId::STONE)}};
            ctx->host.send(peer,{Lan::MessageType::ChunkDelta,0,Lan::encodeChunkDelta(delta)},true);
        }
    }
    static void sendRawAction(GameSession& s,const Lan::GameAction& action) {context(s)->client.send({Lan::MessageType::Action,0,Lan::encodeGameAction(action)});}
    static void safeSpawn(GameSession& s) {s.recoverPlayerSpawn();}
    static void beginAutosave(GameSession& s,const std::function<void()>& error) {s.requestAutosave(error);}
    static void processAutosave(GameSession& s,const std::function<void()>& error) {s.advanceAutosave(error);}
    static void anchorSpawn(GameSession& s,glm::ivec3 p) {
        complete(s);
        const auto source=s.activeDimension();
        if(source!=DimensionId::Nether){if(!s.switchDimension(DimensionId::Nether,RuntimeClock{}.now()))throw std::runtime_error("anchor fixture travel failed");complete(s);}
        auto& terrain=world(s);const auto old=terrain.getBlock(p.x,p.y,p.z);
        terrain.setBlock(p.x,p.y,p.z,respawnAnchorBlock(1));
        auto& actor=player(s);const auto position=actor.getPosition();const auto yaw=actor.getYaw(),pitch=actor.getPitch();
        const auto low=terrain.getBlock(p.x-1,p.y-1,p.z),high=terrain.getBlock(p.x-1,p.y,p.z);
        terrain.setBlock(p.x-1,p.y-1,p.z,BlockId::AIR);terrain.setBlock(p.x-1,p.y,p.z,BlockId::AIR);
        actor.setPosition(glm::dvec3(p)+glm::dvec3(-.5,-1,.5));
        const auto delta=glm::dvec3(p)+glm::dvec3(.5)-actor.getEyePosition();
        actor.setOrientation(float(glm::degrees(std::atan2(delta.x,delta.z))),
                             float(glm::degrees(std::atan2(delta.y,std::hypot(delta.x,delta.z)))));
        const auto selected=actor.activeItem();const auto slot=actor.selectedSlot();s.inventory().slot(slot).clear();
        s.interactRestBlock(p);s.inventory().slot(slot)=selected;terrain.setBlock(p.x,p.y,p.z,old);
        terrain.setBlock(p.x-1,p.y-1,p.z,low);terrain.setBlock(p.x-1,p.y,p.z,high);
        actor.setPosition(position);actor.setOrientation(yaw,pitch);
        if(s.metadata().dimensions.at(2).anchorSpawn!=p)throw std::runtime_error("anchor fixture registration failed");
        if(source!=DimensionId::Nether){if(!s.switchDimension(source,RuntimeClock{}.now()))throw std::runtime_error("anchor fixture return failed");complete(s);}
    }
    static void sleepChoice(GameSession& s,GameSession::Feedback feedback) {
        ensureWorld(s);const auto phase=s.daylightState().phase();
        if(s.isSleeping()){s.cancelSleep({});s.updatePlaying(.4f,nullptr,{},false);}
        const glm::ivec3 bed{0,1,2};world(s).setBlock(0,0,2,BlockId::STONE);world(s).setBlock(0,0,1,BlockId::STONE);
        world(s).setBlock(0,1,2,bedBlock(BedPart::Foot,BedDirection::North));world(s).setBlock(0,1,1,bedBlock(BedPart::Head,BedDirection::North));
        if(!s.isSleeping()) {
            runtime(s,s.activeDimension()).daylight.setNight();
            if(!s.beginSleepAtBed(bed))throw std::runtime_error("scenario could not begin valid sleep");
        }
        runtime(s,s.activeDimension()).daylight.setPhase(phase);
        s.chooseSleepAction(GameSession::SleepAction::SleepUntilMorning,RuntimeClock{}.now(),feedback);
    }
    static bool beginGuestSleep(GameSession& s,uint64_t id,glm::ivec3 bed) {
        auto& g=guest(s,id);auto& actor=g.player;
        const auto delta=glm::dvec3(bed)+glm::dvec3(.5,.3,.5)-actor.getEyePosition();
        actor.setOrientation(float(glm::degrees(std::atan2(delta.x,delta.z))),
                             float(glm::degrees(std::atan2(delta.y,std::hypot(delta.x,delta.z)))));
        actor.setMouseLocked(true);actor.handleMouseButton(MouseButton::Right,ButtonAction::Release);
        actor.handleMouseButton(MouseButton::Right,ButtonAction::Press);actor.handleMouseButton(MouseButton::Right,ButtonAction::Release);
        return g.sleep!=0;
    }
    static bool skipLanNight(GameSession& s,DimensionId) {const bool night=s.isNight();tickLanPlayers(s,0);return night&&!s.isNight();}
    static void pumpExtra(GameSession& s) {
        auto ctx=context(s);ctx->networkTime+=.005;s.pollLan(ctx->networkTime);
        for(auto& client:ctx->extraClients)client->poll(ctx->networkTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    static LanPlayerRuntime& anchorGuest(GameSession& s,glm::ivec3 p) {
        auto ctx=context(s);if(!s.hostingLan()&&!s.openLanRoom(0,2,true))throw std::runtime_error("anchor room unavailable");
        auto identity=Lan::ProfileStore::localIdentity(ctx->saves.parent_path()/"anchor-client");
        Lan::PlayerProfile profile;profile.identity=identity;profile.mode=GameMode::Survival;profile.netherAnchor=p;
        profile.dimension=DimensionId::Overworld;profile.positions[0]={.5,65.01,.5};profile.positioned[0]=true;profile.health=0;
        Lan::ProfileStore(s.pluginWorldDirectory()/"players").save(profile);
        auto client=std::make_unique<Lan::Client>();
        if(!client->join("::1",s.lanPort(),GameSession::lanCompatibility(),identity,ctx->networkTime))throw std::runtime_error("anchor guest join failed");
        auto* wire=client.get();ctx->extraClients.push_back(std::move(client));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while((wire->state()!=Lan::Client::State::Connected||!s.lanGuestCount())&&std::chrono::steady_clock::now()<deadline)pumpExtra(s);
        auto& result=guest(s,wire->peerId());tickLanPlayers(s,0);
        if(!result.dead)throw std::runtime_error("zero-health saved guest did not enter death state");return result;
    }
    static bool respawnGuest(GameSession& s,LanPlayerRuntime& g) {
        auto ctx=context(s);Lan::GameAction action;action.kind=Lan::ActionKind::Respawn;action.sequence=g.actionAcknowledged+1;action.epoch=g.chunkEpoch;
        for(auto& wire:ctx->extraClients)if(wire->peerId()==g.player.roomPlayerId())wire->send({Lan::MessageType::Action,0,Lan::encodeGameAction(action)});
        for(int i=0;i<20&&!g.pendingRespawn&&g.dead;++i)pumpExtra(s);
        return !g.dead;
    }

};
