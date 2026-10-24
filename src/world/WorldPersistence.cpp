#include "world/WorldPersistence.h"
#include "plugins/ContentRegistry.h"

#include "Config.h"
#include "debug/Log.h"
#include "game/SaveStore.h"
#include "world/BlockEntityLogic.h"
#include "world/Chunk.h"
#include "world/ChunkStore.h"
#include "world/FluidLogic.h"
#include "world/Structure.h"
#include "world/WorldGenContext.h"

#include <algorithm>

namespace {
uint32_t localIndexFor(const glm::ivec3& position, int cx, int cz) {
    const int lx = position.x - cx * Config::CHUNK_SIZE_X;
    const int lz = position.z - cz * Config::CHUNK_SIZE_Z;
    return static_cast<uint32_t>(lx + lz * Config::CHUNK_SIZE_X +
        Config::worldYToStorageY(position.y) *
            Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z);
}
int worldToChunkX(double wx) {
    return static_cast<int>(std::floor(wx / Config::CHUNK_SIZE_X));
}
int worldToChunkZ(double wz) {
    return static_cast<int>(std::floor(wz / Config::CHUNK_SIZE_Z));
}

struct LootEntry {
    ItemId item;
    uint8_t minimum;
    uint8_t maximum;
    uint8_t chance;
};

std::vector<LootEntry> lootEntries(StructureLootProfile profile) {
    using P=StructureLootProfile;
    switch(profile) {
        case P::NetherFortress:return {{ItemId::NETHER_WART,2,6,100},{ItemId::GOLD_INGOT,1,4,70},{ItemId::DIAMOND,1,2,25},{ItemId::IRON_INGOT,1,5,65}};
        case P::NetherBastion:return {{ItemId::NETHERITE_UPGRADE_TEMPLATE,1,1,100},{ItemId::SADDLE,1,1,20},{ItemId::GOLD_INGOT,3,12,100},{ItemId::CRYING_OBSIDIAN,2,6,70},{ItemId::ANCIENT_DEBRIS,1,2,30},{ItemId::DIAMOND,1,3,35}};
        case P::RuinedPortal:return {{ItemId::OBSIDIAN,2,6,90},{ItemId::FLINT_AND_STEEL,1,1,65},{ItemId::GOLD_INGOT,1,4,60}};
        case P::AbandonedFarmstead:
        case P::Village: return {{ItemId::BREAD,1,4,90},{ItemId::WHEAT,2,7,80},
            {ItemId::WHEAT_SEEDS,2,8,75},{ItemId::COAL,1,4,45},
            {ItemId::OAK_SAPLING,1,3,40},{ItemId::RAW_IRON,1,2,20}};
        case P::SwampHut:
        case P::TravelerHut: return {{ItemId::BREAD,1,3,80},{ItemId::STICK,2,8,90},
            {ItemId::COAL,1,4,65},{ItemId::LEATHER,1,3,45},
            {ItemId::OAK_SAPLING,1,2,55}};
        case P::AbandonedCamp: return {{ItemId::STICK,2,7,90},{ItemId::STRING,1,4,70},
            {ItemId::ARROW,2,8,75},{ItemId::BONE,1,4,60},
            {ItemId::ROTTEN_FLESH,1,3,35},{ItemId::BREAD,1,2,30}};
        case P::Igloo: return {{ItemId::COAL,2,6,85},{ItemId::BREAD,1,3,75},
            {ItemId::LEATHER,1,3,55}};
        case P::DesertTemple:
        case P::JungleRuins:
        case P::MountainWatchtower:
        case P::StoneCircle:
        case P::RuinedTower: return {{ItemId::ARROW,3,10,90},{ItemId::BONE,2,6,80},
            {ItemId::COAL,2,5,65},{ItemId::RAW_IRON,1,3,35},
            {ItemId::GOLD_INGOT,1,2,12}};
        case P::LumberCamp: return {{ItemId::SPRUCE_LOG,3,10,95},{ItemId::STICK,3,12,90},
            {ItemId::SPRUCE_SAPLING,1,4,75},{ItemId::WOODEN_AXE,1,1,25}};
        default:return{};
    }
}

void populateGeneratedLoot(BlockEntity& entity, StructureLootProfile profile,
                           uint64_t seed) {
    if(entity.type!=BlockEntityType::Chest||profile==StructureLootProfile::None)
        return;
    std::vector<LootEntry> entries=lootEntries(profile);
    for(size_t i=0;i<entries.size();++i) {
        const LootEntry& entry=entries[i];
        const uint64_t h=WorldGenContext::mix(seed^
            (static_cast<uint64_t>(i)+1u)*0x9E3779B97F4A7C15ULL);
        if(h%100u>=entry.chance)continue;
        const uint8_t count=static_cast<uint8_t>(entry.minimum+
            ((h>>8)%(entry.maximum-entry.minimum+1u)));
        size_t slot=static_cast<size_t>((h>>24)%entity.chest.size());
        for(size_t probe=0;probe<entity.chest.size();++probe) {
            ItemStack& target=entity.chest[(slot+probe)%entity.chest.size()];
            if(target.empty()){target={entry.item,count,0};break;}
        }
    }
}
}  // namespace

