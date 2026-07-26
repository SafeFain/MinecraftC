#include "ui/UIRenderer.h"
#include "renderer/Shader.h"
#include "debug/OpenGL.h"
#include "game/SurvivalRules.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdio>
#include <algorithm>

// ── Constructor / Destructor ──────────────────────────────────────────────

UIRenderer::~UIRenderer() {
    if (m_quadEBO) GL_CHECK(glDeleteBuffers(1, &m_quadEBO));
    if (m_quadVBO) GL_CHECK(glDeleteBuffers(1, &m_quadVBO));
    if (m_quadVAO) GL_CHECK(glDeleteVertexArrays(1, &m_quadVAO));
}

// ── Initialization ────────────────────────────────────────────────────────

void UIRenderer::initialize(GLuint blockAtlasTexture, bool framebufferSrgb) {
    m_blockAtlasTexture = blockAtlasTexture;
    m_manualGamma = !framebufferSrgb;
    // Compile UI rectangle shader
    m_uiShader = std::make_unique<Shader>(
        "assets/shaders/ui.vert",
        "assets/shaders/ui.frag"
    );

    // Create unit-square VAO for rectangles with index buffer
    const float quadVerts[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f
    };
    const unsigned int quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    GL_CHECK(glGenVertexArrays(1, &m_quadVAO));
    GL_CHECK(glBindVertexArray(m_quadVAO));

    GL_CHECK(glGenBuffers(1, &m_quadVBO));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW));
    GL_CHECK(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr));
    GL_CHECK(glEnableVertexAttribArray(0));
    GL_CHECK(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  reinterpret_cast<void*>(2 * sizeof(float))));
    GL_CHECK(glEnableVertexAttribArray(1));

    GL_CHECK(glGenBuffers(1, &m_quadEBO));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO));
    GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW));

    GL_CHECK(glBindVertexArray(0));

    // Initialize font renderer
    m_fontRenderer.initialize(m_manualGamma);
}

// ── Frame management ──────────────────────────────────────────────────────

void UIRenderer::beginUIFrame(int screenWidth, int screenHeight) {
    // Save current GL state
    glGetBooleanv(GL_DEPTH_TEST, &m_prevDepthTest);
    glGetBooleanv(GL_CULL_FACE, &m_prevCullFace);
    glGetBooleanv(GL_BLEND, &m_prevBlend);
    glGetIntegerv(GL_BLEND_SRC, &m_prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST, &m_prevBlendDst);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &m_prevActiveTexture);

    // Set UI state
    GL_CHECK(glDisable(GL_DEPTH_TEST));
    GL_CHECK(glDisable(GL_CULL_FACE));
    GL_CHECK(glEnable(GL_BLEND));
    GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // Orthographic projection: (0,0) at bottom-left, (w,h) at top-right
    m_projection = glm::ortho(0.0f, static_cast<float>(screenWidth),
                               0.0f, static_cast<float>(screenHeight));
}

void UIRenderer::endUIFrame() {
    // Restore previous GL state
    if (m_prevDepthTest) GL_CHECK(glEnable(GL_DEPTH_TEST)); else GL_CHECK(glDisable(GL_DEPTH_TEST));
    if (m_prevCullFace)  GL_CHECK(glEnable(GL_CULL_FACE));  else GL_CHECK(glDisable(GL_CULL_FACE));
    if (m_prevBlend)     GL_CHECK(glEnable(GL_BLEND));       else GL_CHECK(glDisable(GL_BLEND));
    GL_CHECK(glBlendFunc(m_prevBlendSrc, m_prevBlendDst));
    GL_CHECK(glActiveTexture(m_prevActiveTexture));
}

// ── Rectangle drawing ─────────────────────────────────────────────────────

