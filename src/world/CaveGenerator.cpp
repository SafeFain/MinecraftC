#include "world/CaveGenerator.h"
#include "world/Noise.h"
#include "Config.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr float PI = 3.14159265358979323846f;
}

CaveVolume::CaveVolume(int minX, int minZ, int width, int depth)
    : m_minX(minX), m_minZ(minZ), m_width(width), m_depth(depth),
      m_cells(static_cast<size_t>(width) * Config::WORLD_HEIGHT * depth,
              CaveCell::Solid) {}

size_t CaveVolume::index(int wx, int y, int wz) const {
    return (static_cast<size_t>(wz - m_minZ) * Config::WORLD_HEIGHT +
            static_cast<size_t>(Config::worldYToStorageY(y))) * static_cast<size_t>(m_width) +
           static_cast<size_t>(wx - m_minX);
}

bool CaveVolume::contains(int wx, int y, int wz) const {
    return wx >= m_minX && wx < m_minX + m_width && wz >= m_minZ &&
           wz < m_minZ + m_depth && Config::isValidWorldY(y);
}

CaveCell CaveVolume::get(int wx, int y, int wz) const {
    return contains(wx, y, wz) ? m_cells[index(wx, y, wz)] : CaveCell::Solid;
}

void CaveVolume::set(int wx, int y, int wz, CaveCell cell) {
    if (contains(wx, y, wz)) m_cells[index(wx, y, wz)] = cell;
}

CaveGenerator::CaveGenerator(const Noise& noise, uint64_t seed)
    : m_noise(noise), m_seed(seed) {}

uint64_t CaveGenerator::hashCell(int x, int z, uint64_t seed) {
    uint64_t h = static_cast<uint64_t>(static_cast<int64_t>(x)) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<uint64_t>(static_cast<int64_t>(z)) * 0xBF58476D1CE4E5B9ULL;
    h ^= seed;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    return h ^ (h >> 31);
}