void WorldPersistence::forEachOverride(const OverrideVisitor& fn) const {
    m_chunks.withShared([&](ChunkStore& store) {
        for (const auto& [key, overrides] : m_blockOverrides) {
            const Chunk* chunk = store.findUnlocked(key.first, key.second);
            if (chunk == nullptr || !chunk->generated.load()) continue;
            for (const auto& [index, block] : overrides) {
                fn(key, index, block);
            }
        }
    });
}

void WorldPersistence::registerGeneratedBlockEntityUnlocked(
    int cx, int cz, uint32_t localIndex, BlockId id,
    StructureLootProfile lootProfile, uint64_t lootSeed,
    const RegionGenerationData::PendingBlock* metadata) {
    const auto kind=blockEntityTypeForBlock(id);
    if(!kind)return;
    auto& entities = m_blockEntities[{cx, cz}];
    if (entities.count(localIndex) != 0) return;
    BlockEntity entity;
    entity.type = *kind;
    entity.lootSeed=lootSeed;
    if(metadata){entity.lootKey=metadata->lootKey;entity.lootRevision=metadata->lootRevision;entity.structureKey=metadata->structureKey;entity.structureRevision=metadata->structureRevision;}
    if(!entity.lootKey.empty()) {
        const auto& table=Plugins::content().extensions.loot.at(entity.lootKey);
        for(size_t i=0;i<table.entries.size();++i){const auto& entry=table.entries[i];const uint64_t h=WorldGenContext::mix(lootSeed^(uint64_t(i)+1)*0x9E3779B97F4A7C15ULL);if(h%100>=entry.chance)continue;const auto amount=entry.minimum+((h>>8)%(entry.maximum-entry.minimum+1));if(!amount)continue;size_t slot=(h>>24)%entity.chest.size();for(size_t probe=0;probe<entity.chest.size();++probe){auto& target=entity.chest[(slot+probe)%entity.chest.size()];if(target.empty()){target={Plugins::resolveItem(entry.item),static_cast<uint8_t>(amount),0};break;}}}
    } else populateGeneratedLoot(entity,lootProfile,lootSeed);
    entity.lootInitialized=entity.type==BlockEntityType::Chest;
    entities.emplace(localIndex, entity);
    m_dirtyBlockEntityChunks.insert({cx, cz});
}

void WorldPersistence::recordOverride(int cx, int cz, uint32_t localIndex,
                                      BlockId id) {
    m_chunks.withUnique([&](ChunkStore&) {
        const auto existing=m_blockOverrides.find({cx,cz});
        if(existing!=m_blockOverrides.end()) {
            const auto value=existing->second.find(localIndex);
            if(value!=existing->second.end() && value->second==id)return;
        }
        m_blockOverrides[{cx, cz}][localIndex] = id;
        registerButtonUnlocked(cx,cz,localIndex,id);
        m_dirtyOverrideChunks.insert({cx, cz});
        m_overridesApplied.insert({cx, cz});
        auto& entities = m_blockEntities[{cx, cz}];
        const auto timer=entities.find(localIndex);
        if(timer!=entities.end()&&timer->second.type==BlockEntityType::Resource){entities.erase(timer);m_dirtyBlockEntityChunks.insert({cx,cz});}
        ButtonState button;
        if (blockEntityTypeForBlock(id) || decodeButton(id, button)) {
            const auto expected=blockEntityTypeForBlock(id).value_or(BlockEntityType::Button);
            const auto old=entities.find(localIndex);
            if(old!=entities.end() && old->second.type!=expected)entities.erase(old);
            if (entities.count(localIndex) == 0) {
                BlockEntity entity;
                entity.type = blockEntityTypeForBlock(id).value_or(BlockEntityType::Button);
                entity.buttonRemaining = button.pressed ? (buttonReleaseTicks(button)) : 0;
                entities.emplace(localIndex, entity);
                m_dirtyBlockEntityChunks.insert({cx, cz});
            }
        } else if (entities.erase(localIndex) != 0) {
            m_dirtyBlockEntityChunks.insert({cx, cz});
        }
    });
}

