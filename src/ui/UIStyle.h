#pragma once

// ── Modern block UI theme ─────────────────────────────────────────────────────────
//
// Shared, backend-neutral visual theme for every menu, inventory, container
// and HUD surface.  Surfaces use rounded colored geometry
// and smooth text so all UI elements share identical output without
// any new texture assets.  Every helper is a template over any object that
// exposes drawRect/drawRoundedRect/renderTextAlpha/measureText (UIRenderer and its backend).

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
#include "game/TextWrap.h"

namespace UiTheme {

// Shared logical-pixel design tokens. Item/status sprites remain pixel art.
inline constexpr float PANEL_RADIUS = 10.0f;
inline constexpr float BUTTON_RADIUS = 6.0f;
inline constexpr float SLOT_RADIUS = 4.0f;
inline constexpr float BODY_SCALE = 1.15f;
inline constexpr float TITLE_SCALE = 2.4f;
inline constexpr float CAPTION_SCALE = 0.9f;
inline constexpr glm::vec4 INK(0.055f,0.071f,0.086f,1);
inline constexpr glm::vec4 BORDER(0.23f,0.29f,0.34f,1);
inline constexpr glm::vec4 PANEL(0.133f,0.169f,0.20f,0.98f);
inline constexpr glm::vec4 PANEL_DEEP(0.09f,0.114f,0.137f,0.98f);
inline constexpr glm::vec4 BUTTON(0.18f,0.224f,0.263f,1);
inline constexpr glm::vec4 BUTTON_HOVER(0.235f,0.294f,0.333f,1);
inline constexpr glm::vec4 BUTTON_SELECTED(0.20f,0.32f,0.29f,1);
inline constexpr glm::vec4 BUTTON_PRESSED(0.115f,0.169f,0.165f,1);
inline constexpr glm::vec4 BUTTON_DANGER(0.36f,0.18f,0.20f,1);
inline constexpr glm::vec4 BUTTON_DANGER_HOVER(0.48f,0.22f,0.24f,1);
inline constexpr glm::vec4 ACCENT(0.396f,0.788f,0.569f,1);
inline constexpr glm::vec4 ACCENT_DIM(0.23f,0.46f,0.35f,1);
inline constexpr glm::vec4 SLOT(0.09f,0.122f,0.145f,1);
inline constexpr glm::vec4 SLOT_HOVER(0.18f,0.25f,0.27f,1);
inline constexpr glm::vec3 TEXT(0.929f,0.949f,0.961f);
inline constexpr glm::vec3 TEXT_DIM(0.655f,0.702f,0.745f);
inline constexpr glm::vec3 TEXT_TITLE = TEXT;
inline constexpr glm::vec3 TEXT_HOVER(0.91f,1.0f,0.95f);
inline constexpr glm::vec4 OVERLAY(0.025f,0.04f,0.05f,0.64f);
inline constexpr glm::vec4 TOOLTIP_FILL(0.075f,0.102f,0.122f,0.98f);

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
    ACCENT,                                    // 1 arrow body
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
                           float alpha = 1.0f, float = 1.0f, float = -1.0f) {
    if (!text.empty()) ui.renderTextAlpha(text,x,y,scale,color,alpha);
}

template <class T>
inline float fittedScale(T& ui,const std::string& label,float scale,float width) {
    const float measured=ui.measureText(label,scale).x;
    return measured>width && measured>0 ? scale*std::max(0.0f,width)/measured : scale;
}

template <class T>
inline void rounded(T& ui,float x,float y,float w,float h,float radius,
                    const glm::vec4& color) {
    if (w>0 && h>0) ui.drawRoundedRect(x,y,w,h,radius,color);
}

template <class T>
inline void outline(T& ui,float x,float y,float w,float h,float radius,
                    const glm::vec4& fill,const glm::vec4& border,float thickness=1) {
    rounded(ui,x,y,w,h,radius,border);
    rounded(ui,x+thickness,y+thickness,w-2*thickness,h-2*thickness,
            std::max(0.0f,radius-thickness),fill);
}

