#pragma once

#include <array>
#include <filesystem>

#include "core/Input.h"

struct ClientSettings {
    static constexpr int FORMAT_VERSION = 1;

    int renderDistance = 8;
    int cloudRenderDistance = 192;
    int dayCycleMinutes = 20;
    bool autoJump = true;
    float mouseSensitivity = 0.15f;
    bool invertMouseY = false;
    bool rawMouseInput = true;
    int guiScale = 0; // 0 = Auto
    std::array<InputBinding, INPUT_ACTION_COUNT> bindings{};

    ClientSettings();
    static ClientSettings load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    void resetBindings();
    void validate();
};

int effectiveGuiScale(int framebufferWidth, int framebufferHeight, int configuredScale);
