#pragma once

#include "OverworldStructureFixtures.h"
#include <deque>

// Half-height standing states and quarter-block horizontal samples exercise
// actual collision boxes, including both halves of stairs, with player-sized
// clearance. Upward motion is limited to a half step (no jump or digging).
template <typename BlockAt>
std::set<std::tuple<int,int,int>> walkStructure(
    const StructurePlacement& p, const BlockAt& blockAt) {
    using Node = std::tuple<int,int,int>; // quarter X/Z, half Y
    const int minX = p.minX*4, maxX = (p.maxX+1)*4-1;
    const int minZ = p.minZ*4, maxZ = (p.maxZ+1)*4-1;
    constexpr double halfWidth = Config::PLAYER_WIDTH*0.5;
    constexpr double playerHeight = Config::PLAYER_HEIGHT;
    const auto collides = [&](double px, double py, double pz) {
        for (int y = static_cast<int>(std::floor(py)); y <= std::floor(py+playerHeight-0.001); ++y)
            for (int z = static_cast<int>(std::floor(pz-halfWidth)); z <= std::floor(pz+halfWidth-0.001); ++z)
                for (int x = static_cast<int>(std::floor(px-halfWidth)); x <= std::floor(px+halfWidth-0.001); ++x) {
                    BlockId id=blockAt(x,y,z); DoorState door;
                    if(decodeDoor(id,door) && door.material!=DoorMaterial::Iron) {door.open=true;id=doorBlock(door);}
                    const auto boxes = blockCollisionBoxes(id);
                    for (uint8_t i = 0; i < boxes.count; ++i) {
                        const auto& box = boxes.boxes[i];
                        if (px+halfWidth > x+box.min.x+0.001 && px-halfWidth < x+box.max.x-0.001 &&
                            py+playerHeight > y+box.min.y+0.001 && py < y+box.max.y-0.001 &&
                            pz+halfWidth > z+box.min.z+0.001 && pz-halfWidth < z+box.max.z-0.001)
                            return true;
                    }
                }
        return false;
    };
    const int minH = (p.baseY-5)*2, maxH = (p.baseY+19)*2;
    const int gridWidth = maxX-minX+1, gridDepth = maxZ-minZ+1;
    std::vector<int8_t> standing(static_cast<size_t>(gridWidth)*gridDepth*(maxH-minH+1),-1);
    const auto canStand = [&](int qx, int hy, int qz) {
        auto& cached = standing[(static_cast<size_t>(hy-minH)*gridDepth+(qz-minZ))*gridWidth+(qx-minX)];
        if (cached >= 0) return cached != 0;
        const double px = qx*0.25+0.125, py = hy*0.5, pz = qz*0.25+0.125;
        cached = !collides(px,py,pz) && collides(px,py-0.01,pz);
        return cached != 0;
    };
    std::set<Node> visited;
    std::deque<Node> pending;
    // Any clear edge column is an exterior entrance to the reserved site.
    for (int qz = minZ; qz <= maxZ; ++qz) for (int qx = minX; qx <= maxX; ++qx) {
        if (qx-minX >= 4 && maxX-qx >= 4 && qz-minZ >= 4 && maxZ-qz >= 4) continue;
        const int hy = (p.baseY+1)*2;
        if (canStand(qx,hy,qz)) {
            visited.emplace(qx,hy,qz);
            pending.emplace_back(qx,hy,qz);
        }
    }
    constexpr std::array<std::pair<int,int>,4> directions{{{1,0},{-1,0},{0,1},{0,-1}}};
    while (!pending.empty()) {
        const auto [qx,hy,qz] = pending.front();
        pending.pop_front();
        for (const auto& [dx,dz] : directions) {
            const int nx = qx+dx, nz = qz+dz;
            if (nx < minX || nx > maxX || nz < minZ || nz > maxZ) continue;
            for (int nh : {hy+1,hy,hy-1,hy-2}) {
                if (nh < minH || nh > maxH) continue;
                const Node node{nx,nh,nz};
                if (visited.count(node) != 0 || !canStand(nx,nh,nz)) continue;
                // Test clearance at the higher of the two positions before
                // allowing a step; a low ceiling cannot be bypassed sideways.
                if (collides(nx*0.25+0.125, std::max(nh,hy)*0.5, nz*0.25+0.125) ||
                    collides(qx*0.25+0.125, std::max(nh,hy)*0.5, qz*0.25+0.125)) continue;
                visited.insert(node);
                pending.push_back(node);
            }
        }
    }
    return visited;
}

