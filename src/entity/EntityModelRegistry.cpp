#include "entity/EntityModelRegistry.h"
#include "model/ModelRenderer.h"
#include "debug/Log.h"

#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
std::shared_ptr<const model::ModelAsset> makePlaceholder() {
    auto asset = std::make_shared<model::ModelAsset>();
    asset->materials.push_back({glm::vec4(1, 0, 1, 1), -1,
                                model::AlphaMode::Opaque, 0.5f, false});
    model::Primitive primitive;
    primitive.material = 0;
    const glm::vec3 corners[] = {
        {-.5f,0,-.5f},{.5f,0,-.5f},{.5f,1,-.5f},{-.5f,1,-.5f},
        {-.5f,0,.5f},{.5f,0,.5f},{.5f,1,.5f},{-.5f,1,.5f}};
    const uint32_t faces[][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},
        {1,5,6,2},{3,2,6,7},{4,5,1,0}};
    const glm::vec3 normals[] = {{0,0,-1},{0,0,1},{-1,0,0},
        {1,0,0},{0,1,0},{0,-1,0}};
    for (int face = 0; face < 6; ++face) {
        const uint32_t base = static_cast<uint32_t>(primitive.vertices.size());
        const glm::vec2 uv[] = {{0,0},{1,0},{1,1},{0,1}};
        for (int corner = 0; corner < 4; ++corner) {
            model::Vertex vertex;
            vertex.position = corners[faces[face][corner]];
            vertex.normal = normals[face];
            vertex.uv = uv[corner];
            primitive.vertices.push_back(vertex);
        }
        primitive.indices.insert(primitive.indices.end(),
            {base,base+1,base+2,base,base+2,base+3});
    }
    asset->primitives.push_back(std::move(primitive));
    model::Node node;
    node.primitives.push_back(0);
    asset->nodes.push_back(std::move(node));
    asset->sceneRoots.push_back(0);
    asset->boundsMin = {-0.5f, 0.0f, -0.5f};
    asset->boundsMax = {0.5f, 1.0f, 0.5f};
    return asset;
}

const char* clipName(EntityPlayback playback) {
    switch (playback) {
        case EntityPlayback::Walk: return "walk";
        case EntityPlayback::Hurt: return "hurt";
        case EntityPlayback::Death: return "death";
        case EntityPlayback::Attack: return "attack";
        default: return "idle";
    }
}
}

EntityModelRegistry::EntityModelRegistry(Loader loader)
    : m_loader(std::move(loader)), m_definitions{{
        {EntityType::Cow,"cow.glb",{},0,false},
        {EntityType::Pig,"pig.glb",{},0,false},
        {EntityType::Sheep,"sheep.glb",{},0,false},
        {EntityType::Chicken,"chicken.glb",{},0,false},
        {EntityType::Zombie,"zombie.glb",{},0,false},
        {EntityType::Skeleton,"skeleton.glb",{},0,false},
        {EntityType::Spider,"spider.glb",{},0,false},
        {EntityType::Blastling,"blastling.glb",{},0,false}}},
      m_placeholder(makePlaceholder()) {}

void EntityModelRegistry::loadAll(const std::filesystem::path& assetRoot) {
    const std::filesystem::path root = assetRoot / "models" / "entities";
    for (EntityModelDefinition& definition : m_definitions) {
        const std::filesystem::path path = (root / definition.filename).lexically_normal();
        auto cached = m_cache.find(path);
        if (cached == m_cache.end()) {
            model::LoadResult loaded = m_loader(path);
            if (!loaded)
                LOG_WARN("Entity model fallback for " << path.u8string()
                         << ": " << loaded.error);
            const auto asset = loaded ? loaded.asset : m_placeholder;
            cached = m_cache.emplace(path, asset).first;
        }
        definition.asset = cached->second;
        definition.usesPlaceholder = definition.asset == m_placeholder;
    }
}

