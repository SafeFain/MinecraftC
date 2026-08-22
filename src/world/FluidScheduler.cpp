#include "world/FluidScheduler.h"

#include "Config.h"
#include "world/Chunk.h"
#include "world/ChunkStore.h"
#include "world/FluidLogic.h"
#include "world/World.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace {
constexpr glm::ivec3 DOWN{0, -1, 0};

bool isSame(BlockId block, bool lava) {
    return lava ? isLava(block) : isWater(block);
}

bool isOpposite(BlockId block, bool lava) {
    return lava ? isWater(block) : isLava(block);
}

bool isSource(BlockId block) {
    const auto state = decodeFluidState(block);
    return state.has_value() && state->source;
}

// Slope routing revisits the same few cells while exploring four directions.
// Keep a small per-cell cache around the origin so each coordinate crosses the
// World/Chunk locking boundary at most once during one update.
class FluidNeighborhoodCache {
public:
    FluidNeighborhoodCache(World& world, const glm::ivec3& origin)
        : m_world(world), m_origin(origin) {}

    BlockId block(const glm::ivec3& position) {
        const int index = indexFor(position);
        if (index < 0) return m_world.getBlock(
            position.x, position.y, position.z);
        if (m_cached[static_cast<size_t>(index)] == 0) {
            m_blocks[static_cast<size_t>(index)] = m_world.getBlock(
                position.x, position.y, position.z);
            m_cached[static_cast<size_t>(index)] = 1;
        }
        return m_blocks[static_cast<size_t>(index)];
    }

    bool available(const glm::ivec3& position) {
        if (!Config::isValidWorldY(position.y)) return false;
        const int index = indexFor(position);
        if (index < 0) return m_world.isGeneratedAt(position.x, position.z);
        if (m_availableCached[static_cast<size_t>(index)] == 0) {
            m_available[static_cast<size_t>(index)] =
                m_world.isGeneratedAt(position.x, position.z) ? 1 : 2;
            m_availableCached[static_cast<size_t>(index)] = 1;
        }
        return m_available[static_cast<size_t>(index)] == 1;
    }

private:
    static constexpr int RADIUS = 5;
    static constexpr int WIDTH = RADIUS * 2 + 1;
    static constexpr int HEIGHT = 3;

    int indexFor(const glm::ivec3& position) const {
        const int x = position.x - m_origin.x + RADIUS;
        const int y = position.y - m_origin.y + 1;
        const int z = position.z - m_origin.z + RADIUS;
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT ||
            z < 0 || z >= WIDTH) return -1;
        return x + z * WIDTH + y * WIDTH * WIDTH;
    }

    World& m_world;
    glm::ivec3 m_origin;
    std::array<BlockId, WIDTH * WIDTH * HEIGHT> m_blocks{};
    std::array<uint8_t, WIDTH * WIDTH * HEIGHT> m_cached{};
    std::array<uint8_t, WIDTH * WIDTH * HEIGHT> m_available{};
    std::array<uint8_t, WIDTH * WIDTH * HEIGHT> m_availableCached{};
};

}

