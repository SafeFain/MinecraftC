#include "app/GameSession.h"
#include "game/SurvivalRules.h"
#include <cmath>

namespace {
uint64_t fingerprint(const InventoryModel& inventory, const InventoryTransaction& transaction) {
    uint64_t hash = 1469598103934665603ULL;
    const auto value = [&hash](uint64_t n) { for (int i = 0; i < 8; ++i) { hash ^= static_cast<uint8_t>(n); hash *= 1099511628211ULL; n >>= 8; } };
    const auto stack = [&value](ItemStack item) { value(static_cast<uint16_t>(item.id)); value(item.count); value(item.damage); };
    value(transaction.revision());
    for (auto item : inventory.storage()) stack(item);
    for (auto item : inventory.armor()) stack(item);
    stack(inventory.offhand()); stack(transaction.cursor());
    for (auto item : transaction.crafting()) stack(item);
    return hash;
}
uint64_t fingerprint(const BlockEntity& container) {
    uint64_t hash = 1469598103934665603ULL;
    const auto stack = [&hash](ItemStack item) {
        for (uint64_t n : {uint64_t(static_cast<uint16_t>(item.id)), uint64_t(item.count), uint64_t(item.damage)}) {
            hash ^= n; hash *= 1099511628211ULL;
        }
    };
    hash ^= static_cast<uint8_t>(container.type);
    for (auto item : container.chest) stack(item);
    stack(container.input); stack(container.fuel); stack(container.output);
    return hash;
}
bool reachable(const Player& owner, const World& world, glm::ivec3 position) {
    const float reach = owner.isSurvival() ? 3.0f : Config::REACH_DISTANCE;
    const auto hit = world.raycast(owner.getEyePosition(), owner.getForward(), reach);
    return hit && hit->blockPos == position;
}
bool canOpenWindow(const Player& owner,DimensionSimulation& runtime,InventoryWindowKind kind,glm::ivec3 position) {
    if (owner.isSpectator() || owner.survivalStats().dead() || kind == InventoryWindowKind::Closed) return false;
    if (kind != InventoryWindowKind::Player) {
        if (!reachable(owner, runtime.world, position)) return false;
        const auto block = runtime.world.getLoadedBlock(position.x, position.y, position.z);
        if (kind == InventoryWindowKind::CraftingTable && block != BlockId::CRAFTING_TABLE) return false;
        if (kind == InventoryWindowKind::Container && block != BlockId::CHEST && block != BlockId::FURNACE) return false;
        if (kind == InventoryWindowKind::Container && !runtime.world.getBlockEntity(position)) return false;
    }
    return true;
}
void drops(Player& owner, EntityManager& entities, const std::vector<ItemStack>& items) {
    for (auto item : items) entities.spawnItem(owner.getEyePosition() + glm::dvec3(owner.getForward()) * .65,
        item, owner.getForward() * 4.5f + glm::vec3(0, 1.5f, 0), .8f);
}
}

