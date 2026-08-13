#include "ui/SettingsMenu.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

SettingsMenu::SettingsMenu(ClientSettings& settings,
                           std::function<void()> onChanged,
                           std::function<void()> onBack,
                           const Localization& localization,
                           RendererBackendAvailability renderers)
    : m_onBack(std::move(onBack)), m_onChanged(std::move(onChanged)),
      m_settings(settings), m_localization(localization),
      m_renderers(renderers) { refreshButtons(); }

std::string SettingsMenu::labelForRenderDist() const {
    return m_localization.format(
        "settings.render_distance", {std::to_string(m_settings.renderDistance)});
}
std::string SettingsMenu::frameRateLabel() const {
    return m_localization.format(
        "settings.frame_rate", {std::to_string(m_settings.frameRateLimit)});
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

void SettingsMenu::showPage(SettingsPage page) {
    m_page = page;
    m_selectedIdx = 0;
    m_controlOffset = 0;
    m_captureAction = -1;
    refreshButtons();
}

void SettingsMenu::refreshButtons() {
    m_buttons.clear();
    m_frameRateButton = -1;
    m_frameRateDragging = false;
    if (m_page == SettingsPage::General) {
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
        m_buttons.emplace_back(m_localization.text("settings.video"), [this]{
            showPage(SettingsPage::Video);
        });
        m_buttons.emplace_back(m_localization.text("settings.key_bindings"), [this]{
            showPage(SettingsPage::KeyBindings);
        });
        m_buttons.emplace_back(m_localization.text("common.back"), m_onBack);
    } else if (m_page == SettingsPage::Video) {
        if (rendererBackendSwitchable(m_renderers)) {
            const std::string rendererName =
                m_settings.rendererBackend == RendererBackend::Vulkan
                    ? m_localization.text("settings.renderer_vulkan")
                    : m_localization.text("settings.renderer_opengl");
            m_buttons.emplace_back(m_localization.format("settings.renderer", {
                rendererName}), [this]{
                    m_settings.rendererBackend =
                        m_settings.rendererBackend == RendererBackend::OpenGL
                            ? RendererBackend::Vulkan : RendererBackend::OpenGL;
                    m_onChanged(); refreshButtons();
                });
        }
        m_frameRateButton = static_cast<int>(m_buttons.size());
        m_buttons.emplace_back(frameRateLabel(), [this] {
            m_settings.frameRateLimit += 10;
            if (m_settings.frameRateLimit > ClientSettings::MAX_FRAME_RATE)
                m_settings.frameRateLimit = ClientSettings::MIN_FRAME_RATE;
            m_onChanged();
            refreshButtons();
        });
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
        const char* visualNames[] = {"settings.visual_low", "settings.visual_medium",
            "settings.visual_high", "settings.visual_ultra"};
        m_buttons.emplace_back(m_localization.format("settings.visual_quality", {
            m_localization.text(visualNames[static_cast<int>(m_settings.visualQuality)])}),
            [this]{
                m_settings.visualQuality = static_cast<VisualQuality>(
                    (static_cast<int>(m_settings.visualQuality) + 1) % 4);
                m_onChanged(); refreshButtons();
            });
        const char* shadowNames[] = {"common.off", "settings.shadow_low",
            "settings.shadow_medium", "settings.shadow_high"};
        m_buttons.emplace_back(m_localization.format("settings.shadows", {
            m_localization.text(shadowNames[static_cast<int>(m_settings.shadowQuality)])}), [this]{
                m_settings.shadowQuality = static_cast<ShadowQuality>(
                    (static_cast<int>(m_settings.shadowQuality) + 1) % 4);
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.gui_scale", {
            m_settings.guiScale == 0 ? m_localization.text("common.auto") :
            std::to_string(m_settings.guiScale) + "x"}), [this]{
                m_settings.guiScale = (m_settings.guiScale + 1) % 5;
                m_onChanged(); refreshButtons();
        });
        m_buttons.emplace_back(m_localization.text("settings.back"), [this]{
            showPage(SettingsPage::General);
        });
    } else if (m_page == SettingsPage::KeyBindings) {
        m_buttons.emplace_back(m_localization.text("settings.keyboard_mouse"), [this]{
            showPage(SettingsPage::KeyboardMouse);
        });
        m_buttons.emplace_back(m_localization.text("settings.controller"), [this]{
            showPage(SettingsPage::Controller);
        });
        m_buttons.emplace_back(m_localization.text("settings.touch_controls"), [this]{
            showPage(SettingsPage::Touch);
        });
        m_buttons.emplace_back(m_localization.text("settings.back"), [this]{
            showPage(SettingsPage::General);
        });
    } else if (m_page == SettingsPage::KeyboardMouse) {
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
        m_buttons.emplace_back(m_localization.text("settings.back_to_bindings"), [this]{
            showPage(SettingsPage::KeyBindings);
        });
    } else if (m_page == SettingsPage::Controller) {
        constexpr int visible = 7;
        const int end = std::min<int>(INPUT_ACTION_COUNT, m_controlOffset + visible);
        for (int i = m_controlOffset; i < end; ++i) {
            const InputAction action = static_cast<InputAction>(i);
            m_buttons.emplace_back(
                m_localization.actionName(action) + ": " +
                    (m_captureAction == i
                        ? m_localization.text("settings.controller_capture")
                        : gamepadBindingName(
                            m_settings.gamepadBindings[static_cast<size_t>(i)])),
                [this, i] { m_captureAction = i; refreshButtons(); });
        }
        const auto decimal = [](float value) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << value;
            return out.str();
        };
        m_buttons.emplace_back(m_localization.format("settings.controller_deadzone", {
            decimal(m_settings.gamepadDeadzone)}), [this] {
                m_settings.gamepadDeadzone += .02f;
                if (m_settings.gamepadDeadzone > .5001f)
                    m_settings.gamepadDeadzone = .10f;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.controller_sensitivity", {
            decimal(m_settings.gamepadLookSensitivity)}), [this] {
                m_settings.gamepadLookSensitivity += .25f;
                if (m_settings.gamepadLookSensitivity > 3.001f)
                    m_settings.gamepadLookSensitivity = .25f;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.controller_invert_y", {
            m_localization.text(m_settings.invertGamepadY
                ? "common.on" : "common.off")}), [this] {
                    m_settings.invertGamepadY = !m_settings.invertGamepadY;
                    m_onChanged(); refreshButtons();
                });
        m_buttons.emplace_back(m_localization.format("settings.controller_vibration", {
            decimal(m_settings.gamepadRumble)}), [this] {
                m_settings.gamepadRumble += .25f;
                if (m_settings.gamepadRumble > 1.001f)
                    m_settings.gamepadRumble = 0.0f;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.text("settings.controller_reset"), [this] {
            m_settings.resetGamepadBindings(); m_onChanged(); refreshButtons();
        });
        m_buttons.emplace_back(m_localization.text("settings.back_to_bindings"), [this] {
            showPage(SettingsPage::KeyBindings);
        });
    } else if (m_page == SettingsPage::Touch) {
        const char* modeKey = m_settings.controlMode == ControlMode::Auto ? "settings.touch_mode_auto" :
            m_settings.controlMode == ControlMode::KeyboardMouse ? "settings.touch_mode_keyboard" : "settings.touch_mode_touch";
        m_buttons.emplace_back(m_localization.format("settings.touch_mode",{m_localization.text(modeKey)}),[this]{
            m_settings.controlMode=static_cast<ControlMode>((static_cast<int>(m_settings.controlMode)+1)%3);m_onChanged();refreshButtons();});
        std::ostringstream sensitivity;sensitivity<<std::fixed<<std::setprecision(2)<<m_settings.touchSensitivity;
        m_buttons.emplace_back(m_localization.format("settings.touch_sensitivity",{sensitivity.str()}),[this]{
            m_settings.touchSensitivity+=.25f;if(m_settings.touchSensitivity>3.001f)m_settings.touchSensitivity=.5f;m_onChanged();refreshButtons();});
        m_buttons.emplace_back(m_localization.format("settings.touch_size",{std::to_string(static_cast<int>(m_settings.touchControlSize*100))}),[this]{
            constexpr float values[]={.75f,1,1.25f,1.5f};auto it=std::find(std::begin(values),std::end(values),m_settings.touchControlSize);
            m_settings.touchControlSize=values[(it==std::end(values)?0:(it-std::begin(values)+1)%4)];m_onChanged();refreshButtons();});
        m_buttons.emplace_back(m_localization.format("settings.touch_opacity",{std::to_string(static_cast<int>(m_settings.touchControlOpacity*100))}),[this]{
            constexpr float values[]={.35f,.5f,.65f,.8f,1};auto it=std::find(std::begin(values),std::end(values),m_settings.touchControlOpacity);
            m_settings.touchControlOpacity=values[(it==std::end(values)?0:(it-std::begin(values)+1)%5)];m_onChanged();refreshButtons();});
        m_buttons.emplace_back(m_localization.format("settings.touch_layout",{m_localization.text(m_settings.touchLeftHanded?"settings.touch_left":"settings.touch_right")}),[this]{
            m_settings.touchLeftHanded=!m_settings.touchLeftHanded;m_onChanged();refreshButtons();});
        m_buttons.emplace_back(m_localization.text("settings.back_to_bindings"),[this]{showPage(SettingsPage::KeyBindings);});
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

void SettingsMenu::assignGamepadBinding(GamepadBinding binding) {
    if(m_captureAction<0)return;
    m_settings.gamepadBindings[static_cast<size_t>(m_captureAction)]=binding;
    m_captureAction=-1;m_settings.validate();m_onChanged();refreshButtons();
}

void SettingsMenu::onGamepadBinding(GamepadBinding binding) {
    assignGamepadBinding(binding);
}

void SettingsMenu::render(UIRenderer& ui, int width, int height) {
    ui.drawRect(0, 0, static_cast<float>(width), static_cast<float>(height),
                Config::UIColors::BACKGROUND);
    const std::string title = m_localization.text(
        m_page == SettingsPage::KeyboardMouse ? "settings.keyboard_mouse_title" :
        m_page == SettingsPage::Controller ? "settings.controller_title" :
        m_page == SettingsPage::Video ? "settings.video_title" :
        m_page == SettingsPage::KeyBindings ? "settings.key_bindings_title" :
        m_page == SettingsPage::Touch ? "settings.touch_title" : "settings.title");
    const auto titleSize = ui.measureText(title, 3.0f);
    const float titleY = height * 0.78f;
    ui.renderText(title, (width - titleSize.x) * .5f, titleY, 3.0f,
                  Config::UIColors::TEXT_TITLE);
    const bool hasHelp = m_page == SettingsPage::KeyboardMouse ||
                         m_page == SettingsPage::Controller;
    const SettingsButtonLayout layout = settingsButtonLayout(
        titleY, m_buttons.size(), hasHelp);
    if (hasHelp) {
        const std::string help = m_localization.text(
            m_page == SettingsPage::Controller
                ? "settings.controller_help" : "settings.controls_help");
        const auto helpSize = ui.measureText(help, 1.0f);
        ui.renderText(help, (width - helpSize.x) * .5f,
                      layout.helpY, 1.0f, glm::vec3(.72f));
    }
    const float x = (width - Config::UI_BUTTON_WIDTH) * .5f;
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const float y = layout.firstButtonY -
                        i * (layout.buttonHeight + 5.0f);
        m_buttons[i].setPosition(x, y);
        m_buttons[i].setSize(Config::UI_BUTTON_WIDTH, layout.buttonHeight);
        m_buttons[i].render(ui);
    }
    if (m_frameRateButton >= 0 &&
        m_frameRateButton < static_cast<int>(m_buttons.size())) {
        const Button& slider = m_buttons[static_cast<size_t>(m_frameRateButton)];
        const float left = slider.x() + 12.0f;
        const float width = std::max(1.0f, slider.width() - 24.0f);
        const float trackY = slider.y() + 3.0f;
        const float filled = width * frameRateSliderFraction(m_settings.frameRateLimit);
        ui.drawRect(left, trackY, width, 3.0f, glm::vec4(.12f, .12f, .16f, .9f));
        ui.drawRect(left, trackY, filled, 3.0f, glm::vec4(.72f, .78f, 1.0f, 1.0f));
        ui.drawRect(left + filled - 2.0f, trackY - 3.0f, 5.0f, 9.0f,
                    glm::vec4(.95f, .95f, 1.0f, 1.0f));
    }
}