uint64_t FluidScheduler::hash(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool FluidScheduler::earlier(const ScheduledFluidTick& a,
                             const ScheduledFluidTick& b) {
    if (a.due != b.due) return a.due < b.due;
    return a.order < b.order;
}

void FluidScheduler::swapHeapNodes(size_t first, size_t second) {
    std::swap(m_fluidTicks[first], m_fluidTicks[second]);
    m_scheduledFluidIndices[FluidTickKey{
        m_fluidTicks[first].position, m_fluidTicks[first].lava}] = first;
    m_scheduledFluidIndices[FluidTickKey{
        m_fluidTicks[second].position, m_fluidTicks[second].lava}] = second;
}

void FluidScheduler::siftUp(size_t index) {
    while (index > 0) {
        const size_t parent = (index - 1) / 2;
        if (!earlier(m_fluidTicks[index], m_fluidTicks[parent])) break;
        swapHeapNodes(index, parent);
        index = parent;
    }
}

void FluidScheduler::siftDown(size_t index) {
    for (;;) {
        const size_t left = index * 2 + 1;
        if (left >= m_fluidTicks.size()) return;
        size_t best = left;
        const size_t right = left + 1;
        if (right < m_fluidTicks.size() &&
            earlier(m_fluidTicks[right], m_fluidTicks[left])) best = right;
        if (!earlier(m_fluidTicks[best], m_fluidTicks[index])) return;
        swapHeapNodes(index, best);
        index = best;
    }
}

void FluidScheduler::removeHeapAt(size_t index) {
    const FluidTickKey removed{
        m_fluidTicks[index].position, m_fluidTicks[index].lava};
    const size_t last = m_fluidTicks.size() - 1;
    if (index != last) swapHeapNodes(index, last);
    m_fluidTicks.pop_back();
    m_scheduledFluidIndices.erase(removed);
    if (index < m_fluidTicks.size()) {
        if (index > 0 && earlier(m_fluidTicks[index],
                                 m_fluidTicks[(index - 1) / 2]))
            siftUp(index);
        else
            siftDown(index);
    }
}

FluidScheduler::ScheduledFluidTick FluidScheduler::removeHeapRoot() {
    ScheduledFluidTick result = m_fluidTicks.front();
    removeHeapAt(0);
    return result;
}

void FluidScheduler::cancelAt(const glm::ivec3& position, bool lava) {
    const auto it = m_scheduledFluidIndices.find({position, lava});
    if (it != m_scheduledFluidIndices.end()) removeHeapAt(it->second);
}

void FluidScheduler::scheduleAt(const glm::ivec3& position,
                                 uint64_t minimumDelay,
                                 bool forceReschedule) {
    if (!Config::isValidWorldY(position.y) ||
        !m_world.generatedAt(position.x, position.z)) {
        cancelAt(position, false);
        cancelAt(position, true);
        return;
    }
    const BlockId block = m_world.getBlock(position.x, position.y, position.z);
    const auto state = decodeFluidState(block);
    if (!state.has_value()) {
        cancelAt(position, false);
        cancelAt(position, true);
        return;
    }

    const FluidTickKey key{position, state->lava};
    // A block can switch fluid type before its old tick reaches the root.
    // Remove that opposite-type key now instead of leaving a stale heap node.
    cancelAt(position, !state->lava);
    const uint64_t delay = std::max<uint64_t>(
        minimumDelay, fluidTickDelay(state->lava));
    const uint64_t due = m_currentWorldTick + delay;
    const auto existing = m_scheduledFluidIndices.find(key);
    if (existing != m_scheduledFluidIndices.end()) {
        ScheduledFluidTick& entry = m_fluidTicks[existing->second];
        if (!forceReschedule && entry.due <= due) return;
        entry.due = due;
        entry.order = m_nextInsertionOrder++;
        if (existing->second > 0 && earlier(
                entry, m_fluidTicks[(existing->second - 1) / 2]))
            siftUp(existing->second);
        else
            siftDown(existing->second);
        return;
    }
    const uint64_t order = m_nextInsertionOrder++;
    m_fluidTicks.push_back({due, order, position, state->lava});
    const size_t index = m_fluidTicks.size() - 1;
    m_scheduledFluidIndices[key] = index;
    siftUp(index);
}

void FluidScheduler::rescheduleAt(const glm::ivec3& position, uint64_t delay) {
    scheduleAt(position, delay, true);
}

void FluidScheduler::scheduleAround(const glm::ivec3& position,
                                     uint64_t minimumDelay) {
    scheduleAt(position, minimumDelay);
    for (const glm::ivec3& offset : FACE_OFFSETS)
        scheduleAt(position + offset, minimumDelay);
}

void FluidScheduler::clear() {
    m_fluidTicks.clear();
    m_scheduledFluidIndices.clear();
    m_currentWorldTick = 0;
    m_nextInsertionOrder = 0;
}

void FluidScheduler::tryMixingAt(const glm::ivec3& lavaPosition) {
    if (!Config::isValidWorldY(lavaPosition.y) ||
        !m_world.generatedAt(lavaPosition.x, lavaPosition.z)) return;
    const BlockId lavaBlock = m_world.getBlock(
        lavaPosition.x, lavaPosition.y, lavaPosition.z);
    const auto lavaState = decodeFluidState(lavaBlock);
    if (!lavaState.has_value() || !lavaState->lava) return;

    // Java checks the block above and the four horizontal faces. Water below
    // a lava cell does not immediately harden it.
    const std::array<glm::ivec3, 5> contactOffsets{{
        {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}
    }};
    for (const glm::ivec3& offset : contactOffsets) {
        const glm::ivec3 water = lavaPosition + offset;
        if (!Config::isValidWorldY(water.y) ||
            !m_world.generatedAt(water.x, water.z) ||
            !isWater(m_world.getBlock(water.x, water.y, water.z))) continue;
        m_world.setBlock(lavaPosition.x, lavaPosition.y, lavaPosition.z,
                         lavaState->source ? BlockId::OBSIDIAN
                                            : BlockId::COBBLESTONE);
        return;
    }
}

void FluidScheduler::onBlockChanged(const glm::ivec3& position,
                                    BlockId previous, BlockId current) {
    if (!isFluid(previous) && !isFluid(current)) return;
    // The changed block can itself be lava, or can be water above/alongside
    // one of these four candidate lava locations.
    tryMixingAt(position);
    tryMixingAt(position + glm::ivec3(0, -1, 0));
    for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS)
        tryMixingAt(position + offset);
}

