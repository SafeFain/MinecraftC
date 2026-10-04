#include "ui/PluginMenu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "core/Input.h"
#include <algorithm>

PluginMenu::PluginMenu(Plugins::PluginManager& manager,Localization& localization,std::function<void()> back)
    : m_manager(manager),m_localization(localization),m_back(std::move(back)){rebuild();}
void PluginMenu::rebuild() {
    m_buttons.clear();m_pressed=-1;
    const auto& plugins=m_manager.plugins();
    m_offset=std::clamp(m_offset,0,std::max(0,static_cast<int>(plugins.size())-m_visible));
    for(int row=0;row<m_visible&&m_offset+row<static_cast<int>(plugins.size());++row) {
        const size_t i=static_cast<size_t>(m_offset+row);const auto& p=plugins[i];
        m_buttons.emplace_back(p.manifest.name+"  "+p.manifest.version,[this,i]{m_manager.setEnabled(i,!m_manager.plugins()[i].enabled);rebuild();});
        std::string detail=m_localization.text(p.manifest.builtin?"plugins.builtin":"plugins.user")+" · "+m_localization.text(p.enabled?"plugins.enabled":"plugins.disabled");
        if(p.status=="Loaded")detail+=" · "+m_localization.text("plugins.loaded");
        else if(p.status=="Restart required")detail+=" · "+m_localization.text("plugins.restart");
        else if(p.status!="Disabled"&&!p.status.empty())detail+=" · "+p.status;
        for(const auto& dep:p.manifest.dependencies)detail+=" · "+dep.id;
        m_buttons.back().setDetail(detail);m_buttons.back().setPrimary(p.enabled);
    }
    m_buttons.emplace_back(m_localization.text("plugins.back"),m_back);
    m_selected=std::clamp(m_selected,0,static_cast<int>(m_buttons.size())-1);m_buttons[m_selected].setSelected(true);
}
void PluginMenu::render(UIRenderer& ui,int width,int height) {
    const int visible=std::clamp((height-180)/62,1,8);if(visible!=m_visible){m_visible=visible;rebuild();}
    const float w=static_cast<float>(width),h=static_cast<float>(height),panelW=std::min(700.0f,w-32),x=(w-panelW)*.5f;
    UiTheme::menuBackground(ui,w,h);UiTheme::panel(ui,x,16,panelW,h-32);
    const auto title=m_localization.text("plugins.title"),hint=m_localization.text("plugins.hint");
    UiTheme::textWithShadow(ui,title,x+16,h-54,UiTheme::fittedScale(ui,title,2.0f,panelW-32),UiTheme::TEXT);
    UiTheme::textWithShadow(ui,hint,x+16,h-80,UiTheme::fittedScale(ui,hint,.85f,panelW-32),UiTheme::TEXT_DIM);
    for(size_t i=0;i<m_buttons.size();++i) {
        const bool back=i+1==m_buttons.size();m_buttons[i].setPosition(x+16,back?28:h-146-static_cast<float>(i)*62);m_buttons[i].setSize(panelW-32,back?36:54);
        prepareButton(m_buttons[i]);m_buttons[i].render(ui);
    }
    if(static_cast<int>(m_manager.plugins().size())>m_visible)UiTheme::scrollBar(ui,x+panelW-8,78,4,h-170,m_offset,m_visible,static_cast<int>(m_manager.plugins().size()));
}
void PluginMenu::onKeyPress(int key,int) {
    navigationFocus();if(key==Key::Escape){m_back();return;}
    if(key==Key::Up){if(m_selected==0&&m_offset>0){--m_offset;rebuild();}else navigateUp(m_buttons,m_selected);}
    else if(key==Key::Down){if(m_selected==static_cast<int>(m_buttons.size())-2&&m_offset+m_visible<static_cast<int>(m_manager.plugins().size())){++m_offset;rebuild();}else navigateDown(m_buttons,m_selected);}
    else if(key==Key::Enter||key==Key::Space)activateSelected(m_buttons,m_selected);
}
void PluginMenu::onMouseMove(double x,double y){pointerFocus(x,y);for(auto& button:m_buttons)button.setHovered(button.containsPoint(static_cast<float>(x),static_cast<float>(y)));}
void PluginMenu::onMouseButton(int button,ButtonAction action,double x,double y) {
    if(button!=MouseButton::Left)return;
    if(action==ButtonAction::Press){for(size_t i=0;i<m_buttons.size();++i)if(m_buttons[i].containsPoint(static_cast<float>(x),static_cast<float>(y))){m_pressed=static_cast<int>(i);m_buttons[i].setPressed(true);break;}}
    else if(action==ButtonAction::Release&&m_pressed>=0){const int i=m_pressed;m_pressed=-1;m_buttons[i].setPressed(false);if(m_buttons[i].containsPoint(static_cast<float>(x),static_cast<float>(y)))m_buttons[i].activate();}
}
void PluginMenu::onScroll(double offset){m_offset+=offset>0?-1:offset<0?1:0;rebuild();}
void PluginMenu::onPointerCancel(){cancelButtons(m_buttons);m_pressed=-1;}