void WorldPersistence::installLoadedChunkDataUnlocked(
    int cx, int cz, const std::vector<BlockOverride>& overrides,
    const std::vector<PersistedBlockEntity>& entities) {
    const std::pair<int, int> key{cx, cz};
    auto& cached = m_blockOverrides[key];
    for (const auto& entry : overrides) {
        if (entry.localIndex >= static_cast<uint32_t>(Config::CHUNK_VOLUME) ||
            !isValidBlockId(entry.block) ||
            isDerivedFluidState(entry.block)) continue;
        cached[entry.localIndex] = entry.block;
        registerButtonUnlocked(cx,cz,entry.localIndex,entry.block);
    }
    auto& target = m_blockEntities[key];
    for (const auto& entity : entities) target[entity.localIndex] = entity.value;
    m_prefetchedChunks.insert(key);
}

void WorldPersistence::applySavedOverridesUnlocked(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_overridesApplied.count(key) != 0) return;
    Chunk* chunk = m_chunks.findUnlocked(cx, cz);
    if (chunk == nullptr || !chunk->generated.load()) return;

    auto& cached = m_blockOverrides[key];
    size_t pruned=0;
    for (auto it=cached.begin();it!=cached.end();) {
        if (isDerivedFluidState(it->second)){it=cached.erase(it);++pruned;}
        else ++it;
    }
    if (m_saveStore && m_prefetchedChunks.count(key) == 0) {
        for (const auto& entry : m_saveStore->loadChunkOverrides(cx, cz)) {
            if (isDerivedFluidState(entry.block)) {++pruned;continue;}
            cached[entry.localIndex] = entry.block;
            registerButtonUnlocked(cx,cz,entry.localIndex,entry.block);
        }
    }
    if(pruned>0){m_dirtyOverrideChunks.insert(key);LOG_INFO(
        "Pruned "<<pruned<<" legacy derived fluid overrides from chunk "<<cx<<','<<cz);}
    for (const auto& [index, block] : cached) {
        int x = 0, z = 0, y = 0;
        decodeChunkIndex(index, x, z, y);
        chunk->setBlock(x, y, z, block);
    }
    m_overridesApplied.insert(key);
}

void WorldPersistence::saveOverrides(int cx, int cz) {
    if (!m_saveStore) return;
    const std::pair<int, int> key{cx, cz};
    if (m_dirtyOverrideChunks.count(key) == 0 &&
        m_pendingOverrideSaves.count(key) == 0) return;
    const auto it = m_blockOverrides.find(key);
    if (it == m_blockOverrides.end()) return;
    std::vector<BlockOverride> serialized;
    serialized.reserve(it->second.size());
    for (const auto& [localIndex, block] : it->second) {
        serialized.push_back({localIndex, block});
    }
    std::sort(serialized.begin(), serialized.end(),
              [](const BlockOverride& a, const BlockOverride& b) {
                  return a.localIndex < b.localIndex;
              });
    m_saveStore->saveChunkOverrides(cx, cz, serialized);
    m_dirtyOverrideChunks.erase(key);
    m_pendingOverrideSaves.erase(key);
}

void WorldPersistence::saveBlockEntities(int cx, int cz) {
    const std::pair<int,int> key{cx,cz};
    if (!m_saveStore || (m_dirtyBlockEntityChunks.count(key) == 0 &&
                         m_pendingBlockEntitySaves.count(key) == 0)) return;
    std::vector<PersistedBlockEntity> persisted;
    auto it = m_blockEntities.find({cx, cz});
    if (it != m_blockEntities.end()) {
        persisted.reserve(it->second.size());
        for (const auto& [index, entity] : it->second)
            persisted.push_back({index, entity});
        std::sort(persisted.begin(), persisted.end(), [](const auto& a, const auto& b) {
            return a.localIndex < b.localIndex;
        });
    }
    m_saveStore->saveBlockEntities(cx, cz, persisted);
    m_dirtyBlockEntityChunks.erase({cx, cz});
    m_pendingBlockEntitySaves.erase({cx, cz});
}

