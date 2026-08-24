#pragma once

#include <array>
#include <filesystem>

#include "core/Input.h"
#include "core/Platform.h"
#include "game/Language.h"
#include "renderer/Shadow.h"
#include "renderer/VisualQuality.h"
#include "world/LodSettings.h"

enum class ControlMode : uint8_t { Auto, KeyboardMouse, Touch };
enum class AttackIndicator : uint8_t { Crosshair, Hotbar, Off };

struct ClientSettings {
    static constexpr int FORMAT_VERSION = 19;
    static constexpr int MIN_FRAME_RATE = 30;
    static constexpr int MAX_FRAME_RATE = 200;
    static constexpr int MIN_LOD_DISTANCE = 32;
    static constexpr int MAX_LOD_DISTANCE = 4096;
    static constexpr int LOD_WARNING_DISTANCE = 512;

    int renderDistance = 8;
    bool lodEnabled = true;
    int lodDistanceChunks = 128;
    LodAggressiveness lodAggressiveness = LodAggressiveness::Balanced;
    LodPrecision lodPrecision = LodPrecision::Medium;
    bool renderClouds = true;
    int cloudRenderDistance = 192;
    int dayCycleMinutes = 20;
    bool autoJump = true;
    float mouseSensitivity = 0.15f;
    bool invertMouseY = false;
    bool smoothLighting = true;
    ShadowQuality shadowQuality = ShadowQuality::Medium;
    VisualQuality visualQuality = VisualQuality::Medium;
    int guiScale = 0; // 0 = Auto
    int frameRateLimit = MAX_FRAME_RATE;
    AttackIndicator attackIndicator = AttackIndicator::Crosshair;
    Language language = Language::English;
    ControlMode controlMode = ControlMode::Auto;
    float touchSensitivity = 1.5f;
    float touchControlSize = 1.0f;
    float touchControlOpacity = 0.65f;
    bool touchLeftHanded = false;
    float gamepadDeadzone = 0.18f;
    float gamepadLookSensitivity = 1.0f;
    bool invertGamepadY = false;
    float gamepadRumble = 1.0f;
    std::array<InputBinding, INPUT_ACTION_COUNT> bindings{};
    std::array<GamepadBinding, INPUT_ACTION_COUNT> gamepadBindings{};

    ClientSettings();
    static ClientSettings load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    void resetBindings();
    void resetGamepadBindings();
    void validate();
};

int effectiveGuiScale(int framebufferWidth, int framebufferHeight, int configuredScale);
VisualQuality defaultVisualQuality(DesktopPlatform platform);
