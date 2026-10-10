#pragma once

namespace LanAuthorityIntegration {
inline int run() {
    const auto root = std::filesystem::temp_directory_path() / "minecraftc-lan-authority-test";
    std::filesystem::remove_all(root);
    Config::RENDER_DISTANCE = 2;
    GameSession session(root / "saves");
    GameSessionTestAccess::world(session).setThreadPool(nullptr);
    const auto worldId = session.createWorld("LAN authority", 42, GameMode::Survival, Difficulty::Normal, true, WorldType::Superflat);
    session.startWorld(worldId, true, 0);
    GameSessionTestAccess::prepareLanScene(session);
    require(session.openLanRoom(0, 2, true), "loaded world opens authoritative listen server");
    Lan::Client client;
    auto identity = Lan::ProfileStore::localIdentity(root / "guest");
    identity.nickname = "Guest";
    double now = 0;
    auto pump = [&]() {
        now += .001;
        session.pollLan(now);
        client.poll(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    require(client.join("::1", session.lanPort(), GameSession::lanCompatibility(), identity, now), "client starts room connection");
    for (int i = 0; i < 500 && (client.state() != Lan::Client::State::Connected || session.lanGuestCount() != 1); ++i) pump();
    require(client.state() == Lan::Client::State::Connected && session.lanGuestCount() == 1,
            "paired socket admission creates exactly one independent player");
    auto& guest = GameSessionTestAccess::guest(session, client.peerId());
    require(&guest.player.inventory() != &session.inventory(), "guest inventory cannot alias host storage");
    Lan::PlayerInput input;
    input.forward = 1; input.yaw = 90;
    require(client.send({Lan::MessageType::Input, 0, Lan::encodePlayerInput(input)}), "client queues movement intent");
    for (int i = 0; i < 20; ++i) pump();
    const auto hostBefore = session.playerState().getPosition();
    const auto guestBefore = guest.player.getPosition();
    GameSessionTestAccess::tickLanPlayers(session, .05f);
    require(guest.player.getPosition().x > guestBefore.x && session.playerState().getPosition() == hostBefore,
            "server input moves only the identified guest using server elapsed time");
    const auto afterOneTick = guest.player.getPosition();
    for (int i = 0; i < 10; ++i) { client.send({Lan::MessageType::Input, 0, Lan::encodePlayerInput(input)}); pump(); }
    require(guest.player.getPosition() == afterOneTick, "TCP packet count cannot advance player time");
    guest.player.inventory().slot(0) = {ItemId::DIAMOND, 3, 0};
    InventoryAction cursor;
    cursor.sequence = 1;
    require(guest.window.apply(guest.player.inventory(), cursor, {}).accepted, "authoritative window owns picked-up stack");
    GameSessionTestAccess::world(session).setBlock(1,100,1,BlockId::DIAMOND_ORE);
    GameSessionTestAccess::setTerrainReady(session, false);
    bool saveError = false; session.saveNow([&] { saveError = true; });
    Lan::ProfileStore loadingProfiles(root / "saves" / worldId / "players");
    const auto loadingProfile = loadingProfiles.load(identity);
    SaveStore loadingStore(root / "saves" / worldId);
    const auto loadingEdits = loadingStore.loadChunkOverrides(0,0);
    require(!saveError && loadingProfile && loadingProfile->cursor.count == 3 &&
            std::any_of(loadingEdits.begin(), loadingEdits.end(), [](const BlockOverride& edit) {
                return edit.localIndex == static_cast<uint32_t>((100 - Config::WORLD_MIN_Y) * 256 + 17) && edit.block == BlockId::DIAMOND_ORE;
            }), "active host saves guest cursor and terrain even while its own dimension is loading");
    GameSessionTestAccess::setTerrainReady(session, true);
    client.close();
    for (int i = 0; i < 500 && session.lanGuestCount(); ++i) pump();
    require(session.lanGuestCount() == 0, "disconnect retires guest runtime");
    Lan::ProfileStore profiles(root / "saves" / worldId / "players");
    const auto saved = profiles.load(identity);
    require(saved && saved->inventory.slot(0).id == ItemId::DIAMOND && saved->inventory.slot(0).count == 3,
            "disconnect returns cursor contents before durable profile save");
    require(client.join("::1", session.lanPort(), GameSession::lanCompatibility(), identity, now), "guest reconnects with persistent identity");
    for (int i = 0; i < 500 && session.lanGuestCount() == 0; ++i) pump();
    require(session.lanGuestCount() == 1 &&
            GameSessionTestAccess::guest(session, client.peerId()).player.inventory().slot(0).count == 3,
            "reconnection restores guest inventory rather than host inventory");
    Lan::Bytes malformed = Lan::encodePlayerInput(input); malformed[2] = 0xff; malformed[3] = 0xff;
    malformed[4] = 0xff; malformed[5] = 0x7f;
    client.send({Lan::MessageType::Input, 0, malformed});
    for (int i = 0; i < 500 && session.lanGuestCount(); ++i) pump();
    require(session.lanGuestCount() == 0, "non-finite gameplay input disconnects without world mutation");
    session.closeLanRoom();
    session.leaveWorld();
    std::filesystem::remove_all(root);
    std::cout << "LAN gameplay authority integration tests passed\n";
    return 0;
}
}
