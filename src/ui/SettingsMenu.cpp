#include "ui/SettingsMenu.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

SettingsMenu::SettingsMenu(ClientSettings& settings,
                           std::function<void()> onChanged,
                           std::function<void()> onBack,
                           const Localization& localization)
    : m_onBack(std::move(onBack)), m_onChanged(std::move(onChanged)),
      m_settings(settings), m_localization(localization) { refreshButtons(); }

std::string SettingsMenu::labelForRenderDist() const {
    return m_localization.format(
        "settings.render_distance", {std::to_string(m_settings.renderDistance)});
}
std::string SettingsMenu::labelForCloudRenderDist() const {
    return m_localization.format(
        "settings.cloud_distance", {std::to_string(m_settings.cloudRenderDistance)});
}
std::string SettingsMenu::labelForDayCycle() const {
    return m_settings.dayCycleMinutes == 0
        ? m_localization.text("settings.day_static")
        : m_localization.format(
            "settings.day_minutes", {std::to_string(m_settings.dayCycleMinutes)});
}
std::string SettingsMenu::labelForAutoJump() const {
    return m_localization.format("settings.auto_jump", {m_localization.text(
        m_settings.autoJump ? "common.on" : "common.off")});
}

void SettingsMenu::cycleRenderDistance() {
    constexpr int options[] = {2,4,6,8,10,12,16};
    auto it = std::find(std::begin(options), std::end(options), m_settings.renderDistance);
    m_settings.renderDistance = options[(it == std::end(options) ? 0 :
        (static_cast<int>(it - std::begin(options)) + 1) % 7)];
    m_onChanged(); refreshButtons();
}
void SettingsMenu::toggleCloudRendering() {
    m_settings.renderClouds = !m_settings.renderClouds;
    m_onChanged(); refreshButtons();
}
void SettingsMenu::cycleCloudRenderDistance() {
    constexpr int options[] = {64,96,128,192,256,512,1024};
    auto it = std::find(std::begin(options), std::end(options),
                        m_settings.cloudRenderDistance);
    m_settings.cloudRenderDistance = options[(it == std::end(options) ? 0 :
        (static_cast<int>(it - std::begin(options)) + 1) % 7)];
    m_onChanged(); refreshButtons();
}
void SettingsMenu::cycleDayCycle() {
    constexpr int options[] = {0,10,20,40};
    auto it = std::find(std::begin(options), std::end(options), m_settings.dayCycleMinutes);
    m_settings.dayCycleMinutes = options[(it == std::end(options) ? 0 :
        (static_cast<int>(it - std::begin(options)) + 1) % 4)];
    m_onChanged(); refreshButtons();
}
void SettingsMenu::toggleAutoJump() {
    m_settings.autoJump = !m_settings.autoJump; m_onChanged(); refreshButtons();
}

void SettingsMenu::refreshButtons() {
    m_buttons.clear();
    if (m_page == Page::General) {
        m_buttons.emplace_back(labelForDayCycle(), [this]{ cycleDayCycle(); });
        m_buttons.emplace_back(labelForAutoJump(), [this]{ toggleAutoJump(); });
        std::ostringstream sensitivity;
        sensitivity << std::fixed << std::setprecision(2) << m_settings.mouseSensitivity;
        m_buttons.emplace_back(m_localization.format(
            "settings.sensitivity", {sensitivity.str()}), [this]{
            m_settings.mouseSensitivity += 0.05f;
            if (m_settings.mouseSensitivity > 0.50f) m_settings.mouseSensitivity = 0.05f;
            m_onChanged(); refreshButtons();
        });
        m_buttons.emplace_back(m_localization.format("settings.invert_y", {
            m_localization.text(m_settings.invertMouseY ? "common.on" : "common.off")}), [this]{
                m_settings.invertMouseY = !m_settings.invertMouseY; m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.raw_mouse", {
            m_localization.text(m_settings.rawMouseInput ? "common.on" : "common.off")}), [this]{
                m_settings.rawMouseInput = !m_settings.rawMouseInput; m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.text("settings.video"), [this]{
            m_page = Page::Video; m_selectedIdx = 0; refreshButtons();
        });
        m_buttons.emplace_back(m_localization.text("settings.controls"), [this]{
            m_page = Page::Controls; m_selectedIdx = 0; refreshButtons();
        });
        m_buttons.emplace_back(m_localization.text("common.back"), m_onBack);
    } else if (m_page == Page::Video) {
        m_buttons.emplace_back(labelForRenderDist(), [this]{ cycleRenderDistance(); });
        m_buttons.emplace_back(m_localization.format("settings.clouds", {
            m_localization.text(m_settings.renderClouds ? "common.on" : "common.off")}),
            [this]{ toggleCloudRendering(); });
        m_buttons.emplace_back(labelForCloudRenderDist(),
                               [this]{ cycleCloudRenderDistance(); });
        m_buttons.emplace_back(m_localization.format("settings.smooth_lighting", {
            m_localization.text(m_settings.smoothLighting ? "common.on" : "common.off")}), [this]{
                m_settings.smoothLighting = !m_settings.smoothLighting;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.gui_scale", {
            m_settings.guiScale == 0 ? m_localization.text("common.auto") :
            std::to_string(m_settings.guiScale) + "x"}), [this]{
                m_settings.guiScale = (m_settings.guiScale + 1) % 5;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.text("settings.back"), [this]{
            m_page = Page::General; m_selectedIdx = 0; refreshButtons();
        });
    } else {
        constexpr int visible = 8;
        const int end = std::min<int>(INPUT_ACTION_COUNT, m_controlOffset + visible);
        for (int i = m_controlOffset; i < end; ++i) {
            const InputAction action = static_cast<InputAction>(i);
            const std::string label = m_localization.actionName(action) + ": " +
                (m_captureAction == i ? m_localization.text("settings.capture") :
                 m_localization.bindingName(m_settings.bindings[static_cast<size_t>(i)]));
            m_buttons.emplace_back(label, [this, i]{ m_captureAction = i; refreshButtons(); });
        }
        m_buttons.emplace_back(m_localization.text("settings.reset"), [this]{
            m_settings.resetBindings(); m_onChanged(); refreshButtons();
        });
        m_buttons.emplace_back(m_localization.text("settings.back"), [this]{
            m_captureAction = -1; m_page = Page::General;
            m_selectedIdx = 0; refreshButtons();
        });
    }
    m_selectedIdx = std::clamp(m_selectedIdx, 0, std::max(0, static_cast<int>(m_buttons.size()) - 1));
    if (!m_buttons.empty()) m_buttons[static_cast<size_t>(m_selectedIdx)].setSelected(true);
}

