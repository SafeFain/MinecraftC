#include "app/GameSession.h"
#include "game/DimensionRules.h"
#include "world/NetherPortal.h"
#include "game/EntityTransferJournal.h"
#include "debug/Log.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

void GameSession::explodeRestBlock(DimensionId source,const glm::ivec3& p) {
    auto& runtime=ensureDimension(source);
    runtime.world.setBlock(p.x,p.y,p.z,BlockId::AIR);
    std::vector<EntityPlayerView> views;
    if(dimension==source)views.push_back({0,&player,!playerDead,!playerDead,!playerDead});
    for(auto& entry:guests)if(entry.second->profile.dimension==source&&!entry.second->loading&&!entry.second->dead)
        views.push_back({entry.first,&entry.second->player,true,true,true});
    runtime.entities.explodeAt(glm::dvec3(p)+glm::dvec3(.5),5.f,views);
}

bool GameSession::useRespawnAnchor(uint64_t id,const glm::ivec3& p) {
    Player* actor=&player;DimensionId source=dimension;
    if(id) {
        const auto found=guests.find(id);
        if(found==guests.end()||found->second->dead||found->second->loading)return false;
        actor=&found->second->player;source=found->second->profile.dimension;
    } else if(playerDead)return false;
    if(actor->isSpectator()||(actor->isSurvival()&&actor->survivalStats().dead()))return false;
    auto& runtime=ensureDimension(source);
    const auto hit=runtime.world.raycast(actor->getEyePosition(),actor->getForward(),Config::REACH_DISTANCE);
    if(!hit||hit->blockPos!=p)return false;
    const int charge=respawnAnchorCharge(runtime.world.getBlock(p.x,p.y,p.z));
    if(charge<0)return false;
    auto& held=actor->inventory().slot(static_cast<size_t>(actor->selectedSlot()));
    if(held.id==ItemId::GLOWSTONE&&charge<4) {
        runtime.world.setBlock(p.x,p.y,p.z,respawnAnchorBlock(charge+1));
        if(actor->isSurvival()&&--held.count==0)held.clear();
        return true;
    }
    if(!dimensionRules(source).respawnAnchorsWork){explodeRestBlock(source,p);return true;}
    if(!charge)return false;
    if(!id) {
        worldMetadata.dimensions[2].anchorSpawn=p;
        updateSaveMetadata();if(saveStore)saveStore->saveMetadata(worldMetadata);
    } else {
        guests.at(id)->profile.netherAnchor=p;
        saveLanPlayer(*guests.at(id));
    }
    return true;
}

std::optional<glm::dvec3> GameSession::consumeAnchorRespawn(const glm::ivec3& p) {
    auto& terrain=ensureDimension(DimensionId::Nether).world;
    terrain.update(glm::dvec3(p));terrain.enqueueGeneration(true);
    terrain.waitForInitialGeneration(1500);terrain.processCompletedGenerations(false);
    const int charge=respawnAnchorCharge(terrain.getBlock(p.x,p.y,p.z));
    if(charge<=0)return {};
    const auto safe=[](BlockId b) {
        return !isSolid(b)&&!isFluid(b)&&b!=BlockId::FIRE&&b!=BlockId::SOUL_FIRE&&!NetherPortal::isPortal(b);
    };
    for(int radius=1;radius<=2;++radius)for(int dy:{0,1,-1})
        for(int z=-radius;z<=radius;++z)for(int x=-radius;x<=radius;++x) {
            if(std::max(std::abs(x),std::abs(z))!=radius)continue;
            const glm::ivec3 q=p+glm::ivec3(x,dy,z);
            if(q.y<1||q.y>256)continue;
            const auto ground=terrain.getLoadedBlock(q.x,q.y-1,q.z),feet=terrain.getLoadedBlock(q.x,q.y,q.z),head=terrain.getLoadedBlock(q.x,q.y+1,q.z);
            if(!ground||!feet||!head||!isFullCollisionBlock(*ground)||*ground==BlockId::MAGMA_BLOCK||!safe(*feet)||!safe(*head))continue;
            terrain.setBlock(p.x,p.y,p.z,respawnAnchorBlock(charge-1));
            terrain.persistChunkAt(p);
            return glm::dvec3(q)+glm::dvec3(.5,.01,.5);
        }
    return {};
}

