#include "player/PlayerRenderer.h"

#include "debug/Log.h"
#include "model/AnimationGraph.h"
#include "model/ModelRenderer.h"
#include "renderer/GameRenderer.h"
#include "Config.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

void PlayerRenderer::initialize(const std::filesystem::path& root,
                                IGameRenderer& renderer) {
    const auto loaded = model::loadGltf(root / "models/player/player.glb");
    if (!loaded) throw std::runtime_error("Player model: " + loaded.error);
    m_asset = loaded.asset;
    auto graph = model::loadAnimationGraph(
        root / "models/player/player.anim.json", *m_asset);
    if (!graph) throw std::runtime_error("Player animation graph: " + graph.error);
    m_graph = std::move(graph.graph);
    m_handle = renderer.modelRenderer().upload(m_asset);
    m_mixer.reset(m_asset.get(), m_graph.get());
    m_mixer.play(m_graph->actionFor("idle"));
    m_locomotion = "idle";
    m_headNode = m_rightArmNode = -1;
    for (size_t i=0;i<m_asset->nodes.size();++i) {
        if(m_asset->nodes[i].name=="head")m_headNode=static_cast<int>(i);
        if(m_asset->nodes[i].name=="arm_r")m_rightArmNode=static_cast<int>(i);
    }
}

void PlayerRenderer::update(const PlayerVisualState& state, float dt) {
    if (!m_asset || !m_graph) return;
    m_sleeping = state.sleeping;
    if (state.sleeping) {
        if (m_locomotion != "idle") {
            m_mixer.play(m_graph->actionFor("idle"), model::PlayPolicy::Replace);
            m_locomotion = "idle";
        }
        m_mixer.advance(dt);
        return;
    }
    const float horizontal = std::hypot(state.velocity.x, state.velocity.z);
    std::string locomotion;
    float speed = 1.0f;
    switch (playerLocomotion(state)) {
        case PlayerLocomotion::Idle: locomotion = "idle"; break;
        case PlayerLocomotion::Jump: locomotion = "jump"; break;
        case PlayerLocomotion::Fall: locomotion = "fall"; break;
        case PlayerLocomotion::Run:
            locomotion = "run";
            speed = std::max(.8f, horizontal / Config::SPRINT_SPEED);
            break;
        case PlayerLocomotion::Walk:
            locomotion = "walk";
            speed = std::max(.65f, horizontal / Config::PLAYER_SPEED);
            break;
    }
    if (locomotion != m_locomotion) {
        m_mixer.play(m_graph->actionFor(locomotion), model::PlayPolicy::Replace, speed);
        m_locomotion = locomotion;
    } else if (locomotion == "walk" || locomotion == "run")
        m_mixer.play(m_graph->actionFor(locomotion), model::PlayPolicy::Replace, speed);
    if (state.swingSequence != m_lastSwingSequence) {
        m_lastSwingSequence = state.swingSequence;
        m_mixer.play(m_graph->actionFor("swing"));
    }
    m_mixer.advance(dt);
}

glm::mat4 PlayerRenderer::renderThirdPerson(
    IGameRenderer& renderer, const glm::dvec3& position,
    const glm::dvec3& renderOrigin, float yawDegrees, float pitchDegrees,
    const glm::mat4& viewProjection, SmoothLightSample light,
    glm::vec3 sleepingFacing) {
    if (!m_asset || !m_handle) return glm::mat4(1.0f);
    m_mixer.evaluate(m_instance.pose);
    if (m_headNode >= 0) {
        const size_t head=static_cast<size_t>(m_headNode);
        m_instance.pose.rotation[head] = glm::angleAxis(
            glm::radians(-std::clamp(pitchDegrees,-70.0f,70.0f)),glm::vec3(1,0,0)) *
            m_instance.pose.rotation[head];
        model::composeGlobals(*m_asset,m_instance.pose);
    }
    m_instance.jointPalettes.clear();
    for(size_t skin=0;skin<m_asset->skins.size();++skin)
        m_instance.jointPalettes.push_back(
            model::jointMatrices(*m_asset,m_instance.pose,skin));
    const glm::vec3 local=glm::vec3(position-renderOrigin);
    const float facingYaw = m_sleeping && glm::length(sleepingFacing) > 0.01f
        ? glm::degrees(std::atan2(sleepingFacing.x, sleepingFacing.z))
        : yawDegrees;
    glm::mat4 world = glm::translate(glm::mat4(1), local) *
        glm::rotate(glm::mat4(1), glm::radians(facingYaw + 180.0f),
                    glm::vec3(0, 1, 0));
    if (m_sleeping) {
        world = glm::translate(glm::mat4(1), local + glm::vec3(0.0f, 0.42f, 0.0f)) *
            glm::rotate(glm::mat4(1), glm::radians(facingYaw + 180.0f),
                        glm::vec3(0, 1, 0)) *
            glm::rotate(glm::mat4(1), glm::radians(90.0f),
                        glm::vec3(0, 0, 1));
    }
    const float sky=std::pow(std::clamp(light.sky,0.0f,1.0f),1.35f);
    const float block=std::pow(std::clamp(light.block,0.0f,1.0f),1.35f);
    const glm::vec3 illumination=glm::max(glm::vec3(sky),
        glm::vec3(1.0f,.72f,.38f)*block);
    renderer.modelRenderer().queue({m_handle,world,&m_instance,
        glm::vec4(glm::max(illumination,glm::vec3(.025f)),1),0.0f,light});
    renderer.flushModels(viewProjection);
    if(m_rightArmNode<0)return world;
    return world*m_instance.pose.global[static_cast<size_t>(m_rightArmNode)]*
        glm::translate(glm::mat4(1),glm::vec3(0,-.42f,-.12f));
}
