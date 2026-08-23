#pragma once

// ── Pixel UI theme ─────────────────────────────────────────────────────────
//
// Shared, backend-neutral visual theme for every menu, inventory, container
// and HUD surface.  All decorations are composed from plain colored quads
// (drawRect) and text so OpenGL and Vulkan render identical output without
// any new texture assets.  Every helper is a template over any object that
// exposes drawRect/renderText/measureText (UIRenderer and both UI backends).

#include <glm/glm.hpp>
#include <glm/common.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

#include "game/Item.h"
#include "game/Localization.h"

namespace UiTheme {

// ── Palette ────────────────────────────────────────────────────────────────

inline constexpr glm::vec4 INK(0.10f, 0.085f, 0.08f, 1.0f);
inline constexpr glm::vec4 BEVEL_DARK(0.10f, 0.09f, 0.08f, 1.0f);
inline constexpr glm::vec4 BEVEL_LIGHT(0.42f, 0.38f, 0.34f, 1.0f);
inline constexpr glm::vec4 BEVEL_LIGHTEST(0.62f, 0.56f, 0.49f, 1.0f);

inline constexpr glm::vec4 PANEL(0.18f, 0.165f, 0.15f, 0.98f);
inline constexpr glm::vec4 PANEL_DEEP(0.13f, 0.12f, 0.11f, 0.98f);

inline constexpr glm::vec4 BUTTON(0.24f, 0.22f, 0.19f, 1.0f);
inline constexpr glm::vec4 BUTTON_HOVER(0.31f, 0.28f, 0.24f, 1.0f);
inline constexpr glm::vec4 BUTTON_SELECTED(0.36f, 0.33f, 0.27f, 1.0f);
inline constexpr glm::vec4 BUTTON_PRESSED(0.17f, 0.16f, 0.14f, 1.0f);
inline constexpr glm::vec4 BUTTON_DANGER(0.36f, 0.18f, 0.14f, 1.0f);
inline constexpr glm::vec4 BUTTON_DANGER_HOVER(0.46f, 0.23f, 0.17f, 1.0f);

inline constexpr glm::vec4 GOLD(0.95f, 0.76f, 0.31f, 1.0f);
inline constexpr glm::vec4 GOLD_DIM(0.70f, 0.55f, 0.22f, 1.0f);

inline constexpr glm::vec4 SLOT(0.13f, 0.12f, 0.10f, 1.0f);
inline constexpr glm::vec4 SLOT_HOVER(0.21f, 0.19f, 0.16f, 1.0f);

inline constexpr glm::vec3 TEXT(0.95f, 0.93f, 0.86f);
inline constexpr glm::vec3 TEXT_DIM(0.66f, 0.62f, 0.56f);
inline constexpr glm::vec3 TEXT_TITLE(0.98f, 0.83f, 0.38f);
inline constexpr glm::vec3 TEXT_HOVER(1.0f, 0.95f, 0.65f);
inline constexpr glm::vec3 TEXT_SHADOW(0.05f, 0.04f, 0.03f);

inline constexpr glm::vec4 OVERLAY(0.0f, 0.0f, 0.0f, 0.55f);
inline constexpr glm::vec4 TOOLTIP_FILL(0.12f, 0.08f, 0.06f, 0.97f);

inline constexpr glm::vec4 DIRT_A(0.24f, 0.19f, 0.14f, 1.0f);
inline constexpr glm::vec4 DIRT_B(0.20f, 0.16f, 0.12f, 1.0f);
inline constexpr glm::vec4 DIRT_C(0.28f, 0.22f, 0.16f, 1.0f);
inline constexpr glm::vec4 GRASS(0.30f, 0.48f, 0.24f, 1.0f);
inline constexpr glm::vec4 GRASS_LIGHT(0.42f, 0.62f, 0.33f, 1.0f);
inline constexpr glm::vec4 GRASS_DARK(0.15f, 0.27f, 0.12f, 1.0f);

inline constexpr glm::vec4 SKY_TOP(0.10f, 0.17f, 0.30f, 1.0f);
inline constexpr glm::vec4 SKY_HORIZON(0.48f, 0.68f, 0.82f, 1.0f);
inline constexpr glm::vec4 MOUNTAIN_FAR(0.25f, 0.31f, 0.42f, 1.0f);
inline constexpr glm::vec4 MOUNTAIN_NEAR(0.16f, 0.20f, 0.28f, 1.0f);
inline constexpr glm::vec4 SNOW(0.86f, 0.90f, 0.95f, 1.0f);
inline constexpr glm::vec4 CLOUD(0.92f, 0.92f, 0.96f, 0.9f);
inline constexpr glm::vec4 CLOUD_SHADE(0.66f, 0.70f, 0.82f, 0.9f);

// ── Widget states ──────────────────────────────────────────────────────────

enum class WidgetState : uint8_t {
    Normal = 0,
    Hover,
    Selected,
    Pressed
};

inline constexpr glm::vec4 PX_TRANSPARENT(0.0f, 0.0f, 0.0f, 0.0f);

inline constexpr std::array<glm::vec4, 10> ARROW_PALETTE{{
    PX_TRANSPARENT,
    GOLD,                                    // 1 arrow body
    INK,                                     // 2 arrow outline
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT}};

inline constexpr const char* ARROW_RIGHT[] = {
    "..1",
    ".11",
    "111",
    ".11",
    "..1"};

// ── Small primitives ───────────────────────────────────────────────────────

inline glm::vec4 withAlpha(glm::vec4 color, float alpha) {
    color.a *= alpha;
    return color;
}

template <class T>
inline void rect(T& ui, float x, float y, float w, float h,
                 const glm::vec4& color) {
    if (w <= 0.0f || h <= 0.0f) return;
    ui.drawRect(x, y, w, h, color);
}

template <class T>
inline void textWithShadow(T& ui, const std::string& text, float x, float y,
                           float scale, const glm::vec3& color,
                           float alpha = 1.0f, float dx = 1.0f,
                           float dy = -1.0f) {
    if (text.empty()) return;
    ui.renderText(text, x + dx, y + dy, scale, TEXT_SHADOW * alpha);
    ui.renderText(text, x, y, scale, color * alpha);
}

// One ink border + 1 px raised/lowered bevel + interior fill.  Small controls
// degrade to a plain 1 px ink frame so they never overflow tiny layouts.
template <class T>
inline void beveledBody(T& ui, float x, float y, float w, float h,
                        const glm::vec4& fill, bool pressed, float alpha = 1.0f) {
    rect(ui, x, y, w, h, withAlpha(INK, alpha));
    if (w < 6.0f || h < 6.0f) {
        rect(ui, x + 1, y + 1, w - 2, h - 2, withAlpha(fill, alpha));
        return;
    }
    rect(ui, x + 1, y + 1, w - 2, h - 2,
         withAlpha(pressed ? BEVEL_DARK : BEVEL_LIGHT, alpha));
    rect(ui, x + 1, y + 1, w - 2, 1,
         withAlpha(pressed ? INK : BEVEL_LIGHTEST, alpha));
    rect(ui, x + 1, y + 1, 1, h - 2,
         withAlpha(pressed ? INK : BEVEL_LIGHTEST, alpha));
    rect(ui, x + 1, y + h - 2, w - 2, 1,
         withAlpha(pressed ? BEVEL_LIGHT : BEVEL_DARK, alpha));
    rect(ui, x + w - 2, y + 1, 1, h - 2,
         withAlpha(pressed ? BEVEL_LIGHT : BEVEL_DARK, alpha));
    rect(ui, x + 2, y + 2, w - 4, h - 4, withAlpha(fill, alpha));
}

// ── Composite widgets ──────────────────────────────────────────────────────

template <class T>
inline void panel(T& ui, float x, float y, float w, float h,
                  const glm::vec4& fill = PANEL, const std::string& title = {},
                  float titleScale = 1.6f, float alpha = 1.0f) {
    beveledBody(ui, x, y, w, h, fill, false, alpha);
    if (title.empty() || h < 34.0f) return;
    const float headH = std::min(26.0f, h * 0.28f);
    rect(ui, x + 2, y + h - 2 - headH, w - 4, headH, withAlpha(PANEL_DEEP, alpha));
    rect(ui, x + 2, y + h - 2 - headH, w - 4, 1, withAlpha(INK, alpha));
    textWithShadow(ui, title,
                   x + (w - ui.measureText(title, titleScale).x) * 0.5f,
                   y + h - 2 - headH + (headH - 14.0f * titleScale) * 0.5f,
                   titleScale, TEXT_TITLE, alpha);
}

template <class T>
inline void button(T& ui, float x, float y, float w, float h,
                   const std::string& label, WidgetState state,
                   bool danger = false, float textScale = 0.0f,
                   float alpha = 1.0f) {
    glm::vec4 fill = danger ? BUTTON_DANGER : BUTTON;
    if (state == WidgetState::Hover)
        fill = danger ? BUTTON_DANGER_HOVER : BUTTON_HOVER;
    else if (state == WidgetState::Selected)
        fill = danger ? BUTTON_DANGER_HOVER : BUTTON_SELECTED;
    else if (state == WidgetState::Pressed)
        fill = danger ? BUTTON_DANGER : BUTTON_PRESSED;
    beveledBody(ui, x, y, w, h, fill, state == WidgetState::Pressed, alpha);
    if (state == WidgetState::Selected) {
        rect(ui, x, y, w, 2, withAlpha(GOLD, alpha));
        rect(ui, x, y + h - 2, w, 2, withAlpha(GOLD, alpha));
        rect(ui, x, y, 2, h, withAlpha(GOLD, alpha));
        rect(ui, x + w - 2, y, 2, h, withAlpha(GOLD, alpha));
    }
    if (textScale <= 0.0f)
        textScale = std::clamp((h - 8.0f) / 14.0f, 0.9f, 1.8f);
    const auto size = ui.measureText(label, textScale);
    const glm::vec3 color =
        (state == WidgetState::Hover || state == WidgetState::Selected)
            ? TEXT_HOVER : TEXT;
    textWithShadow(ui, label, x + (w - size.x) * 0.5f,
                   y + (h - size.y) * 0.5f, textScale, color, alpha);
}

// Recessed inventory-style slot.  fill may carry a per-item tint.
template <class T>
inline void slot(T& ui, float x, float y, float w, float h, WidgetState state,
                 const glm::vec4& fill = SLOT, float alpha = 1.0f) {
    rect(ui, x, y, w, h, withAlpha(INK, alpha));
    if (w < 6.0f || h < 6.0f) {
        rect(ui, x + 1, y + 1, w - 2, h - 2, withAlpha(fill, alpha));
    } else {
        rect(ui, x + 1, y + 1, w - 2, h - 2, withAlpha(BEVEL_DARK, alpha));
        rect(ui, x + 1, y + 1, w - 2, 1, withAlpha(INK, alpha));
        rect(ui, x + 1, y + 1, 1, h - 2, withAlpha(INK, alpha));
        rect(ui, x + 1, y + h - 2, w - 2, 1, withAlpha(BEVEL_LIGHT, alpha));
        rect(ui, x + w - 2, y + 1, 1, h - 2, withAlpha(BEVEL_LIGHT, alpha));
        rect(ui, x + 2, y + 2, w - 4, h - 4,
             withAlpha(state == WidgetState::Hover ? SLOT_HOVER : fill, alpha));
        if (state == WidgetState::Hover)
            rect(ui, x + 2, y + 2, w - 4, h - 4,
                 glm::vec4(1.0f, 1.0f, 1.0f, 0.08f * alpha));
    }
    if (state == WidgetState::Selected) {
        rect(ui, x, y, w, 2, withAlpha(GOLD, alpha));
        rect(ui, x, y + h - 2, w, 2, withAlpha(GOLD, alpha));
        rect(ui, x, y, 2, h, withAlpha(GOLD, alpha));
        rect(ui, x + w - 2, y, 2, h, withAlpha(GOLD, alpha));
    }
}

template <class T>
inline void progressBar(T& ui, float x, float y, float w, float h,
                        float fraction, const glm::vec4& fillColor,
                        float alpha = 1.0f) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    rect(ui, x, y, w, h, withAlpha(INK, alpha));
    if (w >= 6.0f && h >= 6.0f) {
        rect(ui, x + 1, y + 1, w - 2, h - 2, withAlpha(BEVEL_DARK, alpha));
        rect(ui, x + 1, y + 1, w - 2, 1, withAlpha(INK, alpha));
        rect(ui, x + 1, y + 1, 1, h - 2, withAlpha(INK, alpha));
        rect(ui, x + 1, y + h - 2, w - 2, 1, withAlpha(BEVEL_LIGHT, alpha));
        rect(ui, x + w - 2, y + 1, 1, h - 2, withAlpha(BEVEL_LIGHT, alpha));
    }
    const float fw = std::max(0.0f, (w - 4.0f) * fraction);
    const float fh = std::max(0.0f, h - 4.0f);
    if (fw > 0.5f && fh > 0.0f) {
        rect(ui, x + 2, y + 2, fw, fh, withAlpha(fillColor, alpha));
        if (fh > 2.0f) {
            rect(ui, x + 2, y + 2, fw, 1,
                 withAlpha(glm::vec4(std::min(1.0f, fillColor.r * 1.25f),
                                     std::min(1.0f, fillColor.g * 1.25f),
                                     std::min(1.0f, fillColor.b * 1.25f), 1.0f),
                           alpha));
            rect(ui, x + 2, y + 2 + fh - 1, fw, 1,
                 withAlpha(glm::vec4(fillColor.r * 0.72f, fillColor.g * 0.72f,
                                     fillColor.b * 0.72f, 1.0f), alpha));
        }
    }
}

