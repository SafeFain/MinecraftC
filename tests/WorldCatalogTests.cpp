#include "HeavenContentFixture.h"
#include "game/WorldCatalog.h"
#include "world/WorldGenContext.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include "entity/EntityLogic.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

namespace {
std::map<std::string,std::string> snapshotRecoveryFiles(const std::filesystem::path& root) {
    std::map<std::string,std::string> result;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file()) {
        std::ifstream input(entry.path(),std::ios::binary);
        result[entry.path().lexically_relative(root).generic_string()]=std::string(std::istreambuf_iterator<char>(input),{});
    }
    return result;
}
void runRecoveryTests(const std::filesystem::path& root) {
    const auto originalState=std::make_unique<Plugins::ContentState>(Plugins::content());
    Plugins::content().frozen=false;
    Plugins::BlockContent block;block.key="recovery:block";block.name="Recovery block";
    const auto blockId=Plugins::registerBlock(block);
    Plugins::ItemContent item;item.key="recovery:item";item.properties.maxStack=64;
    const auto itemId=Plugins::registerItem(item);
    Plugins::EntityContent definition;definition.id=4096;definition.key="recovery:mob";definition.stateVersion=1;
    Plugins::content().extensions.entities.emplace(definition.id,definition);
    Plugins::GeneratorContent generator;generator.key="recovery:terrain";generator.revision="1";generator.dimension=0;
    Plugins::content().extensions.generators.emplace(generator.key,generator);
    Plugins::ModifierContent modifier;modifier.key="recovery:modifier";modifier.revision="1";modifier.dimension=0;
    Plugins::content().extensions.modifiers.emplace(modifier.key,modifier);
    Plugins::LootContent loot;loot.key="recovery:loot";loot.revision="1";Plugins::content().extensions.loot.emplace(loot.key,loot);
    Plugins::StructureContent structure;structure.key="recovery:structure";structure.revision="1";Plugins::content().extensions.structures.emplace(structure.key,structure);
    Plugins::content().requirements={{"recovery","1.0:old-hash"},{"removed","1.0:removed"}};
    WorldCatalog catalog(root);
    const auto id=catalog.create("Recovery",123,GameMode::Creative,Difficulty::Normal);
    SaveStore store(root/id);auto metadata=store.loadMetadata();
    metadata.generation[0]={"recovery:terrain","1","{}",{{"recovery:modifier","1"}}};
    metadata.inventory.slot(0)={itemId,7,0};metadata.inventory.slot(1)={ItemId::STONE,3,0};
    WorldMetadata::PersistedEntity mob;mob.type=4096;mob.health=10;mob.pluginStateVersion=1;
    metadata.entities.push_back(mob);metadata.activeDimension=DimensionId::Heaven;
    metadata.dimensions[0].hasSafePosition=true;metadata.dimensions[0].safePlayerPosition={4.5,100.01,8.5};
    store.saveMetadata(metadata);
    store.saveChunkOverrides(-2,7,{{0,blockId},{1,BlockId::STONE}});
    store.saveChunkOverrides(1000,-2000,{{0,blockId},{1,BlockId::SUNSTONE}});
    metadata.inventory.slot(2)={ItemId::STARSTEP_SCEPTER,1,0};store.saveMetadata(metadata);
    store.saveChunkEntities(1000,-2000,{mob});store.saveChunkEntityPopulationVersion(1000,-2000,4);
    PersistedBlockEntity chest;chest.localIndex=1;chest.value.type=BlockEntityType::Chest;chest.value.chest[0]={itemId,4,0};chest.value.chest[1]={ItemId::DIRT,2,0};
    chest.value.lootKey="recovery:loot";chest.value.lootRevision="1";chest.value.lootInitialized=true;
    chest.value.structureKey="recovery:structure";chest.value.structureRevision="1";
    PersistedBlockEntity resource;resource.localIndex=2;resource.value.type=BlockEntityType::Resource;resource.value.resourceBlock=static_cast<uint16_t>(blockId);resource.value.resourceRemaining=100;
    store.saveBlockEntities(-2,7,{chest,resource});
    SaveStore(root/id/"dimensions/heaven").saveChunkOverrides(3,4,{{0,blockId}});
    std::filesystem::create_directories(root/id/"players");{std::ofstream f(root/id/"players/private-profile");f<<"opaque LAN player data";}
    std::filesystem::create_directories(root/id/"plugin-private");{std::ofstream f(root/id/"plugin-private/state.bin");f<<"private state";}
    std::filesystem::create_directories(root/id/"generated");{std::ofstream f(root/id/"generated/obsolete.bin");f<<"disposable";}
    std::filesystem::create_directories(root/id/"plugin-private/lod");{std::ofstream f(root/id/"plugin-private/lod/state.bin");f<<"private LOD data";}
    {std::ofstream f(root/id/"plugin-private/spawn.cache");f<<"private spawn data";}
    std::filesystem::create_directories(root/id/"dimensions/heaven/private/chunks");{std::ofstream f(root/id/"dimensions/heaven/private/chunks/plugin.bin");f<<"private non-partition";}
    const auto before=snapshotRecoveryFiles(root/id);
    require(store.inspectCompatibility().compatible(),"unchanged gameplay set is compatible");
    Plugins::content().requirements[0].second="1.0:hash-only-change";
    const auto fingerprintOnly=store.inspectCompatibility();
    require(fingerprintOnly.recoverable()&&std::any_of(fingerprintOnly.issues.begin(),fingerprintOnly.issues.end(),[](const auto& issue){return issue.kind=="changed_plugin"&&issue.saved=="1.0:old-hash"&&issue.current=="1.0:hash-only-change";}),"same-version fingerprint differences are reported in full");
    Plugins::content().extensions.entities.at(4096).stateVersion=2;
    const auto changedEntity=store.inspectCompatibility();
    require(std::any_of(changedEntity.issues.begin(),changedEntity.issues.end(),[](const auto& issue){return issue.kind=="entity_revision";}),"entity state change is visible");
    Plugins::content().extensions.generators.at("recovery:terrain").revision="2";
    Plugins::content().extensions.modifiers.erase("recovery:modifier");
    Plugins::content().extensions.loot.erase("recovery:loot");Plugins::content().extensions.structures.at("recovery:structure").revision="2";
    Plugins::content().blocks.erase(static_cast<uint16_t>(blockId));Plugins::content().items.erase(static_cast<uint16_t>(itemId));
    Plugins::content().extensions.entities.erase(4096);Plugins::content().extensions.dimensions.erase(1);
    const auto reservedBlock=static_cast<uint16_t>(BlockId::SUNSTONE),reservedItem=static_cast<uint16_t>(ItemId::STARSTEP_SCEPTER);
    Plugins::content().blocks.erase(reservedBlock);Plugins::content().validBlocks[reservedBlock]=0;
    Plugins::content().items.erase(reservedItem);Plugins::content().validItems[reservedItem]=0;
    Plugins::content().requirements={{"recovery","2.0:new-hash"},{"extra","1.0:extra"}};
    const auto report=store.inspectCompatibility();
    if(!report.recoverable())std::cerr<<"Recovery blocked: "<<report.blocker<<std::endl;
    require(report.recoverable(),"missing plugin content is recoverable");
    for(const std::string kind:{"missing_plugin","extra_plugin","changed_plugin","missing_content","missing_entity","missing_dimension","generator_revision","missing_modifier","loot_revision","structure_revision"})
        require(std::any_of(report.issues.begin(),report.issues.end(),[&](const auto& issue){return issue.kind==kind;}),"report includes every difference category");
    require(snapshotRecoveryFiles(root/id)==before,"inspection preserves original bytes");
    bool refused=false;try{store.loadMetadata();}catch(const std::exception&){refused=true;}
    require(refused,"ordinary load still rejects plugin differences");
    {
        WorldRecovery cancelled(root/id,report.token);cancelled.step();cancelled.cancel();
        require(!cancelled.succeeded()&&snapshotRecoveryFiles(root/id)==before,"cancel preserves original world");
    }
    refused=false;try{WorldRecovery stale(root/id,"stale-token");}catch(const std::exception&){refused=true;}
    require(refused,"stale confirmation is rejected");
    {
        WorldRecovery failed(root/id,report.token);
        {std::ofstream obstacle(failed.backupPath().parent_path()/"staged/chunks");obstacle<<"not a directory";}
        for(int n=0;!failed.done()&&n<100;++n)failed.step();
        require(!failed.succeeded()&&!failed.error().empty()&&snapshotRecoveryFiles(root/id)==before,"staging write failure preserves original bytes");
    }
    {
        WorldRecovery changed(root/id,report.token);
        {std::ofstream mutation(root/id/"external-edit");mutation<<"changed since confirmation";}
        const auto edited=snapshotRecoveryFiles(root/id);
        for(int n=0;!changed.done()&&n<100;++n)changed.step();
        require(!changed.succeeded()&&snapshotRecoveryFiles(root/id)==edited,"external source change invalidates confirmation without overwriting it");
        std::filesystem::remove(root/id/"external-edit");
    }
    {
        WorldRecovery collision(root/id,report.token);
        std::filesystem::create_directory(collision.backupPath());
        {std::ofstream sentinel(collision.backupPath()/"sentinel");sentinel<<"do not overwrite";}
        for(int n=0;!collision.done()&&n<100;++n)collision.step();
        require(!collision.succeeded()&&snapshotRecoveryFiles(root/id)==before&&std::filesystem::exists(collision.backupPath()/"sentinel"),"publication collision preserves the source and existing backup destination");
    }
    {
        WorldRecovery journalFailure(root/id,report.token);
        // A directory prevents opening the completion file after publication.
        std::filesystem::create_directory(journalFailure.backupPath().parent_path()/"committed");
        for(int n=0;!journalFailure.done()&&n<100;++n)journalFailure.step();
        require(!journalFailure.succeeded()&&snapshotRecoveryFiles(root/id)==before&&!std::filesystem::exists(journalFailure.backupPath()),"completion-journal write failure rolls the published copy back to the original");
    }
    WorldRecovery recovery(root/id,report.token);
    for(int n=0;!recovery.done()&&n<100;++n)recovery.step();
    if(!recovery.succeeded())std::cerr<<recovery.error()<<std::endl;
    require(recovery.succeeded(),"recovery publishes strictly loadable world");
    require(snapshotRecoveryFiles(recovery.backupPath())==before,"backup retains every original file byte");
    require(store.inspectCompatibility().compatible(),"recovered world has no remaining plugin mismatch");
    const auto loaded=store.loadMetadata();
    require(std::filesystem::exists(root/id/SaveStore::RECOVERY_SPAWN_MARKER),"missing dimension queues terrain-aware safe landing before play");
    require(loaded.activeDimension==DimensionId::Overworld&&loaded.playerPosition==glm::dvec3(4.5,100.01,8.5),"missing active dimension returns to saved safe Overworld position");
    require(loaded.generation.at(0).revision=="2"&&loaded.generation.at(0).modifiers.empty(),"current generator revision used and missing modifier removed");
    require(loaded.inventory.slot(2).empty()&&store.loadChunkOverrides(1000,-2000)[1].block==BlockId::AIR,"missing fixed reserved provider slots are degraded");
    require(loaded.inventory.slot(0).empty()&&loaded.inventory.slot(1).count==3&&loaded.entities.empty(),"missing inventory and entities removed, valid items retained");
    require(store.loadChunkOverrides(-2,7)[0].block==BlockId::AIR&&store.loadChunkOverrides(1000,-2000)[0].block==BlockId::AIR,"all near and unvisited edits converted");
    require(store.loadChunkEntities(1000,-2000).empty()&&store.loadChunkEntityPopulationVersion(1000,-2000)==4,"entities removed while population record survives");
    const auto container=store.loadBlockEntities(-2,7);
    require(container.size()==1&&container[0].value.lootKey.empty()&&container[0].value.structureKey.empty()&&container[0].value.lootInitialized,"incompatible loot/structure metadata cleared without refilling; missing resource timer removed");
    require(container.size()==1&&container[0].value.chest[0].empty()&&container[0].value.chest[1].count==2,"container recovery preserves available items");
    require(!std::filesystem::exists(root/id/"dimensions/heaven")&&!std::filesystem::exists(root/id/"generated"),"missing dimensions and derived caches removed from recovered copy");
    const auto after=snapshotRecoveryFiles(root/id);
    require(after.at("plugin-private/lod/state.bin")==before.at("plugin-private/lod/state.bin")&&after.at("plugin-private/spawn.cache")==before.at("plugin-private/spawn.cache"),"similarly named private cache files are retained");
    require(after.at("players/private-profile")==before.at("players/private-profile")&&after.at("plugin-private/state.bin")==before.at("plugin-private/state.bin"),"LAN and private plugin files stay unchanged");
    store.saveMetadata(loaded);store.validatePluginFiles();
    const auto worlds=catalog.list();require(worlds.size()==1&&worlds[0].compatible,"backups stay out of world list and saved recovery reopens");
    // Simulate interruption between the two publication directory renames.
    const auto transaction=root/".plugin-recovery/interrupted";std::filesystem::create_directories(transaction);
    {std::ofstream journal(transaction/"world-id");journal<<id;}
    {std::ofstream incomplete(transaction/"committed");} // Interrupted completion write.
    std::filesystem::rename(root/id,transaction/"original");
    WorldRecovery::restoreInterrupted(root);
    require(SaveStore(root/id).loadMetadata().seed==123,"interrupted publication restores original automatically");
    // A checksum failure must never become a recoverable plugin warning.
    {std::fstream file(root/id/"chunks/c.-2.7.bin",std::ios::in|std::ios::out|std::ios::binary);file.seekp(-1,std::ios::end);char bad='x';file.write(&bad,1);}
    require(!store.inspectCompatibility().blocker.empty(),"corrupt durable partition remains blocked");
    require(catalog.deleteWorld(id),"recovered world can be explicitly deleted");
    require(catalog.list().empty(),"completed backups never resurrect an explicitly deleted world");
    Plugins::content()=*originalState;
    const auto heaven=Plugins::content().extensions.dimensions.at(1);
    Plugins::content().extensions.dimensions.erase(1);
    Plugins::GeneratorContent lost;lost.key="lost:terrain";lost.revision="1";lost.dimension=0;
    Plugins::content().extensions.generators.emplace(lost.key,lost);Plugins::content().requirements.push_back({"lost","1.0:old"});
    const auto fallbackId=catalog.create("Fallback",9,GameMode::Creative,Difficulty::Normal,false,WorldType::Normal,{{0,{lost.key,lost.revision,"{}",{}}}});
    Plugins::content().extensions.dimensions.emplace(1,heaven);Plugins::content().extensions.generators.erase(lost.key);Plugins::content().requirements=originalState->requirements;
    SaveStore fallback(root/fallbackId);const auto fallbackReport=fallback.inspectCompatibility();
    require(fallbackReport.recoverable()&&std::any_of(fallbackReport.issues.begin(),fallbackReport.issues.end(),[](const auto& issue){return issue.kind=="extra_dimension";}),"newly installed dimension is reported");
    WorldRecovery fallbackRecovery(root/fallbackId,fallbackReport.token);
    for(int n=0;!fallbackRecovery.done()&&n<100;++n)fallbackRecovery.step();
    require(fallbackRecovery.succeeded()&&fallback.loadMetadata().generation.at(0).generator.empty()&&fallback.loadMetadata().generation.at(1)==heaven.generation,"missing generator falls back and added dimension gets current defaults");
    Plugins::content()=*originalState;
}
}