void EntityModelRegistry::uploadAll(model::ModelRenderer& renderer) {
    std::map<const model::ModelAsset*, model::ModelHandle> uploaded;
    for (EntityModelDefinition& definition : m_definitions) {
        if (!definition.asset) definition.asset = m_placeholder;
        auto found = uploaded.find(definition.asset.get());
        if (found == uploaded.end())
            found = uploaded.emplace(definition.asset.get(),
                                     renderer.upload(definition.asset)).first;
        definition.handle = found->second;
    }
}

const EntityModelDefinition& EntityModelRegistry::definition(EntityType type) const {
    for (const auto& definition : m_definitions)
        if (definition.type == type) return definition;
    throw std::out_of_range("entity type has no model definition");
}

std::string EntityModelRegistry::clipFor(EntityType type,
                                         EntityPlayback playback) const {
    const auto& asset = *definition(type).asset;
    const char* requested = clipName(playback);
    if (asset.findClip(requested)) return requested;
    if (playback == EntityPlayback::Attack) return {};
    return asset.findClip("idle") ? "idle" : std::string{};
}

void EntityModelRegistry::beginFrame() { m_seen.clear(); }

void EntityModelRegistry::queue(
    EntityType type, uint64_t id, const glm::dvec3& position,
    const glm::vec3& velocity, uint32_t behaviorSeed, bool hurt, bool dead,
    float sourceTime, const glm::dvec3& renderOrigin,
    const glm::vec3& cameraPosition, model::ModelRenderer& renderer) {
    m_seen.insert(id);
    const auto& definition = this->definition(type);
    InstanceEntry& entry = m_instances[id];
    const float horizontalSpeed = std::hypot(velocity.x, velocity.z);
    const EntityPlayback playback = selectEntityPlayback(horizontalSpeed, hurt, dead);
    const float dt = std::max(0.0f, sourceTime - entry.lastSourceTime);
    entry.lastSourceTime = sourceTime;
    if (playback != entry.playback) {
        entry.previousPlayback = entry.playback;
        entry.instance.previousState = entry.instance.state;
        entry.instance.previousTime = entry.instance.stateTime;
        entry.instance.stateTime = 0.0f;
        entry.instance.transition = 0.0f;
        entry.playback = playback;
    }
    const float rate = playback == EntityPlayback::Walk
        ? walkPlaybackRate(horizontalSpeed) : 1.0f;
    entry.instance.stateTime += dt * rate;
    entry.instance.previousTime += dt;
    entry.instance.transition = std::min(1.0f,
        entry.instance.transition + dt / 0.15f);
    model::Pose current;
    model::evaluatePose(*definition.asset, clipFor(type, playback),
        entry.instance.stateTime, playback != EntityPlayback::Death, current);
    if (entry.instance.transition < 1.0f) {
        model::Pose previous;
        model::evaluatePose(*definition.asset,
                            clipFor(type, entry.previousPlayback),
                            entry.instance.previousTime, true, previous);
        model::blendPoses(previous, current, entry.instance.transition,
                          entry.instance.pose);
    } else entry.instance.pose = std::move(current);
    model::composeGlobals(*definition.asset, entry.instance.pose);
    entry.instance.jointPalettes.clear();
    for (std::size_t skin = 0; skin < definition.asset->skins.size(); ++skin)
        entry.instance.jointPalettes.push_back(
            model::jointMatrices(*definition.asset, entry.instance.pose, skin));
    const float yaw = horizontalSpeed > ENTITY_WALK_SPEED_THRESHOLD
        ? std::atan2(-velocity.x, -velocity.z)
        : static_cast<float>(behaviorSeed % 628u) * 0.01f;
    const glm::vec3 localPosition(glm::dvec3(position) - renderOrigin);
    const glm::mat4 transform = glm::translate(glm::mat4(1), localPosition) *
        glm::rotate(glm::mat4(1), yaw, glm::vec3(0,1,0));
    const glm::vec3 delta = localPosition - cameraPosition;
    renderer.queue({definition.handle, transform, &entry.instance,
                    glm::vec4(1), glm::dot(delta, delta)});
}

void EntityModelRegistry::endFrame() {
    for (auto it = m_instances.begin(); it != m_instances.end();) {
        if (!m_seen.count(it->first)) it = m_instances.erase(it);
        else ++it;
    }
}