// Forward declarations: scrollBar needs the sprite helpers defined below.
template <class T>
inline void spriteRows(T& ui, float x, float y, float px,
                       const char* const* rows, size_t rowCount,
                       const std::array<glm::vec4, 10>& palette, float alpha);

template <class T, size_t N>
inline void sprite(T& ui, float x, float y, float px,
                   const char* const (&rows)[N],
                   const std::array<glm::vec4, 10>& palette,
                   float alpha = 1.0f);

template <class T>
inline void sprite(T& ui, float x, float y, float px,
                   std::initializer_list<const char*> rows,
                   const std::array<glm::vec4, 10>& palette,
                   float alpha = 1.0f);

template <class T>
inline void scrollBar(T& ui, float x, float y, float w, float h,
                      int offset, int visible, int total) {
    rect(ui, x, y, w, h, INK);
    rect(ui, x + 1, y + 1, w - 2, h - 2, BEVEL_DARK);
    const float arrowH = std::min(14.0f, std::max(8.0f, h * 0.08f));
    // Arrows: ink outline behind the gold glyph for crisp pixel contrast.
    sprite(ui, x + w * 0.5f - 2.5f, y + h - arrowH + 1.0f, 1.0f,
           {"..2..", ".222.", "22222"}, ARROW_PALETTE);
    sprite(ui, x + w * 0.5f - 2.5f - 1.0f, y + h - arrowH + 2.0f, 1.0f,
           {"..1..", ".111.", "11111"}, ARROW_PALETTE);
    sprite(ui, x + w * 0.5f - 2.5f, y + 1.0f, 1.0f,
           {"22222", ".222.", "..2.."}, ARROW_PALETTE);
    sprite(ui, x + w * 0.5f - 2.5f - 1.0f, y + 2.0f, 1.0f,
           {"11111", ".111.", "..1.."}, ARROW_PALETTE);
    const int maximum = std::max(0, total - visible);
    const float trackTop = y + arrowH + 2.0f;
    const float trackBottom = y + h - arrowH - 2.0f;
    const float trackH = std::max(0.0f, trackBottom - trackTop);
    const float thumbH = std::max(10.0f,
        trackH * static_cast<float>(visible) / static_cast<float>(std::max(1, total)));
    const float fraction = maximum == 0
        ? 0.0f : static_cast<float>(offset) / static_cast<float>(maximum);
    const float thumbY = trackTop + (trackH - thumbH) * (1.0f - fraction);
    beveledBody(ui, x, thumbY, w, thumbH, BUTTON, false, 1.0f);
}

