#include "ui/TouchControls.h"
#include "ui/UIRenderer.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

void TouchControls::configure(int width, int height, const TouchControlConfig& config) {
    m_width = std::max(1, width); m_height = std::max(1, height); m_config = config;
    const float scale = std::clamp(config.size, .75f, 1.5f);
    const float button = 52.0f * scale, gap = 10.0f * scale, margin = 18.0f * scale;
    m_moveRadius = 58.0f * scale;
    const bool left = !config.leftHanded;
    m_moveCenter = {left ? margin + m_moveRadius : m_width - margin - m_moveRadius,
                    margin + m_moveRadius + 42.0f};
    m_moveArea = {m_moveCenter.x - m_moveRadius, m_moveCenter.y - m_moveRadius,
                  m_moveRadius * 2.0f, m_moveRadius * 2.0f};
    const float actionX = left ? m_width - margin - button : margin;
    m_jump = {actionX, margin + button + gap + 42.0f, button, button};
    m_sneak = {actionX, margin + 42.0f, button, button};
    m_attack = {actionX - (left ? button + gap : -(button + gap)),
                margin + button + gap + 42.0f, button, button};
    m_use = {m_attack.x, margin + 42.0f, button, button};
    m_inventory = {margin, m_height - margin - button * .72f, button * 1.25f, button * .72f};
    m_command = {(m_width - button * 1.25f) * .5f,
                 m_height - margin - button * .72f, button * 1.25f, button * .72f};
    m_pause = {m_width - margin - button * 1.25f, m_height - margin - button * .72f,
               button * 1.25f, button * .72f};

    const float slot = Config::HOTBAR_SLOT_SIZE, hotbarGap = Config::HOTBAR_GAP;
    const float total = 9.0f * slot + 8.0f * hotbarGap;
    const float start = (m_width - total) * .5f;
    for (int i = 0; i < 9; ++i)
        m_hotbar[static_cast<size_t>(i)] = {start + i * (slot + hotbarGap), 0, slot, slot + 14};
}

TouchControls::Target TouchControls::targetAt(float x, float y, int& slot) const {
    if (m_inventory.contains(x,y)) return Target::Inventory;
    if (m_command.contains(x,y)) return Target::Command;
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
void TouchControls::cancelAll(){m_touches.clear();m_move={0,0};m_lookDelta={0,0};m_jumpHeld=false;m_sneakHeld=false;}

void TouchControls::render(UIRenderer& ui) const {
    const float alpha=std::clamp(m_config.opacity,.35f,1.0f);
    const auto draw=[&](const TouchRect&r,const char*label){ui.drawRect(r.x,r.y,r.w,r.h,{.08f,.09f,.12f,alpha});
        const auto s=ui.measureText(label,1.0f);ui.renderText(label,r.x+(r.w-s.x)*.5f,r.y+(r.h-s.y)*.5f,1.0f,{1,1,1});};
    ui.drawRect(m_moveArea.x,m_moveArea.y,m_moveArea.w,m_moveArea.h,{.08f,.09f,.12f,alpha*.7f});
    const glm::vec2 knob=m_moveCenter+m_move*m_moveRadius;
    ui.drawRect(knob.x-16,knob.y-16,32,32,{.75f,.78f,.85f,alpha});
    draw(m_jump,ui.localization().text("touch.jump").c_str());
    draw(m_sneak,ui.localization().text("touch.sneak").c_str());
    draw(m_attack,ui.localization().text("touch.attack").c_str());
    draw(m_use,ui.localization().text("touch.use").c_str());
    draw(m_inventory,ui.localization().text("touch.inventory").c_str());
    draw(m_command,ui.localization().text("touch.command").c_str());
    draw(m_pause,"II");
}
