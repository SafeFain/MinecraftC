#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Config.h"
#include "renderer/RenderHandles.h"
#include "world/Block.h"
#include "world/BlockLightLogic.h"
#include "world/FluidLogic.h"

struct MeshVertex {
    float px, py, pz;
    float ao, skyLight, blockLight, alpha;
    float u, v;
    float tile;
    float face;
};

struct ChunkMesh {
    // Mesh construction samples one voxel beyond each horizontal edge for
    // face visibility, smooth lighting, and ambient occlusion.
    inline static constexpr std::array<std::array<int, 2>, 8>
        NEIGHBOR_DEPENDENCY_OFFSETS{{
            {{-1, -1}}, {{0, -1}}, {{1, -1}},
            {{-1,  0}},             {{1,  0}},
            {{-1,  1}}, {{0,  1}}, {{1,  1}}
        }};

    std::vector<MeshVertex> vertices;
    std::vector<unsigned int> indices;

    RenderMeshHandle renderHandle;
    size_t indexCount = 0;
    size_t opaqueIndexCount = 0;
    size_t translucentIndexOffset = 0;
    size_t translucentIndexCount = 0;
    size_t shadowCasterIndexOffset = 0;
    size_t shadowCasterIndexCount = 0;
    bool gpuReady = false;

    void clear() {
        vertices.clear();
        indices.clear();
        indexCount = 0;
        opaqueIndexCount = 0;
        translucentIndexOffset = 0;
        translucentIndexCount = 0;
        shadowCasterIndexOffset = 0;
        shadowCasterIndexCount = 0;
        gpuReady = false;
    }

    void abandonGpuResources() {
        renderHandle = {};
        gpuReady = false;
    }

    bool empty() const {
        return vertices.empty() || indices.empty();
    }

    size_t uploadBytes() const {
        return vertices.size() * sizeof(MeshVertex) +
               indices.size() * sizeof(unsigned int);
    }

    // Replace only the CPU-side geometry with a completed worker mesh.
    // GPU ownership remains in Renderer while worker-produced CPU geometry is
    // transferred into the active chunk mesh.
    void adoptCpuGeometry(ChunkMesh& completed) {
        using std::swap;
        swap(vertices, completed.vertices);
        swap(indices, completed.indices);
        swap(indexCount, completed.indexCount);
        swap(opaqueIndexCount, completed.opaqueIndexCount);
        swap(translucentIndexOffset, completed.translucentIndexOffset);
        swap(translucentIndexCount, completed.translucentIndexCount);
        swap(shadowCasterIndexOffset, completed.shadowCasterIndexOffset);
        swap(shadowCasterIndexCount, completed.shadowCasterIndexCount);
    }

    // ── Greedy mesh builder ──────────────────────────────────────────
    // For each of 6 face directions:
    //   1. Build 2D visibility mask per depth layer
    //   2. Greedily merge adjacent same-type visible cells
    //   3. Emit quads (4 vertices + 6 indices each)
    //
    // neighborGetter(worldX, worldY, worldZ) -> BlockId

