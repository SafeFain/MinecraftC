#pragma once
#include "plugins/official_heaven/HeavenGenerator.h"
#include "plugins/ContentRegistry.h"

namespace OfficialHeaven {
HeavenGenerator& generator(uint64_t derivedSeed);
MC_GeneratorDefinitionV2 generatorDefinition();
MC_GeneratorDefinitionV3 generatorDefinitionV3();
std::vector<MC_StructureDefinitionV2> structureDefinitions();
int32_t sampleSpawn(const MC_GenerationContext*,double*,void*);
inline void installStructureAliases() {
    auto& aliases=Plugins::content().extensions.legacyStructureAliases;
    aliases.emplace(uint32_t(StructureType::XiguangRuin),"official_heaven:ruin");
    aliases.emplace(uint32_t(StructureType::StarCrystalGeode),"official_heaven:geode");
    aliases.emplace(uint32_t(StructureType::CloudspireTower),"official_heaven:spire");
    aliases.emplace(uint32_t(StructureType::SkywayShrine),"official_heaven:skyway_shrine");
}

template<class Generator,class Structure,class Loot>
void describeGeneration(Generator addGenerator,Structure addStructure,Loot addLoot) {
    addGenerator(generatorDefinitionV3());
    const auto table=[&](const char* key,std::initializer_list<MC_LootEntry> entries) {
        MC_LootTable d{sizeof(d),key,"9",entries.begin(),uint32_t(entries.size())};addLoot(d);
    };
    // Entry order is part of v9 loot hashing and slot selection.
    const auto key=[](ItemId id){return Plugins::itemKey(id);};
    const auto sun=key(ItemId::SUNSTONE),crystal=key(ItemId::STAR_CRYSTAL),flower=key(ItemId::STARFLOWER),
        bloom=key(ItemId::CLOUD_BLOOM),cloud=key(ItemId::CLOUDSTONE),tool=key(ItemId::STARSTEP_SCEPTER),
        shard=key(ItemId::STAR_CRYSTAL_SHARD),berry=key(ItemId::CLOUDBERRY),
        sunBrick=key(ItemId::SUNSTONE_BRICKS),moonBrick=key(ItemId::MOONSTONE_BRICKS),cloudBrick=key(ItemId::CLOUDSTONE_BRICKS);
    table("official_heaven:ruin_loot",{{sun.c_str(),3,9,100},{crystal.c_str(),3,7,90},{flower.c_str(),1,4,70},{bloom.c_str(),1,3,50},{shard.c_str(),2,5,85},{berry.c_str(),2,4,70},{sunBrick.c_str(),3,8,80}});
    table("official_heaven:geode_loot",{{crystal.c_str(),2,6,100},{cloud.c_str(),2,7,80},{flower.c_str(),1,4,60},{shard.c_str(),2,5,85},{berry.c_str(),2,4,70},{moonBrick.c_str(),3,8,80}});
    table("official_heaven:spire_loot",{{cloud.c_str(),2,6,100},{sun.c_str(),1,4,70},{crystal.c_str(),1,2,35},{bloom.c_str(),1,3,45},{shard.c_str(),2,5,85},{berry.c_str(),2,4,70},{cloudBrick.c_str(),3,8,80}});
    for(bool origin:{false,true})table(origin?"official_heaven:origin_loot":"official_heaven:skyway_loot",
        {{tool.c_str(),1,1,origin?100u:25u},{crystal.c_str(),2,5,100},{sun.c_str(),2,6,origin?100u:90u},{cloud.c_str(),3,8,100},{shard.c_str(),2,5,85},{berry.c_str(),2,4,70},{cloudBrick.c_str(),3,8,80}});
    for(const auto& d:structureDefinitions())addStructure(d);
}

// Standalone regression fixtures use the same descriptors without a loader.
inline void installGenerationFixture() {
    installStructureAliases();
    auto& d=Plugins::content().extensions.dimensions.at(1);
    d.seedDomain=HeavenGenerator::SEED_DOMAIN;d.generation={"official_heaven:heaven","9","{}",{}};
    d.spawn=MC_SPAWN_SAMPLED;d.sampleSpawn=sampleSpawn;
    describeGeneration([](const MC_GeneratorDefinitionV3& definition) {
        const auto& d=definition.base;
        Plugins::GeneratorContent g;g.key=d.base.key;g.name=d.base.name;g.revision=d.base.revision;g.dimension=d.base.dimension;
        g.column=d.base.column;g.voxel=d.base.voxel;g.batch=d.batch;g.terrainLod=d.terrain_lod;g.decorationLod=d.decoration_lod;g.lodRevision=d.lod_revision;
        g.layers=definition.layers;Plugins::content().extensions.generators.emplace(g.key,g);
    },[](const MC_StructureDefinitionV2& d) {
        Plugins::StructureContent s;s.key=d.key;s.name=d.name;s.revision=d.revision;s.dimension=d.dimension;
        s.omitCoarseLod=true;s.cell=d.cell_size;s.radius=d.radius;s.minY=d.min_y;s.maxY=d.max_y;s.order=d.order;s.placements=d.placements;s.buildV2=d.build;s.userdata=d.userdata;s.namespaces={"minecraftc","official_heaven"};
        Plugins::content().extensions.structures.emplace(s.key,s);
    },[](const MC_LootTable& d) {
        Plugins::LootContent l;l.key=d.key;l.revision=d.revision;
        for(uint32_t i=0;i<d.count;++i){const auto& e=d.entries[i];l.entries.push_back({e.item,e.minimum,e.maximum,e.chance});}
        Plugins::content().extensions.loot.emplace(l.key,l);
    });
}
}