void FluidScheduler::spreadTo(const glm::ivec3& from, const glm::ivec3& to,
                              bool lava, uint8_t amount, bool falling,
                              uint64_t tick) {
    if (!Config::isValidWorldY(to.y) || !m_world.generatedAt(to.x, to.z)) return;
    const BlockId target = m_world.getBlock(to.x, to.y, to.z);
    const auto targetState = decodeFluidState(target);
    const bool downward = to == from + DOWN;

    if (isOpposite(target, lava)) {
        if (lava && downward) {
            // Lava flowing down into water creates stone. The water cell is
            // consumed, while water below lava remains untouched.
            m_world.setBlock(to.x, to.y, to.z, BlockId::STONE);
            return;
        }
        if (!lava && !downward && targetState.has_value() &&
            targetState->amount >= 4) {
            // Water can replace a sufficiently deep horizontal lava flow;
            // the contact update below chooses obsidian/cobblestone.
            m_world.setBlock(to.x, to.y, to.z,
                             targetState->source ? BlockId::OBSIDIAN
                                                 : BlockId::COBBLESTONE);
        }
        return;
    }
    if (!fluidCanReceiveAmount(target, lava, amount, falling)) return;

    const BlockId replacement = fluidBlockFromAmount(lava, amount,
                                                       downward && falling);
    const auto oldState = decodeFluidState(target);
    if (replacement == target) return;
    if (decodeFluidState(replacement)->source)
        m_world.setBlock(to.x, to.y, to.z, replacement);
    else
        m_world.setDerivedBlock(to, replacement);

    const uint64_t random = hash(Config::WORLD_SEED ^ tick * 0x9e3779b97f4a7c15ULL ^
        static_cast<uint64_t>(static_cast<uint32_t>(to.x)) ^
        (static_cast<uint64_t>(static_cast<uint32_t>(to.z)) << 32) ^
        static_cast<uint64_t>(static_cast<uint32_t>(to.y)));
    rescheduleAt(to, fluidSpreadDelay(lava,
                                      oldState.has_value() ? target : replacement,
                                      replacement, random));
}

