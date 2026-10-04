#include "game/FishingSystem.h"
#include "Config.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
glm::ivec3 cell(const glm::dvec3& p) { return glm::ivec3(glm::floor(p)); }
constexpr float step = 1.0f / 120.0f;
float unit(float value) { return std::clamp(value, 0.0f, 0.999999f); }
}
FishingSystem::FishingSystem(Random random) : m_random(std::move(random)) {}
void FishingSystem::reset(uint64_t seed) {
    cancel(); m_events.clear(); m_cooldown = 0; m_randomState = seed ? seed : 1;
}
void FishingSystem::cancel() { m_view = {}; m_remainder = 0; m_remaining = 0; }
float FishingSystem::random() {
    if (m_random) return unit(m_random());
    m_randomState ^= m_randomState << 13;
    m_randomState ^= m_randomState >> 7;
    m_randomState ^= m_randomState << 17;
    return static_cast<float>(m_randomState >> 40) / 16777216.0f;
}
void FishingSystem::wait() {
    m_view.phase = FishingPhase::Waiting;
    m_view.biteSeconds = 0;
    m_remaining = 5.0f + random() * 25.0f;
}
bool FishingSystem::openWater(glm::ivec3 surface, const FishingEnvironment& env) {
    for (int z = -2; z <= 2; ++z) for (int x = -2; x <= 2; ++x)
        for (int y = -1; y <= 2; ++y) {
            const auto b = env.block(surface + glm::ivec3(x,y,z));
            if (!b || *b != (y <= 0 ? BlockId::WATER : BlockId::AIR)) return false;
        }
    return true;
}
ItemStack FishingSystem::loot(bool open, float category, float selection) {
    const float c = unit(category), s = unit(selection);
    if (c < (open ? .85f : .90f))
        return {s < .60f ? ItemId::RAW_COD : ItemId::RAW_SALMON, 1, 0};
    if (!open || c < .95f) {
        constexpr ItemId junk[] = {ItemId::STICK, ItemId::STRING, ItemId::BONE, ItemId::ROTTEN_FLESH};
        return {junk[static_cast<size_t>(s * 4)], 1, 0};
    }
    constexpr ItemId treasure[] = {ItemId::BOW, ItemId::FISHING_ROD, ItemId::EMERALD};
    return {treasure[static_cast<size_t>(s * 3)], 1, 0};
}
void FishingSystem::use(const glm::dvec3& eye, const glm::vec3& forward,
                        const glm::vec3& inherited, const FishingEnvironment& env) {
    if (m_cooldown > 0) return;
    m_cooldown = .25f;
    if (m_view.active()) {
        FishingEvent event{FishingEventKind::Reel, m_view.position, {}, 0};
        if (m_view.phase == FishingPhase::Bite) {
            const float category = random();
            const float selection = random();
            event.catchItem = loot(openWater(m_surface,env), category, selection);
            event.wear = 1;
        } else if (m_view.phase == FishingPhase::Stuck) event.wear = 2;
        m_events.push_back(event);
        cancel();
        return;
    }
    if (glm::dot(forward, forward) < .00001f) return;
    const auto direction = glm::normalize(forward);
    m_view.phase = FishingPhase::Flying;
    // Start at the eye so the sweep cannot skip a nearby wall.
    m_view.position = eye;
    m_velocity = direction * Config::FISHING_CAST_SPEED + inherited;
    m_events.push_back({FishingEventKind::Cast, eye, {}, 0});
}
void FishingSystem::update(float dt, const glm::dvec3& owner, const FishingEnvironment& env) {
    if (dt < 0 || !std::isfinite(dt)) return;
    m_cooldown = std::max(0.0f, m_cooldown - dt);
    if (!m_view.active()) return;
    if (glm::length(m_view.position - owner) > Config::FISHING_MAX_DISTANCE ||
        m_view.position.y < Config::WORLD_MIN_Y || m_view.position.y >= Config::WORLD_MAX_Y ||
        !env.block(cell(m_view.position))) { cancel(); return; }
    if (m_view.phase != FishingPhase::Flying && m_view.phase != FishingPhase::Stuck) {
        const auto water=env.block(m_surface);
        const auto above=env.block(m_surface+glm::ivec3(0,1,0));
        if (!water || !above || !isWater(*water) || isWater(*above) ||
            blockCollisionBoxes(*above).count>0) { cancel();return; }
    }
    m_remainder += dt;
    while (m_remainder + 1e-9 >= step && m_view.active()) {
        m_remainder -= step;
        tick(step, env);
        if (m_view.active() && glm::length(m_view.position-owner)>Config::FISHING_MAX_DISTANCE)
            cancel();
    }
}
void FishingSystem::tick(float dt, const FishingEnvironment& env) {
    m_view.animationSeconds += dt;
    if (m_view.phase == FishingPhase::Flying) {
        const glm::dvec3 target = m_view.position + glm::dvec3(m_velocity) *
            static_cast<double>(dt) + glm::dvec3(0, -.5 * Config::FISHING_GRAVITY * dt * dt, 0);
        const glm::dvec3 delta = target - m_view.position;
        const int samples = std::max(1, static_cast<int>(std::ceil(glm::length(delta) / .04)));
        const glm::dvec3 origin = m_view.position;
        for (int i = 1; i <= samples; ++i) {
            const glm::dvec3 p = origin + delta * (static_cast<double>(i) / samples);
            const auto at = cell(p);
            const auto block = env.block(at);
            if (!block || p.y < Config::WORLD_MIN_Y || p.y >= Config::WORLD_MAX_Y) {
                cancel(); return;
            }
            if (isLava(*block)) { cancel(); return; }
            if (isWater(*block) && p.y <= at.y + fluidSurfaceHeight(*block)) {
                m_surface = at;
                m_view.position = p;
                m_view.position.y = at.y + fluidSurfaceHeight(*block);
                m_velocity = glm::vec3(0);
                wait();
                m_events.push_back({FishingEventKind::Splash,m_view.position,{},0});
                return;
            }
            const auto boxes = blockCollisionBoxes(*block);
            const auto local = glm::vec3(p - glm::dvec3(at));
            for (uint8_t b = 0; b < boxes.count; ++b) {
                const auto& box = boxes.boxes[b];
                if (glm::all(glm::greaterThanEqual(local,box.min)) &&
                    glm::all(glm::lessThanEqual(local,box.max))) {
                    m_view.position = origin;
                    m_view.phase = FishingPhase::Stuck;
                    return;
                }
            }
            m_view.position = p;
        }
        m_velocity.y -= Config::FISHING_GRAVITY * dt;
        return;
    }
    if (m_view.phase == FishingPhase::Stuck) return;
    const auto water = env.block(m_surface);
    const auto above = env.block(m_surface + glm::ivec3(0,1,0));
    if (!water || !above || !isWater(*water) || isWater(*above) ||
        blockCollisionBoxes(*above).count > 0) { cancel(); return; }
    m_view.position.y = m_surface.y + fluidSurfaceHeight(*water) +
        std::sin(m_view.animationSeconds * 4.0f) * .025;
    const auto exposure = m_surface + glm::ivec3(0,1,0);
    float speed = 1.0f;
    if (!env.skyAccess(exposure)) speed = .5f;
    else if (env.rain(exposure)) speed = 1.25f;
    m_remaining -= dt * (m_view.phase == FishingPhase::Waiting ? speed : 1.0f);
    if (m_view.phase == FishingPhase::Approaching) {
        m_wakeSeconds -= dt;
        if (m_wakeSeconds <= 0) {
            const float radius = 2.0f * std::max(0.0f,m_remaining) / m_approachDuration;
            const glm::dvec3 wake=m_view.position+glm::dvec3(
                std::cos(m_approachAngle)*radius,.02,std::sin(m_approachAngle)*radius);
            const auto at=cell(wake);
            const auto waterAt=env.block(at);
            if (waterAt && isWater(*waterAt))
                m_events.push_back({FishingEventKind::Approach,wake,{},0});
            m_wakeSeconds=.15f;
        }
    }
    if (m_view.phase == FishingPhase::Bite) {
        m_view.position.y -= .16;
        m_view.biteSeconds = std::max(0.0f,m_remaining);
    }
    if (m_remaining > 0) return;
    if (m_view.phase == FishingPhase::Waiting) {
        m_view.phase = FishingPhase::Approaching;
        m_remaining = m_approachDuration = 1.0f + random();
        m_approachAngle=random()*6.2831853f;
        m_wakeSeconds=0;
    } else if (m_view.phase == FishingPhase::Approaching) {
        m_view.phase = FishingPhase::Bite;
        m_remaining = Config::FISHING_BITE_SECONDS;
        m_view.biteSeconds = m_remaining;
        m_events.push_back({FishingEventKind::Bite,m_view.position,{},0});
    } else wait();
}
std::vector<FishingEvent> FishingSystem::takeEvents() {
    auto result = std::move(m_events); m_events.clear(); return result;
}
