#include "ui/PluginMenu.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "core/Input.h"
#include "game/ArabicShaper.h"
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
    const float w=static_cast<float>(width),h=static_cast<float>(height),panelW=std::min(700.0f,w-32),x=(w-panelW)*.5f;
    const UiMenuFooter footer(width,height);
    const bool compact=height<300;
    const float top=h-(compact?42.0f:104.0f),buttonHeight=compact?36.0f:54.0f;
    const float scale=compact?.75f:.9f,lineHeight=compact?12.0f:15.0f;
    std::vector<std::vector<std::string>> descriptions;
    size_t maxLines=0;
    for(const auto& plugin:m_manager.plugins()) {
        auto text=plugin.manifest.description(std::string(languageCode(m_localization.language())));
        if(m_localization.language()==Language::Arabic)text=shapeArabic(text);
        auto lines=text.empty()?std::vector<std::string>{}:wrapTextPixels(text,panelW-52,[&](const std::string& line){return ui.measureText(line,scale).x;});
        if(lines.size()>3){lines.resize(3);lines.back()+="...";}
        maxLines=std::max(maxLines,lines.size());descriptions.push_back(std::move(lines));
    }
    const float rowHeight=buttonHeight+static_cast<float>(maxLines)*lineHeight+8;
    const int visible=std::clamp(static_cast<int>((top-footer.contentBottom)/rowHeight),1,8);
    if(visible!=m_visible){m_visible=visible;rebuild();}
    UiTheme::menuBackground(ui,w,h);UiTheme::panel(ui,x,16,panelW,h-32);
    const auto title=m_localization.text("plugins.title"),hint=m_localization.text("plugins.hint");
    UiTheme::textWithShadow(ui,title,x+16,h-(compact?36:54),UiTheme::fittedScale(ui,title,compact?1.3f:2.0f,panelW-32),UiTheme::TEXT);
    if(!compact)UiTheme::textWithShadow(ui,hint,x+16,h-80,UiTheme::fittedScale(ui,hint,.85f,panelW-32),UiTheme::TEXT_DIM);
    for(size_t i=0;i<m_buttons.size();++i) {
        if(i+1==m_buttons.size()){renderFooter(ui,m_buttons[i],width,height);continue;}
        const float y=top-static_cast<float>(i)*rowHeight-buttonHeight;
        m_buttons[i].setPosition(x+16,y);m_buttons[i].setSize(panelW-32,buttonHeight);
        prepareButton(m_buttons[i]);m_buttons[i].render(ui);
        const auto& lines=descriptions[static_cast<size_t>(m_offset)+i];
        for(size_t line=0;line<lines.size();++line)
            UiTheme::textWithShadow(ui,lines[line],x+26,y-4-static_cast<float>(line+1)*lineHeight,
                UiTheme::fittedScale(ui,lines[line],scale,panelW-52),UiTheme::TEXT_DIM);
    }
    if(static_cast<int>(m_manager.plugins().size())>m_visible)UiTheme::scrollBar(ui,x+panelW-8,footer.contentBottom,4,top-footer.contentBottom,m_offset,m_visible,static_cast<int>(m_manager.plugins().size()));
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
