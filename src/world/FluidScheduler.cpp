#include "world/FluidScheduler.h"

#include "Config.h"
#include "world/ChunkStore.h"
#include "world/FluidLogic.h"
#include "world/World.h"

#include <algorithm>

void FluidScheduler::scheduleAround(const glm::ivec3& position,
                                    uint64_t minimumDelay) {
    auto schedule = [&](const glm::ivec3& p) {
        if (!Config::isValidWorldY(p.y) || !m_world.generatedAt(p.x, p.z)) return;
        const BlockId block = m_world.getBlock(p.x, p.y, p.z);
        if (!isFluid(block)) return;
        const uint64_t delay = std::max<uint64_t>(
            minimumDelay, fluidTickDelay(isLava(block)));
        const uint64_t due = m_currentWorldTick + delay;
        const auto existing = m_scheduledFluidDue.find(p);
        if (existing != m_scheduledFluidDue.end() && existing->second <= due) return;
        m_scheduledFluidDue[p] = due;
        m_fluidTicks.push({due, p});
    };
    schedule(position);
    for (const glm::ivec3& offset : FACE_OFFSETS) schedule(position + offset);
}

void FluidScheduler::clear() {
    m_fluidTicks = {};
    m_scheduledFluidDue.clear();
    m_currentWorldTick = 0;
}

void FluidScheduler::tick(uint64_t tick) {
    m_currentWorldTick = tick;
    constexpr size_t MAX_UPDATES = 512;
    size_t processed = 0;
    while (!m_fluidTicks.empty() && m_fluidTicks.top().due <= tick &&
           processed < MAX_UPDATES) {
        const ScheduledFluidTick scheduled = m_fluidTicks.top();
        m_fluidTicks.pop();
        const auto current = m_scheduledFluidDue.find(scheduled.position);
        if (current == m_scheduledFluidDue.end() || current->second != scheduled.due)
            continue;
        m_scheduledFluidDue.erase(current);
        if (!m_world.generatedAt(scheduled.position.x, scheduled.position.z)) {
            scheduleAround(scheduled.position, 20);
            continue;
        }
        updateCell(scheduled.position, tick);
        ++processed;
    }
}

void FluidScheduler::updateCell(const glm::ivec3& p, uint64_t tick) {
    BlockId current = m_world.getBlock(p.x, p.y, p.z);
    if (!isFluid(current)) return;
    const bool lava = isLava(current);
    auto same = [&](BlockId block) { return lava ? isLava(block) : isWater(block); };
    auto opposite = [&](BlockId block) { return lava ? isWater(block) : isLava(block); };

    // Contact solidification happens before spreading so update order cannot
    // allow one fluid to overwrite the other.
    if (lava) {
        for (const glm::ivec3& offset : FACE_OFFSETS) {
            if (!isWater(m_world.getBlock(p.x + offset.x, p.y + offset.y, p.z + offset.z)))
                continue;
            const BlockId product = offset.y > 0 ? BlockId::STONE :
                (fluidLevel(current) == 0 ? BlockId::OBSIDIAN : BlockId::COBBLESTONE);
            m_world.setBlock(p.x, p.y, p.z, product);
            return;
        }
    }

    if (fluidLevel(current) != 0) {
        uint8_t desired = 8;
        const BlockId above = m_world.getBlock(p.x, p.y + 1, p.z);
        if (same(above)) {
            desired = 1;
        } else {
            int sourceNeighbors = 0;
            for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS) {
                const BlockId neighbor = m_world.getBlock(
                    p.x + offset.x, p.y, p.z + offset.z);
                if (!same(neighbor)) continue;
                if (fluidLevel(neighbor) == 0) ++sourceNeighbors;
                desired = std::min<uint8_t>(desired,
                    nextFluidLevel(lava,fluidLevel(neighbor)));
            }
            const BlockId below = m_world.getBlock(p.x,p.y-1,p.z);
            if (!lava && sourceNeighbors >= 2 &&
                (isSolid(below) || (isWater(below) && fluidLevel(below)==0))) desired=0;
        }
        if (desired > 7) {
            m_world.setDerivedBlock(p,BlockId::AIR);
            return;
        }
        const BlockId recomputed = fluidBlock(lava, desired);
        if (recomputed != current) {
            // A two-neighbor water source is permanent Minecraft state; unlike
            // ordinary flow depth it must survive removal of its parent sources.
            if(desired==0)m_world.setBlock(p.x,p.y,p.z,recomputed);
            else m_world.setDerivedBlock(p,recomputed);
            current = recomputed;
        }
    }

    for (const glm::ivec3& offset : FACE_OFFSETS) {
        const glm::ivec3 q = p + offset;
        if (m_world.getBlock(q.x, q.y, q.z) != BlockId::TNT) continue;
        if (lava) {
            m_world.setBlock(q.x, q.y, q.z, BlockId::AIR);
            m_world.pushTntIgnition(q);
        }
    }

    const glm::ivec3 below = p + glm::ivec3(0, -1, 0);
    if (Config::isValidWorldY(below.y) && m_world.generatedAt(below.x, below.z)) {
        const BlockId target = m_world.getBlock(below.x, below.y, below.z);
        if (opposite(target)) {
            m_world.setBlock(below.x, below.y, below.z, BlockId::STONE);
            return;
        }
        if (same(target)) {
            if (fluidLevel(target)>1)m_world.setDerivedBlock(below,fluidBlock(lava,1));
            return;
        }
        if (isReplaceableByFluid(target)) {
            m_world.setDerivedBlock(below,fluidBlock(lava,1));
            return;
        }
    }

    const bool falling=same(m_world.getBlock(p.x,p.y+1,p.z));
    const uint8_t spreadLevel=falling?0:fluidLevel(current);
    const uint8_t nextLevel = nextFluidLevel(lava,spreadLevel);
    if (nextLevel > 7) return;
    const FluidSample sample=[this](const glm::ivec3& position){
        return m_world.getBlock(position.x,position.y,position.z);};
    const FluidAvailable available=[this](const glm::ivec3& position){
        return Config::isValidWorldY(position.y)&&m_world.generatedAt(position.x,position.z);};
    for (const glm::ivec3& offset : preferredFluidDirections(
             p,lava,spreadLevel,sample,available)) {
        const glm::ivec3 q = p + offset;
        const BlockId target = m_world.getBlock(q.x, q.y, q.z);
        if (opposite(target)) {
            if (lava) {
                m_world.setBlock(p.x,p.y,p.z,fluidLevel(current)==0
                    ? BlockId::OBSIDIAN : BlockId::COBBLESTONE);
            } else {
                m_world.setBlock(q.x,q.y,q.z,fluidLevel(target)==0
                    ? BlockId::OBSIDIAN : BlockId::COBBLESTONE);
            }
            continue;
        }
        if (fluidCanOccupy(target,lava,nextLevel))
            m_world.setDerivedBlock(q,fluidBlock(lava,nextLevel));
    }
    (void)tick;
}
