#pragma once

#include <array>
#include <filesystem>

#include "core/Input.h"
#include "game/Language.h"

enum class ControlMode : uint8_t { Auto, KeyboardMouse, Touch };

struct ClientSettings {
    static constexpr int FORMAT_VERSION = 5;

    int renderDistance = 8;
    bool renderClouds = true;
    int cloudRenderDistance = 192;
    int dayCycleMinutes = 20;
    bool autoJump = true;
    float mouseSensitivity = 0.15f;
    bool invertMouseY = false;
    bool rawMouseInput = true;
    bool smoothLighting = true;
    int guiScale = 0; // 0 = Auto
    Language language = Language::English;
    ControlMode controlMode = ControlMode::Auto;
    float touchSensitivity = 1.0f;
    float touchControlSize = 1.0f;
    float touchControlOpacity = 0.65f;
    bool touchLeftHanded = false;
    std::array<InputBinding, INPUT_ACTION_COUNT> bindings{};

    ClientSettings();
    static ClientSettings load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    void resetBindings();
    void validate();
};

int effectiveGuiScale(int framebufferWidth, int framebufferHeight, int configuredScale);