void FluidScheduler::updateCell(const glm::ivec3& position, uint64_t tick) {
    BlockId current = m_world.getBlock(position.x, position.y, position.z);
    auto state = decodeFluidState(current);
    if (!state.has_value()) return;
    const bool lava = state->lava;

    if (lava) {
        tryMixingAt(position);
        current = m_world.getBlock(position.x, position.y, position.z);
        state = decodeFluidState(current);
        if (!state.has_value()) return;
    }

    int sourceNeighbors = 0;
    if (!state->source) {
        uint8_t desiredAmount = 0;
        bool desiredFalling = false;
        uint8_t maximum = 0;
        for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS) {
            const glm::ivec3 neighbor = position + offset;
            if (!m_world.generatedAt(neighbor.x, neighbor.z)) continue;
            const auto neighborState = decodeFluidState(
                m_world.getBlock(neighbor.x, neighbor.y, neighbor.z));
            if (!neighborState.has_value() || neighborState->lava != lava) continue;
            maximum = std::max(maximum, neighborState->amount);
            if (neighborState->source) ++sourceNeighbors;
        }
        const glm::ivec3 below = position + DOWN;
        const BlockId belowBlock = m_world.getBlock(below.x, below.y, below.z);
        const bool supported = isSolid(belowBlock) ||
            (isWater(belowBlock) && isSource(belowBlock));
        if (!lava && sourceNeighbors >= 2 && supported) {
            desiredAmount = 8;
            desiredFalling = false;
        }
        const glm::ivec3 above = position + glm::ivec3(0, 1, 0);
        if (desiredAmount == 0 && Config::isValidWorldY(above.y) &&
            m_world.generatedAt(above.x, above.z) &&
            isSame(m_world.getBlock(above.x, above.y, above.z), lava)) {
            desiredAmount = 8;
            desiredFalling = true;
        } else if (desiredAmount == 0 && maximum > fluidHorizontalDecay(lava)) {
            desiredAmount = static_cast<uint8_t>(
                maximum - fluidHorizontalDecay(lava));
        }

        if (desiredAmount == 0) {
            m_world.setDerivedBlock(position, BlockId::AIR);
            return;
        }
        const BlockId replacement = fluidBlockFromAmount(
            lava, desiredAmount, desiredFalling);
        if (replacement != current) {
            const BlockId oldCurrent = current;
            if (decodeFluidState(replacement)->source)
                m_world.setBlock(position.x, position.y, position.z, replacement);
            else
                m_world.setDerivedBlock(position, replacement);
            const uint64_t random = hash(
                Config::WORLD_SEED ^ tick * 0x9e3779b97f4a7c15ULL ^
                static_cast<uint64_t>(static_cast<uint32_t>(position.x)) ^
                (static_cast<uint64_t>(static_cast<uint32_t>(position.z)) << 32) ^
                static_cast<uint64_t>(static_cast<uint32_t>(position.y)));
            rescheduleAt(position, fluidSpreadDelay(
                lava, oldCurrent, replacement, random));
            current = replacement;
            state = decodeFluidState(current);
            if (!state.has_value()) return;
        }
    } else {
        for (const glm::ivec3& offset : FLUID_HORIZONTAL_OFFSETS) {
            const glm::ivec3 neighbor = position + offset;
            if (!m_world.generatedAt(neighbor.x, neighbor.z)) continue;
            const auto neighborState = decodeFluidState(
                m_world.getBlock(neighbor.x, neighbor.y, neighbor.z));
            if (neighborState.has_value() && neighborState->lava == lava &&
                neighborState->source) ++sourceNeighbors;
        }
    }

    const glm::ivec3 below = position + DOWN;
    bool fell = false;
    bool waterHoleBelow = false;
    if (Config::isValidWorldY(below.y) && m_world.generatedAt(below.x, below.z)) {
        const BlockId target = m_world.getBlock(below.x, below.y, below.z);
        const auto targetState = decodeFluidState(target);
        waterHoleBelow = isFluidWaterHole(target, lava);
        const bool canDown = !isOpposite(target, lava) &&
            fluidCanReceiveAmount(target, lava, 8, true);
        if (canDown) {
            spreadTo(position, below, lava, 8, true, tick);
            fell = true;
        } else if (isOpposite(target, lava) && lava) {
            spreadTo(position, below, lava, 8, true, tick);
            fell = true;
        } else if (targetState.has_value() && targetState->lava == lava &&
                   !targetState->source && targetState->amount < 8) {
            spreadTo(position, below, lava, 8, true, tick);
            fell = true;
        }
    }

    // Java only adds horizontal spread after a successful drop when three or
    // more horizontal source neighbors exist. Otherwise the drop takes the
    // whole update. If no drop was possible, all nearest slope directions are
    // considered below.
    if (!fell && !state->source && waterHoleBelow) return;
    if (fell && sourceNeighbors < 3) return;
    if (fell && sourceNeighbors >= 3) {
        FluidNeighborhoodCache neighborhood(m_world, position);
        for (const glm::ivec3& offset : preferredFluidDirectionsByAmount(
                 position, lava, state->amount, state->falling,
                 [&neighborhood](const glm::ivec3& p) {
                     return neighborhood.block(p);
                 },
                 [&neighborhood](const glm::ivec3& p) {
                     return neighborhood.available(p);
                 })) {
            spreadTo(position, position + offset, lava,
                     state->falling ? 7 :
                         (state->amount > fluidHorizontalDecay(lava)
                              ? static_cast<uint8_t>(state->amount -
                                  fluidHorizontalDecay(lava)) : 0),
                     false, tick);
        }
        return;
    }

    FluidNeighborhoodCache neighborhood(m_world, position);
    const auto sample = [&neighborhood](const glm::ivec3& p) {
        return neighborhood.block(p);
    };
    const auto available = [&neighborhood](const glm::ivec3& p) {
        return neighborhood.available(p);
    };
    for (const glm::ivec3& offset : preferredFluidDirectionsByAmount(
             position, lava, state->amount, state->falling, sample, available)) {
        const uint8_t amount = state->falling
            ? static_cast<uint8_t>(7)
            : (state->amount > fluidHorizontalDecay(lava)
                ? static_cast<uint8_t>(state->amount - fluidHorizontalDecay(lava)) : 0);
        if (amount == 0) continue;
        spreadTo(position, position + offset, lava, amount, false, tick);
    }
}