void WorldPersistence::loadBlockEntities(int cx, int cz) {
    const std::pair<int, int> key{cx, cz};
    if (m_blockEntitiesApplied.count(key)) return;
    if (m_saveStore && m_prefetchedChunks.count(key) == 0) {
        auto& target = m_blockEntities[key];
        for (const auto& persisted : m_saveStore->loadBlockEntities(cx, cz))
            target[persisted.localIndex] = persisted.value;
    }
    if(const auto chunk=m_chunks.findUnlocked(cx,cz))
        for(const auto& [index,entity]:m_blockEntities[key]) {
            if(entity.type==BlockEntityType::Button)
                registerButtonUnlocked(cx,cz,index,chunk->getBlock(index%16,
                    Config::WORLD_MIN_Y+static_cast<int>(index/256),(index/16)%16));
        }
    m_blockEntitiesApplied.insert(key);
}

void WorldPersistence::beginModifiedChunkAutosave() {
    m_chunks.withUnique([&](ChunkStore&) {
        m_pendingOverrideSaves.insert(m_dirtyOverrideChunks.begin(),
                                      m_dirtyOverrideChunks.end());
        m_pendingBlockEntitySaves.insert(m_dirtyBlockEntityChunks.begin(),
                                         m_dirtyBlockEntityChunks.end());
        m_dirtyOverrideChunks.clear();
        m_dirtyBlockEntityChunks.clear();
    });
}

bool WorldPersistence::flushModifiedChunks(size_t maxFiles) {
    bool done = false;
    m_chunks.withUnique([&](ChunkStore&) {
        size_t saved = 0;
        std::vector<std::pair<int, int>> dirty(
            m_pendingOverrideSaves.begin(), m_pendingOverrideSaves.end());
        for (const auto& [cx, cz] : dirty) {
            if (saved >= maxFiles) break;
            saveOverrides(cx, cz);
            ++saved;
        }
        std::vector<std::pair<int, int>> dirtyEntities(
            m_pendingBlockEntitySaves.begin(), m_pendingBlockEntitySaves.end());
        for (const auto& [cx, cz] : dirtyEntities) {
            if (saved >= maxFiles) break;
            saveBlockEntities(cx, cz);
            ++saved;
        }
        done = m_pendingOverrideSaves.empty() &&
               m_pendingBlockEntitySaves.empty();
    });
    return done;
}

BlockEntity* WorldPersistence::getBlockEntity(const glm::ivec3& position) {
    if (!Config::isValidWorldY(position.y)) return nullptr;
    const int cx = worldToChunkX(position.x);
    const int cz = worldToChunkZ(position.z);
    BlockEntity* result = nullptr;
    m_chunks.withUnique([&](ChunkStore&) {
        auto chunkIt = m_blockEntities.find({cx, cz});
        if (chunkIt == m_blockEntities.end()) return;
        auto it = chunkIt->second.find(localIndexFor(position, cx, cz));
        if (it == chunkIt->second.end()) return;
        m_dirtyBlockEntityChunks.insert({cx, cz});
        result = &it->second;
    });
    return result;
}

const BlockEntity* WorldPersistence::getBlockEntity(
    const glm::ivec3& position) const {
    if (!Config::isValidWorldY(position.y)) return nullptr;
    const int cx = worldToChunkX(position.x);
    const int cz = worldToChunkZ(position.z);
    const BlockEntity* result = nullptr;
    m_chunks.withShared([&](ChunkStore&) {
        auto chunkIt = m_blockEntities.find({cx, cz});
        if (chunkIt == m_blockEntities.end()) return;
        auto it = chunkIt->second.find(localIndexFor(position, cx, cz));
        if (it == chunkIt->second.end()) return;
        result = &it->second;
    });
    return result;
}

std::vector<ItemStack> WorldPersistence::takeBlockEntityContents(
    const glm::ivec3& position) {
    std::vector<ItemStack> contents;
    BlockEntity* entity = getBlockEntity(position);
    if (!entity) return contents;
    if (entity->type == BlockEntityType::Chest) {
        for (auto& stack : entity->chest) if (!stack.empty()) contents.push_back(stack);
    } else if(entity->type==BlockEntityType::BrewingStand||entity->type==BlockEntityType::SmithingTable) {
        const int count=entity->type==BlockEntityType::BrewingStand?5:3;
        for(int i=0;i<count;++i)if(!entity->chest[i].empty())contents.push_back(entity->chest[i]);
    } else if(entity->type==BlockEntityType::Furnace) {
        for (auto* stack : {&entity->input, &entity->fuel, &entity->output})
            if (!stack->empty()) contents.push_back(*stack);
    }
    return contents;
}