void GameSession::closeAuthorityWindow(Player& owner, DimensionSimulation& runtime,
                                      InventoryTransaction& transaction, InventoryWindowView& view) {
    drops(owner, runtime.entities, transaction.close(owner.inventory()));
    ++view.id; view.kind = InventoryWindowKind::Closed; view.container.reset();
}
bool GameSession::openAuthorityWindow(Player& owner, DimensionSimulation& runtime,
                                     InventoryTransaction& transaction, InventoryWindowView& view,
                                     InventoryWindowKind kind, glm::ivec3 position) {
    if(!canOpenWindow(owner,runtime,kind,position))return false;
    closeAuthorityWindow(owner, runtime, transaction, view);
    view.kind = kind; view.position = position;
    owner.cancelBowCharge();
    return true;
}
InventoryWindowView GameSession::authorityWindow(Player& owner, DimensionSimulation& runtime,
                                                InventoryTransaction& transaction, InventoryWindowView& view) {
    if (view.kind != InventoryWindowKind::Closed &&
        (owner.isSpectator() || owner.survivalStats().dead() ||
         (view.kind != InventoryWindowKind::Player &&
          glm::length(owner.getEyePosition() - (glm::dvec3(view.position) + .5)) > 6)))
        closeAuthorityWindow(owner, runtime, transaction, view);
    if (view.kind == InventoryWindowKind::CraftingTable &&
        runtime.world.getLoadedBlock(view.position.x, view.position.y, view.position.z) != BlockId::CRAFTING_TABLE)
        closeAuthorityWindow(owner, runtime, transaction, view);
    if (view.kind == InventoryWindowKind::Container) {
        const auto* container = runtime.world.getBlockEntity(view.position);
        if (!container || container->type > BlockEntityType::Furnace)
            closeAuthorityWindow(owner, runtime, transaction, view);
        else { view.container = *container; view.containerRevision = fingerprint(*container); }
    }
    view.revision = fingerprint(owner.inventory(), transaction);
    view.cursor = transaction.cursor(); view.crafting = transaction.crafting();
    return view;
}
void GameSession::applyAuthorityInventory(Player& owner, DimensionSimulation& runtime, InventoryTransaction& transaction,
                                         InventoryWindowView& view, InventoryAction action) {
    const auto snapshot = authorityWindow(owner, runtime, transaction, view);
    // Storage shortcuts work with no screen; cursor/crafting/container gestures
    // require the matching open window and every supplied revision to agree.
    if (owner.isSpectator() || owner.survivalStats().dead() || action.revision != snapshot.revision) return;
    if (snapshot.kind == InventoryWindowKind::Closed &&
        action.operation != InventoryOperation::Drop && action.operation != InventoryOperation::SwapOffhand &&
        action.operation != InventoryOperation::CreativeGrant) return;
    InventoryContext context;
    context.creative = owner.gameMode() == GameMode::Creative;
    context.craftingTable = snapshot.kind == InventoryWindowKind::CraftingTable;
    if (snapshot.kind == InventoryWindowKind::Container) {
        context.container = runtime.world.getBlockEntity(snapshot.position);
        context.containerRevision = snapshot.containerRevision;
    }
    action.revision = transaction.revision();
    const auto result = transaction.apply(owner.inventory(), action, context);
    drops(owner, runtime.entities, result.drops);
    (void)authorityWindow(owner, runtime, transaction, view);
}
InventoryWindowView GameSession::inventoryWindow() const {
    if (lanJoining) return replicaWindow;
    // All windows and commands are queried on the main thread. Refreshing a
    // snapshot also invalidates a destroyed/out-of-range authority window.
    auto& session = const_cast<GameSession&>(*this);
    return session.authorityWindow(session.player, session.simulation(), session.hostWindow, session.hostWindowView);
}
void GameSession::openInventoryWindow(InventoryWindowKind kind, glm::ivec3 position) {
    if (!usesInventoryCommands()) return;
    if (lanJoining) {
        Lan::GameAction action; action.kind = Lan::ActionKind::OpenWindow; action.windowKind = kind; action.position = position;
        queueReplicaAction(std::move(action)); return;
    }
    (void)openAuthorityWindow(player, simulation(), hostWindow, hostWindowView, kind, position);
}
void GameSession::submitInventoryAction(InventoryAction action) {
    if (!usesInventoryCommands()) return;
    if (lanJoining) {
        Lan::GameAction command; command.kind = Lan::ActionKind::Inventory; command.inventory = std::move(action);
        queueReplicaAction(std::move(command)); return;
    }
    const auto snapshot = inventoryWindow();
    action.sequence = ++hostActionSequence; action.revision = snapshot.revision; action.containerRevision = snapshot.containerRevision;
    applyAuthorityInventory(player, simulation(), hostWindow, hostWindowView, std::move(action));
}
void GameSession::closeInventoryWindow() {
    if (!usesInventoryCommands()) return;
    if (lanJoining) { Lan::GameAction action; action.kind = Lan::ActionKind::CloseWindow; queueReplicaAction(std::move(action)); }
    else closeAuthorityWindow(player, simulation(), hostWindow, hostWindowView);
}
void GameSession::queueReplicaAction(Lan::GameAction action) {
    if (!lanWorldReady() || replicaActions.size() >= 32) return;
    action.sequence = ++replicaActionSequence; action.epoch = replicaEpoch;
    replicaActions.push_back(std::move(action)); sendReplicaAction();
}
void GameSession::sendReplicaAction() {
    if (replicaActionInFlight || replicaActions.empty() || lanClient.state() != Lan::Client::State::Connected) return;
    auto action = replicaActions.front();
    if (action.epoch != replicaEpoch) { replicaActions.pop_front(); sendReplicaAction(); return; }
    action.window = replicaWindow.id; action.inventory.revision = replicaWindow.revision;
    if (action.kind != Lan::ActionKind::Trade) action.inventory.containerRevision = replicaWindow.containerRevision;
    if (lanClient.send({Lan::MessageType::Action, 0, Lan::encodeGameAction(action)})) replicaActionInFlight = action.sequence;
}
void GameSession::applyLanAction(LanPlayerRuntime& guest, const Lan::GameAction& action) {
    if (action.sequence <= guest.actionAcknowledged) return;
    guest.actionAcknowledged = action.sequence; guest.lastStateSent = -1;
    if (action.epoch != guest.chunkEpoch) return;
    auto& runtime = *simulations[static_cast<size_t>(guest.profile.dimension)];
    const auto actorEvent=guest.player.pluginEvent(MC_USE_PRE);
    const Plugins::ActorScope actor({actorEvent.player_id,actorEvent.dimension,MC_HOST});
    if (action.kind == Lan::ActionKind::PluginCommand) {
        if(guest.loading||guest.dead||guest.sleep||action.command.empty()||action.command.front()!='/')return;
        guest.chatTokens=std::min(5.0f,guest.chatTokens+static_cast<float>(std::max(0.0,lanNow-guest.chatLast)));
        guest.chatLast=lanNow;if(guest.chatTokens<1)return;guest.chatTokens-=1;
        const bool accepted=Plugins::commandDispatcher()&&Plugins::commandDispatcher()(action.command);
        if(!accepted) {
            const Lan::ChatMessage message{Lan::ChatKind::Message,"","Plugin command unavailable or restricted to the host"};
            (void)lanHost.send(actorEvent.player_id,{Lan::MessageType::Chat,0,Lan::encodeChat(message)});
        }
    } else if (action.kind == Lan::ActionKind::OpenWindow) {
        if (!guest.loading && !guest.dead && !guest.sleep && !guest.player.isSpectator()) {
            auto e=guest.player.pluginEvent(MC_USE_PRE);e.item=static_cast<uint16_t>(guest.player.activeItem().id);
            e.x=action.position.x;e.y=action.position.y;e.z=action.position.z;
            e.block=static_cast<uint16_t>(runtime.world.getBlock(e.x,e.y,e.z));
            if(!canOpenWindow(guest.player,runtime,action.windowKind,action.position))return;
            const bool use=action.windowKind!=InventoryWindowKind::Player;
            if(use&&!Plugins::dispatch(e))return;
            if(!openAuthorityWindow(guest.player,runtime,guest.window,guest.windowView,action.windowKind,action.position))return;
            if(use){e.kind=MC_USE_POST;e.cancelled=0;Plugins::dispatch(e);}
        }
    } else if (action.kind == Lan::ActionKind::CloseWindow) {
        if (action.window == guest.windowView.id) closeAuthorityWindow(guest.player, runtime, guest.window, guest.windowView);
    } else if (action.kind == Lan::ActionKind::Inventory) {
        if (action.window != guest.windowView.id || guest.loading || guest.dead) return;
        auto inventory = action.inventory; inventory.sequence = action.sequence;
        applyAuthorityInventory(guest.player, runtime, guest.window, guest.windowView, std::move(inventory));
    } else if (action.kind == Lan::ActionKind::Trade) {
        if (guest.loading || guest.dead || guest.sleep || guest.player.isSpectator() || action.inventory.argument >= 5) return;
        const auto snapshot = authorityWindow(guest.player, runtime, guest.window, guest.windowView);
        const auto* entity = runtime.entities.entityById(action.target);
        if (!entity || action.inventory.revision != snapshot.revision ||
            action.inventory.containerRevision != villagerQuoteRevision(entity->villager) ||
            !runtime.entities.villagerUsable(action.target, guest.player.getEyePosition(), guest.player.getForward(), 3.0f)) return;
        (void)runtime.entities.tradeWith(action.target, static_cast<uint8_t>(action.inventory.argument), guest.player.inventory());
        guest.lastEntitiesSent = -1;
    } else if (action.kind == Lan::ActionKind::Respawn) {
        if (guest.dead) travelLanPlayer(guest, DimensionId::Overworld, true);
    } else if (action.kind == Lan::ActionKind::SleepChoice) {
        if ((guest.sleep != 1 && guest.sleep != 2) || action.inventory.argument > 2) return;
        const auto choice = static_cast<SleepAction>(action.inventory.argument);
        if (choice == SleepAction::TravelToHeaven && guest.profile.dimension == DimensionId::Overworld)
            travelLanPlayer(guest, DimensionId::Heaven);
        else if (choice == SleepAction::SleepUntilMorning) {
            guest.wantsMorning = true;
            (void)trySkipLanNight(guest.profile.dimension);
        } else finishLanSleep(guest);
    } else if (action.kind == Lan::ActionKind::PickBlock) {
        if (guest.dead || guest.loading || guest.player.isSpectator()) return;
        const auto hit = runtime.world.raycast(guest.player.getEyePosition(), guest.player.getForward(), Config::REACH_DISTANCE);
        if (!hit) return;
        const ItemId item = itemForBlock(runtime.world.getBlock(hit->blockPos.x, hit->blockPos.y, hit->blockPos.z));
        if (item == ItemId::EMPTY) return;
        auto& items = guest.player.inventory();
        for (size_t i = 0; i < InventoryModel::STORAGE_SIZE; ++i) if (items.slot(i).id == item) {
            if (i < 9) guest.player.setSelectedSlot(static_cast<int>(i));
            else std::swap(items.slot(i), items.slot(static_cast<size_t>(guest.player.selectedSlot())));
            return;
        }
        if (guest.player.gameMode() == GameMode::Creative) {
            InventoryAction grant; grant.operation = InventoryOperation::CreativeGrant;
            grant.slot.index = static_cast<uint8_t>(guest.player.selectedSlot()); grant.argument = static_cast<uint16_t>(item);
            grant.sequence = action.sequence; grant.revision = authorityWindow(guest.player, runtime, guest.window, guest.windowView).revision;
            applyAuthorityInventory(guest.player, runtime, guest.window, guest.windowView, grant);
        }
    }
}

bool GameSession::sendPluginCommand(const std::string& command) {
    if(!lanJoining||!lanWorldReady()||lanClient.state()!=Lan::Client::State::Connected||replicaActions.size()>=32||command.empty()||command.size()>512||command.front()!='/')return false;
    try{Lan::Writer validate;validate.text(command,512);}catch(const Lan::ProtocolError&){return false;}
    Lan::GameAction action;action.kind=Lan::ActionKind::PluginCommand;action.command=command;queueReplicaAction(std::move(action));return true;
}