void FluidScheduler::randomTickLava(uint64_t tick) {
    // Three deterministic samples per 16-block section match the default
    // randomTickSpeed without depending on worker order or platform RNG.
    for (const Chunk* chunk : m_world.getActiveChunks()) {
        if (chunk == nullptr || !chunk->generated.load()) continue;
        for (int section = Config::WORLD_MIN_Y; section < Config::WORLD_MAX_Y;
             section += 16) {
            for (int sampleIndex = 0; sampleIndex < 3; ++sampleIndex) {
                uint64_t random = hash(
                    Config::WORLD_SEED ^ tick * 0x632be59bd9b4e019ULL ^
                    static_cast<uint64_t>(static_cast<uint32_t>(chunk->cx)) ^
                    (static_cast<uint64_t>(static_cast<uint32_t>(chunk->cz)) << 32) ^
                    static_cast<uint64_t>(section * 31 + sampleIndex * 17));
                const glm::ivec3 lavaPosition{
                    chunk->worldX() + static_cast<int>(random % 16),
                    section + static_cast<int>((random >> 8) % 16),
                    chunk->worldZ() + static_cast<int>((random >> 16) % 16)};
                if (!isLava(m_world.getBlock(lavaPosition.x, lavaPosition.y,
                                              lavaPosition.z))) continue;
                const int branch = static_cast<int>(random % 3);
                if (branch > 0) {
                    glm::ivec3 candidate = lavaPosition;
                    for (int step = 0; step < branch; ++step) {
                        random = hash(random + static_cast<uint64_t>(step));
                        candidate.x += static_cast<int>(random % 3) - 1;
                        candidate.z += static_cast<int>((random >> 8) % 3) - 1;
                        if (!Config::isValidWorldY(candidate.y) ||
                            !m_world.generatedAt(candidate.x, candidate.z)) break;
                        const BlockId block = m_world.getBlock(
                            candidate.x, candidate.y, candidate.z);
                        if (block == BlockId::AIR) {
                            bool fuel = false;
                            for (const glm::ivec3& offset : FACE_OFFSETS)
                                fuel = fuel || isFlammable(m_world.getBlock(
                                    candidate.x + offset.x, candidate.y + offset.y,
                                    candidate.z + offset.z));
                            if (fuel) {
                                m_world.setBlock(candidate.x, candidate.y,
                                                 candidate.z, BlockId::FIRE);
                            }
                            break;
                        }
                        if (isSolid(block)) break;
                    }
                } else {
                    for (int attempt = 0; attempt < 3; ++attempt) {
                        random = hash(random + static_cast<uint64_t>(attempt + 1));
                        const glm::ivec3 base = lavaPosition + glm::ivec3(
                            static_cast<int>(random % 3) - 1, 0,
                            static_cast<int>((random >> 8) % 3) - 1);
                        const glm::ivec3 above = base + glm::ivec3(0, 1, 0);
                        if (!Config::isValidWorldY(above.y) ||
                            !m_world.generatedAt(above.x, above.z) ||
                            m_world.getBlock(above.x, above.y, above.z) != BlockId::AIR)
                            continue;
                        if (isFlammable(m_world.getBlock(base.x, base.y, base.z))) {
                            m_world.setBlock(above.x, above.y, above.z, BlockId::FIRE);
                            break;
                        }
                    }
                }
            }
        }
    }
}

