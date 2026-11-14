#pragma once

#include "TestSupport.h"
#include "world/RedstoneBlocks.h"

namespace RedstoneTickScenarios {
// Public World behavior: work slices must preserve logical delays and pulse
// lengths. Existing circuit tests use the default budget and cannot expose
// resampling between due events. A real off edge is the observer control.
// Superflat, one chunk and finite synchronous steps avoid worker-window races.
struct Scene {
    TestSupport::TemporaryDirectory directory;
    SaveStore store{directory.path()};
    ThreadPool pool{2};
    World world;
    Scene() {
        world.setThreadPool(&pool);world.setSaveStore(&store);
        world.resetForNewSeed(1001,WorldType::Superflat);
        loadTarget(world);generateTarget(world,pool);
        for(int x=3;x<=13;++x)for(int z=3;z<=13;++z)
            world.setBlock(x,210,z,BlockId::STONE);
    }
    ~Scene() {drainWorkers(world,pool);}
    void put(glm::ivec3 p,Redstone::Kind kind,Redstone::Face face=Redstone::Face::North,
             uint8_t power=0,bool on=false,uint8_t delay=1) {
        Redstone::State state{kind,face,power,delay,on,false};
        require(world.placeRedstone(p,Redstone::block(state)),"tick fixture placement");
    }
    uint8_t power(glm::ivec3 p) const {
        const auto value=Redstone::blockState(world.getBlock(p.x,p.y,p.z));
        require(value.has_value(),"tick fixture lost a circuit block");return value->power;
    }
    template<class Pending>
    unsigned step(RedstoneWorkBudget budget,Pending pending) {
        unsigned slices=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!world.tickInteractiveBlocks([](const auto&){return false;},budget)) {
            ++slices;pending();
            require(std::chrono::steady_clock::now()<deadline,"bounded circuit tick did not finish");
        }
        return slices;
    }
    unsigned step(RedstoneWorkBudget budget={}) {return step(budget,[]{});}
};

inline void comparatorBatch(RedstoneWorkBudget budget,bool reverse) {
    Scene scene;
    const int source=reverse?9:7,target=8;
    const auto face=reverse?Redstone::Face::West:Redstone::Face::East;
    scene.world.setBlock(reverse?10:6,211,8,BlockId::CHEST); // empty: source falls after two ticks
    scene.put({source,211,8},Redstone::Kind::Comparator,face,9);
    scene.put({target,211,8},Redstone::Kind::Comparator,face);
    const uint8_t expected[]={0,9,9,0,0,0};
    for(const auto power:expected) {
        scene.step(budget);
        require(scene.power({target,211,8})==power,"event slices changed a comparator's two-tick sampled delay");
    }
}

inline void lampHandoff(RedstoneWorkBudget budget) {
    Scene scene;
    const glm::ivec3 lamp{8,211,8},observer{8,211,9};
    scene.put({7,211,8},Redstone::Kind::Lever,Redstone::Face::Up,0,true);
    scene.put({10,211,8},Redstone::Kind::Lever,Redstone::Face::Up);
    scene.put({9,211,8},Redstone::Kind::Repeater,Redstone::Face::West,0,false,2);
    scene.put(lamp,Redstone::Kind::Lamp);
    scene.put(observer,Redstone::Kind::Observer,Redstone::Face::South);
    for(int i=0;i<12;++i)scene.step(budget);
    scene.world.interactRedstone({7,211,8});
    scene.world.interactRedstone({10,211,8});
    // Repeater on and lamp off are due together. The settled lamp never changes.
    for(int i=0;i<10;++i) {
        scene.step(budget,[&] {require(scene.power(lamp)==15,"unfinished tick published a lamp-off glitch");});
        require(scene.power(lamp)==15,"stable handoff extinguished the lamp");
        require(scene.power(observer)==0,"observer saw a transient lamp-off state");
    }
    scene.world.interactRedstone({10,211,8});
    unsigned pulseTicks=0;
    for(int i=0;i<14;++i) {scene.step(budget);pulseTicks+=scene.power(observer)==15;}
    require(scene.power(lamp)==0 && pulseTicks==2,"real lamp-off edge must produce one two-tick observer pulse");
}

