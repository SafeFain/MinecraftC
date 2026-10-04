#pragma once
#include "ui/Menu.h"
#include "plugins/PluginManager.h"

class PluginMenu final : public Menu {
public:
    PluginMenu(Plugins::PluginManager&,Localization&,std::function<void()> back);
    void render(UIRenderer&,int,int) override;
    void onKeyPress(int,int=0) override;
    void onMouseMove(double,double) override;
    void onMouseButton(int,ButtonAction,double,double) override;
    void onScroll(double) override;
    void onPointerCancel() override;
private:
    Plugins::PluginManager& m_manager;
    Localization& m_localization;
    std::function<void()> m_back;
    std::vector<Button> m_buttons;
    int m_selected=0,m_pressed=-1,m_offset=0,m_visible=3;
    void rebuild();
};