FluidTickStats FluidScheduler::tick(uint64_t tick,
                                    const FluidTickBudget& budget) {
    m_currentWorldTick = tick;
    FluidTickStats stats;
    m_world.beginFluidBatch(tick);
    while (!m_fluidTicks.empty() && m_fluidTicks.front().due <= tick &&
           stats.examined < budget.maximumUpdates) {
        if (std::chrono::steady_clock::now() >= budget.deadline) {
            stats.deadlineReached = true;
            break;
        }
        const ScheduledFluidTick scheduled = removeHeapRoot();
        ++stats.examined;
        if (!m_world.generatedAt(scheduled.position.x, scheduled.position.z)) continue;
        const auto state = decodeFluidState(m_world.getBlock(
            scheduled.position.x, scheduled.position.y, scheduled.position.z));
        if (!state.has_value() || state->lava != scheduled.lava) continue;
        updateCell(scheduled.position, tick);
        ++stats.updated;
    }
    const auto& mutations = m_world.endFluidBatch();
    stats.changed = mutations.size();
    m_affectedSet.clear();
    m_affectedPositions.clear();
    m_affectedSet.reserve(mutations.size() * 7);
    m_affectedPositions.reserve(mutations.size() * 7);
    for (const auto& mutation : mutations) {
        const std::array<glm::ivec3, 7> neighborhood{{
            mutation.position, mutation.position + FACE_OFFSETS[0],
            mutation.position + FACE_OFFSETS[1], mutation.position + FACE_OFFSETS[2],
            mutation.position + FACE_OFFSETS[3], mutation.position + FACE_OFFSETS[4],
            mutation.position + FACE_OFFSETS[5]}};
        for (const glm::ivec3& position : neighborhood)
            if (m_affectedSet.insert(position).second)
                m_affectedPositions.push_back(position);
    }
    for (const glm::ivec3& position : m_affectedPositions)
        scheduleAt(position, 1);
    randomTickLava(tick);
    stats.deferred = (!m_fluidTicks.empty() &&
                      m_fluidTicks.front().due <= tick) ? 1u : 0u;
    stats.queueSize = m_fluidTicks.size();
    return stats;
}
