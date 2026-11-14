#pragma once
#include "world/RedstoneBlocks.h"

namespace RedstoneLanIntegration {
inline int run() {
    // Existing replica tests cover independent chunk edits. This checks a real
    // two-client authority, a negative-coordinate seam, motion and reconnect.
    TestSupport::TemporaryDirectory root;
    const int previous=Config::RENDER_DISTANCE;
    struct Restore {int distance;~Restore(){Config::RENDER_DISTANCE=distance;}} restore{previous};
    Config::RENDER_DISTANCE=1;
    ScenarioSession host(root.path()/"host/saves"),first(root.path()/"first/saves"),second(root.path()/"second/saves");
    const auto id=host.createWorld("Redstone authority",1001,GameMode::Creative,Difficulty::Peaceful,true,WorldType::Superflat);
    host.startWorld(id,true,0);SessionScenario::prepareLanScene(host);
    auto& world=SessionScenario::world(host);
    Redstone::State piston;piston.kind=Redstone::Kind::Piston;piston.facing=Redstone::Face::East;piston.alternate=true;
    Redstone::State lever;lever.kind=Redstone::Kind::Lever;lever.facing=Redstone::Face::Up;
    world.setBlock(-1,1,8,Redstone::block(piston));world.setBlock(0,1,8,BlockId::STONE);
    world.setBlock(-1,1,7,Redstone::block(lever));
    world.setBlock(15,1,10,Redstone::block(piston));world.setBlock(16,1,10,BlockId::STONE);
    require(host.openLanRoom(0,3,true),"redstone LAN host failed");
    require(first.setLanNickname("Circuit A") && second.setLanNickname("Circuit B"),"redstone LAN identities failed");
    require(first.joinLanRoom("::1",host.lanPort(),0) && second.joinLanRoom("::1",host.lanPort(),0),"redstone LAN clients failed to connect");
    double now=0,lastSimulation=0;
    const auto started=std::chrono::steady_clock::now();
    const auto pump=[&](bool simulate) {
        now=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        host.pollLan(now);first.pollLan(now);second.pollLan(now);
        if(simulate && now-lastSimulation>=.05){host.updatePlaying(.05f,nullptr,{},false);lastSimulation=now;}
        if(first.lanConnectionFailed() || second.lanConnectionFailed())throw std::runtime_error("redstone LAN transport: A="+first.lanError()+" B="+second.lanError());
        std::this_thread::yield();
    };
    const auto until=[&](const auto& ready,bool simulate=false) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!ready() && std::chrono::steady_clock::now()<deadline)pump(simulate);
        require(ready(),"redstone LAN state did not converge before deadline");
    };
    until([&]{return first.lanWorldReady() && second.lanWorldReady() && first.worldState().getBlock(-1,1,8)==Redstone::block(piston) && first.worldState().getBlock(0,1,8)==BlockId::STONE && second.worldState().getBlock(-1,1,8)==Redstone::block(piston) && second.worldState().getBlock(0,1,8)==BlockId::STONE;});
    auto& pushed=SessionScenario::guest(host,SessionScenario::replicaPeer(first)).player;
    auto& spectator=SessionScenario::guest(host,SessionScenario::replicaPeer(second)).player;
    pushed.setPosition({1.4,1,8.5});
    spectator.configureRules(GameMode::Spectator,Difficulty::Peaceful);spectator.setPosition({1.4,1,8.5});
    require(world.interactRedstone({-1,1,7}),"LAN circuit lever failed");
    Redstone::State constant;constant.kind=Redstone::Kind::Block;constant.power=15;
    world.setBlock(15,2,10,Redstone::block(constant));
    bool sawMotion=false,reconnecting=false;
    const auto extended=[&](const GameSession& client) {
        if(!client.worldState().getLoadedBlock(-1,1,8) || !client.worldState().getLoadedBlock(0,1,8))return false;
        if(reconnecting && !client.worldState().getLoadedBlock(16,1,10))return false;
        const auto state=Redstone::blockState(client.worldState().getBlock(-1,1,8));
        const auto head=client.worldState().getBlock(0,1,8);
        if(state && state->power && client.worldState().pistonMotions().empty()) {
            const auto other=Redstone::blockState(client.worldState().getBlock(15,1,10));
            const auto otherHead=client.worldState().getBlock(16,1,10);
            require(Redstone::native(head) && Redstone::pistonHead(Redstone::nativeSlot(head)) && client.worldState().getBlock(1,1,8)==BlockId::STONE &&
                other && other->power && Redstone::native(otherHead) && Redstone::pistonHead(Redstone::nativeSlot(otherHead)) && client.worldState().getBlock(17,1,10)==BlockId::STONE,
                reconnecting?"reconnected snapshot lost overlapping cross-chunk pushes":"client published half of overlapping cross-chunk pushes");
            return true;
        }
        return false;
    };
    until([&]{
        sawMotion=sawMotion||!first.worldState().pistonMotions().empty()||!second.worldState().pistonMotions().empty();
        return extended(first)&&extended(second);
    },true);
    require(sawMotion,"LAN did not carry authoritative motion progress");
    require(pushed.getPosition().x>2.29 && std::abs(spectator.getPosition().x-1.4)<1e-6,"authoritative piston sweep missed player or moved spectator");
    reconnecting=true;first.leaveWorld();
    until([&]{return host.lanGuestCount()==1;});
    require(first.joinLanRoom("::1",host.lanPort(),now),"redstone reconnect failed");
    until([&]{return first.lanWorldReady() && extended(first);});
    reconnecting=false;
    require(world.interactRedstone({-1,1,7}),"LAN circuit lever release failed");
    world.setBlock(15,2,10,BlockId::AIR);
    until([&]{
        const auto ready=[](const GameSession& client) {
            const auto state=Redstone::blockState(client.worldState().getBlock(-1,1,8));
            const auto other=Redstone::blockState(client.worldState().getBlock(15,1,10));
            return state && !state->power && other && !other->power && client.worldState().pistonMotions().empty() &&
                client.worldState().getBlock(0,1,8)==BlockId::STONE && client.worldState().getBlock(1,1,8)==BlockId::AIR &&
                client.worldState().getBlock(16,1,10)==BlockId::STONE && client.worldState().getBlock(17,1,10)==BlockId::AIR;
        };
        return ready(first)&&ready(second);
    },true);
    first.leaveWorld();second.leaveWorld();host.closeLanRoom();host.leaveWorld();
    std::cout<<"Redstone two-client atomic motion and reconnect passed\n";return 0;
}
} // namespace RedstoneLanIntegration
