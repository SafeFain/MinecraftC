#pragma once
#include <fstream>
#include "plugins/OfficialHeavenContent.h"
#include "plugins/official_heaven/HeavenDefinitions.h"
#include "plugins/official_heaven/HeavenPresentation.h"
#include "plugins/official_heaven/HeavenEnvironment.h"
#include "plugins/official_heaven/HeavenProvider.h"
#include "plugins/OfficialHeavenActions.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include "plugins/Json.h"
// These standalone tests exercise official built-in definitions without a loader.
// Loader/session integration tests initialize their own real package registries.
inline void installHeavenFixture(){
    Plugins::publishHeavenDimension();
    OfficialHeaven::loadDefinitions(std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/plugins/official_heaven");
    const auto package=std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/plugins/official_heaven";
    OfficialHeaven::loadAssetsFixture(package);
    for(const auto& file:std::filesystem::directory_iterator(package/"lang")) {
        std::ifstream input(file.path());const auto data=Plugins::Json::parse(std::string(std::istreambuf_iterator<char>(input),{}));
        for(const auto& value:data.object)Plugins::mutableContentState().extensions.translations[file.path().stem().string()].emplace(value.first,value.second.text());
    }
    Plugins::mutableContentState().extensions.dimensions.at(1).nameKey="official_heaven:dimension.heaven";
    Plugins::mutableContentState().extensions.bedTravel.emplace("official_heaven:bed_travel",Plugins::DimensionTravelContent{0,1,"official_heaven:bed_travel","official_heaven:travel.heaven"});
    Plugins::heavenSemantics([](const MC_BlockBehaviorV2& d){
        Plugins::BlockBehaviorContent b;b.fields=d.fields;b.light=d.light_dampening;b.tags=d.tags;b.substrate=d.substrate_tags;
        b.support=d.base.support;b.replaceable=d.base.replaceable!=0;b.encouragement=d.base.fire_encouragement;b.flammability=d.base.fire_flammability;b.action=d.base.action?d.base.action:"";
        for(uint32_t i=0;i<d.drop_count;++i){const auto& r=d.drops[i];b.drops.push_back({r.item,r.minimum,r.maximum,r.numerator,r.denominator});}
        Plugins::mutableContentState().extensions.blockBehaviors.emplace(static_cast<uint16_t>(Plugins::resolveBlock(d.base.block)),b);
    },[](const MC_ItemBehaviorV2& d){Plugins::ItemBehaviorContent b;b.fields=d.fields;b.fuel=d.fuel_ticks;b.tags=d.tags;b.action=d.base.action?d.base.action:"";b.dimension=d.base.dimension?d.base.dimension:"";Plugins::mutableContentState().extensions.itemBehaviors.emplace(static_cast<uint16_t>(Plugins::resolveItem(d.base.item)),b);});
    Plugins::ActionContent sky;sky.key="official_heaven:skyway";sky.range=256;sky.targetV3=[](const auto& v,auto& t){return Plugins::skywayTargetV3(&v,&t,nullptr)==0;};
    Plugins::mutableContentState().extensions.actions.emplace(sky.key,sky);
    sky.key="official_heaven:starstep";sky.range=96;sky.cooldown=3;sky.durability=1;sky.rangeFromEye=sky.toolPose=sky.invalidMarker=true;sky.useCooldown=.15f;sky.targetV3=[](const auto& v,auto& t){return Plugins::starstepTargetV3(&v,&t,nullptr)==0;};
    Plugins::mutableContentState().extensions.actions.emplace(sky.key,sky);
    OfficialHeaven::installGenerationFixture();
    for(const auto& d:OfficialHeaven::particleDefinitions()) {
        Plugins::ParticleContent p;p.key=d.base.key;p.material=d.base.material;p.definition=d.base;p.definition.key=p.definition.material=nullptr;
        p.advanced=true;p.collision=d.collision!=0;p.shape=d.shape;
        for(size_t i=0;i<3;++i)std::copy(std::begin(d.drift[i]),std::end(d.drift[i]),p.drift[i].begin());
        Plugins::mutableContentState().extensions.particles.emplace(p.key,p);
    }
    const auto ambient=OfficialHeaven::ambientDefinition();Plugins::AmbientContent a;a.key=ambient.key;a.dimension=ambient.dimension;a.stateBytes=ambient.state_bytes;a.replaceNative=ambient.replace_native!=0;
    a.update=[callback=ambient.update](const MC_AmbientContext& context,void* state){return callback(&context,state,nullptr)==0;};
    Plugins::mutableContentState().extensions.ambient.emplace(a.key,a);
    const auto environment=OfficialHeaven::environmentDefinition();Plugins::EnvironmentContent e;e.key=environment.base.key;e.parameters=environment.base.parameters;e.particleIntensity=environment.particle_intensity;e.skyFlags=environment.sky_flags;e.gradeStrength=environment.grade_strength;std::copy(std::begin(environment.grade_color),std::end(environment.grade_color),e.gradeColor.begin());
    e.callback=[callback=environment.base.callback](MC_EnvironmentParameters& p){callback(nullptr,&p,nullptr);};
    for(size_t i=0;i<MC_SKY_LAYER_VECTORS;++i)std::copy(std::begin(environment.sky.layers[i]),std::end(environment.sky.layers[i]),e.skyLayers[i].begin());
    Plugins::mutableContentState().extensions.environments.emplace(1,e);
    ++Plugins::mutableContentState().revision;
}