// ── Pixel sprites ──────────────────────────────────────────────────────────
//
// Each string is one top-down sprite row; '.' is transparent and '1'..'9'
// index into a 10-entry palette.

template <class T>
inline void spriteRows(T& ui, float x, float y, float px,
                       const char* const* rows, size_t rowCount,
                       const std::array<glm::vec4, 10>& palette,
                       float alpha) {
    int row = static_cast<int>(rowCount) - 1;
    for (size_t r = 0; r < rowCount; ++r) {
        const char* line = rows[r];
        for (int col = 0; line[col] != '\0'; ++col) {
            const char c = line[col];
            if (c < '0' || c > '9') continue;
            const glm::vec4& color = palette[static_cast<size_t>(c - '0')];
            if (color.a <= 0.0f) continue;
            rect(ui, x + static_cast<float>(col) * px,
                 y + static_cast<float>(row) * px, px, px,
                 withAlpha(color, alpha));
        }
        --row;
    }
}

template <class T, size_t N>
inline void sprite(T& ui, float x, float y, float px,
                   const char* const (&rows)[N],
                   const std::array<glm::vec4, 10>& palette,
                   float alpha) {
    spriteRows(ui, x, y, px, rows, N, palette, alpha);
}

template <class T>
inline void sprite(T& ui, float x, float y, float px,
                   std::initializer_list<const char*> rows,
                   const std::array<glm::vec4, 10>& palette,
                   float alpha) {
    spriteRows(ui, x, y, px, rows.begin(), rows.size(), palette, alpha);
}

