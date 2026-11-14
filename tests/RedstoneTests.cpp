#include "world/RedstoneDevice.h"
#include "world/RedstoneNativeState.h"
#include "world/RedstoneContent.h"
#include "world/RedstonePersistence.h"
#include "entity/SlimeRules.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace Redstone;
void check(bool condition,const char* message) {
    if(!condition) {std::cerr<<message<<'\n';std::exit(1);}
}
void states() {
    check(namedBlock("redstone_dust")==NativeBlock::REDSTONE_DUST_FIRST,"wire alias default");
    check(namedBlock("redstone_ore")==NativeBlock::REDSTONE_ORE,"ore alias default");
    check(!namedBlock("slime_ball") && !namedBlock("slime_spawn_egg") && !namedBlock("missing"),"non-block alias accepted");
    const auto sticky=nativeState(*namedBlock("sticky_piston"));
    check(sticky && sticky->kind==Kind::Piston && sticky->alternate && !sticky->power,"sticky piston placement default");
    const auto lit=nativeState(*namedBlock("redstone_torch"));
    check(lit && lit->facing==Face::Up && lit->power==15,"torch placement default");
    for(uint32_t bits=0;bits<65536;++bits) {
        const auto s=decode(static_cast<uint16_t>(bits));
        if(s) check(encode(*s)==bits,"state encoding alias");
    }
    check(!decode(0x8000),"reserved bit accepted");
    for(int rear=0;rear<=15;++rear) for(int side=0;side<=15;++side) {
        check(comparator(static_cast<uint8_t>(rear),static_cast<uint8_t>(side),false)==(rear>=side?rear:0),"compare mode");
        check(comparator(static_cast<uint8_t>(rear),static_cast<uint8_t>(side),true)==std::max(rear-side,0),"subtract mode");
    }
    check(fullness({})==0 && fullness({{0,64}})==0,"empty container");
    check(fullness({{64,64}})==15 && fullness({{1,1},{0,64}})==8,"mixed stack capacity");
    check(fullness({{1,64},{0,64}})==1,"one item threshold");
    bool rejected=false;
    try{fullness({{2,1}});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"invalid stack accepted");
    State torch;torch.kind=Kind::Torch;torch.power=15;
    check(output(torch,Face::Down).weak==0 && output(torch,Face::Up).strong==15 &&
          output(torch,Face::North).strong==0,"torch directional power");
    State repeater;repeater.kind=Kind::Repeater;repeater.power=15;
    check(output(repeater,Face::North).strong==15 && output(repeater,Face::South).weak==0,"diode direction");
}
void nativeStates() {
    for(unsigned raw=0;raw<unsigned(NativeBlock::Count);++raw) {
        const auto id=static_cast<NativeBlock>(raw);
        const auto s=nativeState(id);
        if(s) check(nativeBlock(*s)==id,"native state lost a serialized property");
        else check(pistonHead(id)||ore(id),"unassigned native redstone slot");
    }
    check(!nativeState(NativeBlock::Count),"native sentinel accepted");
    State torch;torch.kind=Kind::Torch;torch.facing=Face::Down;
    bool rejected=false;
    try{nativeBlock(torch);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"ceiling torch accepted");
}
std::map<Position,DustPropagation::Wire> wireLine() {
    std::map<Position,DustPropagation::Wire> wires;
    for(int x=-16;x<=1;++x) {
        auto& wire=wires[{x,210,-1}];wire.source=x==-16?15:0;
        if(x>-16)wire.neighbors.push_back({x-1,210,-1});
        if(x<1)wire.neighbors.push_back({x+1,210,-1});
    }
    return wires;
}
void dust() {
    auto wires=wireLine();
    DustPropagation one(wires),many(wires);
    check(!one.step(0),"zero budget consumed work");
    bool rejected=false;
    try {one.result();} catch(const std::logic_error&) {rejected=true;}
    check(rejected,"partial propagation published");
    while(!one.step(1)){}
    check(many.step(10000),"large budget incomplete");
    check(one.result()==many.result(),"budget changes wire result");
    for(int x=-16;x<=1;++x)check(one.result().at({x,210,-1})==std::max(15-(x+16),0),"wire attenuation");
    // Close the line into a loop, then remove every source.
    wires.begin()->second.neighbors.push_back(wires.rbegin()->first);
    wires.rbegin()->second.neighbors.push_back(wires.begin()->first);
    wires.begin()->second.source=0;
    // Initialization is now part of the public work budget, including source-free rings.
    DustPropagation removed(wires);while(!removed.step(1)){}
    for(const auto& pair:removed.result())check(pair.second==0,"ring retained removed source");
    wires.begin()->second.source=10;wires.rbegin()->second.source=15;
    DustPropagation joined(wires);while(!joined.step(2)){}
    check(joined.result().at(wires.begin()->first)==14,"multi-input max");
}
std::vector<uint64_t> run(size_t budget) {
    RedstoneScheduler queue;
    for(int x=2;x>=-2;--x)queue.schedule({x,0,0},1,EventKind::Output,15,2);
    queue.schedule({0,0,0},1,EventKind::Output,0,1); // duplicate cannot replace rising edge
    // The emitted trace below proves duplicate suppression and cancellation;
    // container size is not the scheduler's observable behavior.
    std::vector<uint64_t> trace;
    while(queue.tick()<3 || queue.pendingTick()) {
        queue.advance(budget,[](Position p,uint64_t){return p.x!=1;},[&](Event e){
            trace.push_back(queue.tick()*100+static_cast<uint64_t>(e.position.x+2));
            check(e.value==15,"short pulse overwrote scheduled edge");
        });
    }
    check(trace==std::vector<uint64_t>({200,201,202,204}),"event order, timing or identity");
    return trace;
}
void events() {
    check(run(1)==run(100),"event budget changed tick trace");
    RedstoneScheduler q;
    q.schedule({-1,0,0},7,EventKind::Output,15,4);
    const auto valid=[](Position,uint64_t){return true;};
    const auto noop=[](Event){};
    q.advance(10,valid,noop);
    const auto paused=q.pause([](Position){return true;});
    check(paused.size()==1 && paused[0].remaining==3,"pause remaining time");
    for(int i=0;i<20;++i)q.advance(10,valid,noop);
    q.resume(paused);
    int fired=0;
    for(int i=0;i<2;++i)q.advance(10,valid,[&](Event){++fired;});
    check(fired==0,"unloaded timer elapsed");
    q.advance(10,valid,[&](Event){++fired;});check(fired==1,"resumed timer missing");
    q.schedule({0,0,0},8,EventKind::Output,15,1);q.cancel({0,0,0},8);
    q.advance(10,valid,[&](Event){++fired;});check(fired==1,"removed component event fired");
    q.schedule({0,0,0},9,EventKind::Output,15,1);
    q.advance(10,[](Position,uint64_t identity){return identity==10;},[&](Event){++fired;});
    check(fired==1,"replaced component accepted stale event");
    // Reentrant zero-delay work must obey the same tick budget.
    q.schedule({0,0,0},10,EventKind::Retry,0,1);
    const auto repeat=[&](Event e){++fired;q.schedule(e.position,e.identity,e.kind,0,0);};
    const auto before=q.tick();
    check(!q.advance(3,valid,repeat) && fired==4 && q.tick()==before+1,"reentrant budget failed");
    check(!q.advance(2,valid,repeat) && fired==6 && q.tick()==before+1,"continuation advanced logical tick");
    q.cancel({0,0,0},10);
    check(q.advance(0,valid,noop) && q.tick()==before+1,"empty continuation advanced logical tick");
}
void burnout() {
    TorchBurnout history;
    for(int i=0;i<7;++i) {
        check(!history.recordOff(),"torch burned out before eighth transition");
        history.advance();
    }
    check(history.recordOff() && history.cooldown==160 && history.valid(),"torch burnout threshold");
    const auto saved=history;
    // No calls to advance while unloaded: copying/restoring preserves the timer.
    history=saved;
    for(int i=0;i<159;++i)history.advance();
    check(history.cooldown==1,"torch cooldown ended early");
    history.advance();check(history.cooldown==0 && history.count==0,"torch cooldown recovery");
    for(int i=0;i<8;++i) {
        check(!history.recordOff(),"expired torch transitions counted");
        for(int t=0;t<61;++t)history.advance();
    }
}
void snapshots() {
    CircuitSnapshot source;source.burnout.recordOff();source.burnout.advance();
    source.observerPending=true;source.events={{EventKind::ObserverOff,2,0},{EventKind::Retry,160,15}};
    const auto bytes=encodeSnapshot(source);const auto restored=decodeSnapshot(bytes);
    check(encodeSnapshot(restored)==bytes,"circuit snapshot roundtrip");
    check(restored.events[1].remaining==160 && restored.burnout.ages[0]==1,"circuit timers changed");
    for(size_t length=1;length<bytes.size();++length) {
        bool rejected=false;try{decodeSnapshot({bytes.begin(),bytes.begin()+length});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"truncated circuit snapshot accepted");
    }
    auto bad=bytes;bad.push_back(0);bool rejected=false;
    try{decodeSnapshot(bad);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"trailing circuit data accepted");
    source.events.push_back(source.events[0]);rejected=false;
    try{encodeSnapshot(source);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"duplicate circuit delayed event accepted");
}
void slimes() {
    int chunks=0,variation=0;
    for(int x=-50;x<50;++x)for(int z=-50;z<50;++z) {
        const bool a=isSlimeChunk(42,x,z);chunks+=a?1:0;
        check(a==isSlimeChunk(42,x,z),"slime chunk depends on query order");
        variation+=a!=isSlimeChunk(43,x,z)?1:0;
    }
    check(chunks>850 && chunks<1150 && variation>1000,"slime chunk density or seed separation");
    check(slimeHealth(0)==1 && slimeHealth(1)==4 && slimeHealth(2)==16 && slimeHealth(3)==0,"slime size health");
    check(slimeDamage(0)==0 && slimeDamage(1)==2 && slimeDamage(2)==4,"slime size damage");
}
void devices() {
    const Position p{0,0,0};
    RedstoneScheduler queue;
    State state;state.kind=Kind::Repeater;state.delay=4;
    Device repeater(state);
    const auto tick=[&](Device& device) {
        device.activeTick();
        check(queue.advance(100,[](Position,uint64_t){return true;},[&](Event e){device.apply(e,queue);}),"device queue stalled");
    };
    repeater.input({15,0,false,false},p,1,queue);
    tick(repeater);repeater.input({},p,1,queue);
    for(int i=0;i<6;++i)tick(repeater);
    check(repeater.state().power==0,"repeater rising edge early");
    tick(repeater);check(repeater.state().power==15,"short repeater input lost");
    for(int i=0;i<7;++i)tick(repeater);
    check(repeater.state().power==15,"repeater minimum pulse shortened");
    tick(repeater);check(repeater.state().power==0,"repeater falling edge missing");
    repeater.input({15,0,true,false},p,1,queue);
    for(int i=0;i<10;++i)tick(repeater);
    check(repeater.state().locked && !repeater.state().power,"locked repeater changed output");
    repeater.input({15,0,false,false},p,1,queue);
    for(int i=0;i<8;++i)tick(repeater);
    check(repeater.state().power==15,"unlock did not resume repeater");
    repeater.input({},p,1,queue);
    tick(repeater);repeater.input({15,0,false,false},p,1,queue);
    for(int i=0;i<8;++i)tick(repeater);
    check(repeater.state().power==15 && !queue.size(),"short off-pulse passed through repeater");
    repeater.input({},p,1,queue);
    for(int i=0;i<8;++i)tick(repeater);
    check(repeater.state().power==0,"sustained off input did not depower repeater");
    queue=RedstoneScheduler{};
    state.kind=Kind::Observer;state.power=0;Device observer(state);
    observer.input({0,0,false,true},p,2,queue);observer.input({0,0,false,true},p,2,queue);
    tick(observer);check(observer.state().power==0,"observer delay too short");
    tick(observer);check(observer.state().power==15,"observer rising edge missing");
    observer.input({0,0,false,true},p,2,queue);tick(observer);
    check(observer.state().power==15,"observer pulse shortened");
    tick(observer);check(observer.state().power==0 && !observer.observerPending() && !queue.size(),"observer duplicate pulse");
    queue=RedstoneScheduler{};state.kind=Kind::Lamp;Device lamp(state);
    lamp.input({1,0,false,false},p,3,queue);check(lamp.state().power==15,"lamp activation delayed");
    lamp.input({},p,3,queue);for(int i=0;i<3;++i)tick(lamp);
    check(lamp.state().power==15,"lamp extinguished early");
    lamp.input({1,0,false,false},p,3,queue);tick(lamp);
    check(lamp.state().power==15 && !queue.size(),"repowered lamp extinguished");
    lamp.input({},p,3,queue);for(int i=0;i<4;++i)tick(lamp);
    check(lamp.state().power==0,"lamp off delay missing");
    queue=RedstoneScheduler{};state.kind=Kind::Comparator;state.alternate=true;Device compare(state);
    compare.input({12,4,false,false},p,4,queue);tick(compare);check(compare.state().power==0,"comparator delay missing");
    tick(compare);check(compare.state().power==8,"comparator subtraction wrong");
    queue=RedstoneScheduler{};state.kind=Kind::Torch;state.power=15;Device torch(state);
    torch.input({15,0,false,false},p,5,queue);tick(torch);
    check(torch.state().power==15,"torch inversion early");
    tick(torch);check(torch.state().power==0,"torch inversion missing");
    torch.input({},p,5,queue);tick(torch);tick(torch);
    check(torch.state().power==15,"torch relight missing");
    // Input removed before the scheduled sample must not invent an off edge.
    torch.input({15,0,false,false},p,5,queue);tick(torch);
    torch.input({},p,5,queue);tick(torch);
    check(torch.state().power==15,"torch transient produced ghost edge");
}
void pistons() {
    int length=12;
    auto query=[&](Position p)->std::optional<PushCell>{
        if(p.x<0 || p.x>30 || p.y< -64 || p.y>=320)return std::nullopt;
        return PushCell{static_cast<uint16_t>(p.x<length?42:0),PushReaction::Move};
    };
    auto plan=planPush({0,-64,0},Face::East,query);
    check(plan && plan->moves.size()==12 && plan->moves.front().from.x==11,"twelve-block push");
    length=13;check(!planPush({0,0,0},Face::East,query),"thirteen-block push accepted");
    length=1;check(!planPush({0,-64,0},Face::Down,query),"push through world bottom");
    check(!planPush({0,319,0},Face::Up,query),"push through world top");
    auto blocked=[](Position)->std::optional<PushCell>{return PushCell{42,PushReaction::Block};};
    check(!planPush({0,0,0},Face::East,blocked),"immovable block moved");
    auto unknown=[](Position)->std::optional<PushCell>{return std::nullopt;};
    check(!planPush({0,0,0},Face::East,unknown),"unloaded chunk treated as air");
    auto fragile=[](Position)->std::optional<PushCell>{return PushCell{42,PushReaction::Destroy};};
    plan=planPush({0,0,0},Face::East,fragile);
    check(plan && plan->destroy==std::optional<Position>({0,0,0}) && plan->moves.empty(),"destroy reaction");
    for(uint8_t raw=0;raw<6;++raw) {
        const auto face=static_cast<Face>(raw);
        const auto delta=offset(face);
        const auto line=[delta](Position pos)->std::optional<PushCell> {
            const int distance=pos.x*delta.x+pos.y*delta.y+pos.z*delta.z;
            return PushCell{static_cast<uint16_t>(distance<12?42:0),PushReaction::Move};
        };
        plan=planPush({0,0,0},face,line);
        check(plan && plan->moves.size()==12 && plan->moves.back().to==delta,"six-direction piston planning");
    }
}
int main() {states();nativeStates();dust();events();burnout();snapshots();slimes();devices();pistons();std::cout<<"Redstone rules and scheduling passed\n";}
