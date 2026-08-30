#pragma once

#include "ui/Menu.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

enum class SettingsPage {
    General,
    Video,
    Lod,
    KeyBindings,
    KeyboardMouse,
    Controller,
    Touch
};

inline SettingsPage settingsParentPage(SettingsPage page) {
    switch (page) {
        case SettingsPage::KeyboardMouse:
        case SettingsPage::Controller:
        case SettingsPage::Touch:
            return SettingsPage::KeyBindings;
        case SettingsPage::Video:
            return SettingsPage::General;
        case SettingsPage::Lod:
            return SettingsPage::Video;
        case SettingsPage::KeyBindings:
        case SettingsPage::General:
            return SettingsPage::General;
    }
    return SettingsPage::General;
}

struct SettingsButtonLayout {
    float helpY = 0.0f;
    float firstButtonY = 0.0f;
    float buttonHeight = 0.0f;
    float buttonWidth = 0.0f;
    float leftX = 0.0f;
    float columnGap = 0.0f;
    size_t rowCount = 0;
};

inline SettingsButtonLayout settingsButtonLayout(
    float screenWidth, float titleY, size_t buttonCount, bool hasHelp,
    bool standaloneLast = false) {
    constexpr float horizontalMargin = 14.0f;
    constexpr float columnGap = 8.0f;
    const float helpY = titleY - 34.0f;
    const float contentTop = hasHelp ? helpY - 10.0f : titleY - 18.0f;
    const size_t contentCount = buttonCount -
        (standaloneLast && buttonCount > 0 ? 1 : 0);
    const size_t contentRows = (contentCount + 1) / 2;
    const size_t rowCount = std::max<size_t>(
        1, contentRows + (standaloneLast && buttonCount > 0 ? 1 : 0));
    const float buttonHeight = std::clamp(
        (contentTop - 14.0f) / rowCount - 5.0f,
        22.0f, Config::UI_BUTTON_HEIGHT);
    const float buttonWidth = std::max(1.0f, std::min(
        Config::UI_BUTTON_WIDTH,
        (screenWidth - horizontalMargin * 2.0f - columnGap) * 0.5f));
    return {helpY, contentTop - buttonHeight, buttonHeight, buttonWidth,
            (screenWidth - buttonWidth * 2.0f - columnGap) * 0.5f,
            columnGap, rowCount};
}

inline glm::vec2 settingsButtonPosition(
    const SettingsButtonLayout& layout, size_t index, size_t buttonCount,
    bool standaloneLast = false) {
    const bool isStandaloneLast = standaloneLast && buttonCount > 0 &&
        index + 1 == buttonCount;
    const size_t contentCount = buttonCount - (standaloneLast ? 1 : 0);
    const size_t row = isStandaloneLast ? (contentCount + 1) / 2 : index / 2;
    const bool centeredLast = isStandaloneLast ||
        (buttonCount % 2 == 1 && index + 1 == buttonCount);
    const float x = centeredLast
        ? layout.leftX + (layout.buttonWidth + layout.columnGap) * 0.5f
        : layout.leftX + static_cast<float>(index % 2) *
            (layout.buttonWidth + layout.columnGap);
    return {x, layout.firstButtonY - static_cast<float>(row) *
        (layout.buttonHeight + 5.0f)};
}

inline int settingsGridNeighbor(int current, size_t buttonCount,
                                int columnDelta, int rowDelta,
                                bool standaloneLast = false) {
    if (buttonCount == 0) return 0;
    const int count = static_cast<int>(buttonCount);
    const int contentCount = count - (standaloneLast ? 1 : 0);
    current = std::clamp(current, 0, count - 1);
    if (standaloneLast && current == count - 1) {
        if (columnDelta != 0 || rowDelta == 0) return current;
        return rowDelta > 0 ? 0 : std::max(0, contentCount - 1);
    }
    if (columnDelta != 0) {
        const int neighbor = current + (columnDelta > 0 ? 1 : -1);
        return neighbor >= 0 && neighbor < contentCount &&
            neighbor / 2 == current / 2
            ? neighbor : current;
    }
    if (rowDelta == 0) return current;
    const int column = current % 2;
    int neighbor = current + (rowDelta > 0 ? 2 : -2);
    if (neighbor >= 0 && neighbor < contentCount) return neighbor;
    if (standaloneLast && neighbor >= contentCount) return count - 1;
    if (standaloneLast && neighbor < 0) return count - 1;
    if (rowDelta > 0) return std::min(column, count - 1);
    const int lastInColumn = ((count - 1 - column) / 2) * 2 + column;
    return lastInColumn < count ? lastInColumn : std::max(0, lastInColumn - 2);
}