inline void wireHandoff(RedstoneWorkBudget budget) {
    Scene scene;
    const glm::ivec3 wire{8,211,8},observer{8,211,9};
    scene.put({7,211,8},Redstone::Kind::Lever,Redstone::Face::Up,0,true);
    scene.put({10,211,8},Redstone::Kind::Lever,Redstone::Face::Up);
    scene.put({9,211,8},Redstone::Kind::Repeater,Redstone::Face::West,0,false,2);
    scene.put(wire,Redstone::Kind::Dust);
    scene.put(observer,Redstone::Kind::Observer,Redstone::Face::South);
    for(int i=0;i<12;++i)scene.step(budget);
    scene.world.interactRedstone({10,211,8});
    for(int i=0;i<3;++i)scene.step(budget);
    scene.world.interactRedstone({7,211,8});
    // Pre-event wire recomputation removes the old input; the due repeater
    // restores it in the same tick. Only the committed edge is observable.
    for(int i=0;i<8;++i) {
        scene.step(budget);
        require(scene.power(wire)==15 && scene.power(observer)==0,"observer saw an intermediate wire propagation edge");
    }
    scene.world.interactRedstone({10,211,8});
    unsigned pulseTicks=0;
    for(int i=0;i<14;++i) {scene.step(budget);pulseTicks+=scene.power(observer)==15;}
    require(scene.power(wire)==0 && pulseTicks==2,"real wire-off edge must produce one two-tick observer pulse");
}