std::optional<glm::dvec3> GameSession::netherPortalLanding(
    DimensionId target,const glm::dvec3& desired) {
    auto& terrain=ensureDimension(target).world;
    struct RestoreInterests {std::function<void()> restore;~RestoreInterests(){restore();}} restore{[this]{updateLanInterests();}};
    const auto prepare=[&](const glm::dvec3& point) {
        std::vector<StreamingInterest> interests;
        interests.push_back({World::worldToChunkX(point.x),World::worldToChunkZ(point.z),2});
        if(dimension==target) {
            const auto p=player.getPosition();interests.push_back({World::worldToChunkX(p.x),World::worldToChunkZ(p.z),std::clamp(Config::RENDER_DISTANCE,2,16)});
        }
        for(const auto& guest:guests)if(guest.second->profile.dimension==target) {
            const auto p=guest.second->player.getPosition();interests.push_back({World::worldToChunkX(p.x),World::worldToChunkZ(p.z),guest.second->radius});
        }
        terrain.setAdditionalStreamingInterests(std::move(interests));
        terrain.update(point);terrain.enqueueGeneration(true);terrain.waitForInitialGeneration(1500);terrain.processCompletedGenerations(false);
    };
    glm::dvec3 center=desired;
    constexpr double landingBorder=30000000.0-32;
    center.x=std::clamp(center.x,-landingBorder,landingBorder);
    center.z=std::clamp(center.z,-landingBorder,landingBorder);
    center.y=std::clamp(center.y,target==DimensionId::Nether?1.0:double(Config::WORLD_MIN_Y+1),target==DimensionId::Nether?248.0:double(Config::WORLD_MAX_Y-8));
    prepare(center);
    const int radius=target==DimensionId::Nether?16:128;
    std::optional<glm::dvec3> best;
    double distance=std::numeric_limits<double>::max();
    std::set<std::tuple<int,int,int,bool>> seen;
    const auto consider=[&](const NetherPortalFrame& frame) {
        // Compare supported bottom-row entry cells, including wide gates whose
        // stored origin lies outside the search radius. Ties choose lower cells.
        const double relative=frame.alongZ?center.z-frame.origin.z:center.x-frame.origin.x;
        const int u=std::clamp(int(std::ceil(relative-1.0)),0,frame.width-1);
        const auto landing=glm::dvec3(frame.at(u,0))+glm::dvec3(.5,.01,.5);
        if(std::abs(landing.x)>30000000||std::abs(landing.z)>30000000||std::abs(landing.x-center.x)>radius||std::abs(landing.z-center.z)>radius)return;
        const auto delta=landing-center;const double d=glm::dot(delta,delta);
        if(d<distance||(d==distance&&(!best||std::tie(landing.x,landing.y,landing.z)<std::tie(best->x,best->y,best->z)))){distance=d;best=landing;}
    };
    for(const auto* chunk:terrain.getSimulationChunks()) {
        if(!chunk->generated.load())continue;
        if(std::abs(chunk->worldX()-center.x)>radius+16||std::abs(chunk->worldZ()-center.z)>radius+16)continue;
        std::vector<uint16_t> blocks;chunk->copyRawBlocks(blocks);
        for(int y=Config::WORLD_MIN_Y;y<Config::WORLD_MAX_Y;++y)for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
            const auto b=static_cast<BlockId>(blocks[x+z*16+Config::worldYToStorageY(y)*256]);
            if(!NetherPortal::isPortal(b))continue;
            const glm::ivec3 p(chunk->worldX()+x,y,chunk->worldZ()+z);
            if(std::abs(p.x+.5-center.x)>radius||std::abs(p.z+.5-center.z)>radius)continue;
            const auto f=NetherPortal::find([&](glm::ivec3 q){return terrain.getLoadedBlock(q.x,q.y,q.z).value_or(BlockId::AIR);},p,b==BlockId::NETHER_PORTAL_Z,true);
            if(!f||!seen.emplace(f->origin.x,f->origin.y,f->origin.z,f->alongZ).second)continue;
            consider(*f);
        }
    }
    bool waitingForIndexedPortal=false;
    for(const auto& origin:terrain.savedNetherPortalsNear(center,radius+21)) {
        const auto landing=glm::dvec3(origin)+glm::dvec3(.5,.01,.5);
        if(seen.count({origin.x,origin.y,origin.z,false})||seen.count({origin.x,origin.y,origin.z,true}))continue;
        // An orientation-independent lower bound avoids loading distant records.
        const auto near=glm::clamp(center,landing,landing+glm::dvec3(20,0,20));
        const auto delta=near-center;
        if(std::abs(delta.x)>radius||std::abs(delta.z)>radius||glm::dot(delta,delta)>distance)continue;
        if(!terrain.getLoadedBlock(origin.x,origin.y,origin.z)) {
            prepare(landing);
        }
        bool missing=false;
        const auto read=[&](glm::ivec3 q){const auto b=terrain.getLoadedBlock(q.x,q.y,q.z);if(!b)missing=true;return b.value_or(BlockId::AIR);};
        const auto block=read(origin);
        auto frame=NetherPortal::isPortal(block)?NetherPortal::find(read,origin,block==BlockId::NETHER_PORTAL_Z,true):std::optional<NetherPortalFrame>{};
        if(missing) {
            prepare(landing);missing=false;
            const auto loaded=read(origin);
            frame=NetherPortal::isPortal(loaded)?NetherPortal::find(read,origin,loaded==BlockId::NETHER_PORTAL_Z,true):std::optional<NetherPortalFrame>{};
        }
        if(missing){waitingForIndexedPortal=true;continue;}
        if(!frame)continue;
        consider(*frame);
    }
    if(waitingForIndexedPortal)return {};
    if(best) {
        prepare(*best);
        if(!terrain.getLoadedBlock(int(std::floor(best->x)),int(std::floor(best->y)),int(std::floor(best->z))))return {};
        terrain.flushModifiedChunks();return best;
    }
    prepare(center);
    const int baseX=int(std::floor(center.x)),baseZ=int(std::floor(center.z));
    for(int candidateRadius=0;candidateRadius<=8;++candidateRadius)for(int dz=-candidateRadius;dz<=candidateRadius;++dz)for(int dx=-candidateRadius;dx<=candidateRadius;++dx) {
        if(std::max(std::abs(dx),std::abs(dz))!=candidateRadius)continue;
        const int x=baseX+dx,z=baseZ+dz;
        const int floor=target==DimensionId::Nether?terrain.generator().nether().surface(x,z):terrain.getSurfaceY(x,z);
        if(floor<(target==DimensionId::Nether?0:Config::WORLD_MIN_Y)||floor+5>=(target==DimensionId::Nether?256:Config::WORLD_MAX_Y))continue;
        bool safe=true;
        for(int u=-1;u<=2 && safe;++u)for(int v=1;v<=4 && safe;++v)for(int normal=-1;normal<=1;++normal) {
            const auto air=terrain.getLoadedBlock(x+u,floor+v,z+normal);
            const auto ground=terrain.getLoadedBlock(x+u,floor,z+normal);
            if(!air||*air!=BlockId::AIR||!ground||!isFullCollisionBlock(*ground)||*ground==BlockId::MAGMA_BLOCK){safe=false;break;}
        }
        if(!safe)continue;
        for(int u=-1;u<=2;++u)for(int v=0;v<=4;++v)if(u==-1||u==2||v==0||v==4)terrain.setBlock(x+u,floor+v,z,BlockId::OBSIDIAN);
        if(NetherPortal::ignite(terrain,{x,floor+1,z})){terrain.flushModifiedChunks();return glm::dvec3(x+.5,floor+1.01,z+.5);}
    }
    const int x=baseX,z=baseZ;
    // Natural shelf when available; otherwise a protected obsidian apron.
    int floor=target==DimensionId::Nether?terrain.generator().nether().surface(x,z):terrain.getSurfaceY(x,z);
    if(floor<32||floor>110)floor=static_cast<int>(std::floor(center.y));
    floor=std::clamp(floor,target==DimensionId::Nether?0:Config::WORLD_MIN_Y,target==DimensionId::Nether?248:Config::WORLD_MAX_Y-8);
    for(int dz=-4;dz<=4;++dz)for(int dx=-4;dx<=5;++dx)
        if(!terrain.getLoadedBlock(x+dx,floor,z+dz))return {};
    for(int dz=-4;dz<=4;++dz)for(int dx=-4;dx<=5;++dx)for(int dy=0;dy<8;++dy) {
        const bool barrier=dy==0||dy==7||dx==-4||dx==5||dz==-4||dz==4;
        terrain.setBlock(x+dx,floor+dy,z+dz,barrier?BlockId::OBSIDIAN:BlockId::AIR);
    }
    for(int u=-1;u<=2;++u)for(int v=0;v<=4;++v)
        if(u==-1||u==2||v==0||v==4)terrain.setBlock(x+u,floor+v,z,BlockId::OBSIDIAN);
    if(!NetherPortal::ignite(terrain,{x,floor+1,z}))return {};
    terrain.flushModifiedChunks();
    return glm::dvec3(x+.5,floor+1.01,z+.5);
}

