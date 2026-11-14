#pragma once

#include "EntityAiTestRenderer.h"
#include "world/LodTerrainSystem.h"
#include <cmath>
#include <map>
#include <tuple>

// Records the public renderer handoff. Handles follow geometry across pending
// mesh moves; production is free to discard CPU vertices after upload.
class LodMeshRecorder final : public EntityAiTestRenderer {
public:
    void uploadChunkMesh(ChunkMesh& mesh) override {
        mesh.renderHandle = {++nextHandle};
        meshes.emplace(mesh.renderHandle.value, mesh);
        mesh.gpuReady = true;
    }
    void releaseChunkMesh(ChunkMesh& mesh) override {
        meshes.erase(mesh.renderHandle.value);
        mesh.abandonGpuResources();
    }
    const ChunkMesh* geometry(const LodRenderSubmission& draw) const {
        if (!draw.mesh || !draw.mesh->gpuReady) return nullptr;
        const auto found = meshes.find(draw.mesh->renderHandle.value);
        return found == meshes.end() ? nullptr : &found->second;
    }
    const ChunkMesh* tile(const std::vector<LodRenderSubmission>& draws,
                          const LodTileKey& key, const glm::dvec3& camera) const {
        const int side = LodTileData::SIDE << key.level;
        for (const auto& draw : draws)
            if (draw.tileSize == side &&
                std::abs(double(draw.model[3].x)+camera.x-double(key.x)*side)<.01 &&
                std::abs(double(draw.model[3].z)+camera.z-double(key.z)*side)<.01)
                return geometry(draw);
        return nullptr;
    }
    bool hasMaterial(const std::vector<LodRenderSubmission>& draws,
                     const LodTileKey& key, const glm::dvec3& camera,
                     BlockTexture material, float bottom, float top) const {
        const auto* mesh = tile(draws,key,camera);
        if (!mesh) return false;
        const auto index = getAtlasTextureIndex(material);
        for (const auto& vertex : mesh->vertices)
            if (vertex.py >= bottom && vertex.py <= top &&
                uint16_t(std::floor(vertex.tile)) == index) return true;
        return false;
    }
    static bool sameGeometry(const ChunkMesh& a, const ChunkMesh& b) {
        if (a.indices != b.indices || a.vertices.size() != b.vertices.size() ||
            a.opaqueIndexCount != b.opaqueIndexCount ||
            a.translucentIndexOffset != b.translucentIndexOffset ||
            a.translucentIndexCount != b.translucentIndexCount) return false;
        const auto attributes = [](const MeshVertex& v) {
            return std::tie(v.px,v.py,v.pz,v.ao,v.skyLight,v.blockLight,v.alpha,v.u,v.v,v.tile,v.face);
        };
        for (size_t i=0;i<a.vertices.size();++i)
            if (attributes(a.vertices[i]) != attributes(b.vertices[i])) return false;
        return true;
    }
private:
    uint32_t nextHandle = 0;
    std::map<uint32_t,ChunkMesh> meshes;
};