template <typename BlockAt>
void requireStructureAccess(const StructurePlacement& p, const BlockAt& blockAt) {
    const auto reachable = walkStructure(p, blockAt);
    size_t chests = 0;
    bool topReached = p.type != StructureType::MountainWatchtower;
    for (int z = p.minZ; z <= p.maxZ; ++z) for (int x = p.minX; x <= p.maxX; ++x) {
        for (int y = p.baseY-5; y <= p.baseY+18; ++y) {
            const BlockId id = blockAt(x,y,z);
            if (id != BlockId::CHEST && id != BlockId::FURNACE) continue;
            bool accessible = false;
            for (const auto& [qx,hy,qz] : reachable) {
                const double dx = qx*0.25+0.125-(x+0.5);
                const double dz = qz*0.25+0.125-(z+0.5);
                if (dx*dx+dz*dz < 1.6*1.6 && std::abs(hy*0.5-y) < 0.6) {
                    accessible = true;
                    break;
                }
            }
            if (!accessible) std::cerr << "unreachable supplies: " << structureCommandName(p.type)
                                      << " at " << x << ',' << y << ',' << z
                                      << " variant=" << p.variant << " reached=" << reachable.size() << '\n';
            require(accessible, "new structure supplies require jumping, digging or lack headroom");
            if (id == BlockId::CHEST) ++chests;
        }
    }
    for (const auto& [qx,hy,qz] : reachable) {
        (void)qx; (void)qz;
        if (hy == (p.baseY+17)*2) topReached = true;
    }
    require(chests == (p.type == StructureType::DesertTemple ? 2u : 1u),
            "new structure has an unexpected final chest count");
    require(topReached, "watchtower stairs do not reach the viewing platform");
}

