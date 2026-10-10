#include "app/GameSession.h"
#include <algorithm>

void GameSession::broadcastGameEvent(Lan::GameEvent event, bool ownerOnly, bool locally) {
    if (!hostingLan()) return;
    if (locally && dimension == event.dimension && (!ownerOnly || event.owner == 0)) {
        if (lanEvents.size() < 256) lanEvents.push_back(event);
    }
    if (lanEventsSent >= 128) return;
    ++lanEventsSent;
    for (const auto& entry : guests) {
        const auto& guest = *entry.second;
        if (guest.profile.dimension != event.dimension || (ownerOnly && event.owner != entry.first)) continue;
        if (!ownerOnly && glm::distance(event.position, guest.player.getPosition()) > std::max(160, guest.radius * 16)) continue;
        event.epoch = guest.chunkEpoch;
        (void)lanHost.send(entry.first, {Lan::MessageType::Event, 0, Lan::encodeGameEvent(event)});
    }
}
void GameSession::bindLanFeedback(Player& owner, uint64_t id) {
    auto makeEvent = [this, id](Lan::EventKind kind, glm::dvec3 position) {
        Lan::GameEvent e; e.kind = kind; e.owner = id; e.position = position;
        e.dimension = id ? guests.at(id)->profile.dimension : dimension; return e;
    };
    owner.setBlockBreakCallback([this, id, makeEvent](glm::ivec3 position, BlockId block) {
        if (!id && blockBreakFeedback) blockBreakFeedback(position, block);
        auto event = makeEvent(Lan::EventKind::BlockBreak, position); event.block = block;
        broadcastGameEvent(event, false, id != 0);
    });
    owner.setDamageCallback([this, id, &owner, makeEvent](float amount) {
        if (!id && damageFeedback) damageFeedback(amount);
        if (id) finishLanSleep(*guests.at(id));
        auto event = makeEvent(Lan::EventKind::Damage, owner.getPosition()); event.amount = amount;
        broadcastGameEvent(event, true, false);
    });
    owner.setDefenseCallback([this, id, &owner, makeEvent](const DamageOutcome& outcome) {
        if (!id && defenseFeedback) defenseFeedback(outcome);
        auto event = makeEvent(Lan::EventKind::Defense, owner.getPosition()); event.flags = (outcome.blocked ? 1 : 0) | (outcome.shieldBroken ? 2 : 0); event.amount = outcome.appliedDamage;
        broadcastGameEvent(event, true, false);
    });
    owner.setCombatCallback([this, id, &owner, makeEvent](const CombatFeedback& result) {
        if (!id && combatFeedback) combatFeedback(result);
        auto event = makeEvent(Lan::EventKind::Combat, result.kind == AttackKind::Miss ? owner.getPosition() : result.position); event.flags = static_cast<uint8_t>(result.kind); event.amount = result.damage;
        broadcastGameEvent(event, true, false);
        if (result.kind == AttackKind::Critical || result.kind == AttackKind::Sweep) {
            event.kind = result.kind == AttackKind::Critical ? Lan::EventKind::Critical : Lan::EventKind::Sweep; event.flags = 0; event.position = result.position;
            broadcastGameEvent(event, false, id != 0);
            for (const auto& position : result.sweptPositions) { event.flags = 1; event.position = position; broadcastGameEvent(event, false, id != 0); }
        }
    });
}
void GameSession::presentLanEvents(const Feedback& feedback) {
    auto events = std::move(lanEvents); lanEvents.clear();
    for (const auto& event : events) {
        if (event.dimension != dimension || (lanJoining && event.epoch != replicaEpoch)) continue;
        const bool self = lanJoining ? event.owner == lanClient.peerId() : event.owner == 0;
        const auto delta = event.position - player.getPosition(); const float distance = static_cast<float>(glm::length(delta));
        switch (event.kind) {
        case Lan::EventKind::BlockBreak:
            if (self && blockBreakFeedback) blockBreakFeedback(glm::ivec3(event.position), event.block);
            else particles.emitBlockBreak(glm::ivec3(event.position), event.block);
            break;
        case Lan::EventKind::Critical: if (!self || event.flags) particles.emitCriticalHit(event.position); break;
        case Lan::EventKind::Sweep: if (!self || event.flags) particles.emitSweepAttack(event.position); break;
        case Lan::EventKind::Combat:
            if (self && combatFeedback) combatFeedback({static_cast<AttackKind>(event.flags), event.amount, event.position, {}});
            break;
        case Lan::EventKind::Damage:
            if (self && damageFeedback) damageFeedback(event.amount);
            break;
        case Lan::EventKind::Defense:
            if (self && defenseFeedback) defenseFeedback({0, event.amount, (event.flags & 1) != 0, (event.flags & 2) != 0});
            break;
        case Lan::EventKind::Fishing: {
            const auto kind = static_cast<FishingEventKind>(event.flags);
            if (self && feedback.playFishing && kind != FishingEventKind::Approach) feedback.playFishing(kind);
            if (kind == FishingEventKind::Splash || kind == FishingEventKind::Bite || kind == FishingEventKind::Approach)
                particles.emitFishingSplash(event.position, kind == FishingEventKind::Bite);
            if (self && kind == FishingEventKind::Bite && feedback.rumble) feedback.rumble(.4f, 160);
            break;
        }
        case Lan::EventKind::Explosion:
            particles.emitExplosion(event.position);
            if (feedback.playExplosion) feedback.playExplosion(std::clamp(static_cast<float>(delta.x) / 24, -1.0f, 1.0f), std::clamp(1 - distance / 96, .16f, 1.0f));
            if (feedback.rumble && distance < 32) feedback.rumble(std::clamp(1 - distance / 20, .15f, 1.0f), 260);
            break;
        case Lan::EventKind::Lightning:
            lightningEvents.push_back({event.position, .5f}); particles.appendLightning(event.position);
            if (feedback.playThunder) feedback.playThunder(std::clamp(static_cast<float>(delta.x) / 32, -1.0f, 1.0f), std::clamp(1 - distance / 160, .18f, 1.0f));
            if (feedback.rumble && distance < 64) feedback.rumble(std::clamp(1 - distance / 48, .12f, .8f), 220);
            break;
        case Lan::EventKind::Interaction:
            if (distance < 16 && feedback.playBlockInteraction) feedback.playBlockInteraction(event.flags & 1, event.flags & 2, event.flags & 4);
            break;
        }
    }
}
