#include "entity/EntityModelRegistry.h"
#include "model/AnimationGraph.h"
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

std::shared_ptr<const model::AnimationGraphAsset> fallbackGraph(
    const model::ModelAsset& asset, EntityType type) {
    auto graph = std::make_shared<model::AnimationGraphAsset>();
    graph->layers = {
        {"base", 0, model::LayerBlendMode::Override,
         std::vector<float>(asset.nodes.size(), 1.0f)},
        {"action", 100, model::LayerBlendMode::Override,
         std::vector<float>(asset.nodes.size(), 1.0f)},
        {"reaction", 200, model::LayerBlendMode::Additive,
         std::vector<float>(asset.nodes.size(), 1.0f)},
        {"death", 300, model::LayerBlendMode::Override,
         std::vector<float>(asset.nodes.size(), 1.0f)}
    };
    auto add = [&](const std::string& name, const std::string& layer,
                   bool loop, int priority) {
        const model::AnimationClip* clip = asset.findClip(name);
        model::AnimationActionDefinition action;
        action.name = action.clip = name;
        action.layer = layer;
        action.loop = loop;
        action.duration = clip ? std::max(clip->duration, 0.001f) : 1.0f;
        action.priority = priority;
        graph->actions.emplace(name, action);
        graph->bindings.emplace(name, name);
    };
    add("idle", "base", true, 0);
    add("walk", "base", true, 0);
    add("hurt", "reaction", false, 200);
    add("death", "death", false, 300);
    model::AnimationActionDefinition attack;
    attack.name = attack.clip = "attack";
    attack.layer = "action";
    attack.priority = 100;
    if (type == EntityType::Skeleton) {
        attack.duration = 0.75f;
        attack.events.push_back({"shoot", 0.45f});
    } else if (type == EntityType::Blastling) {
        attack.duration = 1.20f;
        attack.events.push_back({"explode", 1.0f});
    } else {
        attack.duration = 0.55f;
        attack.events.push_back({"melee", 0.30f});
    }
    graph->actions.emplace("attack", attack);
    graph->bindings.emplace("attack", "attack");
    return graph;
}

std::string graphContractError(EntityType type,
                               const model::AnimationGraphAsset& graph) {
    for (const char* semantic : {"idle", "walk", "hurt", "death"})
        if (!graph.findAction(graph.actionFor(semantic)))
            return std::string("missing required semantic action ") + semantic;
    const bool hostile = type == EntityType::Zombie || type == EntityType::Skeleton ||
                         type == EntityType::Spider || type == EntityType::Blastling;
    if (!hostile) return {};
    const model::AnimationActionDefinition* attack =
        graph.findAction(graph.actionFor("attack"));
    if (!attack) return "missing required hostile attack action";
    const char* requiredEvent = type == EntityType::Skeleton ? "shoot" :
        (type == EntityType::Blastling ? "explode" : "melee");
    for (const auto& event : attack->events)
        if (event.name == requiredEvent) return {};
    return std::string("attack is missing required event ") + requiredEvent;
}
}

EntityModelRegistry::EntityModelRegistry(Loader loader)
    : m_loader(std::move(loader)), m_definitions{{
        {EntityType::Cow,"cow.glb",{},{},0,false},
        {EntityType::Pig,"pig.glb",{},{},0,false},
        {EntityType::Sheep,"sheep.glb",{},{},0,false},
        {EntityType::Chicken,"chicken.glb",{},{},0,false},
        {EntityType::Zombie,"zombie.glb",{},{},0,false},
        {EntityType::Skeleton,"skeleton.glb",{},{},0,false},
        {EntityType::Spider,"spider.glb",{},{},0,false},
        {EntityType::Blastling,"blastling.glb",{},{},0,false}}},
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
        const std::filesystem::path graphPath = path.parent_path() /
            (path.stem().u8string() + ".anim.json");
        model::AnimationGraphLoadResult loadedGraph =
            model::loadAnimationGraph(graphPath, *definition.asset);
        if (loadedGraph) {
            const std::string contract = graphContractError(
                definition.type, *loadedGraph.graph);
            if (!contract.empty()) {
                loadedGraph.graph.reset();
                loadedGraph.error = graphPath.u8string() + ": " + contract;
            }
        }
        if (!loadedGraph) {
            LOG_WARN("Entity action graph fallback for " << graphPath.u8string()
                     << ": " << loadedGraph.error);
            definition.graph = fallbackGraph(*definition.asset, definition.type);
        } else definition.graph = std::move(loadedGraph.graph);
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

void EntityModelRegistry::clearInstances() {
    m_instances.clear();
    m_seen.clear();
}

const EntityModelDefinition& EntityModelRegistry::definition(EntityType type) const {
    for (const auto& definition : m_definitions)
        if (definition.type == type) return definition;
    throw std::out_of_range("entity type has no model definition");
}

EntityModelRegistry::InstanceEntry& EntityModelRegistry::ensure(
    EntityType type, uint64_t id) {
    InstanceEntry& entry = m_instances[id];
    if (!entry.initialized || entry.type != type) {
        const auto& current = definition(type);
        entry = {};
        entry.type = type;
        entry.initialized = true;
        entry.mixer.reset(current.asset.get(), current.graph.get());
        const std::string idle = current.graph->actionFor("idle");
        entry.mixer.play(idle);
    }
    return entry;
}

void EntityModelRegistry::setLocomotion(EntityType type, uint64_t id, float speed) {
    InstanceEntry& entry = ensure(type, id);
    const auto& graph = *definition(type).graph;
    const bool walking = speed > ENTITY_WALK_SPEED_THRESHOLD;
    entry.mixer.play(graph.actionFor(walking ? "walk" : "idle"),
                     model::PlayPolicy::Replace,
                     walking ? walkPlaybackRate(speed) : 1.0f);
}

bool EntityModelRegistry::playAction(EntityType type, uint64_t id,
                                     const std::string& semantic,
                                     model::PlayPolicy policy) {
    InstanceEntry& entry = ensure(type, id);
    return entry.mixer.play(definition(type).graph->actionFor(semantic), policy);
}

std::vector<model::FiredAnimationEvent> EntityModelRegistry::advance(
    EntityType type, uint64_t id, float dt) {
    return ensure(type, id).mixer.advance(dt);
}

bool EntityModelRegistry::playing(EntityType type, uint64_t id,
                                  const std::string& semantic) {
    InstanceEntry& entry = ensure(type, id);
    return entry.mixer.playing(definition(type).graph->actionFor(semantic));
}

void EntityModelRegistry::beginFrame() { m_seen.clear(); }

void EntityModelRegistry::queue(
    EntityType type, uint64_t id, const glm::dvec3& position,
    const glm::vec3& facing, uint32_t behaviorSeed,
    const glm::dvec3& renderOrigin,
    const glm::vec3& cameraPosition, model::ModelRenderer& renderer) {
    m_seen.insert(id);
    const auto& definition = this->definition(type);
    InstanceEntry& entry = ensure(type, id);
    entry.mixer.evaluate(entry.instance.pose);
    entry.instance.jointPalettes.clear();
    for (std::size_t skin = 0; skin < definition.asset->skins.size(); ++skin)
        entry.instance.jointPalettes.push_back(
            model::jointMatrices(*definition.asset, entry.instance.pose, skin));
    const float facingLength = std::hypot(facing.x, facing.z);
    const float yaw = facingLength > 0.001f
        ? std::atan2(-facing.x, -facing.z)
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