void SettingsMenu::onKeyPress(int key, int) {
    if (m_captureAction >= 0) {
        if (key == Key::Escape) { m_captureAction = -1; refreshButtons(); }
        else if (key == Key::Backspace &&
                 inputActionCanUnbind(static_cast<InputAction>(m_captureAction)))
            assignBinding({});
        else if (m_page != SettingsPage::Controller)
            assignBinding({InputDevice::Keyboard, key});
        return;
    }
    if (m_page == SettingsPage::Video && m_selectedIdx == m_frameRateButton &&
        (key == Key::Left || key == Key::Right)) {
        m_settings.frameRateLimit = std::clamp(
            m_settings.frameRateLimit + (key == Key::Right ? 1 : -1),
            ClientSettings::MIN_FRAME_RATE, ClientSettings::MAX_FRAME_RATE);
        m_onChanged();
        refreshButtons();
        return;
    }
    switch (key) {
        case Key::Up: case Key::W: navigateUp(m_buttons, m_selectedIdx); break;
        case Key::Down: case Key::S: navigateDown(m_buttons, m_selectedIdx); break;
        case Key::Enter: case Key::Space: activateSelected(m_buttons, m_selectedIdx); break;
        case Key::Escape:
            if (m_page != SettingsPage::General)
                showPage(settingsParentPage(m_page));
            else
                m_onBack();
            break;
        default: break;
    }
}