int main() {
    installHeavenFixture();
    const auto root = std::filesystem::temp_directory_path() /
        ("minecraftc-catalog-test-" + std::to_string(
            static_cast<unsigned long long>(std::rand())));
    std::filesystem::remove_all(root);
    try {
        runRecoveryTests(root/"recovery-fixtures");
        WorldCatalog catalog(root);
        const std::string first = catalog.create(
            "My Survival World", 42, GameMode::Survival, Difficulty::Normal, true);
        const std::string second = catalog.create(
            "My Survival World", 99, GameMode::Creative, Difficulty::Peaceful,
            false, WorldType::Superflat);
        WorldMetadata legacy;
        legacy.displayName = "Legacy v2";
        legacy.seed = 7;
        legacy.generationVersion = 2;
        SaveStore(root / "legacy-v2").saveMetadata(legacy);
        legacy.displayName = "Legacy v3";
        legacy.generationVersion = 3;
        SaveStore(root / "legacy-v3").saveMetadata(legacy);
        legacy.displayName = "Surface v14";
        legacy.generationVersion = 14;
        SaveStore(root / "surface-v14").saveMetadata(legacy);
        legacy.generationVersion = 15;
        SaveStore(root / "surface-v15").saveMetadata(legacy);
        legacy.generationVersion = WorldGenContext::GENERATION_VERSION + 1;
        SaveStore(root / "future-world").saveMetadata(legacy);
        require(first == "my-survival-world", "display name creates a safe id");
        require(second == "my-survival-world-2", "duplicate names get unique ids");
        const auto worlds = catalog.list();
        require(worlds.size() == 7, "catalog lists compatible and legacy saves");
        bool sawLegacy = false;
        bool sawSuperflat = false;
        for (const auto& world : worlds) {
            if (world.id == "surface-v14" || world.id == "surface-v15" || world.id == "future-world") {
                require(!world.compatible,
                        "catalog rejects every previous or future generation version");
                continue;
            }
            require(world.generationVersion == 2 || world.generationVersion == 3 ||
                    world.generationVersion == WorldGenContext::GENERATION_VERSION,
                    "catalog lost generation version");
            if (world.generationVersion < WorldGenContext::GENERATION_VERSION) {
                sawLegacy = true;
                require(!world.compatible, "legacy generation was marked compatible");
            } else {
                require(world.compatible, "current generation was marked incompatible");
                if (world.seed == 99) {
                    require(world.worldType == WorldType::Superflat,
                            "catalog summary preserves the superflat type");
                    sawSuperflat = true;
                }
            }
        }
        require(sawLegacy, "legacy world was hidden instead of marked incompatible");
        require(sawSuperflat, "catalog lists the selected terrain type");
        require(catalog.open(first).loadMetadata().seed == 42,
                "catalog opens the selected world");
        require(catalog.open(first).loadMetadata().cheatsEnabled,
                "catalog persists the create-world cheat option");
        require(catalog.deleteWorld(first), "catalog deletes the selected world");
        require(!std::filesystem::exists(root / first),
                "catalog left deleted world files on disk");
        require(catalog.open(second).loadMetadata().seed == 99,
                "deleting one world damaged another save");
        require(catalog.open(second).loadMetadata().worldType == WorldType::Superflat,
                "catalog persists the superflat terrain type");
        require(!catalog.deleteWorld(first),
                "deleting an already missing world should report no change");
        bool rejected = false;
        try {
            (void)catalog.open("../escape");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "catalog rejects path traversal ids");
        rejected = false;
        try {
            (void)catalog.deleteWorld("../escape");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "catalog deletion rejects path traversal ids");
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
    std::cout << "World catalog tests passed\n";
    return 0;
}
