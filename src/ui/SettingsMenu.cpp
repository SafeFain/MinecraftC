#include "ui/SettingsMenu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "Config.h"

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
    m_lodDistanceEditing = false;
    m_lodDistanceInvalid = false;
    m_lodWarningPending = false;
    refreshButtons();
}

void SettingsMenu::refreshButtons() {
    m_buttons.clear();
    m_frameRateButton = -1;
    m_backButton = -1;
    m_frameRateDragging = false;
    if (m_page == SettingsPage::General) {
        m_buttons.emplace_back(labelForDayCycle(), [this]{ cycleDayCycle(); });
        m_buttons.emplace_back(labelForAutoJump(), [this]{ toggleAutoJump(); });
        m_buttons.emplace_back(m_localization.text("settings.video"), [this]{
            showPage(SettingsPage::Video);
        });
        m_buttons.emplace_back(m_localization.text("settings.key_bindings"), [this]{
            showPage(SettingsPage::KeyBindings);
        });
        m_backButton = static_cast<int>(m_buttons.size());
        m_buttons.emplace_back(m_localization.text("common.back"), m_onBack);
    } else if (m_page == SettingsPage::Video) {
        m_frameRateButton = static_cast<int>(m_buttons.size());
        m_buttons.emplace_back(frameRateLabel(), [this] {
            m_settings.frameRateLimit += 10;
            if (m_settings.frameRateLimit > ClientSettings::MAX_FRAME_RATE)
                m_settings.frameRateLimit = ClientSettings::MIN_FRAME_RATE;
            m_onChanged();
            refreshButtons();
        });
        m_buttons.emplace_back(labelForRenderDist(), [this]{ cycleRenderDistance(); });
        const char* visualNames[] = {"settings.visual_low", "settings.visual_medium",
            "settings.visual_high", "settings.visual_ultra"};
        m_buttons.emplace_back(m_localization.format("settings.visual_quality", {
            m_localization.text(visualNames[static_cast<int>(m_settings.visualQuality)])}),
            [this]{
                m_settings.visualQuality = static_cast<VisualQuality>(
                    (static_cast<int>(m_settings.visualQuality) + 1) % 4);
                m_settings.transparentLeaves =
                    defaultLeafTransparency(m_settings.visualQuality);
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.enhanced_visuals", {
            m_localization.text(m_settings.enhancedVisuals
                ? "common.on" : "common.off")}), [this]{
                m_settings.enhancedVisuals = !m_settings.enhancedVisuals;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.smooth_lighting", {
            m_localization.text(m_settings.smoothLighting ? "common.on" : "common.off")}), [this]{
                m_settings.smoothLighting = !m_settings.smoothLighting;
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
        m_buttons.emplace_back(m_localization.format("settings.transparent_leaves", {
            m_localization.text(m_settings.transparentLeaves
                ? "common.on" : "common.off")}), [this]{
                m_settings.transparentLeaves = !m_settings.transparentLeaves;
                m_onChanged(); refreshButtons();
            });
        m_buttons.emplace_back(m_localization.format("settings.clouds", {
            m_localization.text(m_settings.renderClouds ? "common.on" : "common.off")}),
            [this]{ toggleCloudRendering(); });
        m_buttons.emplace_back(labelForCloudRenderDist(),
                               [this]{ cycleCloudRenderDistance(); });
        const char* indicatorNames[] = {
            "settings.attack_crosshair", "settings.attack_hotbar",
            "settings.attack_off"};
        m_buttons.emplace_back(m_localization.format("settings.attack_indicator", {
            m_localization.text(indicatorNames[
                static_cast<int>(m_settings.attackIndicator)])}), [this] {
            m_settings.attackIndicator = static_cast<AttackIndicator>(
                (static_cast<int>(m_settings.attackIndicator) + 1) % 3);
            m_onChanged();
            refreshButtons();
        });
        m_buttons.emplace_back(m_localization.format("settings.gui_scale", {
            m_settings.guiScale == 0 ? m_localization.text("common.auto") :
            std::to_string(m_settings.guiScale) + "x"}), [this]{
                m_settings.guiScale = (m_settings.guiScale + 1) % 5;
                m_onChanged(); refreshButtons();
        });
        m_buttons.emplace_back(m_localization.text("settings.lod"), [this]{
            showPage(SettingsPage::Lod);
        });
        m_backButton = static_cast<int>(m_buttons.size());
        m_buttons.emplace_back(m_localization.text("settings.back"), [this]{
            showPage(SettingsPage::General);
        });
    } else if (m_page == SettingsPage::Lod) {
        if (m_lodWarningPending) {
            m_buttons.emplace_back(m_localization.text("settings.lod_warning_apply"), [this] {
                m_settings.lodDistanceChunks = m_pendingLodDistance;
                m_lodWarningPending = false;
                m_onChanged();
                refreshButtons();
            });
            m_buttons.emplace_back(m_localization.text("common.cancel"), [this] {
                m_lodWarningPending = false;
                refreshButtons();
            });
        } else {
            m_buttons.emplace_back(m_localization.format("settings.lod_enabled", {
                m_localization.text(m_settings.lodEnabled ? "common.on" : "common.off")}),
                [this] {
                    m_settings.lodEnabled = !m_settings.lodEnabled;
                    m_onChanged(); refreshButtons();
                });
            const std::string distance = m_lodDistanceEditing
                ? "> " + m_lodDistanceText.text() + "_ <"
                : std::to_string(m_settings.lodDistanceChunks);
            m_buttons.emplace_back(m_localization.format(
                "settings.lod_distance", {distance}), [this] { beginLodDistanceEdit(); });
            const char* aggressiveness[] = {
                "settings.lod_power_saver", "settings.lod_balanced",
                "settings.lod_fast", "settings.lod_extreme"};
            m_buttons.emplace_back(m_localization.format("settings.lod_aggressiveness", {
                m_localization.text(aggressiveness[
                    static_cast<int>(m_settings.lodAggressiveness)])}), [this] {
                m_settings.lodAggressiveness = static_cast<LodAggressiveness>(
                    (static_cast<int>(m_settings.lodAggressiveness) + 1) % 4);
                m_onChanged(); refreshButtons();
            });
            const char* precision[] = {
                "settings.lod_low", "settings.lod_medium",
                "settings.lod_high", "settings.lod_ultra"};
            m_buttons.emplace_back(m_localization.format("settings.lod_precision", {
                m_localization.text(precision[static_cast<int>(m_settings.lodPrecision)])}),
                [this] {
                    m_settings.lodPrecision = static_cast<LodPrecision>(
                        (static_cast<int>(m_settings.lodPrecision) + 1) % 4);
                    m_onChanged(); refreshButtons();
                });
            m_backButton = static_cast<int>(m_buttons.size());
            m_buttons.emplace_back(m_localization.text("settings.lod_back"), [this] {
                showPage(SettingsPage::Video);
            });
        }
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
        m_backButton = static_cast<int>(m_buttons.size());
        m_buttons.emplace_back(m_localization.text("settings.back"), [this]{
            showPage(SettingsPage::General);
        });
    } else if (m_page == SettingsPage::KeyboardMouse) {
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
        m_backButton = static_cast<int>(m_buttons.size());
        m_buttons.emplace_back(m_localization.text("settings.back_to_bindings"), [this]{
            showPage(SettingsPage::KeyBindings);
        });
    } else if (m_page == SettingsPage::Controller) {
        constexpr int visible = 6;
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
        m_backButton = static_cast<int>(m_buttons.size());
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
        m_backButton = static_cast<int>(m_buttons.size());
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
    UiTheme::dirtBackground(ui, static_cast<float>(width),
                            static_cast<float>(height));
    const std::string title = m_localization.text(
        m_page == SettingsPage::KeyboardMouse ? "settings.keyboard_mouse_title" :
        m_page == SettingsPage::Controller ? "settings.controller_title" :
        m_page == SettingsPage::Lod ? "settings.lod_title" :
        m_page == SettingsPage::Video ? "settings.video_title" :
        m_page == SettingsPage::KeyBindings ? "settings.key_bindings_title" :
        m_page == SettingsPage::Touch ? "settings.touch_title" : "settings.title");
    const auto titleSize = ui.measureText(title, 3.0f);
    const float titleY = height * 0.78f;
    UiTheme::textWithShadow(ui, title, (width - titleSize.x) * .5f, titleY,
                            3.0f, UiTheme::TEXT_TITLE, 1.0f, 2.0f, -2.0f);
    const bool hasHelp = m_page == SettingsPage::KeyboardMouse ||
                         m_page == SettingsPage::Controller ||
                         m_page == SettingsPage::Lod;
    const bool standaloneBack = m_backButton >= 0 &&
        m_backButton + 1 == static_cast<int>(m_buttons.size());
    const SettingsButtonLayout layout = settingsButtonLayout(
        static_cast<float>(width), titleY, m_buttons.size(), hasHelp,
        standaloneBack);
    if (hasHelp) {
        const std::string help = m_localization.text(
            m_page == SettingsPage::Lod
                ? (m_lodWarningPending ? "settings.lod_warning" :
                   m_lodDistanceInvalid ? "settings.lod_distance_invalid" :
                                          "settings.lod_help")
                : m_page == SettingsPage::Controller
                    ? "settings.controller_help" : "settings.controls_help");
        const auto helpSize = ui.measureText(help, 1.0f);
        UiTheme::textWithShadow(ui, help, (width - helpSize.x) * .5f,
                                layout.helpY, 1.0f, glm::vec3(.72f));
    }
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const glm::vec2 position = settingsButtonPosition(
            layout, i, m_buttons.size(), standaloneBack);
        m_buttons[i].setPosition(position.x, position.y);
        m_buttons[i].setSize(layout.buttonWidth, layout.buttonHeight);
        m_buttons[i].render(ui);
    }
    if (m_frameRateButton >= 0 &&
        m_frameRateButton < static_cast<int>(m_buttons.size())) {
        const Button& slider = m_buttons[static_cast<size_t>(m_frameRateButton)];
        const float left = slider.x() + 12.0f;
        const float width = std::max(1.0f, slider.width() - 24.0f);
        const float trackY = slider.y() + 3.0f;
        const float filled = width * frameRateSliderFraction(m_settings.frameRateLimit);
        UiTheme::progressBar(ui, left, trackY, width, 8.0f,
                             frameRateSliderFraction(m_settings.frameRateLimit),
                             UiTheme::GOLD);
        UiTheme::beveledBody(ui, left + filled - 3.0f, trackY - 2.0f, 8.0f,
                             12.0f, UiTheme::BUTTON_HOVER, false);
    }
}