inline constexpr std::array<glm::vec4, 10> HEART_PALETTE{{
    PX_TRANSPARENT,
    glm::vec4(0.13f, 0.02f, 0.02f, 1.0f),   // 1 outline
    glm::vec4(0.92f, 0.10f, 0.13f, 1.0f),   // 2 red
    glm::vec4(1.0f, 0.55f, 0.55f, 1.0f),    // 3 highlight
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT, PX_TRANSPARENT}};

inline constexpr std::array<glm::vec4, 10> HUNGER_PALETTE{{
    PX_TRANSPARENT,
    glm::vec4(0.15f, 0.08f, 0.02f, 1.0f),   // 1 outline
    glm::vec4(0.93f, 0.47f, 0.10f, 1.0f),   // 2 meat
    glm::vec4(1.0f, 0.78f, 0.40f, 1.0f),    // 3 meat highlight
    glm::vec4(0.95f, 0.88f, 0.70f, 1.0f),   // 4 bone
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT}};

inline constexpr std::array<glm::vec4, 10> ARMOR_PALETTE{{
    PX_TRANSPARENT,
    glm::vec4(0.10f, 0.12f, 0.15f, 1.0f),   // 1 outline
    glm::vec4(0.62f, 0.70f, 0.78f, 1.0f),   // 2 steel
    glm::vec4(0.88f, 0.93f, 0.97f, 1.0f),   // 3 highlight
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT, PX_TRANSPARENT}};

