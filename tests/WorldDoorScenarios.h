#pragma once

#include "world/ChunkMeshBuilder.h"
#include "world/RedstoneBlocks.h"

// Behavioral integration: power follows local geometry and survives disk/streaming.
void testDoorsAndButtons() {
    size_t stateCount=0;
    for(uint16_t raw=static_cast<uint16_t>(BlockId::DOOR_FIRST);raw<static_cast<uint16_t>(BlockId::COUNT);++raw) {
        const auto id=static_cast<BlockId>(raw);
        DoorState ds; ButtonState bs;
        const bool door=decodeDoor(id,ds),button=decodeButton(id,bs);
        if(!door&&!button)continue;
        ++stateCount;
        require(door?doorBlock(ds)==id:buttonBlock(bs)==id,
            "interactive state encoding lost a serialized bit");
        require(itemForBlock(id)!=ItemId::EMPTY,"interactive state lost its inventory item");
    }
    require(stateCount==792,"append-only door/button state count");
    // A lone panel must hand off real geometry and shadow layers in every orientation.
    for(int facing=0;facing<4;++facing)for(int hinge=0;hinge<2;++hinge)for(int opened=0;opened<2;++opened) {
        Chunk chunk(-1,-1); DoorState ds;
        ds.direction=static_cast<BedDirection>(facing);ds.rightHinge=hinge;ds.open=opened;
        chunk.setBlock(8,211,8,doorBlock(ds));
        int maxima[16][16];chunk.copyColumnMaxY(maxima);ChunkMesh mesh;
        mesh.build(-16,-16,chunk.rawBlocks(),maxima,[](int,int,int){return BlockId::AIR;},
            [](int,int,int){return LightSample{15,0};});
        require(mesh.opaqueIndexCount==36 && mesh.translucentIndexCount==0 &&
            mesh.shadowCasterIndexOffset==36 && mesh.indexCount==72,
            "door panel index-layer handoff mismatch");
        const auto box=blockCollisionBoxes(doorBlock(ds)).boxes[0];
        for(const auto& v:mesh.vertices)
            require(v.px>=8+box.min.x && v.px<=8+box.max.x &&
                v.py>=211+box.min.y && v.py<=211+box.max.y &&
                v.pz>=8+box.min.z && v.pz<=8+box.max.z,"door mesh disagrees with movement collision");
    }
    const int oldDistance=Config::RENDER_DISTANCE;
    Config::RENDER_DISTANCE=1;
    const auto root=std::filesystem::temp_directory_path()/"minecraftc-doors-buttons";
    std::filesystem::remove_all(root);
    // Generation workers and the independent cache I/O lane must both stop
    // before cleanup. World destruction drains cache writes while its store
    // and thread pool are still alive.
    {
        SaveStore store(root); ThreadPool pool(2); World world;
        world.setThreadPool(&pool); world.setSaveStore(&store);
        world.resetForNewSeed(1001); loadTarget(world); generateTarget(world,pool);
        for(int x=-2;x<=3;++x)for(int z=0;z<=4;++z)for(int y=210;y<=214;++y)
            world.setBlock(x,y,z,BlockId::AIR);
        const glm::ivec3 doorPos(0,211,1), support(1,211,1), stonePos(1,211,2), woodPos(1,211,0);
        world.setBlock(0,210,1,BlockId::STONE);
        DoorState door;door.material=DoorMaterial::Iron;door.direction=BedDirection::South;
        require(world.placeDoor(doorPos,door),"iron door placement failed");
        ButtonState onDoor;onDoor.attachment=FaceDir::BACK;
        require(!world.placeButton({0,211,2},onDoor),"moving door accepted an attached button");
        require(world.interactDoor(doorPos),"iron door use must consume the interaction");
        require(decodeDoor(world.getBlock(0,211,1),door) && !door.open,"iron door opened manually");
        world.setBlock(support.x,support.y,support.z,BlockId::STONE);
        ButtonState stone;stone.material=DoorMaterial::Iron;stone.attachment=FaceDir::BACK;
        ButtonState wood;wood.material=DoorMaterial::Birch;wood.attachment=FaceDir::FRONT;
        require(world.placeButton(stonePos,stone) && world.placeButton(woodPos,wood),"supported button placement failed");
        require(world.activateButton(stonePos) && world.activateButton(woodPos),"button activation failed");
        DoorState upper;
        require(decodeDoor(world.getBlock(0,211,1),door) && door.open && door.powered &&
            decodeDoor(world.getBlock(0,212,1),upper) && upper.upper && upper.open && upper.powered,
            "local strong power did not synchronize both door halves");
        const auto ray=world.raycast({1.5,211.5,4.5},{0,0,-1},6);
        require(ray && ray->blockPos==stonePos,"button selection missed its narrow shape");
        require(blockCollisionBoxes(world.getBlock(1,211,2)).count==0,"button blocks movement");
        for(int tick=0;tick<20;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        require(decodeButton(world.getBlock(1,211,2),stone) && !stone.pressed &&
            decodeDoor(world.getBlock(0,211,1),door) && door.open,"stone timing or multi-button power failed");
        world.activateButton(woodPos); // Already pressed: does not reset the remaining ten ticks.
        for(int tick=0;tick<10;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        require(decodeDoor(world.getBlock(0,211,1),door) && !door.open && !door.powered,
            "last pulse did not close iron door or repeated use extended pulse");
        world.activateButton(woodPos);
        for(int tick=0;tick<10;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        world.beginModifiedChunkAutosave();
        while(!world.flushModifiedChunks(32)) {}
        drainWorkers(world,pool);world.resetForNewSeed(1001);
        loadTarget(world);generateTarget(world,pool);
        for(int tick=0;tick<19;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        require(decodeDoor(world.getBlock(0,211,1),door) && door.open,"saved button timer expired early");
        world.tickInteractiveBlocks([](const auto&){return false;});
        require(decodeDoor(world.getBlock(0,211,1),door) && !door.open,"saved button timer failed to release");
        world.activateButton(woodPos);
        for(int tick=0;tick<60;++tick)world.tickInteractiveBlocks([&](const auto& p){return p==woodPos;});
        require(decodeButton(world.getBlock(1,211,0),wood) && wood.pressed,"arrow did not hold wooden button");
        for(int tick=0;tick<30;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        require(decodeButton(world.getBlock(1,211,0),wood) && !wood.pressed,"removed arrow left button latched");
        world.activateButtonsAtArrow({1.5,211.5,2.03125});
        require(decodeButton(world.getBlock(1,211,2),stone) && !stone.pressed,"arrow activated stone button");
        world.activateButtonsAtArrow({1.5,211.5,.9375});
        require(decodeButton(world.getBlock(1,211,0),wood) && wood.pressed,"arrow impact failed to activate wood button");
        for(int tick=0;tick<30;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        world.setBlock(1,211,1,BlockId::GLASS);
        world.activateButton(woodPos);
        require(decodeDoor(world.getBlock(0,211,1),door) && !door.open,"transparent support conducted strong power");
        for(int tick=0;tick<30;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        world.setBlock(1,211,1,BlockId::STONE);
        // Power crosses a negative-coordinate chunk edge without recursive wiring.
        const glm::ivec3 edgeDoor(-1,211,3),edgeButton(0,211,4);
        world.setBlock(-1,210,3,BlockId::STONE);world.setBlock(0,211,3,BlockId::STONE);
        door.open=door.powered=door.upper=false;
        require(world.placeDoor(edgeDoor,door) && world.placeButton(edgeButton,stone),"boundary fixture placement failed");
        world.activateButton(edgeButton);
        require(decodeDoor(world.getBlock(-1,211,3),door) && door.open,"button power failed across negative chunk edge");
        for(int tick=0;tick<20;++tick)world.tickInteractiveBlocks([](const auto&){return false;});
        require(decodeDoor(world.getBlock(-1,211,3),door) && !door.open,"cross-boundary release failed");
        world.setBlock(0,212,1,BlockId::AIR);
        require(world.getBlock(0,211,1)==BlockId::AIR,"breaking upper door left lower half");
        door.material=DoorMaterial::Oak;door.open=door.powered=door.upper=false;
        require(world.placeDoor(doorPos,door) && world.setDoorOpen(doorPos,true),"wooden door interaction failed");
        require(!world.placeDoor(doorPos,door),"occupied door placement succeeded");
        world.setBlock(0,210,1,BlockId::AIR);
        require(world.getBlock(0,211,1)==BlockId::AIR && world.getBlock(0,212,1)==BlockId::AIR,
            "support removal left door halves");
        const auto drops=world.takeSupportDrops();
        require(drops.size()==1 && drops[0].second.id==ItemId::OAK_DOOR,"door support dropped more than one item");
        world.setBlock(1,211,1,BlockId::AIR);
        require(world.getBlock(1,211,0)==BlockId::AIR && world.getBlock(1,211,2)==BlockId::AIR,
            "unsupported buttons remained installed");
        require(world.takeSupportDrops().size()==2,"unsupported buttons did not drop once each");
        drainWorkers(world,pool);
    }
    Config::RENDER_DISTANCE=oldDistance;
    std::filesystem::remove_all(root);
}

void testRedstoneCircuits() {
    const int oldDistance=Config::RENDER_DISTANCE;Config::RENDER_DISTANCE=1;
    const auto root=std::filesystem::temp_directory_path()/("minecraftc-redstone-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        SaveStore store(root);ThreadPool pool(2);World world;world.setThreadPool(&pool);world.setSaveStore(&store);
        world.resetForNewSeed(1001);loadTarget(world);generateTarget(world,pool);
        for(int x=-12;x<=12;++x)for(int z=5;z<=11;++z) {
            world.setBlock(x,210,z,BlockId::STONE);
            for(int y=211;y<=214;++y)world.setBlock(x,y,z,BlockId::AIR);
        }
        Redstone::State lever;lever.kind=Redstone::Kind::Lever;lever.facing=Redstone::Face::Up;
        require(world.placeRedstone({-9,211,8},Redstone::block(lever)),"lever placement");
        Redstone::State dust;
        for(int x=-8;x<=6;++x)require(world.placeRedstone({x,211,8},Redstone::block(dust)),"dust placement");
        DoorState door;door.material=DoorMaterial::Iron;
        require(world.placeDoor({7,211,8},door),"wired door placement");
        const auto tick=[&](int count){for(int i=0;i<count;++i)world.tickInteractiveBlocks([](const auto&){return false;});};
        require(world.interactRedstone({-9,211,8}),"lever interaction");tick(2);
        require(decodeDoor(world.getBlock(7,211,8),door)&&door.open,"fifteen-wire door did not open");
        for(int x=-8;x<=6;++x) {
            const auto state=Redstone::blockState(world.getBlock(x,211,8));
            require(state && state->power==15-(x+8),"world dust attenuation");
        }
        world.setBlock(0,211,8,BlockId::AIR);tick(2);
        require(decodeDoor(world.getBlock(7,211,8),door)&&!door.open,"cut wire retained power");
        require(world.placeRedstone({0,211,8},Redstone::block(dust)),"wire reconnect");tick(2);
        require(decodeDoor(world.getBlock(7,211,8),door)&&door.open,"reconnected wire did not power door");
        Redstone::State repeater;repeater.kind=Redstone::Kind::Repeater;repeater.facing=Redstone::Face::East;repeater.delay=2;
        world.setBlock(0,211,8,Redstone::block(repeater));tick(5);
        require(Redstone::blockState(world.getBlock(0,211,8))->power==15,"world repeater failed");
        require(Redstone::blockState(world.getBlock(1,211,8))->power==15,"repeater did not restore wire strength");
        world.interactRedstone({-9,211,8});tick(6);
        require(decodeDoor(world.getBlock(7,211,8),door)&&!door.open,"wired door did not depower");
        Redstone::State observer;observer.kind=Redstone::Kind::Observer;observer.facing=Redstone::Face::East;
        world.setBlock(0,211,10,Redstone::block(observer));tick(1);
        world.setBlock(-1,211,10,BlockId::STONE);tick(1);
        require(Redstone::blockState(world.getBlock(0,211,10))->power==0,"observer world pulse early");
        tick(1);require(Redstone::blockState(world.getBlock(0,211,10))->power==15,"observer world pulse missing");
        tick(2);require(Redstone::blockState(world.getBlock(0,211,10))->power==0,"observer world pulse stuck");
        world.setBlock(-9,210,8,BlockId::AIR);
        require(world.getBlock(-9,211,8)==BlockId::AIR,"unsupported lever survived");
        world.setBlock(-9,210,8,BlockId::STONE);
        require(world.placeRedstone({-9,211,8},Redstone::block(lever)),"restore lever after support loss");
        world.interactRedstone({-9,211,8});tick(1);
        require(Redstone::blockState(world.getBlock(0,211,8))->power==0,"saved repeater fixture already fired");
        world.beginModifiedChunkAutosave();while(!world.flushModifiedChunks(32)){}
        drainWorkers(world,pool);world.resetForNewSeed(1001);loadTarget(world);generateTarget(world,pool);
        tick(2);require(Redstone::blockState(world.getBlock(0,211,8))->power==0,"saved repeater delay expired early");
        tick(1);require(Redstone::blockState(world.getBlock(0,211,8))->power==15,"saved repeater delay restarted or was lost");
        world.interactRedstone({-9,211,8});tick(1);
        world.interactRedstone({-9,211,8});
        for(int i=0;i<6;++i) {
            tick(1);require(Redstone::blockState(world.getBlock(0,211,8))->power==15,"world repeater passed short off-pulse");
        }
        // Compare an eight-strength inventory input with eligible and ineligible
        // side sources. Inventory mutations must not trigger an observer.
        for(int x=-4;x<=4;++x)for(int z=6;z<=10;++z) {
            world.setBlock(x,220,z,BlockId::STONE);
            for(int y=221;y<=223;++y)world.setBlock(x,y,z,BlockId::AIR);
        }
        Redstone::State compare;compare.kind=Redstone::Kind::Comparator;compare.facing=Redstone::Face::East;
        world.setBlock(-1,221,8,BlockId::CHEST);
        auto* chest=world.getBlockEntity({-1,221,8});require(chest!=nullptr,"comparator chest fixture");
        for(int i=0;i<14;++i)chest->chest[i]={ItemId::STONE,64,0};
        world.setBlock(0,221,8,Redstone::block(compare));
        lever.alternate=true;world.setBlock(0,221,7,Redstone::block(lever));tick(3);
        const auto comparePower=[&](){return Redstone::blockState(world.getBlock(0,221,8))->power;};
        require(comparePower()==8,"lever incorrectly supplied comparator side input");
        Redstone::State torch;torch.kind=Redstone::Kind::Torch;torch.facing=Redstone::Face::Up;torch.power=15;
        world.setBlock(0,221,7,Redstone::block(torch));tick(4);
        require(comparePower()==8,"torch incorrectly supplied comparator side input");
        Redstone::State constant;constant.kind=Redstone::Kind::Block;constant.power=15;
        world.setBlock(0,221,7,Redstone::block(constant));tick(3);
        require(comparePower()==0,"redstone block failed comparator side input");
        // A side diode contributes its actual output face and analog strength.
        Redstone::State side;side.kind=Redstone::Kind::Comparator;side.facing=Redstone::Face::South;
        world.setBlock(0,221,6,BlockId::CHEST);
        auto* sideChest=world.getBlockEntity({0,221,6});require(sideChest!=nullptr,"side comparator chest fixture");
        for(int i=0;i<6;++i)sideChest->chest[i]={ItemId::STONE,64,0};
        world.setBlock(0,221,7,Redstone::block(side));tick(6);
        require(comparePower()==8,"comparison mode rejected weaker side diode");
        require(world.interactRedstone({0,221,8}),"switch comparator mode");tick(3);
        require(comparePower()==4,"world comparator subtraction");
        world.setBlock(0,221,7,BlockId::AIR);world.setBlock(0,221,6,BlockId::AIR);
        world.interactRedstone({0,221,8});
        // The rear solid block's strength fifteen takes precedence over a
        // container beyond it; weaker power must yield to the container read.
        world.setBlock(-2,221,8,BlockId::CHEST);
        auto* farChest=world.getBlockEntity({-2,221,8});require(farChest!=nullptr,"far comparator chest fixture");
        farChest->chest[0]={ItemId::STONE,1,0};
        world.setBlock(-1,221,8,BlockId::STONE);tick(3);
        require(comparePower()==1,"comparator failed to read through conductive block");
        auto wallLever=lever;wallLever.facing=Redstone::Face::North;
        world.setBlock(-1,221,7,Redstone::block(wallLever));tick(3);
        require(comparePower()==15,"strength fifteen did not override distant container");
        world.setBlock(-1,221,7,BlockId::AIR);tick(3);
        require(comparePower()==1,"container did not resume after rear block depowered");
        observer.facing=Redstone::Face::West;
        world.setBlock(-3,221,8,Redstone::block(observer));tick(4);
        farChest=world.getBlockEntity({-2,221,8});
        for(int i=0;i<14;++i)farChest->chest[i]={ItemId::STONE,64,0};
        for(int i=0;i<8;++i) {
            tick(1);require(Redstone::blockState(world.getBlock(-3,221,8))->power==0,"inventory mutation triggered observer");
        }
        require(comparePower()==8,"inventory change failed to update comparator");
        for(int x=-4;x<=3;++x)for(int z=6;z<=11;++z) {
            world.setBlock(x,230,z,BlockId::STONE);
            for(int y=231;y<=233;++y)world.setBlock(x,y,z,BlockId::AIR);
        }
        compare.alternate=false;
        world.setBlock(0,231,8,Redstone::block(compare));world.setBlock(-1,231,8,BlockId::STONE);
        wallLever.facing=Redstone::Face::West;
        world.setBlock(-2,231,8,Redstone::block(wallLever));tick(3);
        const auto conductedPower=[&](){return Redstone::blockState(world.getBlock(0,231,8))->power;};
        require(conductedPower()==15,"rear strongly charged block failed to power comparator");
        world.setBlock(-2,231,8,BlockId::STONE);
        world.setBlock(-3,231,8,Redstone::block(wallLever));tick(3);
        require(conductedPower()==0,"conductors relayed power through two solid blocks");
        world.setBlock(-2,231,8,Redstone::block(constant));tick(3);
        require(conductedPower()==0,"redstone block incorrectly charged adjacent conductor");
        world.setBlock(-1,231,8,Redstone::block(constant));tick(3);
        require(conductedPower()==15,"direct redstone block failed to power comparator");
        world.setBlock(0,231,10,Redstone::block(torch));
        world.setBlock(1,230,10,Redstone::block(constant));tick(3);
        require(Redstone::blockState(world.getBlock(0,231,10))->power==15,"redstone block incorrectly inverted torch through support");
        wallLever.facing=Redstone::Face::East;
        world.setBlock(1,230,10,Redstone::block(wallLever));tick(3);
        require(Redstone::blockState(world.getBlock(0,231,10))->power==0,"attached lever failed to invert torch");
        world.setBlock(0,230,10,BlockId::GLASS);tick(3);
        require(Redstone::blockState(world.getBlock(0,231,10))->power==15,"transparent support conducted power into torch");
        world.setBlock(0,230,10,Redstone::block(constant));tick(3);
        require(Redstone::blockState(world.getBlock(0,231,10))->power==0,"redstone block failed to invert attached torch");
        drainWorkers(world,pool);
    }
    // Warm chunks remain in CPU memory but are outside the active simulation.
    // Neither button release time nor component delay may elapse there.
    Config::RENDER_DISTANCE=0;
    {
        SaveStore store(root/"warm");ThreadPool pool(2);World world;
        world.setThreadPool(&pool);world.setSaveStore(&store);world.resetForNewSeed(1001);
        loadTarget(world);generateTarget(world,pool);
        for(int x=5;x<=11;++x)for(int z=6;z<=11;++z) {
            world.setBlock(x,220,z,BlockId::STONE);
            for(int y=221;y<=223;++y)world.setBlock(x,y,z,BlockId::AIR);
        }
        Redstone::State source;source.kind=Redstone::Kind::Lever;source.facing=Redstone::Face::Up;source.alternate=true;
        world.setBlock(7,221,8,Redstone::block(source));
        Redstone::State delayed;delayed.kind=Redstone::Kind::Repeater;delayed.facing=Redstone::Face::East;delayed.delay=4;
        world.setBlock(8,221,8,Redstone::block(delayed));
        ButtonState button;button.material=DoorMaterial::Iron;button.attachment=FaceDir::TOP;
        require(world.placeButton({8,221,10},button),"warm button placement");
        require(world.activateButton({8,221,10}),"warm button activation");
        const auto tick=[&](int count){for(int i=0;i<count;++i)world.tickInteractiveBlocks([](const auto&){return false;});};
        tick(2);
        auto* origin=world.getChunk(0,0);
        for(int i=0;i<400 && origin->lifecycle.load()!=Chunk::LifecycleState::Warm;++i) {
            world.update({16.5,64.0,.5},1);world.enqueueGeneration();world.processCompletedGenerations(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(origin->lifecycle.load()==Chunk::LifecycleState::Warm,"circuit fixture did not retire warm");
        tick(40);
        require(world.getBlockEntity({8,221,10})->buttonRemaining==18,"warm button consumed release time");
        world.update({.5,64.0,.5},1);world.enqueueGeneration();world.processCompletedGenerations(false);
        require(world.getChunk(0,0)==origin && origin->lifecycle.load()!=Chunk::LifecycleState::Warm,"warm circuit promotion");
        tick(5);require(Redstone::blockState(world.getBlock(8,221,8))->power==0,"warm repeater expired early");
        tick(1);require(Redstone::blockState(world.getBlock(8,221,8))->power==15,"warm repeater lost remaining delay");
        tick(11);require(decodeButton(world.getBlock(8,221,10),button)&&button.pressed,"promoted button released early");
        tick(1);require(decodeButton(world.getBlock(8,221,10),button)&&!button.pressed,"promoted button failed release");
        drainWorkers(world,pool);
    }
    std::filesystem::remove_all(root);
    Config::RENDER_DISTANCE=oldDistance;
}
