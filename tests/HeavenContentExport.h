#pragma once
#include "../external/nlohmann/json.hpp"
#include <fstream>

inline void exportHeavenContent(const std::filesystem::path& path) {
    using J=nlohmann::json;J root;root["version"]=1;
    for(uint16_t raw=1;raw<uint16_t(BlockId::COUNT);++raw) {
        const auto id=BlockId(raw);if(!Plugins::heavenBlockSlot(id))continue;
        const auto& p=getBlockProps(id);const auto& survival=getBlockSurvivalProps(id);
        J b={{"id",raw},{"key",Plugins::blockKey(id)},{"name",p.name},{"solid",p.solid},{"transparent",p.transparent},
            {"shape",uint32_t(p.shape)},{"layer",uint32_t(p.layer)},{"alpha",p.alpha},{"color",{p.color.r,p.color.g,p.color.b}},
            {"hardness",survival.hardness},{"tool",uint32_t(survival.preferredTool)},{"tier",uint32_t(survival.minimumHarvestTier)},
            {"unbreakable",survival.unbreakable},{"emission",getLightEmission(id)},{"drop",uint16_t(itemForBlock(id))}};
        for(int face=0;face<6;++face)b["materials"].push_back(getBlockTextureAssetName(getFaceTexture(id,FaceDir(face))));
        root["blocks"].push_back(b);
    }
    for(uint16_t raw=1;raw<uint16_t(ItemId::COUNT);++raw) {
        const auto id=ItemId(raw);if(!Plugins::heavenItemSlot(id))continue;const auto& p=getItemProps(id);
        root["items"].push_back({{"id",raw},{"key",Plugins::itemKey(id)},{"name",p.name},{"command",itemCommandName(id)},
            {"kind",uint32_t(p.kind)},{"stack",p.maxStack},{"durability",p.maxDurability},{"tool",uint32_t(p.tool)},
            {"tier",uint32_t(p.tier)},{"damage",p.attackDamage},{"speed",p.attackSpeed},{"food",p.food},
            {"saturation",p.saturation},{"block",p.placedBlock?int(*p.placedBlock):-1},{"category",uint32_t(creativeInventoryCategory(id))}});
    }
    size_t index=0;for(const auto& r:craftingRecipes()) {
        bool heaven=Plugins::heavenItemSlot(r.output.id);for(auto id:r.ingredients)heaven=heaven||Plugins::heavenItemSlot(id);
        if(heaven){J j={{"key","official_heaven:crafting_"+std::to_string(index)},{"width",r.width},{"height",r.height},
            {"output",uint16_t(r.output.id)},{"count",r.output.count},{"mirror",r.allowMirror},{"plank_variants",r.allowPlankVariants}};
            for(auto id:r.ingredients)j["ingredients"].push_back(uint16_t(id));root["crafting"].push_back(j);}
        ++index;
    }
    for(uint16_t raw=1;raw<uint16_t(ItemId::COUNT);++raw)if(const auto* r=findSmeltingRecipe(ItemId(raw))) {
        if(Plugins::heavenItemSlot(r->input)||Plugins::heavenItemSlot(r->output.id))root["smelting"].push_back(
            {{"key","official_heaven:smelting_"+std::to_string(raw)},{"input",raw},{"output",uint16_t(r->output.id)},{"count",r->output.count},{"ticks",r->cookTicks}});
    }
    std::ofstream out(path);out<<root.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot export Heaven reference");
}