inline void testNewStructureBlueprints() {
    static_assert(static_cast<uint8_t>(StructureType::SkywayShrine) == 12);
    static_assert(static_cast<uint8_t>(StructureLootProfile::OriginSkywayShrine) == 11);
    static_assert(WorldGenContext::GENERATION_VERSION == 20);
    for (const auto& fixture : NEW_STRUCTURE_FIXTURES) {
        for (int base : {Config::WORLD_MIN_Y+5, Config::WORLD_MAX_Y-fixture.height-1}) {
            StructurePlacement limit;
            limit.type = fixture.type;
            limit.baseY = base;
            limit.minX = limit.minZ = -fixture.radius;
            limit.maxX = limit.maxZ = fixture.radius;
            StructureGenerator::build(limit, [](int,int y,int,BlockId) {
                require(Config::isValidWorldY(y), "new structure writes beyond a world build limit");
            });
        }
        for (uint64_t transform = 0; transform < 8; ++transform) {
            StructurePlacement p;
            p.type = fixture.type;
            p.baseY = 100;
            p.minX = -32-fixture.radius;
            p.maxX = -32+fixture.radius;
            p.minZ = -48-fixture.radius;
            p.maxZ = -48+fixture.radius;
            p.variant = 0x102030405060708ULL | (transform<<60);
            std::map<std::tuple<int,int,int>,BlockId> blocks;
            const auto naturalY = [](int x, int z) { return 98+((x-z)%5+5)%5; };
            const auto blockAt = [&](int x, int y, int z) {
                const auto it = blocks.find({x,y,z});
                return it != blocks.end() ? it->second :
                    y <= naturalY(x,z) ? BlockId::STONE : BlockId::AIR;
            };
            StructureGenerator::build(p, [&](int x,int y,int z,BlockId id) {
                require(x >= p.minX && x <= p.maxX && z >= p.minZ && z <= p.maxZ,
                        "new transformed blueprint exceeded its reservation");
                require(y >= p.baseY-5 && y <= p.baseY+fixture.height,
                        "new blueprint exceeded its declared height range");
                blocks[{x,y,z}] = id;
            }, naturalY);
            requireStructureAccess(p, blockAt);
            // Every raised floor/support has continuous ground beneath the
            // graded surface even on the downward side of this test slope.
            for (int z = p.minZ; z <= p.maxZ; ++z) for (int x = p.minX; x <= p.maxX; ++x) {
                if (!isSolid(blockAt(x,p.baseY,z))) continue;
                bool chamber = false;
                for (int y = p.baseY-4; y < p.baseY; ++y) {
                    const auto it = blocks.find({x,y,z});
                    chamber = chamber || (it != blocks.end() && it->second == BlockId::AIR);
                }
                if (chamber) {
                    require(p.type == StructureType::DesertTemple && isSolid(blockAt(x,p.baseY-5,z)),
                            "underground chamber lacks its supported floor");
                    continue;
                }
                for (int y = naturalY(x,z)+1; y < p.baseY; ++y)
                    require(isSolid(blockAt(x,y,z)), "new structure has a floating foundation");
            }
        }
        std::cout << structureCommandName(fixture.type) << " all eight blueprint transforms walkable\n";
    }
}