void UIRenderer::drawRect(float x, float y, float w, float h,
                           const glm::vec4& color) {
    m_uiShader->bind();
    m_uiShader->setMat4("uProjection", m_projection);

    // Set 4-component color uniform
    m_uiShader->setVec4("uColor", color);
    m_uiShader->setInt("uUseTexture", 0);
    m_uiShader->setInt("uManualGamma", m_manualGamma ? 1 : 0);

    // Build 4 screen-space vertices using unit square + translation/scale
    // Since our shader takes screen-space positions directly and we use
    // orthographic projection, we need to upload dynamic vertices.
    float verts[] = {
        x,   y,   0.0f, 0.0f,
        x+w, y,   1.0f, 0.0f,
        x+w, y+h, 1.0f, 1.0f,
        x,   y+h, 0.0f, 1.0f
    };

    GL_CHECK(glBindVertexArray(m_quadVAO));

    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW));

    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO));
    GL_CHECK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr));

    GL_CHECK(glBindVertexArray(0));
}

void UIRenderer::drawBlockIcon(float x, float y, float w, float h, BlockId block) {
    if (!m_blockAtlasTexture || block == BlockId::AIR) return;

    FaceDir iconFace = FaceDir::TOP;
    if (block == BlockId::WOOD || block == BlockId::BIRCH_WOOD ||
        block == BlockId::SPRUCE_WOOD || block == BlockId::JUNGLE_WOOD ||
        block == BlockId::ACACIA_WOOD || getBlockProps(block).shape == RenderShape::Cross) {
        iconFace = FaceDir::FRONT;
    }
    const int tile = static_cast<int>(getFaceTextureIndex(block, iconFace));
    constexpr float atlasTiles = 8.0f;
    constexpr float inset = 0.5f / (16.0f * atlasTiles);
    float u0 = static_cast<float>(tile % 8) / atlasTiles + inset;
    float v0 = static_cast<float>(tile / 8) / atlasTiles + inset;
    float u1 = static_cast<float>(tile % 8 + 1) / atlasTiles - inset;
    float v1 = static_cast<float>(tile / 8 + 1) / atlasTiles - inset;

    float verts[] = {
        x,   y,   u0, v0,
        x+w, y,   u1, v0,
        x+w, y+h, u1, v1,
        x,   y+h, u0, v1
    };

    m_uiShader->bind();
    m_uiShader->setMat4("uProjection", m_projection);
    m_uiShader->setVec4("uColor", glm::vec4(1.0f));
    m_uiShader->setInt("uUseTexture", 1);
    m_uiShader->setInt("uTexture", 0);
    m_uiShader->setInt("uManualGamma", m_manualGamma ? 1 : 0);
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_blockAtlasTexture));
    GL_CHECK(glBindVertexArray(m_quadVAO));
    GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO));
    GL_CHECK(glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW));
    GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_quadEBO));
    GL_CHECK(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr));
    GL_CHECK(glBindVertexArray(0));
}

void UIRenderer::drawPanel(float x, float y, float w, float h, const glm::vec4& fill) {
    drawRect(x, y, w, h, glm::vec4(0.02f, 0.02f, 0.025f, fill.a));
    drawRect(x + 2, y + 2, w - 4, h - 4, glm::vec4(0.48f, 0.48f, 0.52f, fill.a));
    drawRect(x + 4, y + 4, w - 8, h - 8, fill);
}