void SettingsMenu::onKeyPress(int key, int mods) {
    if (m_lodWarningPending && key == Key::Escape) {
        m_lodWarningPending = false;
        refreshButtons();
        return;
    }
    if (m_lodDistanceEditing) {
        const bool selecting = (mods & KeyModifier::Shift) != 0;
        const bool control = (mods & KeyModifier::Control) != 0;
        if (key == Key::Enter) commitLodDistanceEdit();
        else if (key == Key::Escape) {
            m_lodDistanceEditing = false; m_lodDistanceInvalid = false; refreshButtons();
        } else if (key == Key::Backspace) { m_lodDistanceText.backspace(); refreshButtons(); }
        else if (key == Key::Delete) { m_lodDistanceText.eraseForward(); refreshButtons(); }
        else if (key == Key::Left) { m_lodDistanceText.moveLeft(selecting); refreshButtons(); }
        else if (key == Key::Right) { m_lodDistanceText.moveRight(selecting); refreshButtons(); }
        else if (key == Key::Home) { m_lodDistanceText.moveHome(selecting); refreshButtons(); }
        else if (key == Key::End) { m_lodDistanceText.moveEnd(selecting); refreshButtons(); }
        else if (control && key == Key::A) { m_lodDistanceText.selectAll(); refreshButtons(); }
        return;
    }
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
    const auto selectNeighbor = [this](int columnDelta, int rowDelta) {
        if (m_buttons.empty()) return;
        m_buttons[static_cast<size_t>(m_selectedIdx)].setSelected(false);
        m_selectedIdx = settingsGridNeighbor(
            m_selectedIdx, m_buttons.size(), columnDelta, rowDelta,
            m_backButton >= 0 &&
                m_backButton + 1 == static_cast<int>(m_buttons.size()));
        m_buttons[static_cast<size_t>(m_selectedIdx)].setSelected(true);
    };
    switch (key) {
        case Key::Up: case Key::W: selectNeighbor(0, -1); break;
        case Key::Down: case Key::S: selectNeighbor(0, 1); break;
        case Key::Left: case Key::A: selectNeighbor(-1, 0); break;
        case Key::Right: case Key::D: selectNeighbor(1, 0); break;
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

void SettingsMenu::onChar(unsigned int codepoint) {
    if (!m_lodDistanceEditing || codepoint < '0' || codepoint > '9') return;
    m_lodDistanceText.insert(std::string(1, static_cast<char>(codepoint)));
    m_lodDistanceInvalid = false;
    refreshButtons();
}

void SettingsMenu::beginLodDistanceEdit() {
    m_lodDistanceText.setText(std::to_string(m_settings.lodDistanceChunks));
    m_lodDistanceText.selectAll();
    m_lodDistanceEditing = true;
    m_lodDistanceInvalid = false;
    refreshButtons();
}

void SettingsMenu::commitLodDistanceEdit() {
    const std::optional<int> parsed = parseLodDistance(m_lodDistanceText.text());
    if (!parsed) {
        m_lodDistanceInvalid = true;
        refreshButtons();
        return;
    }
    const int value = *parsed;
    m_lodDistanceEditing = false;
    m_lodDistanceInvalid = false;
    if (lodDistanceNeedsWarning(value) &&
        value != m_settings.lodDistanceChunks) {
        m_pendingLodDistance = value;
        m_lodWarningPending = true;
        refreshButtons();
        return;
    }
    m_settings.lodDistanceChunks = value;
    m_onChanged();
    refreshButtons();
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
    const int visible = m_page == SettingsPage::Controller ? 6 : 8;
    const int maximum = std::max(0, static_cast<int>(INPUT_ACTION_COUNT) - visible);
    m_controlOffset = std::clamp(m_controlOffset + (yOffset < 0 ? 1 : -1), 0, maximum);
    refreshButtons();
}
