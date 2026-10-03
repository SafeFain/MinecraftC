#include "ui/TouchControls.h"
#include "ui/UIRenderer.h"
#include "ui/UIStyle.h"
#include "ui/UILayout.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

void TouchControls::configure(int width, int height, const TouchControlConfig& config) {
    m_width = std::max(1, width); m_height = std::max(1, height); m_config = config;
    const UiHotbarLayout hotbar(m_width);
    const float scale=std::min(
        std::clamp(config.size,.75f,1.5f)*std::min({1.0f,m_width/480.0f,m_height/300.0f}),
        std::max(.1f,(m_height-hotbar.y-hotbar.height-20)/170));
    const float button = 52.0f * scale, gap = 10.0f * scale, margin = 18.0f * scale;
    m_moveRadius = 58.0f * scale;
    const float actionBottom=std::max(margin+42,hotbar.y+hotbar.height+12);
    const bool left = !config.leftHanded;
    m_moveCenter = {left ? margin + m_moveRadius : m_width - margin - m_moveRadius,
                    actionBottom+m_moveRadius};
    m_moveArea = {m_moveCenter.x - m_moveRadius, m_moveCenter.y - m_moveRadius,
                  m_moveRadius * 2.0f, m_moveRadius * 2.0f};
    const float actionX = left ? m_width - margin - button : margin;
    m_jump = {actionX, actionBottom+button+gap, button, button};
    m_sneak = {actionX, actionBottom, button, button};
    m_attack = {actionX - (left ? button + gap : -(button + gap)),
                actionBottom+button+gap, button, button};
    m_use = {m_attack.x, actionBottom, button, button};
    m_inventory = {margin, m_height - margin - button * .72f, button * 1.25f, button * .72f};
    m_perspective = {(m_width - button * 1.10f) * .5f,
                 m_height - margin - button * .72f, button * 1.10f, button * .72f};
    m_command = {m_perspective.x - button * 1.20f - gap,
                 m_perspective.y, button * 1.20f, button * .72f};
    m_pause = {m_width - margin - button * 1.25f, m_height - margin - button * .72f,
               button * 1.25f, button * .72f};

    for (int i=0;i<9;++i)
        m_hotbar[static_cast<size_t>(i)]={hotbar.x+hotbar.padX+i*(hotbar.slot+hotbar.gap),
            hotbar.y+hotbar.padY,hotbar.slot,hotbar.slot};

}

TouchControls::Target TouchControls::targetAt(float x, float y, int& slot) const {
    if (m_inventory.contains(x,y)) return Target::Inventory;
    if (m_command.contains(x,y)) return Target::Command;
    if (m_perspective.contains(x,y)) return Target::Perspective;
    if (m_pause.contains(x,y)) return Target::Pause;
    if (m_jump.contains(x,y)) return Target::Jump;
    if (m_sneak.contains(x,y)) return Target::Sneak;
    if (m_attack.contains(x,y)) return Target::Attack;
    if (m_use.contains(x,y)) return Target::Use;
    for (int i=0;i<9;++i) if(m_hotbar[static_cast<size_t>(i)].contains(x,y)){slot=i;return Target::Hotbar;}
    if (m_moveArea.contains(x,y)) return Target::Move;
    return Target::Look;
}

void TouchControls::updateMove(float x, float y) {
    glm::vec2 delta{x - m_moveCenter.x, y - m_moveCenter.y};
    const float length = glm::length(delta);
    if (length > m_moveRadius) delta *= m_moveRadius / length;
    m_move = delta / m_moveRadius;
    if (glm::length(m_move) < .15f) m_move = glm::vec2(0.0f);
}

std::vector<TouchCommandEvent> TouchControls::onTouch(const TouchEvent& event) {
    std::vector<TouchCommandEvent> commands;
    if (event.phase == TouchPhase::Cancel) { cancelAll(); return commands; }
    const float x=static_cast<float>(event.x), y=static_cast<float>(event.y);
    if (event.phase == TouchPhase::Begin) {
        int slot=-1; const Target target=targetAt(x,y,slot);
        const bool occupied=std::any_of(m_touches.begin(),m_touches.end(),
            [target](const auto& entry){return entry.second.target==target;});
        const Target captured=occupied?Target::Look:target;
        if (captured>=Target::Jump && captured<Target::Hotbar)
            m_feedback[static_cast<size_t>(captured)].activate();
        m_touches[event.id]={captured,{x,y},slot};
        switch(captured){
            case Target::Move:updateMove(x,y);break;
            case Target::Jump:m_jumpHeld=true;break;
            case Target::Sneak:m_sneakHeld=true;break;
            case Target::Attack:commands.push_back({TouchCommand::AttackPress});break;
            case Target::Use:commands.push_back({TouchCommand::UsePress});break;
            case Target::Inventory:commands.push_back({TouchCommand::OpenInventory});break;
            case Target::Command:commands.push_back({TouchCommand::OpenCommand});break;
            case Target::Pause:commands.push_back({TouchCommand::Pause});break;
            case Target::Perspective:commands.push_back({TouchCommand::ChangePerspective});break;
            case Target::Hotbar:commands.push_back({TouchCommand::SelectHotbar,slot});break;
            case Target::Look:break;
        }
        return commands;
    }
    const auto found=m_touches.find(event.id); if(found==m_touches.end()) return commands;
    auto& capture=found->second;
    if(event.phase==TouchPhase::Move){
        if(capture.target==Target::Move)updateMove(x,y);
        else if(capture.target==Target::Look){
            const glm::vec2 next{x,y}; m_lookDelta+=(next-capture.last)*m_config.sensitivity;
            capture.last=next;
        }
        return commands;
    }
    commands=release(capture); m_touches.erase(found); return commands;
}