inline constexpr std::array<glm::vec4, 10> BUBBLE_PALETTE{{
    PX_TRANSPARENT,
    glm::vec4(0.03f, 0.10f, 0.16f, 1.0f),   // 1 outline
    glm::vec4(0.10f, 0.22f, 0.34f, 1.0f),   // 2 shell
    glm::vec4(0.45f, 0.78f, 1.0f, 1.0f),    // 3 water
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT, PX_TRANSPARENT}};

inline constexpr std::array<glm::vec4, 10> FLAME_PALETTE{{
    PX_TRANSPARENT,
    glm::vec4(0.35f, 0.12f, 0.02f, 1.0f),   // 1 outline
    glm::vec4(1.0f, 0.45f, 0.06f, 1.0f),    // 2 orange
    glm::vec4(1.0f, 0.85f, 0.25f, 1.0f),    // 3 yellow
    glm::vec4(1.0f, 0.98f, 0.90f, 1.0f),    // 4 core
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT}};

inline constexpr std::array<glm::vec4, 10> JOY_PALETTE{{
    PX_TRANSPARENT,
    glm::vec4(0.85f, 0.82f, 0.75f, 1.0f),   // 1 light
    glm::vec4(0.10f, 0.09f, 0.08f, 1.0f),   // 2 ink
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
    PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT}};