void SettingsMenu::assignBinding(InputBinding binding) {
    if (m_captureAction < 0) return;
    m_settings.bindings[static_cast<size_t>(m_captureAction)] = binding;
    m_captureAction = -1;
    m_settings.validate();
    m_onChanged();
    refreshButtons();
}

void SettingsMenu::render(UIRenderer& ui, int width, int height) {
    ui.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height),
                Config::UIColors::BACKGROUND);
    const std::string title = m_localization.text(
        m_page == Page::Controls ? "settings.controls_title" :
        m_page == Page::Video ? "settings.video_title" : "settings.title");
    const auto titleSize = ui.measureText(title, 3.0f);
    const float titleY = height * 0.78f;
    ui.renderText(title, (width - titleSize.x) * .5f, titleY, 3.0f,
                  Config::UIColors::TEXT_TITLE);
    if (m_page == Page::Controls)
        {
            const std::string help = m_localization.text("settings.controls_help");
            const auto helpSize = ui.measureText(help, 1.0f);
            ui.renderText(help, (width - helpSize.x) * .5f,
                          titleY - 34.0f, 1.0f, glm::vec3(.72f));
        }
    const float startY = titleY - 58.0f;
    const float x = (width - Config::UI_BUTTON_WIDTH) * .5f;
    const float buttonHeight = std::clamp(
        (startY - 14.0f) / std::max<size_t>(1, m_buttons.size()) - 5.0f,
        22.0f, Config::UI_BUTTON_HEIGHT);
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const float y = startY - i * (buttonHeight + 5.0f);
        m_buttons[i].setPosition(x, y);
        m_buttons[i].setSize(Config::UI_BUTTON_WIDTH, buttonHeight);
        m_buttons[i].render(ui);
    }
}

void SettingsMenu::onKeyPress(int key) {
    if (m_captureAction >= 0) {
        if (key == GLFW_KEY_ESCAPE) { m_captureAction = -1; refreshButtons(); }
        else if (key == GLFW_KEY_BACKSPACE &&
                 inputActionCanUnbind(static_cast<InputAction>(m_captureAction)))
            assignBinding({});
        else assignBinding({InputDevice::Keyboard, key});
        return;
    }
    switch (key) {
        case GLFW_KEY_UP: case GLFW_KEY_W: navigateUp(m_buttons, m_selectedIdx); break;
        case GLFW_KEY_DOWN: case GLFW_KEY_S: navigateDown(m_buttons, m_selectedIdx); break;
        case GLFW_KEY_ENTER: case GLFW_KEY_SPACE: activateSelected(m_buttons, m_selectedIdx); break;
        case GLFW_KEY_ESCAPE:
            if (m_page != Page::General) {
                m_captureAction = -1;
                m_page = Page::General;
                m_selectedIdx = 0;
                refreshButtons();
            } else m_onBack();
            break;
        default: break;
    }
}

void SettingsMenu::onMouseMove(double x, double y) {
    for (auto& button : m_buttons)
        button.setHovered(button.containsPoint(static_cast<float>(x), static_cast<float>(y)));
}

void SettingsMenu::onMouseButton(int button, int action, double x, double y) {
    if (m_captureAction >= 0 && action == GLFW_PRESS) {
        assignBinding({InputDevice::Mouse, button});
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
                m_pressedButton = static_cast<int>(i); m_buttons[i].setPressed(true); return;
            }
    } else if (action == GLFW_RELEASE && m_pressedButton >= 0) {
        const int captured = m_pressedButton; m_pressedButton = -1;
        m_buttons[static_cast<size_t>(captured)].setPressed(false);
        if (m_buttons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y)))
            m_buttons[static_cast<size_t>(captured)].activate();
    }
}

void SettingsMenu::onScroll(double yOffset) {
    if (m_captureAction >= 0) { assignBinding({InputDevice::Wheel, yOffset > 0 ? 1 : -1}); return; }
    if (m_page != Page::Controls) return;
    const int maximum = std::max(0, static_cast<int>(INPUT_ACTION_COUNT) - 8);
    m_controlOffset = std::clamp(m_controlOffset + (yOffset < 0 ? 1 : -1), 0, maximum);
    refreshButtons();
}