inline void testNewStructureGeneration(HeightPipeline& terrain, StructureGenerator& structures) {
    constexpr uint64_t seed = 1234567890ULL;
    for (const auto& fixture : NEW_STRUCTURE_FIXTURES) {
        const auto located = structures.locateNearest(fixture.type, 0, 0);
        require(located && located->worldX == fixture.x && located->worldZ == fixture.z &&
                    located->baseY == fixture.y, "new structure fixed-seed locator changed");
        std::vector<StructurePlacement> placements;
        structures.generateStructuresRegion(fixture.x,fixture.z,1,1,placements);
        const auto found = std::find_if(placements.begin(), placements.end(), [&](const auto& p) {
            return p.type == fixture.type;
        });
        require(found != placements.end(), "new structure located an unaccepted anchor");
        const StructurePlacement p = *found;
        require(p.minX == fixture.x-fixture.radius && p.maxX == fixture.x+fixture.radius &&
                    p.minZ == fixture.z-fixture.radius && p.maxZ == fixture.z+fixture.radius,
                "new structure reservation size changed");
        const auto floorChunk = [](int n) { return n/16-(n%16<0); };
        // Put the anchor at the nearer X edge of a region so its reservation
        // crosses that edge even when it sits late in a negative chunk.
        const int anchorCX = floorChunk(fixture.x);
        const int ox = anchorCX-(fixture.x-anchorCX*16+fixture.radius >= 16 ? 2 : 0);
        const int oz = floorChunk(fixture.z);
        std::vector<std::unique_ptr<Chunk>> owned;
        std::vector<Chunk*> chunks;
        for (int z = 0; z < 3; ++z) for (int x = 0; x < 3; ++x) {
            owned.push_back(std::make_unique<Chunk>(ox+x,oz+z));
            chunks.push_back(owned.back().get());
        }
        WorldGenerator region(seed), singleton(seed);
        std::vector<RegionGenerationData::PendingBlock> regionPending, singlePending;
        region.generateRegion(ox,oz,3,Config::REGION_PADDING,chunks,regionPending);
        std::vector<std::unique_ptr<Chunk>> singleChunks;
        // Reverse chunk completion order to exercise order-independent writes.
        for (int i = 0; i < 9; ++i) singleChunks.push_back(std::make_unique<Chunk>(ox+i%3,oz+i/3));
        for (int i = 8; i >= 0; --i)
            singleton.generate(*singleChunks[i], {}, [&](int x,int y,int z,BlockId id) {
                singlePending.push_back({x,y,z,id});
            }, [&](int x,int y,int z,BlockId id,StructureLootProfile loot,uint64_t lootSeed) {
                singlePending.push_back({x,y,z,id,true,id==BlockId::CHEST||id==BlockId::FURNACE,loot,lootSeed});
            });
        const auto apply = [&](auto& destination, const auto& pending) {
            for (int pass = 0; pass < 2; ++pass) for (const auto& b : pending) {
                if (b.overwrite != (pass == 1)) continue;
                const int cx = floorChunk(b.worldX)-ox, cz = floorChunk(b.worldZ)-oz;
                if (cx < 0 || cx >= 3 || cz < 0 || cz >= 3) continue;
                Chunk& c = *destination[cz*3+cx];
                const int x = b.worldX-c.worldX(), z = b.worldZ-c.worldZ();
                const BlockId current = c.getBlock(x,b.worldY,z);
                const bool leaves = current==BlockId::LEAVES || current==BlockId::BIRCH_LEAVES ||
                    current==BlockId::SPRUCE_LEAVES || current==BlockId::JUNGLE_LEAVES || current==BlockId::ACACIA_LEAVES;
                if (!b.overwrite && current!=BlockId::AIR && current!=BlockId::SNOW && !leaves) continue;
                c.setBlock(x,b.worldY,z,b.id);
            }
        };
        apply(owned,regionPending);
        apply(singleChunks,singlePending);
        for (size_t i = 0; i < owned.size(); ++i)
            require(std::equal(owned[i]->rawBlocks(),owned[i]->rawBlocks()+Config::CHUNK_VOLUME,
                               singleChunks[i]->rawBlocks()), "new structure region/singleton voxel output differs");
        std::map<std::tuple<int,int,int>,BlockId> expected;
        StructureGenerator::build(p,[&](int x,int y,int z,BlockId id) { expected[{x,y,z}]=id; },
            [&](int x,int z) { return terrain.sampleColumn(x,z).height; });
        size_t outside = 0, registered = 0;
        for (const auto& [position,id] : expected) {
            const auto [x,y,z] = position;
            const int cx = floorChunk(x)-ox, cz = floorChunk(z)-oz;
            if (cx>=0 && cx<3 && cz>=0 && cz<3)
                require(owned[cz*3+cx]->getBlock(x-owned[cz*3+cx]->worldX(),y,
                            z-owned[cz*3+cx]->worldZ()) == id, "new structure generated voxel differs from blueprint");
            else {
                ++outside;
                require(std::any_of(regionPending.begin(),regionPending.end(),[&](const auto& b) {
                    return b.worldX==x && b.worldY==y && b.worldZ==z && b.id==id && b.overwrite;
                }), "new structure dropped a cross-region overwrite");
            }
            if (id != BlockId::CHEST && id != BlockId::FURNACE) continue;
            for (const auto* pending : {&regionPending,&singlePending})
                require(std::any_of(pending->begin(),pending->end(),[&](const auto& b) {
                    return b.worldX==x && b.worldY==y && b.worldZ==z && b.id==id && b.needsBlockEntity &&
                        b.lootProfile == (id==BlockId::CHEST ? structureLootProfile(p.type) : StructureLootProfile::None) &&
                        b.lootSeed == WorldGenContext::hashPosition(p.variant,x,y,z);
                }), "new structure supplies lost their entity or deterministic loot metadata");
            ++registered;
        }
        require(outside > 0 && registered > 0, "new structure fixture did not exercise pending work");
        std::cout << structureCommandName(fixture.type) << " fixture " << fixture.x << ',' << fixture.y << ',' << fixture.z << '\n';
    }
}
