#pragma once

#include "ui/Menu.h"
#include "core/LanDiscovery.h"

struct LanMenuActions {
    bool host = false;
    std::function<bool()> hosting;
    std::function<std::string()> status;
    std::function<bool(const std::string&)> nickname;
    std::function<bool(const std::string&, uint16_t)> join;
    std::function<bool(uint16_t, size_t)> open;
    std::function<void()> close;
    std::function<bool()> pvp;
    std::function<void(bool)> setPvp;
    std::function<void()> back, refresh;
    std::function<std::vector<Platform::LanDiscoveredRoom>()> rooms;
};

class LanMenu final : public Menu {
public:
    LanMenu(LanMenuActions actions, const Localization& localization,
            platform::Clipboard* clipboard, std::string nickname);
    void render(UIRenderer& ui, int width, int height) override;
    void onKeyPress(int key, int mods = 0) override;
    void onChar(unsigned int codepoint) override;
    void onMouseMove(double x, double y) override;
    void onMouseButton(int button, ButtonAction action, double x, double y) override;
    bool wantsTextInput() const override;
    void onPointerCancel() override;
private:
    LanMenuActions m_actions;
    const Localization& m_localization;
    std::vector<Button> m_buttons;
    TextEditBuffer m_nickname, m_address, m_port;
    int m_selected = 0, m_pressed = -1;
    size_t m_capacity = 8, m_roomPage = 0;
    bool m_browsing = false;
    std::vector<Platform::LanDiscoveredRoom> m_rooms;
    void rebuildButtons();
    void joinRoom(size_t row);
    std::string m_error;
    TextEditBuffer* field();
    void updateLabels();
    void connect();
};
