#pragma once

#include <cstdint>
#include <vector>

class Noise;

enum class CaveCell : uint8_t { Solid = 0, Air, Water, Lava };

struct CaveColumnInfo {
    int surfaceY = 0;
    int waterLevel = 0;
    bool submerged = false;
};

class CaveVolume {
public:
    CaveVolume() = default;
    CaveVolume(int minX, int minZ, int width, int depth);

    int minX() const { return m_minX; }
    int minZ() const { return m_minZ; }
    int width() const { return m_width; }
    int depth() const { return m_depth; }
    bool contains(int worldX, int y, int worldZ) const;
    CaveCell get(int worldX, int y, int worldZ) const;
    void set(int worldX, int y, int worldZ, CaveCell cell);

private:
    int m_minX = 0, m_minZ = 0, m_width = 0, m_depth = 0;
    std::vector<CaveCell> m_cells;
    size_t index(int worldX, int y, int worldZ) const;
};

// Deterministic hybrid cave generator. Noise fields create cheese caverns and
// spaghetti/noodle passages; seeded carvers add connected rooms and branches.
class CaveGenerator {
public:
    CaveGenerator(const Noise& noise, uint64_t seed);

    // columns is row-major [z * width + x] and describes the same XZ bounds.
    CaveVolume generateVolume(int minX, int minZ, int width, int depth,
                              const std::vector<CaveColumnInfo>& columns) const;

private:
    struct Segment { float ax, ay, az, bx, by, bz, radius, verticalScale; };
    struct Room { float x, y, z, radius, verticalScale; };

    const Noise& m_noise;
    uint64_t m_seed;

    static uint64_t hashCell(int x, int z, uint64_t seed);
    static uint64_t next64(uint64_t& state);
    static float random01(uint64_t& state);
    static float randomRange(uint64_t& state, float lo, float hi);
    static int floorDiv(int value, int divisor);

    void generateCarverCell(int cellX, int cellZ,
                            std::vector<Segment>& segments,
                            std::vector<Room>& rooms) const;
    void appendTunnel(float x, float y, float z, float yaw, float pitch,
                      float length, float radius, int branchDepth,
                      uint64_t& state, std::vector<Segment>& out) const;
    void rasterizeSegment(const Segment& segment, CaveVolume& volume,
                          const std::vector<CaveColumnInfo>& columns) const;
    void rasterizeRoom(const Room& room, CaveVolume& volume,
                       const std::vector<CaveColumnInfo>& columns) const;
    CaveCell liquidFor(int worldX, int y, int worldZ) const;
    bool canCarve(int worldX, int y, int worldZ, bool surfaceCarver,
                  const CaveVolume& volume,
                  const std::vector<CaveColumnInfo>& columns) const;
};
