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
    bool gpuReady = false;

    void clear() {
        vertices.clear();
        indices.clear();
        indexCount = 0;
        opaqueIndexCount = 0;
        translucentIndexOffset = 0;
        translucentIndexCount = 0;
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
                                      static_cast<float>(f)};

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
                        if (vtx[0].ao + vtx[2].ao > vtx[1].ao + vtx[3].ao) {
                            const unsigned int flipped[] = {0, 1, 3, 1, 2, 3};
                            for (unsigned int value : flipped)
                                target.push_back(baseIdx + value);
                        } else {
                            const unsigned int standard[] = {0, 1, 2, 0, 2, 3};
                            for (unsigned int value : standard)
                                target.push_back(baseIdx + value);
                        }
                    }
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
                    auto cornerHeight = [&](int cornerX, int cornerZ) {
                        float sum = 0.0f;
                        int count = 0;
                        for (int dz = -1; dz <= 0; ++dz) {
                            for (int dx = -1; dx <= 0; ++dx) {
                                const int wx = chunkWorldX + cornerX + dx;
                                const int wz = chunkWorldZ + cornerZ + dz;
                                const BlockId sample = getNeighbor(wx, y, wz);
                                if ((lava && isLava(sample)) || (!lava && isWater(sample))) {
                                    if (same(wx, y + 1, wz)) return 1.0f;
                                    sum += fluidSurfaceHeight(sample);
                                    ++count;
                                }
                            }
                        }
                        return count ? sum / static_cast<float>(count)
                                     : fluidSurfaceHeight(id);
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
                    auto emit = [&](FaceDir face, const glm::vec3 (&positions)[4]) {
                        const unsigned int base = static_cast<unsigned int>(vertices.size());
                        const glm::vec2 uv[4] = {{1,1},{1,0},{0,0},{0,1}};
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
                        const glm::vec3 p[4] = {{x1,y0+h11,z1},{x1,y0+h10,z0},
                                                {x0,y0+h00,z0},{x0,y0+h01,z1}};
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
            for (unsigned int value : order) opaqueIndices.push_back(base + value);
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
                            opaqueIndices.push_back(base +
                                static_cast<unsigned int>(vertices.size() - base - 1));
                        }
                    }
                }
            }
        }

        indices.reserve(opaqueIndices.size() + translucentIndices.size());
        indices.insert(indices.end(), opaqueIndices.begin(), opaqueIndices.end());
        opaqueIndexCount = opaqueIndices.size();
        translucentIndexOffset = opaqueIndexCount;
        indices.insert(indices.end(), translucentIndices.begin(), translucentIndices.end());
        translucentIndexCount = translucentIndices.size();
        indexCount = indices.size();
    }

    void upload();
    void destroy();
};