// Retained helper name for existing slider handles; now a flat rounded surface.
template <class T>
inline void beveledBody(T& ui,float x,float y,float w,float h,
                        const glm::vec4& fill,bool pressed,float alpha=1) {
    outline(ui,x,y,w,h,BUTTON_RADIUS,withAlpha(fill,alpha),
            withAlpha(pressed?ACCENT_DIM:BORDER,alpha));
}

template <class T>
inline void panel(T& ui,float x,float y,float w,float h,
                  const glm::vec4& fill=PANEL,const std::string& title={},
                  float titleScale=1.6f,float alpha=1) {
    rounded(ui,x,y-3,w,h,PANEL_RADIUS+1,glm::vec4(0,0,0,.16f*alpha));
    outline(ui,x,y,w,h,PANEL_RADIUS,withAlpha(fill,alpha),withAlpha(BORDER,alpha*.7f));
    if (title.empty() || h<34) return;
    const float headH=std::min(38.0f,h*.28f);
    titleScale=fittedScale(ui,title,titleScale,w-32);
    const auto size=ui.measureText(title,titleScale);
    textWithShadow(ui,title,x+16,y+h-headH+(headH-size.y)*.5f,titleScale,TEXT,alpha);
    rect(ui,x+16,y+h-headH,w-32,1,withAlpha(BORDER,alpha*.65f));
}

template <class T>
inline void button(T& ui,float x,float y,float w,float h,
                   const std::string& label,WidgetState state,bool danger=false,
                   float textScale=0,float alpha=1,float hoverBlend=-1,
                   float pressBlend=-1,bool primary=false,float bottomInset=0) {
    const float hover=hoverBlend<0?(state==WidgetState::Hover||state==WidgetState::Selected?1:0):hoverBlend;
    const float press=pressBlend<0?(state==WidgetState::Pressed?1:0):pressBlend;
    const auto normal=danger?BUTTON_DANGER:primary?ACCENT_DIM:BUTTON;
    const auto lit=danger?BUTTON_DANGER_HOVER:primary?glm::vec4(.28f,.58f,.43f,1):BUTTON_HOVER;
    glm::vec4 fill=glm::mix(normal,lit,hover);
    fill=glm::mix(fill,danger?BUTTON_DANGER:BUTTON_PRESSED,press);
    const auto border=state==WidgetState::Selected?ACCENT:
        state==WidgetState::Pressed?ACCENT_DIM:BORDER;
    outline(ui,x,y,w,h,BUTTON_RADIUS,withAlpha(fill,alpha),withAlpha(border,alpha),
            state==WidgetState::Selected?2.0f:1.0f);
    if (textScale<=0) textScale=std::clamp((h-bottomInset-12)/14,0.85f,1.35f);
    textScale=fittedScale(ui,label,textScale,w-24);
    const auto size=ui.measureText(label,textScale);
    textWithShadow(ui,label,x+(w-size.x)*.5f,y+(h-size.y+bottomInset)*.5f-press,
                   textScale,hover>.5f?TEXT_HOVER:TEXT,alpha);
}

template <class T>
inline void slot(T& ui,float x,float y,float w,float h,WidgetState state,
                 const glm::vec4& fill=SLOT,float alpha=1) {
    outline(ui,x,y,w,h,SLOT_RADIUS,
            withAlpha(state==WidgetState::Hover?SLOT_HOVER:fill,alpha),
            withAlpha(state==WidgetState::Selected?ACCENT:BORDER,alpha),
            state==WidgetState::Selected?2.0f:1.0f);
}

template <class T>
inline void progressBar(T& ui,float x,float y,float w,float h,float fraction,
                        const glm::vec4& color,float alpha=1) {
    if (w<=0 || h<=0) return;
    fraction=std::clamp(fraction,0.0f,1.0f);
    rounded(ui,x,y,w,h,std::min(4.0f,h*.5f),withAlpha(SLOT,alpha));
    const float filled=(w-2)*fraction;
    if (filled>0 && h>2)
        rounded(ui,x+1,y+1,filled,h-2,std::min(3.0f,(h-2)*.5f),withAlpha(color,alpha));
}

