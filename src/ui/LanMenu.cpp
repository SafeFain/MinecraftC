#include "ui/LanMenu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "game/Utf8.h"
#include "network/Protocol.h"
#include "network/Session.h"
#include "plugins/ContentRegistry.h"
#include "world/WorldGenContext.h"
#include "game/TextWrap.h"
#include "core/InputCodes.h"
#include <algorithm>

LanMenu::LanMenu(LanMenuActions actions, const Localization& localization,
                 platform::Clipboard* clipboard, std::string nickname)
    : m_actions(std::move(actions)), m_localization(localization),
      m_nickname(std::move(nickname), 32, clipboard), m_address("127.0.0.1", 64, clipboard),
      m_port("25565", 5, clipboard) {
    m_port.setFilter([](uint32_t cp, const std::string&) { return cp >= '0' && cp <= '9'; });
    m_address.setFilter([](uint32_t cp, const std::string&) {
        return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') ||
               (cp >= 'A' && cp <= 'F') || cp == ':' || cp == '.' || cp == '%';
    });
    m_browsing = !m_actions.host;
    rebuildButtons();
}
void LanMenu::rebuildButtons() {
    m_buttons.clear(); m_selected = 0; m_pressed = -1;
    m_buttons.emplace_back("", [this] { m_selected = 0; });
    if (m_browsing) {
        for (size_t i = 0; i < 4; ++i) m_buttons.emplace_back("", [this, i] { joinRoom(i); });
        m_buttons.emplace_back("", [this] { m_roomPage += 4; if (m_roomPage >= m_rooms.size()) m_roomPage = 0; updateLabels(); });
        m_buttons.emplace_back(m_localization.text("lan.direct"), [this] { m_browsing = false; rebuildButtons(); });
        m_buttons.emplace_back(m_localization.text("lan.refresh"), [this] { if (m_actions.refresh) m_actions.refresh(); m_roomPage = 0; });
    } else {
        if (!m_actions.host) m_buttons.emplace_back("", [this] { m_selected = 1; });
        m_buttons.emplace_back("", [this] { m_selected = m_actions.host ? 1 : 2; });
        if (m_actions.host) {
            m_buttons.emplace_back("", [this] { m_capacity = m_capacity == 8 ? 2 : m_capacity + 1; });
            m_buttons.emplace_back("", [this] { if (m_actions.setPvp && m_actions.pvp) m_actions.setPvp(!m_actions.pvp()); });
        } else m_buttons.emplace_back(m_localization.text("lan.find_rooms"), [this] { m_browsing = true; rebuildButtons(); });
        m_buttons.emplace_back("", [this] { connect(); });
    }
    m_buttons.emplace_back(m_localization.text("common.back"), m_actions.back);
    updateLabels();
}
void LanMenu::joinRoom(size_t row) {
    const auto index = m_roomPage + row;
    if (index >= m_rooms.size()) return;
    m_address.setText(m_rooms[index].address); m_port.setText(std::to_string(m_rooms[index].port));
    connect();
}
TextEditBuffer* LanMenu::field() {
    if (m_selected == 0) return &m_nickname;
    if (m_browsing) return nullptr;
    if (m_selected == 1) return m_actions.host ? &m_port : &m_address;
    if (m_selected == 2 && !m_actions.host) return &m_port;
    return nullptr;
}
bool LanMenu::wantsTextInput() const {
    return m_selected == 0 || (!m_browsing && (m_selected == 1 || (!m_actions.host && m_selected == 2)));
}
void LanMenu::updateLabels() {
    const bool hosting = m_actions.host && m_actions.hosting && m_actions.hosting();
    size_t index = 0;
    m_buttons[index++].setLabel(m_localization.text("lan.nickname") + ": " + m_nickname.text());
    if (m_browsing) {
        if (m_actions.rooms) m_rooms = m_actions.rooms();
        if (m_roomPage >= m_rooms.size()) m_roomPage = 0;
        for (size_t row = 0; row < 4; ++row) {
            const auto roomIndex = m_roomPage + row; auto& button = m_buttons[index++];
            if (roomIndex >= m_rooms.size()) { button.setLabel(m_localization.text(row == 0 && m_rooms.empty() ? "lan.searching" : "lan.no_room")); button.setDetail({}); button.setEnabled(false); continue; }
            const auto& room = m_rooms[roomIndex];
            const bool compatible = room.version == Config::GAME_VERSION && room.protocol == Lan::PROTOCOL_VERSION && room.generation == WorldGenContext::GENERATION_VERSION &&
                room.contentSignature == std::to_string(Lan::contentSignature(Plugins::content().networkDescription));
            button.setLabel(room.name + " (" + std::to_string(room.players) + "/" + std::to_string(room.capacity) + ")");
            button.setDetail(!compatible ? m_localization.text("lan.incompatible") : room.players == room.capacity ? m_localization.text("lan.full") : room.address + ":" + std::to_string(room.port));
            button.setEnabled(compatible && room.players < room.capacity);
        }
        m_buttons[index].setLabel(m_localization.text("lan.next_rooms")); m_buttons[index].setEnabled(m_rooms.size() > 4);
        for (size_t i = 0; i < m_buttons.size(); ++i) m_buttons[i].setSelected(static_cast<int>(i) == m_selected);
        return;
    }
    if (!m_actions.host) m_buttons[index++].setLabel(m_localization.text("lan.address") + ": " + m_address.text());
    m_buttons[index++].setLabel(m_localization.text("lan.port") + ": " + m_port.text());
    if (m_actions.host) {
        m_buttons[index].setLabel(m_localization.text("lan.capacity") + ": " + std::to_string(m_capacity));
        m_buttons[index++].setEnabled(!hosting);
        m_buttons[index++].setLabel(m_localization.text("lan.pvp") + ": " + m_localization.text(
            m_actions.pvp && m_actions.pvp() ? "common.on" : "common.off"));
    }
    if (!m_actions.host) ++index;
    m_buttons[index].setLabel(m_localization.text(hosting ? "lan.close" : m_actions.host ? "lan.open" : "lan.join"));
    m_buttons[index].setPrimary(true);
    for (size_t i = 0; i < m_buttons.size(); ++i) m_buttons[i].setSelected(static_cast<int>(i) == m_selected);
}
void LanMenu::connect() {
    if (m_actions.host && m_actions.hosting && m_actions.hosting()) {
        if (m_actions.close) m_actions.close();
        updateLabels(); return;
    }
    unsigned int port = 0;
    for (const char cp : m_port.text()) { if (cp < '0' || cp > '9') { port = 0; break; } port = port * 10 + cp - '0'; }
    if (!port || port > 65535) { m_error = m_localization.text("lan.invalid_port"); return; }
    if (!m_actions.nickname || !m_actions.nickname(m_nickname.text())) { m_error = m_localization.text("lan.invalid_name"); return; }
    const bool host = m_actions.host;
    const bool result = host ? m_actions.open && m_actions.open(static_cast<uint16_t>(port), m_capacity) :
        m_actions.join && m_actions.join(m_address.text(), static_cast<uint16_t>(port));
    if (result && !host) return;
    if (!result) m_error = m_actions.status ? m_actions.status() : m_localization.text("lan.failed");
    else m_error.clear();
    updateLabels();
}
void LanMenu::render(UIRenderer& ui, int width, int height) {
    updateLabels();
    UiTheme::menuBackground(ui, static_cast<float>(width), static_cast<float>(height));
    const float panelHeight = m_browsing ? 650.0f : 490.0f;
    const UiCanvasFit fit(width, height, 520, panelHeight);
    const float s = fit.scale, x = fit.x, y = fit.y;
    UiTheme::panel(ui, x, y, 520*s, panelHeight*s);
    UiTheme::textWithShadow(ui, m_localization.text(m_actions.host ? "lan.host_title" : "lan.join_title"), x+20*s, y+(panelHeight-42)*s, 1.8f*s, UiTheme::TEXT);
    const std::string status = !m_error.empty() ? m_error : m_actions.status ? m_actions.status() : "";
    if (!status.empty()) {
        const auto lines = wrapTextPixels(status, 480*s, [&](const std::string& text) { return ui.measureText(text, .8f*s).x; });
        for (size_t i = 0; i < std::min<size_t>(2, lines.size()); ++i)
            UiTheme::textWithShadow(ui, lines[i], x+20*s, y+(panelHeight-67-i*14)*s, .8f*s, UiTheme::TEXT_DIM);
    }
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        m_buttons[i].setPosition(x+20*s, y+(panelHeight-132-i*52)*s); m_buttons[i].setSize(480*s, 44*s);
        prepareButton(m_buttons[i]); m_buttons[i].render(ui);
    }
}
void LanMenu::onKeyPress(int key, int mods) {
    navigationFocus();
    if (key == Key::Escape) { if (m_actions.back) m_actions.back(); return; }
    if (key == Key::Tab || key == Key::Up || key == Key::Down) {
        if (key == Key::Up || (key == Key::Tab && (mods & KeyModifier::Shift))) navigateUp(m_buttons, m_selected);
        else navigateDown(m_buttons, m_selected);
        updateLabels(); return;
    }
    if (auto* edit = field()) {
        const bool selecting = (mods & KeyModifier::Shift) != 0, control = (mods & KeyModifier::Control) != 0;
        if (key == Key::Backspace) edit->backspace();
        else if (key == Key::Delete) edit->eraseForward();
        else if (key == Key::Left) edit->moveLeft(selecting);
        else if (key == Key::Right) edit->moveRight(selecting);
        else if (key == Key::Home) edit->moveHome(selecting);
        else if (key == Key::End) edit->moveEnd(selecting);
        else if (control && key == Key::A) edit->selectAll();
        else if (control && key == Key::C) edit->copySelection();
        else if (control && key == Key::X) edit->cutSelection();
        else if (control && key == Key::V) edit->pasteClipboard();
        else if (key == Key::Enter) { m_selected = m_browsing ? 1 : static_cast<int>(m_buttons.size()) - 2; }
    } else if (key == Key::Enter || key == Key::Space) { activateSelected(m_buttons, m_selected); return; }
    updateLabels();
}
void LanMenu::onChar(unsigned int codepoint) {
    if (codepoint >= 32) if (auto* edit = field()) { std::string text; appendUtf8(text, codepoint); edit->insert(text); }
    updateLabels();
}
void LanMenu::onMouseMove(double x, double y) {
    pointerFocus(x, y); for (auto& button : m_buttons) button.setHovered(button.containsPoint(static_cast<float>(x), static_cast<float>(y)));
}
void LanMenu::onMouseButton(int button, ButtonAction action, double x, double y) {
    if (button != MouseButton::Left) return;
    if (action == ButtonAction::Press) {
        m_pressed = -1;
        for (size_t i = 0; i < m_buttons.size(); ++i) if (m_buttons[i].containsPoint(static_cast<float>(x), static_cast<float>(y))) {
            m_pressed = static_cast<int>(i); m_selected = m_pressed; m_buttons[i].setPressed(true); break;
        }
    } else if (action == ButtonAction::Release) {
        const int pressed = m_pressed; onPointerCancel();
        if (pressed >= 0 && m_buttons[static_cast<size_t>(pressed)].containsPoint(static_cast<float>(x), static_cast<float>(y)))
            activateSelected(m_buttons, pressed);
    }
}
void LanMenu::onPointerCancel() { cancelButtons(m_buttons); m_pressed = -1; }