std::vector<TouchCommandEvent> TouchControls::release(Capture capture) {
    switch(capture.target){
        case Target::Move:m_move={0,0};break;
        case Target::Jump:m_jumpHeld=false;break;
        case Target::Sneak:m_sneakHeld=false;break;
        case Target::Attack:return {{TouchCommand::AttackRelease}};
        case Target::Use:return {{TouchCommand::UseRelease}};
        default:break;
    }
    return {};
}

void TouchControls::applyTo(InputState& input) const {
    input.setVirtual(InputAction::MoveRight,std::max(0.0f,m_move.x));
    input.setVirtual(InputAction::MoveLeft,std::max(0.0f,-m_move.x));
    input.setVirtual(InputAction::MoveForward,std::max(0.0f,m_move.y));
    input.setVirtual(InputAction::MoveBackward,std::max(0.0f,-m_move.y));
    input.setVirtual(InputAction::Sprint,glm::length(m_move)>=.90f?1.0f:0.0f);
    input.setVirtual(InputAction::Jump,m_jumpHeld?1.0f:0.0f);
    input.setVirtual(InputAction::Sneak,m_sneakHeld?1.0f:0.0f);
}

glm::vec2 TouchControls::consumeLookDelta(){const glm::vec2 value=m_lookDelta;m_lookDelta={0,0};return value;}
void TouchControls::cancelAll(){m_touches.clear();m_move={0,0};m_lookDelta={0,0};m_jumpHeld=false;m_sneakHeld=false;m_feedback={};}

void TouchControls::render(UIRenderer& ui) const {
    const float alpha=std::clamp(m_config.opacity,.35f,1.0f);
    const auto draw=[&](const TouchRect&r,const std::string&label,Target target){
        const bool pressed=std::any_of(m_touches.begin(),m_touches.end(),
            [target](const auto& t){return t.second.target==target;});
        UiTheme::button(ui,r.x,r.y,r.w,r.h,label,
                        pressed ? UiTheme::WidgetState::Pressed
                                : UiTheme::WidgetState::Normal,
                        false,0.0f,alpha,-1,-1,false,0,&m_feedback[static_cast<size_t>(target)]);
    };
    UiTheme::rounded(ui,m_moveCenter.x-m_moveRadius,m_moveCenter.y-m_moveRadius,
        m_moveRadius*2,m_moveRadius*2,m_moveRadius,UiTheme::withAlpha(UiTheme::BORDER,alpha*.6f));
    UiTheme::rounded(ui,m_moveCenter.x-m_moveRadius+2,m_moveCenter.y-m_moveRadius+2,
        m_moveRadius*2-4,m_moveRadius*2-4,m_moveRadius-2,UiTheme::withAlpha(UiTheme::PANEL_DEEP,alpha*.7f));
    const glm::vec2 knob=m_moveCenter+m_move*m_moveRadius;
    const float knobR=m_moveRadius*.36f;
    UiTheme::rounded(ui,knob.x-knobR,knob.y-knobR,knobR*2,knobR*2,knobR,
        UiTheme::withAlpha(glm::length(m_move)>.01f?UiTheme::ACCENT:UiTheme::BUTTON_HOVER,alpha));
    draw(m_jump,ui.localization().text("touch.jump"),Target::Jump);
    draw(m_sneak,ui.localization().text("touch.sneak"),Target::Sneak);
    draw(m_attack,ui.localization().text("touch.attack"),Target::Attack);
    draw(m_use,ui.localization().text("touch.use"),Target::Use);
    draw(m_inventory,ui.localization().text("touch.inventory"),Target::Inventory);
    draw(m_command,ui.localization().text("touch.command"),Target::Command);
    draw(m_perspective,ui.localization().text("touch.perspective"),Target::Perspective);
    draw(m_pause,"II",Target::Pause);
}