template <class T>
inline void scrollBar(T& ui,float x,float y,float w,float h,
                      int offset,int visible,int total) {
    if (h<=0 || total<=visible) return;
    rounded(ui,x,y,w,h,w*.5f,SLOT);
    const float thumbH=std::min(h,std::max(12.0f,h*std::max(0,visible)/std::max(1,total)));
    const float fraction=std::clamp(static_cast<float>(offset)/std::max(1,total-visible),0.0f,1.0f);
    rounded(ui,x,y+(h-thumbH)*(1-fraction),w,thumbH,w*.5f,ACCENT_DIM);
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
                   float alpha = 1.0f) {
    spriteRows(ui, x, y, px, rows, N, palette, alpha);
}

template <class T>
inline void sprite(T& ui, float x, float y, float px,
                   std::initializer_list<const char*> rows,
                   const std::array<glm::vec4, 10>& palette,
                   float alpha = 1.0f) {
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
// Quiet block landscape with bounded decoration independent of resolution.
template <class T>
inline void menuBackground(T& ui,float w,float h) {
    rect(ui,0,0,w,h,PANEL_DEEP);
    constexpr int bands=32;
    for (int i=0;i<bands;++i) {
        const float t=static_cast<float>(i)/bands;
        rect(ui,0,h*t,w,h/bands+1,
             glm::mix(glm::vec4(.14f,.23f,.25f,1),glm::vec4(.065f,.10f,.14f,1),t));
    }
    for (int layer=0;layer<3;++layer) {
        const glm::vec4 color=glm::mix(glm::vec4(.15f,.25f,.26f,1),PANEL_DEEP,layer*.38f);
        for (int i=0;i<32;++i) {
            const float t=static_cast<float>(i)/32;
            const float wave=.5f+.3f*std::sin(t*13+layer*2)+.2f*std::sin(t*29+layer);
            const float top=h*(.10f+(2-layer)*.075f+wave*.12f);
            rect(ui,w*t,0,w/32+1,top,color);
        }
    }
    rect(ui,0,0,w,h,glm::vec4(.025f,.04f,.05f,.25f));
}

template <class T>
inline void dirtBackground(T& ui,float w,float h) { menuBackground(ui,w,h); }

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
    auto number = [](float value) {
        const float rounded = std::round(value);
        if (std::abs(value - rounded) < 0.001f)
            return std::to_string(static_cast<int>(rounded));
        std::string text = std::to_string(value);
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        return text;
    };
    if (stack.count > 1) detail += " x" + std::to_string(stack.count);
    if (props.maxDurability) {
        detail += "  " +
            std::to_string(props.maxDurability -
                           std::min(props.maxDurability, stack.damage)) +
            "/" + std::to_string(props.maxDurability);
    } else if (props.kind == ItemKind::Armor) {
        detail += "  " + (localization ? localization->text("tooltip.armor")
                                       : std::string("Armor"));
    }
    if (props.attackDamage > 0.0f && props.attackSpeed > 0.0f) {
        detail += "  " +
            (localization
                 ? localization->format("tooltip.damage",
                    {number(props.attackDamage)})
                 : "Damage " + number(props.attackDamage));
        detail += "  " +
            (localization
                 ? localization->format("tooltip.attack_speed",
                    {number(props.attackSpeed)})
                 : "Speed " + number(props.attackSpeed));
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
    const float width=std::max(1.0f,ui.canvasWidth()-32.0f);
    const auto lines=wrapTextPixels(text,width,[&](const std::string& value){
        return ui.measureText(value,scale).x;
    });
    std::string wrapped;
    for (const auto& line:lines) { if (!wrapped.empty()) wrapped+='\n';wrapped+=line; }
    auto size=ui.measureText(wrapped,scale);
    if (size.y+16>ui.canvasHeight()-8) {
        scale*=std::max(1.0f,ui.canvasHeight()-24.0f)/std::max(1.0f,size.y);
        size=ui.measureText(wrapped,scale);
    }
    x=std::clamp(x,4.0f,std::max(4.0f,ui.canvasWidth()-size.x-20));
    y=std::clamp(y,4.0f,std::max(4.0f,ui.canvasHeight()-size.y-20));
    panel(ui,x,y,size.x+16,size.y+16,TOOLTIP_FILL);
    textWithShadow(ui,wrapped,x+8,y+8,scale,TEXT);
}

} // namespace UiTheme