bool GameSession::updateNetherPortals(float dt,const Feedback& feedback) {
    if(lanJoining||!terrainGenerated)return false;
    bool hostMoved=false;
    auto update=[&](uint64_t id,Player& actor,DimensionId source,bool ready) {
        auto& state=portalStates[id];
        const bool cooling=state.cooldown>0;
        state.cooldown=std::max(0.f,state.cooldown-dt);state.retry=std::max(0.f,state.retry-dt);
        if(!ready||(source!=DimensionId::Overworld&&source!=DimensionId::Nether)||
           !worldMetadata.gameRules.boolean(GameRuleId::AllowEnteringNetherUsingPortals)) {state.inside=0;return;}
        const auto p=actor.getPosition();
        auto& sourceRuntime=ensureDimension(source);
        if(sourceRuntime.entities.riding(id)){state.inside=0;return;}
        auto& terrain=sourceRuntime.world;
        bool inside=false;
        for(int dy=0;dy<2;++dy)inside=inside||NetherPortal::isPortal(terrain.getBlock(static_cast<int>(std::floor(p.x)),static_cast<int>(std::floor(p.y))+dy,static_cast<int>(std::floor(p.z))));
        if(!inside){state.inside=0;return;}
        if(cooling){state.cooldown=1.f;return;}
        if(state.retry>0)return;
        const glm::ivec3 cell(static_cast<int>(std::floor(p.x)),static_cast<int>(std::floor(p.y)),static_cast<int>(std::floor(p.z)));
        const auto block=terrain.getBlock(cell.x,cell.y,cell.z);
        const auto frame=NetherPortal::find([&](glm::ivec3 q){return terrain.getBlock(q.x,q.y,q.z);},cell,block==BlockId::NETHER_PORTAL_Z,true);
        if(!frame){state.inside=0;return;}
        state.inside+=static_cast<double>(std::max(0.f,dt))*20;
        const auto delay=worldMetadata.gameRules.integer(actor.isSurvival()?GameRuleId::PlayersNetherPortalDefaultDelay:GameRuleId::PlayersNetherPortalCreativeDelay);
        if(state.inside<delay)return;
        const auto target=source==DimensionId::Nether?DimensionId::Overworld:DimensionId::Nether;
        const auto landing=netherPortalLanding(target,NetherPortal::scaledDestination(p,source));
        if(!landing){state.retry=1;return;}
        bool moved=false;
        if(id==0) {
            moved=enterDimension(target,RuntimeClock{}.now(),*landing);
            if(moved){hostMoved=true;if(feedback.dimensionLoading)feedback.dimensionLoading();}
        } else {
            const auto guest=guests.find(id);
            if(guest!=guests.end())moved=travelLanPlayer(*guest->second,target,false,*landing);
        }
        if(moved){state.inside=0;state.cooldown=1;}
    };
    update(0,player,dimension,!playerDead);
    for(auto& entry:guests)update(entry.first,entry.second->player,entry.second->profile.dimension,!entry.second->loading&&!entry.second->dead);
    if(hostMoved)return true;
    if(!worldMetadata.gameRules.boolean(GameRuleId::AllowEnteringNetherUsingPortals))return false;
    // One prepared transfer per frame bounds generation and disk publication.
    for(const auto source:{DimensionId::Overworld,DimensionId::Nether}) {
        const auto index=static_cast<size_t>(source);
        if(index>=simulations.size()||!simulations[index]||!simulations[index]->initialized)continue;
        auto& origin=*simulations[index];
        for(const auto id:origin.entities.portalCandidates(dt)) {
            const auto* entity=origin.entities.entityById(id);if(!entity)continue;
            const auto position=entity->position;
            const auto target=source==DimensionId::Overworld?DimensionId::Nether:DimensionId::Overworld;
            try {
                const auto landing=netherPortalLanding(target,NetherPortal::scaledDestination(position,source));
                if(!landing){origin.entities.deferPortal(id);continue;}
                auto& destination=ensureDimension(target);destination.entities.syncChunks();
                // Any legacy metadata-owned entities are first consolidated into
                // their chunk partitions before metadata drops the old copies.
                origin.entities.flushChunkEntities(std::numeric_limits<size_t>::max(),true);
                destination.entities.flushChunkEntities(std::numeric_limits<size_t>::max(),true);
                updateSaveMetadata();saveStore->saveMetadata(worldMetadata);
                if(!origin.entities.transferTo(id,destination.entities,*landing,saveStore->worldDirectory()))origin.entities.deferPortal(id);
            } catch(const std::exception& error) {
                LOG_ERROR("Native entity transfer: "<<error.what());origin.entities.deferPortal(id);
                if(EntityTransferJournal::exists(saveStore->worldDirectory())){setLocalControl(false);return true;}
            }
            updateLanInterests();return false;
        }
    }
    return false;
}