inline void pistonEdgesAndDrops() {
    for(const bool sticky:{false,true})for(const bool up:{false,true}) {
        Scene scene;const int y=up?318:-63;const auto face=up?Redstone::Face::Up:Redstone::Face::Down;
        for(const auto p:std::vector<glm::ivec3>{{8,y,8},{9,y,8},{8,y+(up?1:-1),8}})scene.world.setBlock(p.x,p.y,p.z,BlockId::AIR);
        scene.put({8,y,8},Redstone::Kind::Piston,face,0,sticky);
        scene.put({9,y,8},Redstone::Kind::Block,Redstone::Face::North,15);
        for(int i=0;i<3;++i)scene.step({1,1});
        require(scene.power({8,y,8})==15,"legal edge piston could not extend");
        scene.world.setBlock(9,y,8,BlockId::AIR);
        for(int i=0;i<3;++i)scene.step({1,1});
        require(scene.power({8,y,8})==0,"edge piston could not retract when pull target was out of bounds");
    }
    for(const bool up:{false,true}) {
        Scene scene;const int y=up?319:-64;
        scene.world.setBlock(8,y,8,BlockId::AIR);scene.world.setBlock(9,y,8,BlockId::AIR);
        scene.put({8,y,8},Redstone::Kind::Piston,up?Redstone::Face::Up:Redstone::Face::Down);
        scene.put({9,y,8},Redstone::Kind::Block,Redstone::Face::North,15);scene.step();
        require(scene.power({8,y,8})==0 && scene.world.pistonMotions().empty(),"out-of-bounds push partially extended");
    }
    {
        Scene scene;
        scene.put({8,230,8},Redstone::Kind::Piston,Redstone::Face::East);
        scene.world.setBlock(9,230,8,BlockId::STONE);
        scene.put({9,231,8},Redstone::Kind::Torch,Redstone::Face::Up,15);
        scene.put({8,231,8},Redstone::Kind::Block,Redstone::Face::North,15);
        for(int i=0;i<4;++i)scene.step({1,1});
        const auto drops=scene.world.takeSupportDrops();
        require(drops.size()==1 && drops[0].second.id==ItemId::REDSTONE_TORCH && drops[0].second.count==1,"moving support did not drop its torch exactly once");
        scene.world.setBlock(9,230,8,BlockId::AIR);
        const auto pistonDrop=scene.world.takeSupportDrops();
        require(scene.world.getBlock(8,230,8)==BlockId::AIR && pistonDrop.size()==1 && pistonDrop[0].second.id==ItemId::PISTON,"broken head left or duplicated its base");
    }
    {
        Scene scene;scene.world.setBlock(9,230,8,BlockId::STONE);
        DoorState door;require(scene.world.placeDoor({9,231,8},door),"paired piston drop fixture");
        scene.put({8,231,8},Redstone::Kind::Piston,Redstone::Face::East);
        scene.put({8,232,8},Redstone::Kind::Block,Redstone::Face::North,15);
        for(int i=0;i<3;++i)scene.step({1,1});
        const auto drops=scene.world.takeSupportDrops();
        require(drops.size()==1 && drops[0].second.id==itemForBlock(doorBlock(door)) && scene.world.getBlock(9,232,8)==BlockId::AIR,"piston duplicated door drop or left its upper half");
    }
    {
        Scene scene;
        scene.put({8,240,8},Redstone::Kind::Piston,Redstone::Face::East);
        scene.world.setBlock(9,240,8,BlockId::STONE);scene.world.setBlock(10,240,8,BlockId::WATER);
        scene.put({8,241,8},Redstone::Kind::Block,Redstone::Face::North,15);
        for(int i=0;i<3;++i)scene.step();
        require(scene.world.getBlock(10,240,8)==BlockId::STONE && scene.world.getBlock(11,240,8)==BlockId::AIR && scene.world.takeSupportDrops().empty(),"piston moved water or produced a fluid item");
    }
    for(const auto blocked:std::array<BlockId,6>{{BlockId::BEDROCK,BlockId::OBSIDIAN,BlockId::CRYING_OBSIDIAN,BlockId::RESPAWN_ANCHOR,BlockId::ENCHANTING_TABLE,BlockId::BLAZE_SPAWNER}}) {
        Scene scene;scene.put({8,240,8},Redstone::Kind::Piston,Redstone::Face::East);
        scene.world.setBlock(9,240,8,blocked);scene.put({8,241,8},Redstone::Kind::Block,Redstone::Face::North,15);scene.step();
        require(scene.power({8,240,8})==0 && scene.world.getBlock(9,240,8)==blocked && scene.world.pistonMotions().empty(),"piston moved a protected block or workstation");
    }
    {
        Scene scene;scene.put({8,240,8},Redstone::Kind::Piston,Redstone::Face::East);
        scene.put({9,240,8},Redstone::Kind::Observer,Redstone::Face::North,15);
        scene.put({8,241,8},Redstone::Kind::Block,Redstone::Face::North,15);
        for(int i=0;i<6;++i)scene.step({1,1});
        require(scene.power({10,240,8})==0,"moved observer retained orphaned power without its off event");
    }
    Config::RENDER_DISTANCE=0;
    {
        Scene scene;
        require(!scene.world.getLoadedBlock(16,240,8),"unknown destination fixture unexpectedly loaded");
        scene.put({14,240,8},Redstone::Kind::Piston,Redstone::Face::East);
        scene.world.setBlock(15,240,8,BlockId::STONE);
        scene.put({14,241,8},Redstone::Kind::Block,Redstone::Face::North,15);scene.step({1,1});
        require(scene.world.pistonMotions().empty() && scene.power({14,240,8})==0 && scene.world.getBlock(15,240,8)==BlockId::STONE && !scene.world.getLoadedBlock(16,240,8),"push changed an unknown boundary or generated its neighbor");
    }
    {
        Scene scene;
        scene.put({15,211,8},Redstone::Kind::Observer,Redstone::Face::West);scene.step();
        scene.world.setBlock(16,211,8,BlockId::STONE); // explicit edit, not a loaded dependency
        require(!scene.world.getLoadedBlock(16,211,8),"observer unknown-dependency fixture loaded unexpectedly");
        for(int i=0;i<5;++i){scene.step({1,1});require(scene.power({15,211,8})==0,"blocked observer consumed a newly notified pulse");}
    }
    Config::RENDER_DISTANCE=1;
    {
        Scene scene;
        scene.put({7,211,8},Redstone::Kind::Lever,Redstone::Face::Up,0,true);
        scene.put({8,211,8},Redstone::Kind::Repeater,Redstone::Face::East,0,false,4);
        scene.step();
        require(!scene.world.tickInteractiveBlocks([](const auto&){return false;},{1,1}),"pending-save fixture did not exhaust scan work");
        scene.world.persistChunkAt({8,211,8});
        scene.world.beginModifiedChunkAutosave();while(!scene.world.flushModifiedChunks(32)){}
        drainWorkers(scene.world,scene.pool);scene.world.resetForNewSeed(1001,WorldType::Superflat);
        loadTarget(scene.world);generateTarget(scene.world,scene.pool);
        for(int i=0;i<5;++i){scene.step({1,1});require(scene.power({8,211,8})==0,"pending save consumed component delay");}
        scene.step({1,1});require(scene.power({8,211,8})==15,"pending save lost component delay");
    }
}