    template<typename NeighborFunc, typename LightFunc>
    void build(int chunkWorldX, int chunkWorldZ,
               const uint8_t* blocks,
               const int columnMaxY[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z],
               NeighborFunc&& getNeighbor, LightFunc&& getLight)
    {
        clear();
        std::vector<unsigned int> opaqueIndices;
        std::vector<unsigned int> translucentIndices;
        std::vector<unsigned int> shadowIndices;
        auto localIdx = [](int x, int worldY, int z) -> int {
            return x + z * Config::CHUNK_SIZE_X
                     + Config::worldYToStorageY(worldY) *
                           Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
        };
        (void)columnMaxY;
        auto voxelLight = [&](int x, int y, int z) {
            return getLight(chunkWorldX+x,y,chunkWorldZ+z);
        };
        auto normalizedLight = [&](int x,int y,int z) {
            const LightSample value=voxelLight(x,y,z);
            return glm::vec2(static_cast<float>(value.sky)/15.0f,
                             static_cast<float>(value.block)/15.0f);
        };
        auto encodeFlatLight = [](float tile,uint8_t sky,uint8_t block) {
            const int packed=std::clamp<int>(sky,0,15)*16+
                             std::clamp<int>(block,0,15);
            return tile+static_cast<float>(packed)/512.0f;
        };

        // Determine if a face is visible and returns its BlockId (or AIR if not)
        auto faceVisible = [&](int x, int y, int z, FaceDir face) -> BlockId {
            BlockId id = static_cast<BlockId>(blocks[localIdx(x, y, z)]);
            if (id == BlockId::AIR) return BlockId::AIR;

            const BlockProperties& props = getBlockProps(id);
            if (props.shape == RenderShape::Cross) return BlockId::AIR;

            const glm::ivec3& off = FACE_OFFSETS[static_cast<int>(face)];
            int nx = chunkWorldX + x + off.x;
            int ny = y + off.y;
            int nz = chunkWorldZ + z + off.z;

            BlockId nid = getNeighbor(nx, ny, nz);

            return shouldRenderCubeFace(id, nid) ? id : BlockId::AIR;
        };

        struct MaskCell {
            uint8_t block = 0;
            uint8_t ao[4] = {3, 3, 3, 3};
            uint8_t sky[4] = {};
            uint8_t light[4] = {};

            bool operator==(const MaskCell& other) const {
                return block == other.block &&
                       std::equal(std::begin(sky),std::end(sky),std::begin(other.sky)) &&
                       std::equal(std::begin(light),std::end(light),std::begin(other.light)) &&
                       ao[0] == other.ao[0] && ao[1] == other.ao[1] &&
                       ao[2] == other.ao[2] && ao[3] == other.ao[3];
            }
        };

        auto occludesAO = [](BlockId id) {
            if (id == BlockId::AIR) return false;
            const auto& props = getBlockProps(id);
            return props.shape == RenderShape::Cube &&
                   props.layer == RenderLayer::Opaque;
        };

        auto faceAxes = [](FaceDir face, glm::ivec3& uAxis, glm::ivec3& vAxis) {
            if (face == FaceDir::TOP || face == FaceDir::BOTTOM) {
                uAxis = {1, 0, 0}; vAxis = {0, 0, 1};
            } else if (face == FaceDir::FRONT || face == FaceDir::BACK) {
                uAxis = {1, 0, 0}; vAxis = {0, 1, 0};
            } else {
                uAxis = {0, 0, 1}; vAxis = {0, 1, 0};
            }
        };

        auto cornerAO = [&](int x, int y, int z, FaceDir face,
                            int uSign, int vSign) -> uint8_t {
            glm::ivec3 uAxis, vAxis;
            faceAxes(face, uAxis, vAxis);
            const glm::ivec3 n = FACE_OFFSETS[static_cast<int>(face)];
            const glm::ivec3 base(chunkWorldX + x, y, chunkWorldZ + z);
            const glm::ivec3 sideU = base + n + uAxis * uSign;
            const glm::ivec3 sideV = base + n + vAxis * vSign;
            const glm::ivec3 corner = sideU + vAxis * vSign;
            const bool a = occludesAO(getNeighbor(sideU.x, sideU.y, sideU.z));
            const bool b = occludesAO(getNeighbor(sideV.x, sideV.y, sideV.z));
            const bool c = occludesAO(getNeighbor(corner.x, corner.y, corner.z));
            if (a && b) return 0;
            return static_cast<uint8_t>(3 - static_cast<int>(a) -
                                        static_cast<int>(b) - static_cast<int>(c));
        };

        auto cornerLight = [&](int x,int y,int z,FaceDir face,
                               int uSign,int vSign) -> LightSample {
            glm::ivec3 uAxis,vAxis;faceAxes(face,uAxis,vAxis);
            const glm::ivec3 n=FACE_OFFSETS[static_cast<int>(face)];
            const glm::ivec3 base(chunkWorldX+x,y,chunkWorldZ+z);
            const glm::ivec3 samples[4]={base+n,base+n+uAxis*uSign,
                base+n+vAxis*vSign,base+n+uAxis*uSign+vAxis*vSign};
            int sky=0,block=0,count=0;
            for(const auto& p:samples) {
                if(getLightDampening(getNeighbor(p.x,p.y,p.z))>=15)continue;
                const LightSample value=getLight(p.x,p.y,p.z);
                sky+=value.sky;block+=value.block;++count;
            }
            if(count==0)return {};
            return {static_cast<uint8_t>((sky+count/2)/count),
                    static_cast<uint8_t>((block+count/2)/count)};
        };

        std::vector<MaskCell> mask;
        std::vector<uint8_t> visited;

        // Process each face direction
        for (int f = 0; f < FACE_COUNT; ++f) {
            FaceDir face = static_cast<FaceDir>(f);

            // Determine plane dimensions for this face
            int size1 = 0, size2 = 0, depthMax = 0;

            switch (face) {
                case FaceDir::TOP: case FaceDir::BOTTOM:
                    size1 = Config::CHUNK_SIZE_X; size2 = Config::CHUNK_SIZE_Z;
                    depthMax = Config::CHUNK_SIZE_Y;
                    break;
                case FaceDir::FRONT: case FaceDir::BACK:
                    size1 = Config::CHUNK_SIZE_X; size2 = Config::CHUNK_SIZE_Y;
                    depthMax = Config::CHUNK_SIZE_Z;
                    break;
                case FaceDir::RIGHT: case FaceDir::LEFT:
                    size1 = Config::CHUNK_SIZE_Z; size2 = Config::CHUNK_SIZE_Y;
                    depthMax = Config::CHUNK_SIZE_X;
                    break;
            }
            if (size1 == 0 || size2 == 0 || depthMax == 0) continue;

            const size_t planeSize = static_cast<size_t>(size1 * size2);
            mask.resize(planeSize);
            visited.resize(planeSize);

            // For each depth layer, build and merge a visibility mask
            for (int d = 0; d < depthMax; ++d) {
                std::fill(mask.begin(), mask.end(), MaskCell{});

                // Fill mask
                for (int u = 0; u < size1; ++u) {
                    for (int v = 0; v < size2; ++v) {
                        int x, y, z;
                        if (face == FaceDir::TOP || face == FaceDir::BOTTOM) {
                            x = u; y = Config::storageYToWorldY(d); z = v;
                        } else if (face == FaceDir::FRONT || face == FaceDir::BACK) {
                            x = u; y = Config::storageYToWorldY(v); z = d;
                        } else { // RIGHT or LEFT
                            x = d; y = Config::storageYToWorldY(v); z = u;
                        }
                        BlockId visible = faceVisible(x, y, z, face);
                        if (visible == BlockId::AIR) continue;
                        MaskCell& cell = mask[u + v * size1];
                        cell.block = static_cast<uint8_t>(visible);
                        cell.ao[0] = cornerAO(x, y, z, face, -1, -1);
                        cell.ao[1] = cornerAO(x, y, z, face,  1, -1);
                        cell.ao[2] = cornerAO(x, y, z, face,  1,  1);
                        cell.ao[3] = cornerAO(x, y, z, face, -1,  1);
                        const int signs[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
                        for(int corner=0;corner<4;++corner) {
                            const LightSample value=cornerLight(x,y,z,face,
                                signs[corner][0],signs[corner][1]);
                            cell.sky[corner]=value.sky;cell.light[corner]=value.block;
                        }
                    }
                }

                // Greedy merge
                std::fill(visited.begin(), visited.end(), uint8_t{0});

                for (int v = 0; v < size2; ++v) {
                    for (int u = 0; u < size1; ++u) {
                        int idx = u + v * size1;
                        if (visited[idx]) continue;
                        const MaskCell cell = mask[idx];
                        if (cell.block == 0) continue;

                        // Find max width (contiguous same block type)
                        int maxU = u;
                        while (maxU + 1 < size1 &&
                               mask[(maxU + 1) + v * size1] == cell &&
                               !visited[(maxU + 1) + v * size1]) {
                            ++maxU;
                        }

                        // Find max height
                        int maxV = v;
                        bool canExtend = true;
                        while (canExtend && maxV + 1 < size2) {
                            for (int uu = u; uu <= maxU; ++uu) {
                                int idx2 = uu + (maxV + 1) * size1;
                                if (visited[idx2] || !(mask[idx2] == cell)) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) ++maxV;
                        }

                        // Mark region as visited
                        for (int vv = v; vv <= maxV; ++vv) {
                            for (int uu = u; uu <= maxU; ++uu) {
                                visited[uu + vv * size1] = true;
                            }
                        }

                        // Emit quad: 4 corners
                        // The quad spans [u, maxU+1] × [v, maxV+1] in plane coords
                        // at depth d (on the outer face side)
                        BlockId bid = static_cast<BlockId>(cell.block);
                        unsigned int baseIdx = static_cast<unsigned int>(vertices.size());

                        float u0 = static_cast<float>(u);
                        float u1 = static_cast<float>(maxU + 1);
                        float v0 = static_cast<float>(v);
                        float v1 = static_cast<float>(maxV + 1);
                        if (face != FaceDir::TOP && face != FaceDir::BOTTOM) {
                            v0 += static_cast<float>(Config::WORLD_MIN_Y);
                            v1 += static_cast<float>(Config::WORLD_MIN_Y);
                        }

                        // Position of the face plane (just outside the block)
                        float depthPos;
                        if (face == FaceDir::TOP)      depthPos = static_cast<float>(Config::storageYToWorldY(d) + 1);
                        else if (face == FaceDir::BOTTOM) depthPos = static_cast<float>(Config::storageYToWorldY(d));
                        else if (face == FaceDir::FRONT)  depthPos = static_cast<float>(d);
                        else if (face == FaceDir::BACK)   depthPos = static_cast<float>(d + 1);
                        else if (face == FaceDir::RIGHT)  depthPos = static_cast<float>(d + 1);
                        else                              depthPos = static_cast<float>(d); // LEFT

                        // Emit 4 vertices in CCW order (from outside the face)
                        // The quad winding depends on the face normal direction
                        // Using standard Minecraft greedy meshing convention

                        MeshVertex vtx[4];
                        float alpha = getBlockProps(bid).alpha;
                        int flatSky=0,flatBlock=0;
                        for(int i=0;i<4;++i){flatSky+=cell.sky[i];flatBlock+=cell.light[i];}
                        const float encodedTile=encodeFlatLight(
                            static_cast<float>(getFaceTextureIndex(bid,face)),
                            static_cast<uint8_t>((flatSky+2)/4),
                            static_cast<uint8_t>((flatBlock+2)/4));
                        for (int i = 0; i < 4; ++i)
                            vtx[i] = {0,0,0, 1.0f, 0.0f, 0.0f, alpha, 0, 0,
                                      encodedTile,
                                      static_cast<float>(f) +
                                          (isLeafBlock(bid) ? 16.0f : 0.0f)};

                        auto setPos = [&](int vi, float px, float py, float pz) {
                            vtx[vi].px = px; vtx[vi].py = py; vtx[vi].pz = pz;
                        };

                        // Winding is CCW from outside in world space.
                        // Faces on (+Y, -Z, +X) use pattern A; faces on (-Y, +Z, -X) use reversed pattern B.
                        // Pattern A: (u1,v1)->(u1,v0)->(u0,v0)->(u0,v1)
                        // Pattern B: (u0,v1)->(u0,v0)->(u1,v0)->(u1,v1)
                        switch (face) {
                            case FaceDir::TOP:    // +Y → pattern A
                                setPos(0, u1, depthPos, v1);
                                setPos(1, u1, depthPos, v0);
                                setPos(2, u0, depthPos, v0);
                                setPos(3, u0, depthPos, v1);
                                break;
                            case FaceDir::BOTTOM: // -Y → pattern B
                                setPos(0, u0, depthPos, v1);
                                setPos(1, u0, depthPos, v0);
                                setPos(2, u1, depthPos, v0);
                                setPos(3, u1, depthPos, v1);
                                break;
                            case FaceDir::FRONT:  // -Z → pattern A
                                setPos(0, u1, v1, depthPos);
                                setPos(1, u1, v0, depthPos);
                                setPos(2, u0, v0, depthPos);
                                setPos(3, u0, v1, depthPos);
                                break;
                            case FaceDir::BACK:   // +Z → pattern B
                                setPos(0, u0, v1, depthPos);
                                setPos(1, u0, v0, depthPos);
                                setPos(2, u1, v0, depthPos);
                                setPos(3, u1, v1, depthPos);
                                break;
                            case FaceDir::RIGHT:  // +X → pattern A
                                setPos(0, depthPos, v1, u1);
                                setPos(1, depthPos, v0, u1);
                                setPos(2, depthPos, v0, u0);
                                setPos(3, depthPos, v1, u0);
                                break;
                            case FaceDir::LEFT:   // -X → pattern B
                                setPos(0, depthPos, v1, u0);
                                setPos(1, depthPos, v0, u0);
                                setPos(2, depthPos, v0, u1);
                                setPos(3, depthPos, v1, u1);
                                break;
                            default: break;
                        }

                        const float width = u1 - u0;
                        const float height = v1 - v0;
                        const bool patternA = face == FaceDir::TOP ||
                                              face == FaceDir::FRONT ||
                                              face == FaceDir::RIGHT;
                        if (patternA) {
                            vtx[0].u = width; vtx[0].v = height;
                            vtx[1].u = width; vtx[1].v = 0;
                            vtx[2].u = 0;     vtx[2].v = 0;
                            vtx[3].u = 0;     vtx[3].v = height;
                        } else {
                            vtx[0].u = 0;     vtx[0].v = height;
                            vtx[1].u = 0;     vtx[1].v = 0;
                            vtx[2].u = width; vtx[2].v = 0;
                            vtx[3].u = width; vtx[3].v = height;
                        }

                        const int aoMapA[4] = {2, 1, 0, 3};
                        const int aoMapB[4] = {3, 0, 1, 2};
                        const int* aoMap = patternA ? aoMapA : aoMapB;
                        for (int i = 0; i < 4; ++i)
                            vtx[i].ao = static_cast<float>(cell.ao[aoMap[i]]) / 3.0f;
                        for (int i = 0; i < 4; ++i) {
                            vtx[i].skyLight=static_cast<float>(cell.sky[aoMap[i]])/15.0f;
                            vtx[i].blockLight=static_cast<float>(cell.light[aoMap[i]])/15.0f;
                        }

                        // Push vertices and indices (2 triangles = 6 indices)
                        for (int i = 0; i < 4; ++i) vertices.push_back(vtx[i]);
                        auto& target = getBlockProps(bid).layer == RenderLayer::Translucent
                            ? translucentIndices : opaqueIndices;
                        const bool castsShadow = getBlockProps(bid).layer != RenderLayer::Translucent ||
                            bid == BlockId::LEAVES || bid == BlockId::BIRCH_LEAVES ||
                            bid == BlockId::SPRUCE_LEAVES || bid == BlockId::JUNGLE_LEAVES ||
                            bid == BlockId::ACACIA_LEAVES;
                        if (vtx[0].ao + vtx[2].ao > vtx[1].ao + vtx[3].ao) {
                            const unsigned int flipped[] = {0, 1, 3, 1, 2, 3};
                            for (unsigned int value : flipped) {
                                target.push_back(baseIdx + value);
                                if (castsShadow) shadowIndices.push_back(baseIdx + value);
                            }
                        } else {
                            const unsigned int standard[] = {0, 1, 2, 0, 2, 3};
                            for (unsigned int value : standard) {
                                target.push_back(baseIdx + value);
                                if (castsShadow) shadowIndices.push_back(baseIdx + value);
                            }
                        }
                    }
                }
            }
        }

        // Architectural slabs and stairs are emitted as their collision-box
        // decomposition. They deliberately stay out of greedy cube masks:
        // partial faces must retain their half-block dimensions and state.
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    const BlockId id = static_cast<BlockId>(blocks[localIdx(x, y, z)]);
                    const RenderShape shape = getBlockProps(id).shape;
                    if (shape != RenderShape::Slab && shape != RenderShape::Stair)
                        continue;
                    const BlockCollisionBoxes geometry = blockCollisionBoxes(id);
                    ArchitecturalBlockState architecture;
                    decodeArchitecturalBlock(id, architecture);
                    const glm::vec2 sampled = normalizedLight(x, y, z);
                    auto neighborOccludes = [&](FaceDir face,
                                                const BlockCollisionBox& box) {
                        const bool boundary =
                            (face == FaceDir::TOP && box.max.y == 1.0f) ||
                            (face == FaceDir::BOTTOM && box.min.y == 0.0f) ||
                            (face == FaceDir::FRONT && box.min.z == 0.0f) ||
                            (face == FaceDir::BACK && box.max.z == 1.0f) ||
                            (face == FaceDir::RIGHT && box.max.x == 1.0f) ||
                            (face == FaceDir::LEFT && box.min.x == 0.0f);
                        if (!boundary) return false;
                        const glm::ivec3 off = FACE_OFFSETS[static_cast<size_t>(face)];
                        const BlockId neighborId = getNeighbor(
                            chunkWorldX + x + off.x, y + off.y,
                            chunkWorldZ + z + off.z);
                        const BlockProperties& neighbor = getBlockProps(neighborId);
                        if (!neighbor.solid ||
                            neighbor.layer != RenderLayer::Opaque) return false;
                        const BlockCollisionBoxes neighborBoxes =
                            blockCollisionBoxes(neighborId);
                        const size_t neighborCount = std::min<size_t>(
                            neighborBoxes.count, neighborBoxes.boxes.size());
                        const int normalAxis = face == FaceDir::TOP ||
                            face == FaceDir::BOTTOM ? 1 :
                            (face == FaceDir::FRONT || face == FaceDir::BACK ? 2 : 0);
                        const int uAxis = normalAxis == 0 ? 1 : 0;
                        const int vAxis = normalAxis == 2 ? 1 : 2;
                        std::array<float, 6> uCuts{};
                        std::array<float, 6> vCuts{};
                        int uCount = 2, vCount = 2;
                        uCuts[0] = box.min[uAxis]; uCuts[1] = box.max[uAxis];
                        vCuts[0] = box.min[vAxis]; vCuts[1] = box.max[vAxis];
                        auto touchesBoundary = [&](const BlockCollisionBox& other) {
                            switch (face) {
                                case FaceDir::TOP: return other.min.y == 0.0f;
                                case FaceDir::BOTTOM: return other.max.y == 1.0f;
                                case FaceDir::FRONT: return other.max.z == 1.0f;
                                case FaceDir::BACK: return other.min.z == 0.0f;
                                case FaceDir::RIGHT: return other.min.x == 0.0f;
                                case FaceDir::LEFT: return other.max.x == 1.0f;
                            }
                            return false;
                        };
                        for (size_t i = 0; i < neighborCount; ++i) {
                            const BlockCollisionBox& other = neighborBoxes.boxes[i];
                            if (!touchesBoundary(other)) continue;
                            for (const float value : {other.min[uAxis], other.max[uAxis]})
                                if (value > box.min[uAxis] && value < box.max[uAxis] &&
                                    uCount < static_cast<int>(uCuts.size()))
                                    uCuts[uCount++] = value;
                            for (const float value : {other.min[vAxis], other.max[vAxis]})
                                if (value > box.min[vAxis] && value < box.max[vAxis] &&
                                    vCount < static_cast<int>(vCuts.size()))
                                    vCuts[vCount++] = value;
                        }
                        auto insertionSort = [](auto& values, int count) {
                            for (int i = 1; i < count; ++i) {
                                const float value = values[i];
                                int j = i;
                                while (j > 0 && values[j-1] > value) {
                                    values[j] = values[j-1];
                                    --j;
                                }
                                values[j] = value;
                            }
                        };
                        insertionSort(uCuts,uCount);
                        insertionSort(vCuts,vCount);
                        for (int ui = 0; ui + 1 < uCount; ++ui) {
                            for (int vi = 0; vi + 1 < vCount; ++vi) {
                                const float u = (uCuts[ui] + uCuts[ui+1]) * .5f;
                                const float v = (vCuts[vi] + vCuts[vi+1]) * .5f;
                                bool covered = false;
                                for (size_t i = 0; i < neighborCount; ++i) {
                                    const BlockCollisionBox& other = neighborBoxes.boxes[i];
                                    covered = covered || (touchesBoundary(other) &&
                                        u >= other.min[uAxis] && u <= other.max[uAxis] &&
                                        v >= other.min[vAxis] && v <= other.max[vAxis]);
                                }
                                if (!covered) return false;
                            }
                        }
                        return true;
                    };
                    auto emitBoxFace = [&](uint8_t boxIndex, FaceDir face) {
                        BlockCollisionBox box=geometry.boxes[boxIndex];
                        if (shape==RenderShape::Stair) {
                            const bool fullHalfFace=boxIndex==0&&
                                ((architecture.half==BlockHalf::Bottom&&
                                  face==FaceDir::TOP)||
                                 (architecture.half==BlockHalf::Top&&
                                  face==FaceDir::BOTTOM));
                            const bool coveredStepFace=boxIndex==1&&
                                ((architecture.half==BlockHalf::Bottom&&
                                  face==FaceDir::BOTTOM)||
                                 (architecture.half==BlockHalf::Top&&
                                  face==FaceDir::TOP));
                            if(coveredStepFace)return;
                            if(fullHalfFace) {
                                switch(architecture.direction) {
                                    case BedDirection::North: box.min.z=.5f;break;
                                    case BedDirection::East: box.max.x=.5f;break;
                                    case BedDirection::South: box.max.z=.5f;break;
                                    case BedDirection::West: box.min.x=.5f;break;
                                }
                            }
                        }
                        if (neighborOccludes(face, box)) return;
                        const float x0 = x + box.min.x, x1 = x + box.max.x;
                        const float y0 = y + box.min.y, y1 = y + box.max.y;
                        const float z0 = z + box.min.z, z1 = z + box.max.z;
                        glm::vec3 p[4]{};
                        switch (face) {
                            case FaceDir::TOP:
                                p[0]={x1,y1,z1}; p[1]={x1,y1,z0};
                                p[2]={x0,y1,z0}; p[3]={x0,y1,z1}; break;
                            case FaceDir::BOTTOM:
                                p[0]={x0,y0,z1}; p[1]={x0,y0,z0};
                                p[2]={x1,y0,z0}; p[3]={x1,y0,z1}; break;
                            case FaceDir::FRONT:
                                p[0]={x1,y1,z0}; p[1]={x1,y0,z0};
                                p[2]={x0,y0,z0}; p[3]={x0,y1,z0}; break;
                            case FaceDir::BACK:
                                p[0]={x0,y1,z1}; p[1]={x0,y0,z1};
                                p[2]={x1,y0,z1}; p[3]={x1,y1,z1}; break;
                            case FaceDir::RIGHT:
                                p[0]={x1,y1,z1}; p[1]={x1,y0,z1};
                                p[2]={x1,y0,z0}; p[3]={x1,y1,z0}; break;
                            case FaceDir::LEFT:
                                p[0]={x0,y1,z0}; p[1]={x0,y0,z0};
                                p[2]={x0,y0,z1}; p[3]={x0,y1,z1}; break;
                        }
                        const float tile = encodeFlatLight(
                            static_cast<float>(getFaceTextureIndex(id, face)),
                            static_cast<uint8_t>(std::round(sampled.x * 15.0f)),
                            static_cast<uint8_t>(std::round(sampled.y * 15.0f)));
                        const unsigned int base =
                            static_cast<unsigned int>(vertices.size());
                        const glm::vec2 uv[4]={{1,1},{1,0},{0,0},{0,1}};
                        for (int i = 0; i < 4; ++i)
                            vertices.push_back({p[i].x,p[i].y,p[i].z,1.0f,
                                sampled.x,sampled.y,1.0f,uv[i].x,uv[i].y,tile,
                                static_cast<float>(face)});
                        for (unsigned int index : {0u,1u,2u,0u,2u,3u}) {
                            opaqueIndices.push_back(base + index);
                            shadowIndices.push_back(base + index);
                        }
                    };
                    for (uint8_t box = 0; box < geometry.count; ++box)
                        for (FaceDir face : {FaceDir::TOP, FaceDir::BOTTOM,
                                             FaceDir::FRONT, FaceDir::BACK,
                                             FaceDir::RIGHT, FaceDir::LEFT})
                            emitBoxFace(box, face);
                }
            }
        }

        // Fluid cells use independent quads because their level-dependent top
        // surfaces cannot participate in cube greedy merging.
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    const BlockId id = static_cast<BlockId>(blocks[localIdx(x, y, z)]);
                    if (!isFluid(id)) continue;
                    const bool lava = isLava(id);
                    auto same = [&](int wx, int wy, int wz) {
                        const BlockId other = getNeighbor(wx, wy, wz);
                        return lava ? isLava(other) : isWater(other);
                    };
                    const FluidSample fluidSample = [&](const glm::ivec3& p) {
                        return getNeighbor(p.x, p.y, p.z);
                    };
                    const FluidAvailable fluidAvailable = [&](const glm::ivec3& p) {
                        return Config::isValidWorldY(p.y);
                    };
                    auto cornerHeight = [&](int cornerX, int cornerZ) {
                        // A chunk with no loaded neighbor still has a valid
                        // local sample; callers that stream a seam rebuild it
                        // once the adjacent chunk is published.
                        return std::max(0.0f, fluidCornerHeight(
                            {chunkWorldX + cornerX, y, chunkWorldZ + cornerZ},
                            lava, fluidSample, fluidAvailable));
                    };
                    const float h00 = cornerHeight(x, z);
                    const float h10 = cornerHeight(x + 1, z);
                    const float h01 = cornerHeight(x, z + 1);
                    const float h11 = cornerHeight(x + 1, z + 1);
                    const float baseTile = static_cast<float>(getFaceTextureIndex(id, FaceDir::TOP));
                    const float alpha = getBlockProps(id).alpha;
                    const glm::vec2 sampled=normalizedLight(x,y,z);
                    const float sky=sampled.x;
                    const float light = lava ? 1.0f : sampled.y;
                    const float tile=encodeFlatLight(baseTile,
                        static_cast<uint8_t>(std::round(sky*15.0f)),
                        static_cast<uint8_t>(std::round(light*15.0f)));
                    const glm::vec2 flow = fluidFlowVector(
                        {chunkWorldX + x, y, chunkWorldZ + z}, lava,
                        fluidSample, fluidAvailable);
                    auto emit = [&](FaceDir face, const glm::vec3 (&positions)[4]) {
                        const unsigned int base = static_cast<unsigned int>(vertices.size());
                        glm::vec2 uv[4] = {{1,1},{1,0},{0,0},{0,1}};
                        if (face == FaceDir::TOP && glm::length(flow) > 0.001f) {
                            const float angle = std::atan2(flow.y, flow.x);
                            const float c = std::cos(angle);
                            const float s = std::sin(angle);
                            for (glm::vec2& coordinate : uv) {
                                const glm::vec2 centered = coordinate - glm::vec2(0.5f);
                                coordinate = glm::vec2(
                                    centered.x * c - centered.y * s,
                                    centered.x * s + centered.y * c) + glm::vec2(0.5f);
                            }
                        }
                        for (int i = 0; i < 4; ++i) {
                            vertices.push_back({positions[i].x, positions[i].y,
                                positions[i].z, 1.0f, sky, light, alpha,
                                uv[i].x, uv[i].y, tile, static_cast<float>(face)});
                        }
                        for (unsigned int index : {0u,1u,2u,0u,2u,3u})
                            translucentIndices.push_back(base + index);
                    };
                    const float x0 = static_cast<float>(x), x1 = x0 + 1.0f;
                    const float z0 = static_cast<float>(z), z1 = z0 + 1.0f;
                    const float y0 = static_cast<float>(y);
                    if (!same(chunkWorldX + x, y + 1, chunkWorldZ + z)) {
                        const glm::vec3 p[4] = {{x1,y0+h11-0.001f,z1},
                                                {x1,y0+h10-0.001f,z0},
                                                {x0,y0+h00-0.001f,z0},
                                                {x0,y0+h01-0.001f,z1}};
                        emit(FaceDir::TOP, p);
                    }
                    auto visibleSide = [&](int wx, int wy, int wz) {
                        const BlockId neighbor = getNeighbor(wx, wy, wz);
                        if ((lava && isLava(neighbor)) || (!lava && isWater(neighbor)))
                            return false;
                        return !isSolid(neighbor);
                    };
                    if (visibleSide(chunkWorldX+x, y, chunkWorldZ+z-1)) {
                        const glm::vec3 p[4] = {{x1,y0+h10,z0},{x1,y0,z0},
                                                {x0,y0,z0},{x0,y0+h00,z0}};
                        emit(FaceDir::FRONT, p);
                    }
                    if (visibleSide(chunkWorldX+x, y, chunkWorldZ+z+1)) {
                        const glm::vec3 p[4] = {{x0,y0+h01,z1},{x0,y0,z1},
                                                {x1,y0,z1},{x1,y0+h11,z1}};
                        emit(FaceDir::BACK, p);
                    }
                    if (visibleSide(chunkWorldX+x+1, y, chunkWorldZ+z)) {
                        const glm::vec3 p[4] = {{x1,y0+h11,z1},{x1,y0,z1},
                                                {x1,y0,z0},{x1,y0+h10,z0}};
                        emit(FaceDir::RIGHT, p);
                    }
                    if (visibleSide(chunkWorldX+x-1, y, chunkWorldZ+z)) {
                        const glm::vec3 p[4] = {{x0,y0+h00,z0},{x0,y0,z0},
                                                {x0,y0,z1},{x0,y0+h01,z1}};
                        emit(FaceDir::LEFT, p);
                    }
                    const BlockId below = getNeighbor(chunkWorldX+x, y-1, chunkWorldZ+z);
                    if (!isSolid(below) && !same(chunkWorldX+x, y-1, chunkWorldZ+z)) {
                        const glm::vec3 p[4] = {{x0,y0,z1},{x0,y0,z0},
                                                {x1,y0,z0},{x1,y0,z1}};
                        emit(FaceDir::BOTTOM, p);
                    }
                }
            }
        }

        // Cross-shaped plants are never greedy-merged. Each diagonal plane is
        // emitted double-sided so the normal culling state can stay enabled.
        auto emitCrossPlane = [&](float x0, float z0, float x1, float z1,
                                  float y, float sky, const BlockProperties& props) {
            unsigned int base = static_cast<unsigned int>(vertices.size());
            const float baseTile = static_cast<float>(getFaceTextureIndex(props.id, FaceDir::FRONT));
            constexpr float CROSS_FACE = 6.0f;
            const glm::vec2 sampled=normalizedLight(static_cast<int>(x0),
                static_cast<int>(y),static_cast<int>(z0));
            const float light = getLightEmission(props.id)>0 ? 1.0f : sampled.y;
            const float tile=encodeFlatLight(baseTile,
                static_cast<uint8_t>(std::round(sky*15.0f)),
                static_cast<uint8_t>(std::round(light*15.0f)));
            vertices.push_back({x0, y,       z0, 1, sky, light, props.alpha, 0, 0, tile, CROSS_FACE});
            vertices.push_back({x1, y,       z1, 1, sky, light, props.alpha, 1, 0, tile, CROSS_FACE});
            vertices.push_back({x1, y + 1.0f,z1, 1, sky, light, props.alpha, 1, 1, tile, CROSS_FACE});
            vertices.push_back({x0, y + 1.0f,z0, 1, sky, light, props.alpha, 0, 1, tile, CROSS_FACE});
            const unsigned int order[] = {
                0, 1, 2, 0, 2, 3,
                2, 1, 0, 3, 2, 0
            };
            for (unsigned int value : order) {
                opaqueIndices.push_back(base + value);
                shadowIndices.push_back(base + value);
            }
        };

        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    BlockId id = static_cast<BlockId>(blocks[localIdx(x, y, z)]);
                    const BlockProperties& props = getBlockProps(id);
                    if (props.shape != RenderShape::Cross) continue;
                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);
                    float fz = static_cast<float>(z);
                    float sky = normalizedLight(x,y,z).x;
                    emitCrossPlane(fx + 0.12f, fz + 0.12f,
                                   fx + 0.88f, fz + 0.88f, fy, sky, props);
                    emitCrossPlane(fx + 0.88f, fz + 0.12f,
                                   fx + 0.12f, fz + 0.88f, fy, sky, props);
                }
            }
        }

        // A weather-created snow layer is a thin box rather than a full cube.
        // It is intentionally not greedy-merged so it cannot merge with the
        // serialized full snow block used by terrain generation.
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    if (static_cast<BlockId>(blocks[localIdx(x, y, z)]) !=
                        BlockId::SNOW_LAYER) continue;
                    const float baseTile = static_cast<float>(
                        getAtlasTextureIndex(BlockTexture::SnowLayer));
                    const glm::vec2 sampled=normalizedLight(x,y,z);
                    const float sky=sampled.x;
                    const float light=sampled.y;
                    const float tile=encodeFlatLight(baseTile,
                        static_cast<uint8_t>(std::round(sky*15.0f)),
                        static_cast<uint8_t>(std::round(light*15.0f)));
                    for (int f : {0, 2, 3, 4, 5}) {
                        const unsigned int base =
                            static_cast<unsigned int>(vertices.size());
                        for (int cornerIndex : FACE_INDICES[static_cast<size_t>(f)]) {
                            glm::vec3 p = CUBE_CORNERS[static_cast<size_t>(cornerIndex)];
                            p.y *= 0.125f;
                            const float u = (f <= 1 || f >= 4) ? p.z : p.x;
                            const float v = f <= 1 ? p.x : p.y * 8.0f;
                            vertices.push_back({
                                p.x + x, p.y + y, p.z + z, 1.0f, sky, light,
                                1.0f, u, v, tile, static_cast<float>(f)});
                            const unsigned int index = base +
                                static_cast<unsigned int>(vertices.size() - base - 1);
                            opaqueIndices.push_back(index);
                            shadowIndices.push_back(index);
                        }
                    }
                }
            }
        }

        // Beds are assembled from narrow frame rails, four end legs, a
        // mattress, and a raised pillow. Each serialized half emits only the
        // geometry inside its own voxel so beds remain valid across chunks.
        for (int y = Config::WORLD_MIN_Y; y < Config::WORLD_MAX_Y; ++y) {
            for (int z = 0; z < Config::CHUNK_SIZE_Z; ++z) {
                for (int x = 0; x < Config::CHUNK_SIZE_X; ++x) {
                    const BlockId id = static_cast<BlockId>(blocks[localIdx(x, y, z)]);
                    if (!isBed(id)) continue;
                    BedPart part = BedPart::Foot;
                    BedDirection direction = BedDirection::North;
                    decodeBed(id, part, direction);
                    const glm::ivec3 partnerPosition =
                        glm::ivec3(chunkWorldX + x, y, chunkWorldZ + z) +
                        bedPartnerOffset(id);
                    const BlockId expectedPartner = bedBlock(
                        part == BedPart::Foot ? BedPart::Head : BedPart::Foot,
                        direction);
                    const bool paired = getNeighbor(
                        partnerPosition.x, partnerPosition.y,
                        partnerPosition.z) == expectedPartner;

                    const glm::vec2 sampled = normalizedLight(x, y, z);
                    const float sky = sampled.x;
                    const float light = sampled.y;
                    auto transform = [&](const glm::vec3& point) {
                        glm::vec3 result = point;
                        switch (direction) {
                            case BedDirection::North:
                                result.x = point.x; result.z = 1.0f - point.z; break;
                            case BedDirection::East:
                                result.x = point.z; result.z = point.x; break;
                            case BedDirection::South:
                                result.x = 1.0f - point.x; result.z = point.z; break;
                            case BedDirection::West:
                                result.x = 1.0f - point.z;
                                result.z = 1.0f - point.x; break;
                        }
                        return result;
                    };
                    FaceDir seamFace = FaceDir::FRONT;
                    if (part == BedPart::Foot) {
                        switch (direction) {
                            case BedDirection::North: seamFace = FaceDir::FRONT; break;
                            case BedDirection::East: seamFace = FaceDir::RIGHT; break;
                            case BedDirection::South: seamFace = FaceDir::BACK; break;
                            case BedDirection::West: seamFace = FaceDir::LEFT; break;
                        }
                    } else {
                        switch (direction) {
                            case BedDirection::North: seamFace = FaceDir::BACK; break;
                            case BedDirection::East: seamFace = FaceDir::LEFT; break;
                            case BedDirection::South: seamFace = FaceDir::FRONT; break;
                            case BedDirection::West: seamFace = FaceDir::RIGHT; break;
                        }
                    }
                    auto emitCuboid = [&](glm::vec3 minimum, glm::vec3 maximum,
                                          BlockTexture texture,
                                          bool touchesSeam = false) {
                        const glm::vec3 a = transform(minimum);
                        const glm::vec3 b = transform(maximum);
                        minimum = glm::min(a, b);
                        maximum = glm::max(a, b);
                        for (int face = 0; face < FACE_COUNT; ++face) {
                            const auto direction = static_cast<FaceDir>(face);
                            const BlockTexture faceTexture = texture == BlockTexture::WhiteBed
                                ? (direction == FaceDir::TOP ? BlockTexture::WhiteBedTop
                                   : direction == FaceDir::BOTTOM ? BlockTexture::WhiteBedBottom
                                   : BlockTexture::WhiteBedSide)
                                : texture;
                            const float tile = encodeFlatLight(
                                static_cast<float>(getAtlasTextureIndex(faceTexture)),
                                static_cast<uint8_t>(std::round(sky * 15.0f)),
                                static_cast<uint8_t>(std::round(light * 15.0f)));
                            if (paired && touchesSeam &&
                                face == static_cast<int>(seamFace)) continue;
                            const unsigned int base =
                                static_cast<unsigned int>(vertices.size());
                            for (int cornerIndex : FACE_INDICES[static_cast<size_t>(face)]) {
                                const glm::vec3 corner =
                                    CUBE_CORNERS[static_cast<size_t>(cornerIndex)];
                                const glm::vec3 position = minimum +
                                    corner * (maximum - minimum);
                                const float u = (face <= 1 || face >= 4)
                                    ? corner.z : corner.x;
                                const float v = face <= 1 ? corner.x : corner.y;
                                vertices.push_back({
                                    position.x + x, position.y + y, position.z + z,
                                    1.0f, sky, light, 1.0f, u, v, tile,
                                    static_cast<float>(face)});
                            }
                            for (unsigned int index = 0; index < 6; ++index) {
                                opaqueIndices.push_back(base + index);
                                shadowIndices.push_back(base + index);
                            }
                        }
                    };

                    constexpr float U = 1.0f / 16.0f;
                    emitCuboid({0, 2*U, 0}, {2*U, 4*U, 1},
                               BlockTexture::Planks, true);
                    emitCuboid({14*U, 2*U, 0}, {1, 4*U, 1},
                               BlockTexture::Planks, true);
                    emitCuboid({U, 4*U, 0}, {15*U, 8*U, 1},
                               BlockTexture::WhiteBed, true);

                    const float endMinimum = part == BedPart::Foot ? 0.0f : 14*U;
                    const float endMaximum = part == BedPart::Foot ? 2*U : 1.0f;
                    emitCuboid({2*U, 2*U, endMinimum},
                               {14*U, 4*U, endMaximum}, BlockTexture::Planks);
                    emitCuboid({0, 0, endMinimum},
                               {2*U, 4*U, endMaximum}, BlockTexture::Planks);
                    emitCuboid({14*U, 0, endMinimum},
                               {1, 4*U, endMaximum}, BlockTexture::Planks);
                    if (part == BedPart::Head) {
                        emitCuboid({2*U, 8*U, 9*U},
                                   {14*U, 9*U, 14*U}, BlockTexture::WhiteWool);
                    }
                }
            }
        }

        indices.reserve(opaqueIndices.size() + translucentIndices.size() +
                        shadowIndices.size());
        indices.insert(indices.end(), opaqueIndices.begin(), opaqueIndices.end());
        opaqueIndexCount = opaqueIndices.size();
        translucentIndexOffset = opaqueIndexCount;
        indices.insert(indices.end(), translucentIndices.begin(), translucentIndices.end());
        translucentIndexCount = translucentIndices.size();
        shadowCasterIndexOffset = indices.size();
        indices.insert(indices.end(), shadowIndices.begin(), shadowIndices.end());
        shadowCasterIndexCount = shadowIndices.size();
        indexCount = indices.size();
    }

    void upload();
    void destroy();
};
