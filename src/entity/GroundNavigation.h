#pragma once

#include "Config.h"
#include "world/Block.h"
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace GroundNavigation {
// Queries never create chunks. Unknown terrain is closed to autonomous travel.
struct Terrain {
    std::function<BlockId(int,int,int)> block;
    std::function<bool(int,int)> loaded;
};
struct Goal {
    glm::dvec3 position{0};
    double radius = 0.35;
    double verticalTolerance = 0.6;
    std::vector<glm::dvec3> alternatives{};
};
enum class Status { Pending, Succeeded, Unreachable };

bool clear(const Terrain& terrain, const glm::vec3& size,
           const glm::dvec3& position, bool avoidHazards = true);
bool supported(const Terrain& terrain, const glm::vec3& size,
               const glm::dvec3& position);
std::optional<glm::dvec3> stand(const Terrain& terrain, const glm::vec3& size,
                               double x, double z, double referenceY,
                               double up = Config::AI_MAX_JUMP_HEIGHT,
                               double down = Config::AI_MAX_DROP);
bool traverse(const Terrain& terrain, const glm::vec3& size,
              const glm::dvec3& from, const glm::dvec3& to);
bool reached(const glm::dvec3& position, const Goal& goal);

class Search {
public:
    Search(const Terrain& terrain, const glm::vec3& size,
           const glm::dvec3& start, const Goal& goal);
    // Returns expanded nodes; every call shares its caller's frame budget.
    size_t advance(const Terrain& terrain, size_t budget);
    Status status() const { return m_status; }
    const std::vector<glm::dvec3>& path() const { return m_path; }
private:
    struct Key {
        int x, y, z;
        bool operator==(const Key& other) const {
            return x==other.x && y==other.y && z==other.z;
        }
    };
    struct Hash { size_t operator()(const Key& key) const; };
    struct Node {
        glm::dvec3 position;
        double cost;
        size_t parent;
        bool closed = false;
    };
    struct Entry {
        double estimate, cost;
        size_t index;
        bool operator<(const Entry& other) const {
            if (estimate != other.estimate) return estimate > other.estimate;
            return index > other.index;
        }
    };
    static Key key(const glm::dvec3& position);
    double heuristic(const glm::dvec3& position) const;
    glm::vec3 m_size;
    glm::dvec3 m_start;
    Goal m_goal;
    Status m_status = Status::Pending;
    std::vector<Node> m_nodes;
    std::unordered_map<Key,size_t,Hash> m_lookup;
    std::priority_queue<Entry> m_open;
    std::vector<glm::dvec3> m_path;
    size_t m_expanded = 0;
};
}