void SettingsMenu::onMouseMove(double x, double y) {
    if (m_frameRateDragging) setFrameRateFromPointer(x);
    for (auto& button : m_buttons)
        button.setHovered(button.containsPoint(static_cast<float>(x), static_cast<float>(y)));
}

void SettingsMenu::onMouseButton(int button, ButtonAction action, double x, double y) {
    if (m_captureAction >= 0 && action == ButtonAction::Press) {
        if (m_page == SettingsPage::KeyboardMouse)
            assignBinding({InputDevice::Mouse, button});
        return;
    }
    if (button != MouseButton::Left) return;
    if (m_frameRateButton >= 0 &&
        m_frameRateButton < static_cast<int>(m_buttons.size())) {
        Button& slider = m_buttons[static_cast<size_t>(m_frameRateButton)];
        if (action == ButtonAction::Press && slider.containsPoint(
                static_cast<float>(x), static_cast<float>(y))) {
            m_pressedButton = -1;
            m_frameRateDragging = true;
            slider.setPressed(true);
            setFrameRateFromPointer(x);
            return;
        }
        if (action == ButtonAction::Release && m_frameRateDragging) {
            setFrameRateFromPointer(x);
            slider.setPressed(false);
            m_frameRateDragging = false;
            m_onChanged();
            return;
        }
    }
    if (action == ButtonAction::Press) {
        m_pressedButton = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
                m_pressedButton = static_cast<int>(i); m_buttons[i].setPressed(true); return;
            }
    } else if (action == ButtonAction::Release && m_pressedButton >= 0) {
        const int captured = m_pressedButton; m_pressedButton = -1;
        m_buttons[static_cast<size_t>(captured)].setPressed(false);
        if (m_buttons[static_cast<size_t>(captured)].containsPoint(
                static_cast<float>(x), static_cast<float>(y)))
            m_buttons[static_cast<size_t>(captured)].activate();
    }
}

