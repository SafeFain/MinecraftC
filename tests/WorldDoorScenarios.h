#pragma once

// Behavioral integration: power follows local geometry and survives disk/streaming.
void testDoorsAndButtons() {
    for(uint16_t raw=static_cast<uint16_t>(BlockId::DOOR_FIRST);raw<static_cast<uint16_t>(BlockId::COUNT);++raw) {
        const auto id=static_cast<BlockId>(raw);
        DoorState ds; ButtonState bs;
        require(decodeDoor(id,ds)?doorBlock(ds)==id:decodeButton(id,bs) && buttonBlock(bs)==id,
            "interactive state encoding lost a serialized bit");
        require(itemForBlock(id)!=ItemId::EMPTY,"interactive state lost its inventory item");
    }
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