using Trace=std::vector<std::array<unsigned,4>>;
// The existing pure push planner cannot exercise World writes, motion ownership,
// saves, head removal or pull behavior. Observe those public contracts directly.
inline void piston(bool sticky,Redstone::Face face,unsigned length,bool blocked=false,bool restore=false) {
    Scene scene;
    const glm::ivec3 base{8,240,8};
    const auto direction=Redstone::vector(Redstone::offset(face));
    const auto source=base+(face<Redstone::Face::North?glm::ivec3(1,0,0):glm::ivec3(0,1,0));
    scene.put(base,Redstone::Kind::Piston,face,0,sticky);
    for(unsigned i=1;i<=length;++i) {
        const auto p=base+direction*int(i);scene.world.setBlock(p.x,p.y,p.z,blocked?BlockId::CHEST:BlockId::STONE);
    }
    scene.put(source,Redstone::Kind::Block,Redstone::Face::North,15);
    scene.step();
    if(blocked || length>12) {
        require(scene.world.pistonMotions().empty() && scene.power(base)==0,"blocked push changed the piston");
        for(unsigned i=1;i<=length;++i) {
            const auto p=base+direction*int(i);
            require(scene.world.getBlock(p.x,p.y,p.z)==(blocked?BlockId::CHEST:BlockId::STONE),"blocked push partially moved its chain");
        }
        return;
    }
    require(scene.world.pistonMotions().size()==1,"powered piston did not start a transaction");
    if(restore) {
        scene.step();
        // Deliberately persist only the base partition. The journal must repair
        // the other side of a cross-chunk write after a partial autosave.
        scene.world.persistChunkAt(base);
        drainWorkers(scene.world,scene.pool);scene.world.resetForNewSeed(1001,WorldType::Superflat);
        loadTarget(scene.world);generateTarget(scene.world,scene.pool);
        require(scene.world.pistonMotions().size()==1 && scene.world.pistonMotions()[0].age==1,"saved piston lost motion progress");
    } else scene.step();
    scene.step();
    require(scene.world.pistonMotions().empty(),"piston motion did not finish in two ticks");
    const auto head=base+direction;
    require(Redstone::native(scene.world.getBlock(head.x,head.y,head.z)) &&
        Redstone::pistonHead(Redstone::nativeSlot(scene.world.getBlock(head.x,head.y,head.z))),"extended piston head missing");
    for(unsigned i=2;i<=length+1;++i) {
        const auto p=base+direction*int(i);require(scene.world.getBlock(p.x,p.y,p.z)==BlockId::STONE,"push lost a block");
    }
    scene.world.setBlock(source.x,source.y,source.z,BlockId::AIR);
    scene.step();scene.step();scene.step();
    require(scene.power(base)==0 && scene.world.pistonMotions().empty(),"piston failed to retract");
    require(scene.world.getBlock(head.x,head.y,head.z)==(sticky && length?BlockId::STONE:BlockId::AIR),"piston pull behavior incorrect");
}
inline Trace buttonTrace(RedstoneWorkBudget budget) {
    Scene scene;
    ButtonState button;button.material=DoorMaterial::Iron;button.attachment=FaceDir::TOP;
    require(scene.world.placeButton({5,211,6},button),"budget button placement");
    for(const auto p:std::vector<glm::ivec3>{{6,211,6},{7,211,6},{9,211,6},{10,211,6},{7,211,8},{7,211,9}})
        scene.put(p,Redstone::Kind::Dust);
    scene.put({8,211,6},Redstone::Kind::Repeater,Redstone::Face::East);
    scene.put({7,211,7},Redstone::Kind::Repeater,Redstone::Face::South);
    const glm::ivec3 first{11,211,6},second{7,211,10};
    scene.put(first,Redstone::Kind::Lamp);scene.put(second,Redstone::Kind::Lamp);
    DoorState door;door.material=DoorMaterial::Iron;
    require(scene.world.placeDoor({12,211,6},door),"budget door placement");
    for(int i=0;i<8;++i)scene.step();
    require(scene.world.activateButton({5,211,6}),"budget button activation");
    Trace trace;unsigned continuations=0;
    for(int tick=0;tick<32;++tick) {
        const auto lampA=scene.power(first),lampB=scene.power(second);
        const bool open=decodeDoor(scene.world.getBlock(12,211,6),door)&&door.open;
        continuations+=scene.step(budget,[&] {
            require(scene.power(first)==lampA && scene.power(second)==lampB,"pending event tick drove a lamp early");
            require(decodeDoor(scene.world.getBlock(12,211,6),door)&&door.open==open,"pending event tick drove a door early");
        });
        require(decodeButton(scene.world.getBlock(5,211,6),button),"budget fixture lost its button");
        require(decodeDoor(scene.world.getBlock(12,211,6),door),"budget fixture lost its door");
        trace.push_back({unsigned(button.pressed),scene.power(first),scene.power(second),unsigned(door.open)});
    }
    require(trace[18][0]==1 && trace[19][0]==0,"queue continuations consumed stone-button pulse time");
    require(trace[10][1]==15 && trace[10][2]==15 && trace[10][3]==1,"button did not drive both branches and door");
    require(trace.back()==std::array<unsigned,4>{0,0,0,0},"circuit did not clear after button release");
    if(budget.propagation==1)require(continuations>0,"small wire budget did not exercise continuation");
    return trace;
}