bool SettingsMenu::capturesPointerDrag(double x, double y) const {
    return m_frameRateButton >= 0 &&
        m_frameRateButton < static_cast<int>(m_buttons.size()) &&
        m_buttons[static_cast<size_t>(m_frameRateButton)].containsPoint(
            static_cast<float>(x), static_cast<float>(y));
}

void SettingsMenu::setFrameRateFromPointer(double x) {
    if (m_frameRateButton < 0 ||
        m_frameRateButton >= static_cast<int>(m_buttons.size())) return;
    Button& slider = m_buttons[static_cast<size_t>(m_frameRateButton)];
    constexpr float inset = 12.0f;
    m_settings.frameRateLimit = frameRateFromSlider(
        static_cast<float>(x), slider.x() + inset,
        std::max(1.0f, slider.width() - inset * 2.0f));
    slider.setLabel(frameRateLabel());
}

void SettingsMenu::onScroll(double yOffset) {
    if (m_captureAction >= 0) {
        if (m_page == SettingsPage::KeyboardMouse)
            assignBinding({InputDevice::Wheel, yOffset > 0 ? 1 : -1});
        return;
    }
    if (m_page != SettingsPage::KeyboardMouse &&
        m_page != SettingsPage::Controller) return;
    const int visible = m_page == SettingsPage::Controller ? 7 : 8;
    const int maximum = std::max(0, static_cast<int>(INPUT_ACTION_COUNT) - visible);
    m_controlOffset = std::clamp(m_controlOffset + (yOffset < 0 ? 1 : -1), 0, maximum);
    refreshButtons();
}