void UIRenderer::drawItemIcon(float x, float y, float w, float h, const ItemStack& stack) {
    if (stack.empty()) return;
    const auto& props = getItemProps(stack.id);
    if (props.placedBlock) { drawBlockIcon(x, y, w, h, *props.placedBlock); return; }
    glm::vec4 material(.72f, .72f, .72f, 1.0f);
    if (props.tier == ToolTier::Wood) material = {.48f,.30f,.14f,1};
    else if (props.tier == ToolTier::Stone) material = {.48f,.50f,.52f,1};
    else if (props.tier == ToolTier::Iron) material = {.82f,.84f,.82f,1};
    else if (props.tier == ToolTier::Gold) material = {.95f,.72f,.12f,1};
    else if (props.tier == ToolTier::Diamond) material = {.18f,.82f,.78f,1};
    const float p = std::max(2.0f, std::floor(std::min(w,h) / 12.0f));
    if (props.kind == ItemKind::Tool || props.kind == ItemKind::Weapon) {
        drawRect(x+w*.45f, y+h*.12f, p*2, h*.62f, {.42f,.24f,.10f,1});
        if (props.tool == ToolKind::Pickaxe)
            drawRect(x+w*.18f, y+h*.67f, w*.68f, p*2, material);
        else if (props.tool == ToolKind::Axe) {
            drawRect(x+w*.50f, y+h*.58f, w*.32f, h*.26f, material);
            drawRect(x+w*.33f, y+h*.64f, w*.24f, p*2, material);
        } else if (props.tool == ToolKind::Bow) {
            drawRect(x+w*.25f,y+h*.18f,p,h*.64f,material);
            drawRect(x+w*.68f,y+h*.18f,p,h*.64f,{.9f,.9f,.75f,1});
        } else if (props.tool == ToolKind::Shield) {
            drawRect(x+w*.20f,y+h*.18f,w*.60f,h*.66f,{.46f,.28f,.12f,1});
            drawRect(x+w*.30f,y+h*.31f,w*.40f,h*.42f,material);
        } else drawRect(x+w*.35f, y+h*.64f, w*.42f, p*2, material);
    } else if (props.kind == ItemKind::Armor) {
        drawRect(x+w*.22f,y+h*.20f,w*.56f,h*.58f,material);
        drawRect(x+w*.36f,y+h*.10f,w*.28f,h*.22f,{.10f,.10f,.12f,1});
    } else if (props.kind == ItemKind::Food) {
        drawRect(x+w*.22f,y+h*.22f,w*.56f,h*.52f,{.72f,.25f,.12f,1});
        drawRect(x+w*.54f,y+h*.70f,p*2,p*2,{.25f,.68f,.18f,1});
    } else {
        drawRect(x+w*.24f,y+h*.24f,w*.52f,h*.52f,material);
        drawRect(x+w*.34f,y+h*.34f,w*.32f,h*.32f,{material.r*.65f,material.g*.65f,material.b*.65f,1});
    }
}

void UIRenderer::drawTooltip(float x, float y, const ItemStack& stack) {
    if (stack.empty()) return;
    const auto& props = getItemProps(stack.id);
    std::string detail = props.name;
    if (stack.count > 1) detail += " x" + std::to_string(stack.count);
    if (props.maxDurability)
        detail += "  " + std::to_string(props.maxDurability - std::min(props.maxDurability, stack.damage)) +
                  "/" + std::to_string(props.maxDurability);
    else if (props.kind == ItemKind::Armor)
        detail += "  Armor";
    else if (props.attackDamage > 0.0f)
        detail += "  Damage " + std::to_string(static_cast<int>(props.attackDamage));
    else if (props.food > 0)
        detail += "  Food +" + std::to_string(props.food);
    const auto size = measureText(detail, .9f);
    drawPanel(x, y, size.x + 14.0f, size.y + 12.0f, {.08f,.05f,.12f,.97f});
    renderText(detail, x + 7.0f, y + 6.0f, .9f, {.95f,.90f,1.0f});
}

void UIRenderer::drawDurability(float x, float y, float w, const ItemStack& stack) {
    const auto& props = stack.empty() ? getItemProps(ItemId::EMPTY) : getItemProps(stack.id);
    if (props.maxDurability == 0 || stack.damage == 0) return;
    const float remaining = durabilityRemaining(stack);
    const glm::vec4 color(1.0f - remaining, remaining, 0.08f, 1.0f);
    drawRect(x, y, w, 4.0f, glm::vec4(0.02f, 0.02f, 0.02f, 0.95f));
    drawRect(x + 1.0f, y + 1.0f, (w - 2.0f) * remaining, 2.0f, color);
}

// ── Text rendering (delegated to FontRenderer) ────────────────────────────

void UIRenderer::renderText(const std::string& text, float x, float y,
                              float scale, const glm::vec3& color) {
    m_fontRenderer.begin(m_projection);
    m_fontRenderer.renderText(text, x, y, scale, color);
    m_fontRenderer.end();
}

glm::vec2 UIRenderer::measureText(const std::string& text, float scale) {
    return m_fontRenderer.measureText(text, scale);
}