inline constexpr const char* HEART_FULL[] = {
    ".22..22.",
    "22332232",
    "23333222",
    "23333222",
    ".233332.",
    "..2332..",
    "...22..."};
inline constexpr const char* HEART_HALF[] = {
    ".22..22.",
    "2233..22",
    "2333.222",
    "2333.222",
    ".233..2.",
    "..23.2..",
    "...22..."};
inline constexpr const char* HEART_EMPTY[] = {
    ".22..22.",
    "22....22",
    "2......2",
    "2......2",
    ".2....2.",
    "..2..2..",
    "...22..."};

inline constexpr const char* HUNGER_FULL[] = {
    "...222..",
    "..22332.",
    "..233322",
    "..233322",
    ".2233332",
    ".4422332",
    ".44.222.",
    ".44....."};
inline constexpr const char* HUNGER_HALF[] = {
    "...222..",
    "..2232..",
    "..233.22",
    "..233.22",
    ".2233.22",
    ".4422.2.",
    ".44.22..",
    ".44....."};
inline constexpr const char* HUNGER_EMPTY[] = {
    "...222..",
    "..22.22.",
    "..2...22",
    "..2...22",
    ".22...2.",
    ".4....22",
    ".4..22..",
    ".4......"};

inline constexpr const char* ARMOR_FULL[] = {
    ".222222.",
    "23333332",
    "23333332",
    "23333332",
    "2.2332.2",
    "2.2332.2",
    "23333332"};
inline constexpr const char* ARMOR_HALF[] = {
    ".222222.",
    "2333...2",
    "2333...2",
    "2333...2",
    "2.23.2.2",
    "2.23.2.2",
    "2333...2"};
inline constexpr const char* ARMOR_EMPTY[] = {
    ".222222.",
    "2......2",
    "2......2",
    "2......2",
    "2..22..2",
    "2..22..2",
    "2......2"};

inline constexpr const char* BUBBLE_FULL[] = {
    "..222..",
    ".23332.",
    "23...32",
    "2.....2",
    "2.....2",
    ".2...2.",
    "..222.."};
inline constexpr const char* BUBBLE_EMPTY[] = {
    "..222..",
    ".2...2.",
    "2.....2",
    "2.....2",
    "2.....2",
    ".2...2.",
    "..222.."};

inline constexpr const char* FLAME[] = {
    "...11...",
    "..1221..",
    "..1221..",
    ".123321.",
    ".1234321",
    ".1234321",
    ".123321.",
    "..1221..",
    "..1221..",
    "...11..."};

inline constexpr const char* RING_16[] = {
    "................",
    ".....111111.....",
    "...1111111111...",
    "..111......111..",
    "..11........11..",
    ".11..........11.",
    ".11..........11.",
    ".11..........11.",
    ".11..........11.",
    ".11..........11.",
    ".11..........11.",
    "..11........11..",
    "..111......111..",
    "...1111111111...",
    ".....111111.....",
    "................"};

