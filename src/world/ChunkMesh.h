#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Config.h"
#include "world/Block.h"

struct MeshVertex {
    float px, py, pz;
    float cr, cg, cb;
};

struct ChunkMesh {
    std::vector<MeshVertex> vertices;
    std::vector<unsigned int> indices;

    GLuint vao = 0;
    size_t indexCount = 0;
    bool gpuReady = false;

    void clear() {
        vertices.clear();
        indices.clear();
        indexCount = 0;
        gpuReady = false;
    }

    bool empty() const {
        return vertices.empty() || indices.empty();
    }

    // ── Greedy mesh builder ──────────────────────────────────────────
    // For each of 6 face directions:
    //   1. Build 2D visibility mask per depth layer
    //   2. Greedily merge adjacent same-type visible cells
    //   3. Emit quads (4 vertices + 6 indices each)
    //
    // neighborGetter(worldX, worldY, worldZ) -> BlockId

    template<typename NeighborFunc>
    void build(int chunkWorldX, int chunkWorldZ,
               const uint8_t* blocks,
               const int /*columnMaxY*/[Config::CHUNK_SIZE_X][Config::CHUNK_SIZE_Z],
               NeighborFunc&& getNeighbor)
    {
        clear();

        auto localIdx = [](int x, int y, int z) -> int {
            return x + z * Config::CHUNK_SIZE_X
                     + y * Config::CHUNK_SIZE_X * Config::CHUNK_SIZE_Z;
        };

        // Determine if a face is visible and returns its BlockId (or AIR if not)
        auto faceVisible = [&](int x, int y, int z, FaceDir face) -> BlockId {
            BlockId id = static_cast<BlockId>(blocks[localIdx(x, y, z)]);
            if (id == BlockId::AIR) return BlockId::AIR;

            const BlockProperties& props = getBlockProps(id);

            const glm::ivec3& off = FACE_OFFSETS[static_cast<int>(face)];
            int nx = chunkWorldX + x + off.x;
            int ny = y + off.y;
            int nz = chunkWorldZ + z + off.z;

            BlockId nid = getNeighbor(nx, ny, nz);

            if (props.solid) {
                // Solid block: face visible if neighbor is air or transparent
                if (nid == BlockId::AIR) return id;
                if (!getBlockProps(nid).solid) return id;
                return BlockId::AIR;
            } else if (props.transparent) {
                // Transparent block (e.g. water): face visible only toward AIR
                if (nid == BlockId::AIR) return id;
                return BlockId::AIR;
            }
            return BlockId::AIR;
        };

        // Process each face direction
        for (int f = 0; f < FACE_COUNT; ++f) {
            FaceDir face = static_cast<FaceDir>(f);

            // Determine plane dimensions for this face
            int size1, size2, depthMax;

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

            // For each depth layer, build and merge a visibility mask
            for (int d = 0; d < depthMax; ++d) {
                // Allocate mask: which block type is visible at (u, v)?
                // Using uint8_t; 0 = AIR (not visible)
                std::vector<uint8_t> mask(size1 * size2, 0);

                // Fill mask
                for (int u = 0; u < size1; ++u) {
                    for (int v = 0; v < size2; ++v) {
                        int x, y, z;
                        if (face == FaceDir::TOP || face == FaceDir::BOTTOM) {
                            x = u; y = d; z = v;
                        } else if (face == FaceDir::FRONT || face == FaceDir::BACK) {
                            x = u; y = v; z = d;
                        } else { // RIGHT or LEFT
                            x = d; y = v; z = u;
                        }
                        if (y >= Config::CHUNK_SIZE_Y) continue;
                        mask[u + v * size1] = static_cast<uint8_t>(faceVisible(x, y, z, face));
                    }
                }

                // Greedy merge
                std::vector<bool> visited(size1 * size2, false);

                for (int v = 0; v < size2; ++v) {
                    for (int u = 0; u < size1; ++u) {
                        int idx = u + v * size1;
                        if (visited[idx]) continue;
                        uint8_t blockType = mask[idx];
                        if (blockType == 0) continue; // AIR = not visible

                        // Find max width (contiguous same block type)
                        int maxU = u;
                        while (maxU + 1 < size1 &&
                               mask[(maxU + 1) + v * size1] == blockType &&
                               !visited[(maxU + 1) + v * size1]) {
                            ++maxU;
                        }

                        // Find max height
                        int maxV = v;
                        bool canExtend = true;
                        while (canExtend && maxV + 1 < size2) {
                            for (int uu = u; uu <= maxU; ++uu) {
                                int idx2 = uu + (maxV + 1) * size1;
                                if (visited[idx2] || mask[idx2] != blockType) {
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
                        BlockId bid = static_cast<BlockId>(blockType);
                        glm::vec3 color = getFaceColor(bid, face);
                        unsigned int baseIdx = static_cast<unsigned int>(vertices.size());

                        float u0 = static_cast<float>(u);
                        float u1 = static_cast<float>(maxU + 1);
                        float v0 = static_cast<float>(v);
                        float v1 = static_cast<float>(maxV + 1);

                        // Position of the face plane (just outside the block)
                        float depthPos;
                        if (face == FaceDir::TOP)      depthPos = static_cast<float>(d + 1);
                        else if (face == FaceDir::BOTTOM) depthPos = static_cast<float>(d);
                        else if (face == FaceDir::FRONT)  depthPos = static_cast<float>(d);
                        else if (face == FaceDir::BACK)   depthPos = static_cast<float>(d + 1);
                        else if (face == FaceDir::RIGHT)  depthPos = static_cast<float>(d + 1);
                        else                              depthPos = static_cast<float>(d); // LEFT

                        // Emit 4 vertices in CCW order (from outside the face)
                        // The quad winding depends on the face normal direction
                        // Using standard Minecraft greedy meshing convention

                        MeshVertex vtx[4];
                        for (int i = 0; i < 4; ++i) vtx[i] = {0,0,0, color.r, color.g, color.b};

                        auto setPos = [&](int vi, float px, float py, float pz) {
                            vtx[vi].px = px; vtx[vi].py = py; vtx[vi].pz = pz;
                        };

                        // Winding must be CW from outside in world space.
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

                        // Push vertices and indices (2 triangles = 6 indices)
                        for (int i = 0; i < 4; ++i) vertices.push_back(vtx[i]);
                        indices.push_back(baseIdx);
                        indices.push_back(baseIdx + 1);
                        indices.push_back(baseIdx + 2);
                        indices.push_back(baseIdx);
                        indices.push_back(baseIdx + 2);
                        indices.push_back(baseIdx + 3);
                    }
                }
            }
        }

        indexCount = indices.size();
    }

    void upload();
    void destroy();
};