inline void run(const std::string& scenario) {
    const int oldDistance=Config::RENDER_DISTANCE;
    struct Restore {int value;~Restore(){Config::RENDER_DISTANCE=value;}} restore{oldDistance};
    Config::RENDER_DISTANCE=0;
    if(scenario=="comparators") {
        for(const auto budget:std::array<RedstoneWorkBudget,3>{{{}, {1,1},{2,2}}})
            for(const bool reverse:{false,true})comparatorBatch(budget,reverse);
    } else if(scenario=="lamps") {
        for(const auto budget:std::array<RedstoneWorkBudget,3>{{{}, {1,1},{2,2}}}) {
            lampHandoff(budget);wireHandoff(budget);
        }
    } else if(scenario=="buttons") {
        const auto reference=buttonTrace({});
        require(buttonTrace({1,1})==reference && buttonTrace({2,2})==reference,"per-logical-tick circuit traces depend on work budget");
    } else if(scenario=="pistons") {
        Config::RENDER_DISTANCE=1;
        for(unsigned face=0;face<6;++face)for(bool sticky:{false,true})piston(sticky,static_cast<Redstone::Face>(face),1);
        piston(false,Redstone::Face::East,12);
        piston(false,Redstone::Face::East,13);
        piston(true,Redstone::Face::East,1,true);
        piston(true,Redstone::Face::East,12,false,true);
        pistonEdgesAndDrops();
    } else throw std::invalid_argument("Unknown redstone tick scenario");
}
} // namespace RedstoneTickScenarios