inline constexpr const char* DISC_16[] = {
    "................",
    "................",
    "................",
    ".....111111.....",
    "....11111111....",
    "...1111111111...",
    "...1111111111...",
    "...1111111111...",
    "...1111111111...",
    "...1111111111...",
    "...1111111111...",
    "....11111111....",
    ".....111111.....",
    "................",
    "................",
    "................"};

// ── Scenic backgrounds ─────────────────────────────────────────────────────

// Pixel dirt with a grass lip on top and deterministic pebbles.  Used by
// settings, loading and death screens.
template <class T>
inline void dirtBackground(T& ui, float w, float h) {
    rect(ui, 0.0f, 0.0f, w, h, DIRT_B);
    constexpr float tile = 32.0f;
    for (float y = 0.0f; y < h; y += tile) {
        for (float x = 0.0f; x < w; x += tile) {
            const int ix = static_cast<int>(x / tile);
            const int iy = static_cast<int>(y / tile);
            rect(ui, x, y, tile, tile, (ix + iy) % 2 ? DIRT_A : DIRT_B);
            rect(ui, x + 2, y + 2, tile - 4, tile - 4, DIRT_C);
            const int hash = (ix * 31 + iy * 17) & 7;
            if (hash == 0)
                rect(ui, x + 8 + (hash % 3) * 6, y + 10, 6, 4,
                     withAlpha(INK, 0.25f));
            else if (hash == 1)
                rect(ui, x + 12, y + 6, 4, 3, withAlpha(INK, 0.2f));
        }
    }
    rect(ui, 0.0f, h - 18.0f, w, 18.0f, GRASS);
    rect(ui, 0.0f, h - 18.0f, w, 4.0f, GRASS_LIGHT);
    for (float x = 0.0f; x < w; x += 8.0f) {
        const int ix = static_cast<int>(x / 8.0f);
        rect(ui, x + (ix % 2) * 4.0f, h - 22.0f, 4.0f, 4.0f, GRASS_DARK);
    }
}

// Layered pixel sky with a sun, deterministic block clouds, stepped mountain
// silhouettes and a dirt ground, used behind the main/pause menus.
template <class T>
inline void menuBackground(T& ui, float w, float h) {
    const float horizon = h * 0.42f;
    constexpr float band = 8.0f;
    const int bands = std::max(1, static_cast<int>((h - horizon) / band));
    for (int i = 0; i < bands; ++i) {
        const float t = static_cast<float>(i) /
            static_cast<float>(std::max(1, bands - 1));
        const glm::vec4 color = SKY_HORIZON * (1.0f - t) + SKY_TOP * t;
        rect(ui, 0.0f, horizon + i * band, w, band + 1.0f, color);
    }
    // Pixel sun with a two-ring glow.
    sprite(ui, w * 0.68f, h * 0.60f, 3.0f, DISC_16,
           {{PX_TRANSPARENT, glm::vec4(1.0f, 0.90f, 0.55f, 0.5f),
             PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
             PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT}});
    sprite(ui, w * 0.68f + 6.0f, h * 0.60f + 6.0f, 3.0f, DISC_16,
           {{PX_TRANSPARENT, glm::vec4(1.0f, 0.84f, 0.42f, 1.0f),
             PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT,
             PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT, PX_TRANSPARENT}});
    // Deterministic blocky clouds (16×8 blocks per cell).
    const auto cloud = [&](float cx, float cy, float s) {
        const auto blk = [&](float bx, float by, bool shade) {
            rect(ui, cx + bx * 16.0f * s, cy + by * 8.0f * s, 16.0f * s,
                 8.0f * s, shade ? CLOUD_SHADE : CLOUD);
        };
        blk(1, 2, false); blk(2, 2, false);
        blk(0, 1, false); blk(1, 1, false); blk(2, 1, false);
        blk(3, 1, false); blk(4, 1, false);
        blk(0, 0, true); blk(1, 0, true); blk(2, 0, true);
        blk(3, 0, true); blk(4, 0, true);
    };
    cloud(w * 0.12f, h * 0.66f, 0.9f);
    cloud(w * 0.55f, h * 0.74f, 1.1f);
    cloud(w * 0.30f, h * 0.82f, 0.7f);
    // Stepped mountains with snow caps.
    const auto mountain = [&](float apexX, float width, float height,
                              const glm::vec4& color) {
        constexpr float step = 8.0f;
        const int steps = std::max(1, static_cast<int>(height / step));
        for (int i = 0; i < steps; ++i) {
            const float t = static_cast<float>(i + 1) / steps;
            const float half = width * 0.5f * t;
            rect(ui, apexX - half, horizon + i * step, half * 2.0f, step + 1.0f,
                 color);
        }
        const float capH = std::min(2.0f * step, height);
        const float capHalf = width * 0.5f * (capH / height) * 0.7f;
        rect(ui, apexX - capHalf, horizon + height - capH, capHalf * 2.0f,
             capH, SNOW);
    };
    mountain(w * 0.18f, w * 0.30f, h * 0.14f, MOUNTAIN_NEAR);
    mountain(w * 0.52f, w * 0.46f, h * 0.24f, MOUNTAIN_NEAR);
    mountain(w * 0.86f, w * 0.36f, h * 0.17f, MOUNTAIN_NEAR);
    dirtBackground(ui, w, horizon + 2.0f);
}

