#include "plugins/official_heaven/HeavenDefinitions.h"
#include "plugins/ContentMutation.h"
#include "plugins/ContentQueries.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace OfficialHeaven {
void loadDefinitions(const std::filesystem::path& package) {
    using J=nlohmann::json;
    const auto path=package/"content.json";
    if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Official content exceeds package limit");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Missing official content definitions");
    J data;file>>data;
    if(data.at("version")!=1)throw std::runtime_error("Unsupported official content definition version");
    if(data.contains("aliases"))for(const auto& value:data.at("aliases").items()) {
        const auto target=value.value().get<std::string>();
        if(target.rfind("official_heaven:",0)!=0)throw std::runtime_error("Invalid official translation alias");
        Plugins::mutableContentState().extensions.translationAliases.emplace(value.key(),target);
    }
    for(const auto& value:data.at("blocks")) {
        const auto raw=value.at("id").get<uint16_t>();const auto id=BlockId(raw);
        if(!Plugins::heavenBlockSlot(id)||value.at("key")!="minecraftc:block_"+std::to_string(raw))throw std::runtime_error("Invalid official block reservation");
        Plugins::BlockContent b;b.key=value.at("key");b.name=value.at("name");b.properties.id=id;
        b.properties.solid=value.at("solid");b.properties.transparent=value.at("transparent");
        const auto shape=value.at("shape").get<uint32_t>(),layer=value.at("layer").get<uint32_t>();
        if(shape>uint32_t(RenderShape::Boxes)||layer>uint32_t(RenderLayer::Translucent))throw std::runtime_error("Invalid official block shape/layer");
        b.properties.shape=RenderShape(shape);b.properties.layer=RenderLayer(layer);b.properties.alpha=value.at("alpha");
        for(size_t i=0;i<3;++i)b.properties.color[int(i)]=value.at("color").at(i);
        b.survival={value.at("hardness"),ToolKind(value.at("tool").get<uint32_t>()),ToolTier(value.at("tier").get<uint32_t>()),value.at("unbreakable")};
        b.emission=value.at("emission");b.drop=ItemId(value.at("drop").get<uint16_t>());
        if(b.drop!=ItemId::EMPTY)b.dropKey="minecraftc:item_"+std::to_string(uint16_t(b.drop));
        for(size_t i=0;i<6;++i)b.materials[i]=value.at("materials").at(i);
        Plugins::registerBlock(std::move(b));
    }
    for(const auto& value:data.at("items")) {
        const auto raw=value.at("id").get<uint16_t>();
        if(!Plugins::heavenItemSlot(ItemId(raw))||value.at("key")!="minecraftc:item_"+std::to_string(raw))throw std::runtime_error("Invalid official item reservation");
        Plugins::ItemContent item;item.key=value.at("key");item.command=value.at("command");
        item.properties={value.at("name"),ItemKind(value.at("kind").get<uint32_t>()),value.at("stack"),value.at("durability"),
            ToolKind(value.at("tool").get<uint32_t>()),ToolTier(value.at("tier").get<uint32_t>()),value.at("damage"),value.at("speed"),value.at("food"),value.at("saturation")};
        const int block=value.at("block");if(block>=0)item.properties.placedBlock=BlockId(block);
        item.category=CreativeItemCategory(value.at("category").get<uint32_t>());
        item.material=value.value("material",std::string("star_crystal"));
        const auto icon=value.at("icon").get<std::string>();
        if(icon.rfind("textures/items/",0)!=0||icon.find("..")!=std::string::npos)throw std::runtime_error("Invalid official item icon path");
        item.icon=package/icon;
        if(!std::filesystem::is_regular_file(item.icon))throw std::runtime_error("Missing official item icon");
        const auto nameKey="official_heaven:item."+item.command;
        Plugins::mutableContentState().extensions.nameKeys.emplace(item.key,nameKey);
        Plugins::mutableContentState().extensions.translationAliases.emplace("item."+std::to_string(raw),nameKey);
        Plugins::registerItem(std::move(item));
    }
    for(const auto& value:data.at("crafting")) {
        CraftingRecipe r;r.width=value.at("width");r.height=value.at("height");r.allowMirror=value.at("mirror");r.allowPlankVariants=value.at("plank_variants");
        for(size_t i=0;i<9;++i)r.ingredients[i]=ItemId(value.at("ingredients").at(i).get<uint16_t>());
        r.output={ItemId(value.at("output").get<uint16_t>()),value.at("count"),0};
        const std::string key=value.at("key");r.sortOrder=uint32_t(std::stoul(key.substr(key.rfind('_')+1)));
        if(!Plugins::mutableContentState().crafting.emplace(key,r).second)throw std::runtime_error("Duplicate official crafting recipe");
    }
    for(const auto& value:data.at("smelting")) {
        SmeltingRecipe r{ItemId(value.at("input").get<uint16_t>()),{ItemId(value.at("output").get<uint16_t>()),value.at("count"),0},value.at("ticks")};
        if(!Plugins::mutableContentState().smelting.emplace(value.at("key"),r).second)throw std::runtime_error("Duplicate official smelting recipe");
    }
    for(uint16_t id=1;id<uint16_t(BlockId::COUNT);++id)if(Plugins::heavenBlockSlot(BlockId(id))&&!Plugins::mutableContentState().blocks.count(id))throw std::runtime_error("Incomplete official block definitions");
    for(uint16_t id=1;id<uint16_t(ItemId::COUNT);++id)if(Plugins::heavenItemSlot(ItemId(id))&&!Plugins::mutableContentState().items.count(id))throw std::runtime_error("Incomplete official item definitions");
}
int32_t registerAssets(const MC_Host* host,MC_PluginContext* ctx,const std::filesystem::path& package) {
    const auto* materials=static_cast<const MC_MaterialSequencesV1*>(host->query_extension(ctx,MC_MATERIAL_SEQUENCES_EXTENSION,1));
    const auto* presentation=static_cast<const MC_PresentationV2*>(host->query_extension(ctx,MC_PRESENTATION_EXTENSION,2));
    if(!materials||!presentation)return -1;
    std::ifstream file(package/"materials.json");nlohmann::json data;file>>data;
    for(const auto& value:data) {
        const std::string key=value.at("key");
        const auto images=value.at("images").get<std::vector<std::string>>(),normals=value.at("normals").get<std::vector<std::string>>(),properties=value.at("properties").get<std::vector<std::string>>();
        const uint32_t count=value.at("variants").get<uint32_t>()*value.at("frames").get<uint32_t>();
        if(images.size()!=count||normals.size()!=count||properties.size()!=count)throw std::runtime_error("Incomplete official material sequence");
        std::vector<const char*> a,b,c;for(uint32_t i=0;i<count;++i){a.push_back(images[i].c_str());b.push_back(normals[i].c_str());c.push_back(properties[i].c_str());}
        MC_MaterialSequenceDefinition d{};d.size=sizeof(d);d.base.size=sizeof(d.base);d.base.key=key.c_str();d.base.replace=1;for(float& color:d.base.color)color=1;
        d.variants=value.at("variants");d.frames=value.at("frames");d.fps_milli=value.at("fps_milli");d.seed=value.at("seed");d.images=a.data();d.normals=b.data();d.properties=c.data();
        if(materials->register_sequence(ctx,&d))return -1;
    }
    for(const auto& entry:Plugins::mutableContentState().items)if(Plugins::heavenItemSlot(ItemId(entry.first))) {
        const auto path=entry.second.icon.lexically_relative(package).generic_string();
        const MC_ItemIcon d{sizeof(d),entry.second.key.c_str(),path.c_str()};if(presentation->register_item_icon(ctx,&d))return -1;
    }
    return 0;
}
void loadAssetsFixture(const std::filesystem::path& package) {
    std::ifstream file(package/"materials.json");nlohmann::json data;file>>data;
    for(const auto& value:data) {
        Plugins::MaterialContent m;m.key=value.at("key");m.replace=true;m.variants=value.at("variants");m.frames=value.at("frames");m.fpsMilli=value.at("fps_milli");m.seed=value.at("seed");
        for(const auto& path:value.at("images"))m.images.push_back(package/path.get<std::string>());
        for(const auto& path:value.at("normals"))m.normals.push_back(package/path.get<std::string>());
        for(const auto& path:value.at("properties"))m.propertyFrames.push_back(package/path.get<std::string>());
        m.image=m.images.at(0);m.normal=m.normals.at(0);m.properties=m.propertyFrames.at(0);
        Plugins::mutableContentState().materials.emplace(m.key,std::move(m));
    }
}
}
