#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct BlockEntity;
struct Entity;
class InventoryModel;

// Narrow capabilities retained by inventory screens while they are open.
class IContainerAccess {
public:
    virtual ~IContainerAccess() = default;
    virtual BlockEntity* blockEntityAt(const glm::ivec3& position) = 0;
};

class ITradeAccess {
public:
    virtual ~ITradeAccess() = default;
    virtual const Entity* tradeEntity(uint64_t entityId) const = 0;
    virtual bool tradeUsable(uint64_t entityId, const glm::dvec3& eye,
                             const glm::vec3& direction, float reach) const = 0;
    virtual void executeTrade(uint64_t entityId, uint8_t offerIndex,
                              InventoryModel& inventory) = 0;
};