// Rounded-corner darkening for dramatic screens (death, sleeping).
template <class T>
inline void vignette(T& ui, float w, float h, const glm::vec4& color,
                     int bands = 6, float step = 18.0f) {
    for (int i = 0; i < bands; ++i) {
        const float a = color.a * (1.0f - static_cast<float>(i) / bands);
        const glm::vec4 c(color.r, color.g, color.b, a);
        rect(ui, 0.0f, i * step, w, step, c);
        rect(ui, 0.0f, h - (i + 1) * step, w, step, c);
        rect(ui, i * step, (i + 1) * step, step, h - 2.0f * (i + 1) * step, c);
        rect(ui, w - (i + 1) * step, (i + 1) * step, step,
             h - 2.0f * (i + 1) * step, c);
    }
}

// ── Tooltips ───────────────────────────────────────────────────────────────

inline std::string tooltipDetail(const ItemStack& stack,
                                 const Localization* localization) {
    if (stack.empty()) return {};
    const auto& props = getItemProps(stack.id);
    std::string detail = localization ? localization->itemName(stack.id)
                                      : props.name;
    if (stack.count > 1) detail += " x" + std::to_string(stack.count);
    if (props.maxDurability) {
        detail += "  " +
            std::to_string(props.maxDurability -
                           std::min(props.maxDurability, stack.damage)) +
            "/" + std::to_string(props.maxDurability);
    } else if (props.kind == ItemKind::Armor) {
        detail += "  " + (localization ? localization->text("tooltip.armor")
                                       : std::string("Armor"));
    } else if (props.attackDamage > 0.0f) {
        detail += "  " +
            (localization
                 ? localization->format("tooltip.damage",
                    {std::to_string(static_cast<int>(props.attackDamage))})
                 : "Damage " + std::to_string(static_cast<int>(props.attackDamage)));
    } else if (props.food > 0) {
        detail += "  " +
            (localization
                 ? localization->format("tooltip.food", {std::to_string(props.food)})
                 : "Food +" + std::to_string(props.food));
    }
    return detail;
}

template <class T>
inline void tooltip(T& ui, float x, float y, const std::string& text,
                    float scale = 0.9f) {
    if (text.empty()) return;
    const auto size = ui.measureText(text, scale);
    panel(ui, x, y, size.x + 14.0f, size.y + 12.0f, TOOLTIP_FILL);
    textWithShadow(ui, text, x + 7.0f, y + 6.0f, scale,
                   glm::vec3(0.95f, 0.90f, 1.0f));
}

} // namespace UiTheme
