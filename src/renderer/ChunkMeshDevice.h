#pragma once

struct ChunkMesh;

class IChunkMeshDevice {
public:
    virtual ~IChunkMeshDevice() = default;
    virtual void uploadChunkMesh(ChunkMesh&) = 0;
    virtual void releaseChunkMesh(ChunkMesh&) = 0;
};
