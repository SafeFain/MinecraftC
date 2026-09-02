#pragma once
#include "entity/GroundNavigation.h"
#include <map>
#include <utility>
#include <tuple>

enum class EntityBehavior { Idle, Wander, Chase, Flee, Work, Sleep, ReturnHome };
enum class NavigationPurpose { Move, BedClaim, WorkClaim };

struct EntityAiState {
    EntityBehavior behavior = EntityBehavior::Idle;
    uint64_t targetId = 0; // 0 is the separately owned player; hasTarget disambiguates.
    bool hasTarget = false;
    bool targetVisible = false;
    bool retaliating = false;
    glm::vec3 previousLocomotion{0};
    glm::dvec3 lastSeen{0};
    double memoryUntil = 0;
    double nextDecision = 0;
    double actionUntil = 0;
    double panicUntil = 0;
    glm::dvec3 danger{0};
    double nextPath = 0;
    double retryDelay = Config::AI_RETRY_MIN_SECONDS;
    bool initialized = false;
    bool grounded = false;
    bool hasDestination = false;
    float speed = 0;
    GroundNavigation::Goal destination;
    GroundNavigation::Goal pathGoal;
    NavigationPurpose purpose = NavigationPurpose::Move;
    GroundNavigation::Status pathStatus = GroundNavigation::Status::Unreachable;
    std::vector<glm::dvec3> path;
    size_t waypoint = 0;
    std::map<std::pair<int,int>,uint64_t> pathRevisions;
    glm::dvec3 progressOrigin{0};
    double progressSince = 0;
    uint32_t sequence = 0;
    // POI failures are temporary and belong to the current terrain revision.
    std::map<std::tuple<int,int,int>,double> failedPois;
    glm::ivec3 candidatePoi{0};
};