void WorldPersistence::tickBlockEntities() {
    m_chunks.withUnique([&](ChunkStore& store) {
        for (auto& [key, entities] : m_blockEntities) {
            const auto* chunk=store.findUnlocked(key.first,key.second);
            if(!chunk||!chunk->generated.load())continue;
            bool changed = false;
            for (auto& [index, entity] : entities) {
                changed = tickFurnace(entity) || changed;
            }
            if (changed) m_dirtyBlockEntityChunks.insert(key);
        }
    });
}


void WorldPersistence::registerButtonUnlocked(int cx,int cz,uint32_t index,BlockId id) {
    ButtonState state;
    if(decodeButton(id,state) && state.pressed)m_pressedButtons.emplace(cx,cz,index);
    else m_pressedButtons.erase({cx,cz,index});
}
std::vector<glm::ivec3> WorldPersistence::tickButtons() {
    std::vector<glm::ivec3> due;
    m_chunks.withUnique([&](ChunkStore& store) {
        for(const auto& [cx,cz,index]:m_pressedButtons) {
            const Chunk* chunk=store.findUnlocked(cx,cz);
            if(!chunk || !chunk->generated.load())continue;
            const auto found=m_blockEntities.find({cx,cz});
            if(found==m_blockEntities.end())continue;
            const auto entry=found->second.find(index);
            if(entry==found->second.end() || entry->second.type!=BlockEntityType::Button)continue;
            auto& entity=entry->second;
            if(entity.buttonRemaining>0) {
                --entity.buttonRemaining;m_dirtyBlockEntityChunks.insert({cx,cz});
            }
            if(entity.buttonRemaining==0 && due.size()<Config::BUTTON_TRANSITIONS_PER_TICK)
                due.emplace_back(cx*16+static_cast<int>(index%16),Config::WORLD_MIN_Y+static_cast<int>(index/256),cz*16+static_cast<int>((index/16)%16));
        }
    });
    return due;
}

std::optional<std::vector<BlockOverride>> WorldPersistence::copyKnownOverrides(int cx, int cz) const {
    std::optional<std::vector<BlockOverride>> result;
    m_chunks.withShared([&](const ChunkStore&) {
        const auto found = m_blockOverrides.find({cx, cz});
        if (found == m_blockOverrides.end() && !m_overridesApplied.count({cx, cz})) return;
        result.emplace();
        if (found != m_blockOverrides.end()) for (const auto& entry : found->second) result->push_back({entry.first, entry.second});
    });
    return result;
}

void WorldPersistence::scheduleResource(const glm::ivec3& p,BlockId id,uint32_t ticks){
    const int cx=worldToChunkX(p.x),cz=worldToChunkZ(p.z);
    m_chunks.withUnique([&](ChunkStore&){BlockEntity e;e.type=BlockEntityType::Resource;e.resourceRemaining=ticks;e.resourceBlock=static_cast<uint16_t>(id);m_blockEntities[{cx,cz}][localIndexFor(p,cx,cz)]=e;m_dirtyBlockEntityChunks.insert({cx,cz});});
}
std::vector<std::pair<glm::ivec3,BlockId>> WorldPersistence::tickResources(){
    std::vector<std::pair<glm::ivec3,BlockId>> due;
    m_chunks.withUnique([&](ChunkStore& store){for(auto& pair:m_blockEntities){const auto& key=pair.first;const auto* chunk=store.findUnlocked(key.first,key.second);if(!chunk||!chunk->generated.load())continue;for(auto it=pair.second.begin();it!=pair.second.end();){auto& e=it->second;if(e.type!=BlockEntityType::Resource){++it;continue;}if(e.resourceRemaining)--e.resourceRemaining;m_dirtyBlockEntityChunks.insert(key);if(!e.resourceRemaining&&due.size()<64){const auto index=it->first;due.push_back({{key.first*16+int(index%16),Config::WORLD_MIN_Y+int(index/256),key.second*16+int((index/16)%16)},static_cast<BlockId>(e.resourceBlock)});it=pair.second.erase(it);}else ++it;}}});return due;
}

std::vector<glm::ivec3> WorldPersistence::loadedSpawners() const {
    std::vector<glm::ivec3> result;
    m_chunks.withShared([&](ChunkStore& store) {
        for(const auto& chunk:m_blockEntities) {
            const auto* loaded=store.findUnlocked(chunk.first.first,chunk.first.second);
            if(!loaded||!loaded->generated.load())continue;
            for(const auto& entry:chunk.second)if(entry.second.type==BlockEntityType::Spawner) {
                const auto index=entry.first;
                result.push_back({chunk.first.first*16+int(index%16),int(index/256)+Config::WORLD_MIN_Y,chunk.first.second*16+int(index/16%16)});
            }
        }
    });
    return result;
}