uint64_t CaveGenerator::next64(uint64_t& s) {
    s += 0x9E3779B97F4A7C15ULL;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

float CaveGenerator::random01(uint64_t& s) {
    return static_cast<float>(next64(s) >> 40) / 16777216.0f;
}

float CaveGenerator::randomRange(uint64_t& s, float lo, float hi) {
    return lo + (hi - lo) * random01(s);
}

int CaveGenerator::floorDiv(int value, int divisor) {
    int q = value / divisor, r = value % divisor;
    return q - ((r != 0 && r < 0) ? 1 : 0);
}

bool CaveGenerator::canCarve(int wx, int y, int wz, bool surfaceCarver,
                             const CaveVolume& volume,
                             const std::vector<CaveColumnInfo>& columns) const {
    if (!volume.contains(wx, y, wz) || y < Config::CAVE_MIN_Y) return false;
    int lx = wx - volume.minX(), lz = wz - volume.minZ();
    const auto& col = columns[static_cast<size_t>(lz) * volume.width() + lx];
    int roof = col.submerged ? Config::CAVE_WET_ROOF :
               (surfaceCarver ? 0 : Config::CAVE_DRY_ROOF);
    return y <= col.surfaceY - roof;
}

CaveCell CaveGenerator::liquidFor(int wx, int y, int wz) const {
    if (y <= Config::CAVE_LAVA_LEVEL) return CaveCell::Lava;
    float levelNoise = m_noise.octave2D(wx * 0.006f + 317.0f,
                                        wz * 0.006f - 911.0f, 2);
    int waterTable = std::clamp(Config::CAVE_AQUIFER_LEVEL_BASE +
        static_cast<int>(std::round(levelNoise * 24.0f)), -32, 54);
    float selector = m_noise.octave3D(wx * 0.012f - 503.0f, y * 0.016f + 127.0f,
                                      wz * 0.012f + 719.0f, 2);
    return (y <= waterTable && selector > Config::CAVE_AQUIFER_THRESHOLD) ?
        CaveCell::Water : CaveCell::Air;
}

void CaveGenerator::appendTunnel(float x, float y, float z, float yaw, float pitch,
                                 float length, float radius, int branchDepth,
                                 uint64_t& state, std::vector<Segment>& out) const {
    int steps = std::max(8, static_cast<int>(length / 2.0f));
    int branchAt = static_cast<int>(steps * randomRange(state, 0.42f, 0.62f));
    for (int i = 0; i < steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float envelope = 0.35f + 0.65f * std::sin(PI * t);
        float r = radius * envelope * randomRange(state, 0.88f, 1.12f);
        float nx = x + std::cos(yaw) * std::cos(pitch) * 2.0f;
        float ny = y + std::sin(pitch) * 2.0f;
        float nz = z + std::sin(yaw) * std::cos(pitch) * 2.0f;
        out.push_back({x, y, z, nx, ny, nz, r, 0.72f});
        x = nx; y = std::clamp(ny, static_cast<float>(Config::CAVE_MIN_Y), 240.0f); z = nz;
        yaw += randomRange(state, -0.16f, 0.16f);
        pitch = pitch * 0.72f + randomRange(state, -0.09f, 0.09f);
        if (branchDepth == 0 && i == branchAt && random01(state) < 0.72f) {
            uint64_t leftState = next64(state), rightState = next64(state);
            float childLength = length * randomRange(state, 0.38f, 0.55f);
            appendTunnel(x, y, z, yaw - PI * 0.45f, pitch * 0.5f, childLength,
                         radius * 0.68f, 1, leftState, out);
            appendTunnel(x, y, z, yaw + PI * 0.45f, pitch * 0.5f, childLength,
                         radius * 0.68f, 1, rightState, out);
        }
    }
}

void CaveGenerator::generateCarverCell(int cellX, int cellZ,
                                       std::vector<Segment>& segments,
                                       std::vector<Room>& rooms) const {
    uint64_t state = hashCell(cellX, cellZ, m_seed ^ 0xC6BC279692B5CC83ULL);
    if (random01(state) >= Config::CAVE_CARVER_CHANCE) return;
    float x = cellX * Config::CAVE_CARVER_CELL_SIZE + randomRange(state, 0.0f, static_cast<float>(Config::CAVE_CARVER_CELL_SIZE));
    float z = cellZ * Config::CAVE_CARVER_CELL_SIZE + randomRange(state, 0.0f, static_cast<float>(Config::CAVE_CARVER_CELL_SIZE));
    float y = -52.0f + std::min(random01(state), random01(state)) * 180.0f;
    bool hasRoom = random01(state) < 0.25f;
    int trunks = hasRoom ? 1 + static_cast<int>(next64(state) % 4) : 1;
    float baseRadius = randomRange(state, 1.8f, 4.2f);
    if (hasRoom) rooms.push_back({x, y, z, randomRange(state, 4.0f, 8.0f),
                                  randomRange(state, 0.55f, 0.78f)});
    for (int i = 0; i < trunks; ++i) {
        float yaw = randomRange(state, -PI, PI);
        float pitch = randomRange(state, -0.18f, 0.18f);
        appendTunnel(x, y, z, yaw, pitch, randomRange(state, 52.0f, 92.0f),
                     baseRadius * randomRange(state, 0.72f, 1.05f), 0, state, segments);
    }
}

void CaveGenerator::rasterizeSegment(const Segment& s, CaveVolume& volume,
                                     const std::vector<CaveColumnInfo>& columns) const {
    float vr = s.radius * s.verticalScale;
    int minX = std::max(volume.minX(), static_cast<int>(std::floor(std::min(s.ax, s.bx) - s.radius)));
    int maxX = std::min(volume.minX() + volume.width() - 1, static_cast<int>(std::ceil(std::max(s.ax, s.bx) + s.radius)));
    int minZ = std::max(volume.minZ(), static_cast<int>(std::floor(std::min(s.az, s.bz) - s.radius)));
    int maxZ = std::min(volume.minZ() + volume.depth() - 1, static_cast<int>(std::ceil(std::max(s.az, s.bz) + s.radius)));
    int minY = std::max(Config::CAVE_MIN_Y, static_cast<int>(std::floor(std::min(s.ay, s.by) - vr)));
    int maxY = std::min(Config::WORLD_MAX_Y - 1, static_cast<int>(std::ceil(std::max(s.ay, s.by) + vr)));
    float dx=s.bx-s.ax, dy=(s.by-s.ay)/s.verticalScale, dz=s.bz-s.az;
    float len2=dx*dx+dy*dy+dz*dz;
    for (int z=minZ; z<=maxZ; ++z) for (int y=minY; y<=maxY; ++y) for (int x=minX; x<=maxX; ++x) {
        if (!canCarve(x,y,z,false,volume,columns)) continue;
        float px=x+0.5f-s.ax, py=(y+0.5f-s.ay)/s.verticalScale, pz=z+0.5f-s.az;
        float t=len2>0 ? std::clamp((px*dx+py*dy+pz*dz)/len2,0.0f,1.0f) : 0.0f;
        float qx=px-t*dx, qy=py-t*dy, qz=pz-t*dz;
        if (qx*qx+qy*qy+qz*qz < s.radius*s.radius) volume.set(x,y,z,liquidFor(x,y,z));
    }
}

void CaveGenerator::rasterizeRoom(const Room& r, CaveVolume& volume,
                                  const std::vector<CaveColumnInfo>& columns) const {
    int minX=std::max(volume.minX(),static_cast<int>(std::floor(r.x-r.radius)));
    int maxX=std::min(volume.minX()+volume.width()-1,static_cast<int>(std::ceil(r.x+r.radius)));
    int minZ=std::max(volume.minZ(),static_cast<int>(std::floor(r.z-r.radius)));
    int maxZ=std::min(volume.minZ()+volume.depth()-1,static_cast<int>(std::ceil(r.z+r.radius)));
    int minY=std::max(Config::CAVE_MIN_Y,static_cast<int>(std::floor(r.y-r.radius*r.verticalScale)));
    int maxY=std::min(Config::WORLD_MAX_Y-1,static_cast<int>(std::ceil(r.y+r.radius*r.verticalScale)));
    for(int z=minZ;z<=maxZ;++z) for(int y=minY;y<=maxY;++y) for(int x=minX;x<=maxX;++x){
        if(!canCarve(x,y,z,false,volume,columns)) continue;
        float dx=(x+0.5f-r.x)/r.radius, dy=(y+0.5f-r.y)/(r.radius*r.verticalScale), dz=(z+0.5f-r.z)/r.radius;
        if(dx*dx+dy*dy+dz*dz<1.0f) volume.set(x,y,z,liquidFor(x,y,z));
    }
}

CaveVolume CaveGenerator::generateVolume(int minX, int minZ, int width, int depth,
                                         const std::vector<CaveColumnInfo>& columns) const {
    if (width <= 0 || depth <= 0 || columns.size() != static_cast<size_t>(width * depth))
        throw std::invalid_argument("CaveGenerator column dimensions do not match volume");
    CaveVolume volume(minX,minZ,width,depth);
    for(int z=minZ;z<minZ+depth;++z) for(int x=minX;x<minX+width;++x){
        const auto& col=columns[static_cast<size_t>(z-minZ)*width+(x-minX)];
        int maxY=std::min(col.surfaceY-Config::CAVE_DRY_ROOF,Config::WORLD_MAX_Y-Config::CAVE_TOP_MARGIN);
        for(int y=Config::CAVE_MIN_Y;y<=maxY;++y){
            float depthFactor=std::clamp((col.surfaceY-y-Config::CAVE_DRY_ROOF)/96.0f,0.0f,1.0f);
            float cheese=m_noise.octave3D(x*Config::CAVE_CHEESE_SCALE_XZ+41.0f,y*Config::CAVE_CHEESE_SCALE_Y-73.0f,z*Config::CAVE_CHEESE_SCALE_XZ+109.0f,3);
            bool cavern=cheese > (Config::CAVE_CHEESE_THRESHOLD-Config::CAVE_CHEESE_DEPTH_BONUS*depthFactor);
            float s1=m_noise.noise3D(x*Config::CAVE_SPAGHETTI_SCALE_XZ+211.0f,y*Config::CAVE_SPAGHETTI_SCALE_Y+37.0f,z*Config::CAVE_SPAGHETTI_SCALE_XZ-419.0f);
            float s2=m_noise.noise3D(x*Config::CAVE_SPAGHETTI_SCALE_XZ-613.0f,y*Config::CAVE_SPAGHETTI_SCALE_Y+283.0f,z*Config::CAVE_SPAGHETTI_SCALE_XZ+157.0f);
            bool spaghetti=std::max(std::abs(s1),std::abs(s2)) < Config::CAVE_SPAGHETTI_THICKNESS;
            float n1=m_noise.noise3D(x*Config::CAVE_NOODLE_SCALE_XZ+827.0f,y*Config::CAVE_NOODLE_SCALE_Y-233.0f,z*Config::CAVE_NOODLE_SCALE_XZ+461.0f);
            float n2=m_noise.noise3D(x*Config::CAVE_NOODLE_SCALE_XZ-179.0f,y*Config::CAVE_NOODLE_SCALE_Y+647.0f,z*Config::CAVE_NOODLE_SCALE_XZ-881.0f);
            bool noodle=std::max(std::abs(n1),std::abs(n2)) < Config::CAVE_NOODLE_THICKNESS;
            if(cavern||spaghetti||noodle) volume.set(x,y,z,liquidFor(x,y,z));
        }
    }
    std::vector<Segment> segments; std::vector<Room> rooms;
    int cx0=floorDiv(minX-Config::CAVE_CARVER_MAX_REACH,Config::CAVE_CARVER_CELL_SIZE), cx1=floorDiv(minX+width-1+Config::CAVE_CARVER_MAX_REACH,Config::CAVE_CARVER_CELL_SIZE);
    int cz0=floorDiv(minZ-Config::CAVE_CARVER_MAX_REACH,Config::CAVE_CARVER_CELL_SIZE), cz1=floorDiv(minZ+depth-1+Config::CAVE_CARVER_MAX_REACH,Config::CAVE_CARVER_CELL_SIZE);
    for(int cz=cz0;cz<=cz1;++cz) for(int cx=cx0;cx<=cx1;++cx) generateCarverCell(cx,cz,segments,rooms);
    for(const auto& room:rooms) rasterizeRoom(room,volume,columns);
    for(const auto& segment:segments) rasterizeSegment(segment,volume,columns);
    return volume;
}