inline int frameRateFromSlider(float x, float left, float width) {
    if (width <= 0.0f) return ClientSettings::MIN_FRAME_RATE;
    const float position = std::clamp((x - left) / width, 0.0f, 1.0f);
    return ClientSettings::MIN_FRAME_RATE + static_cast<int>(std::lround(
        position * (ClientSettings::MAX_FRAME_RATE - ClientSettings::MIN_FRAME_RATE)));
}

inline float frameRateSliderFraction(int frameRate) {
    return static_cast<float>(std::clamp(
        frameRate, ClientSettings::MIN_FRAME_RATE, ClientSettings::MAX_FRAME_RATE) -
        ClientSettings::MIN_FRAME_RATE) /
        static_cast<float>(ClientSettings::MAX_FRAME_RATE - ClientSettings::MIN_FRAME_RATE);
}

inline std::optional<int> parseLodDistance(const std::string& text) {
    if (text.empty() || !std::all_of(text.begin(), text.end(),
            [](unsigned char character) { return character >= '0' && character <= '9'; }))
        return std::nullopt;
    try {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size() || value < ClientSettings::MIN_LOD_DISTANCE ||
            value > ClientSettings::MAX_LOD_DISTANCE) return std::nullopt;
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool lodDistanceNeedsWarning(int value) {
    return value > ClientSettings::LOD_WARNING_DISTANCE;
}

class SettingsMenu : public Menu {
public:
    SettingsMenu(ClientSettings& settings, std::function<void()> onChanged,
                 std::function<void()> onBack, const Localization& localization);

    void render(UIRenderer& ui, int screenWidth, int screenHeight) override;
    void onKeyPress(int key, int mods = 0) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    void onScroll(double yOffset) override;
    void onChar(unsigned int codepoint) override;
    bool wantsTextInput() const override { return m_lodDistanceEditing; }
    bool capturesPointerDrag(double x, double y) const override;
    bool capturingGamepad() const {
        return m_page == SettingsPage::Controller && m_captureAction >= 0;
    }
    bool capturingKeyboardMouse() const {
        return m_page == SettingsPage::KeyboardMouse && m_captureAction >= 0;
    }
    void onGamepadBinding(GamepadBinding binding);

private:
    std::vector<Button> m_buttons;
    int m_selectedIdx = 0;
    std::function<void()> m_onBack;
    std::function<void()> m_onChanged;
    ClientSettings& m_settings;
    const Localization& m_localization;
    SettingsPage m_page = SettingsPage::General;
    int m_controlOffset = 0;
    int m_captureAction = -1;
    int m_pressedButton = -1;
    int m_frameRateButton = -1;
    int m_backButton = -1;
    bool m_frameRateDragging = false;
    TextEditBuffer m_lodDistanceText{{}, 4};
    bool m_lodDistanceEditing = false;
    bool m_lodDistanceInvalid = false;
    bool m_lodWarningPending = false;
    int m_pendingLodDistance = 0;

    void cycleRenderDistance();
    void toggleCloudRendering();
    void cycleCloudRenderDistance();
    void cycleDayCycle();
    void toggleAutoJump();
    std::string labelForRenderDist() const;
    std::string labelForCloudRenderDist() const;
    std::string labelForDayCycle() const;
    std::string labelForAutoJump() const;
    void showPage(SettingsPage page);
    void refreshButtons();
    void assignBinding(InputBinding binding);
    void assignGamepadBinding(GamepadBinding binding);
    std::string frameRateLabel() const;
    void setFrameRateFromPointer(double x);
    void beginLodDistanceEdit();
    void commitLodDistanceEdit();
};
