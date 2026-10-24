#pragma once

#include "game/SaveStore.h"
#include "world/WorldGenContext.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

// A published journal owns both complete chunk after-images. Recovery replays
// replacements, never appends entities, so interruption between writes is safe.
// Only one transaction can be in flight in a world; simulation must stop while
// a published transaction cannot be replayed. Runtime entity IDs are excluded.
namespace EntityTransferJournal {
struct Partition {
    DimensionId dimension=DimensionId::Overworld;
    int x=0,z=0;
    std::vector<WorldMetadata::PersistedEntity> entities;
};
inline std::filesystem::path directory(const std::filesystem::path& root,DimensionId dimension) {
    if(dimension==DimensionId::Overworld)return root;
    if(dimension==DimensionId::Nether)return root/"dimensions"/"nether";
    throw std::runtime_error("Invalid Nether transfer dimension");
}
inline std::filesystem::path pending(const std::filesystem::path& root) {
    return root/".nether-transfer.pending";
}
inline bool exists(const std::filesystem::path& root) {
    return std::filesystem::exists(pending(root));
}
inline void validate(const Partition& p) {
    if((p.dimension!=DimensionId::Overworld&&p.dimension!=DimensionId::Nether)||
        p.x < -1875000 || p.x>1875000 || p.z < -1875000 || p.z>1875000 || p.entities.size()>4096)
        throw std::runtime_error("Invalid entity transfer partition");
    for(const auto& entity:p.entities) {
        if(!std::isfinite(entity.position.x)||!std::isfinite(entity.position.y)||!std::isfinite(entity.position.z)||
            std::floor(entity.position.x/16)!=p.x||std::floor(entity.position.z/16)!=p.z)
            throw std::runtime_error("Entity transfer partition position mismatch");
    }
}
inline void prepare(const std::filesystem::path& root,const Partition& source,const Partition& target) {
    validate(source);validate(target);
    if(source.dimension==target.dimension)throw std::runtime_error("Entity transfer needs distinct dimensions");
    if(EntityTransferJournal::exists(root))throw std::runtime_error("An entity transfer is awaiting recovery");
    // Refuse incompatible roots before creating any transaction artifacts.
    const auto metadata=SaveStore(root).loadMetadata();
    if(!WorldGenContext::canLoadGeneration(metadata.generationVersion))throw std::runtime_error("Incompatible transfer world");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    auto staging=root/(".nether-transfer.stage-"+std::to_string(stamp));
    if(!std::filesystem::create_directory(staging))throw std::runtime_error("Cannot stage entity transfer");
    SaveStore(staging/"source").saveChunkEntities(source.x,source.z,source.entities);
    SaveStore(staging/"target").saveChunkEntities(target.x,target.z,target.entities);
    {
        std::ofstream manifest(staging/"manifest",std::ios::binary|std::ios::trunc);
        manifest<<"minecraftc-nether-transfer-1\n"<<static_cast<uint32_t>(source.dimension)<<' '<<source.x<<' '<<source.z<<'\n'
                <<static_cast<uint32_t>(target.dimension)<<' '<<target.x<<' '<<target.z<<'\n';
        manifest.flush();manifest.close();if(!manifest)throw std::runtime_error("Cannot write entity transfer manifest");
    }
    // Publication is the commit point. Unpublished staging directories are not
    // replayed, and therefore cannot remove a source entity after a failed prepare.
    std::filesystem::rename(staging,pending(root));
}
inline bool recover(const std::filesystem::path& root) {
    const auto journal=pending(root);if(!std::filesystem::exists(journal))return false;
    if(std::filesystem::is_symlink(journal)||!std::filesystem::is_directory(journal))throw std::runtime_error("Invalid entity transfer journal");
    const auto metadata=SaveStore(root).loadMetadata();
    if(!WorldGenContext::canLoadGeneration(metadata.generationVersion))throw std::runtime_error("Incompatible transfer world");
    if(std::filesystem::file_size(journal/"manifest")>256)throw std::runtime_error("Invalid transfer manifest size");
    std::ifstream manifest(journal/"manifest",std::ios::binary);
    std::string magic,extra;uint32_t sourceId=0,targetId=0;Partition source,target;
    if(!(manifest>>magic>>sourceId>>source.x>>source.z>>targetId>>target.x>>target.z)||
        magic!="minecraftc-nether-transfer-1" || (manifest>>extra))throw std::runtime_error("Invalid transfer manifest");
    manifest.close();
    source.dimension=static_cast<DimensionId>(sourceId);target.dimension=static_cast<DimensionId>(targetId);
    validate(source);validate(target);
    if(source.dimension==target.dimension)throw std::runtime_error("Invalid transfer dimensions");
    const auto requireSnapshot=[&](const char* name,const Partition& partition) {
        const auto file=journal/name/"entities"/("e."+std::to_string(partition.x)+"."+std::to_string(partition.z)+".bin");
        if(std::filesystem::is_symlink(journal/name)||std::filesystem::is_symlink(journal/name/"entities")||
            std::filesystem::is_symlink(file)||!std::filesystem::is_regular_file(file))throw std::runtime_error("Missing entity transfer snapshot");
    };
    requireSnapshot("source",source);requireSnapshot("target",target);
    source.entities=SaveStore(journal/"source").loadChunkEntities(source.x,source.z);
    target.entities=SaveStore(journal/"target").loadChunkEntities(target.x,target.z);
    validate(source);validate(target);
    SaveStore(directory(root,source.dimension)).saveChunkEntities(source.x,source.z,source.entities);
    SaveStore(directory(root,target.dimension)).saveChunkEntities(target.x,target.z,target.entities);
    // Both replacements completed. Removing the journal is optional for safety:
    // replaying this exact transaction again produces the same partitions.
    std::filesystem::remove_all(journal);return true;
}
inline void commit(const std::filesystem::path& root,const Partition& source,const Partition& target) {
    prepare(root,source,target);recover(root);
}
} // namespace EntityTransferJournal
