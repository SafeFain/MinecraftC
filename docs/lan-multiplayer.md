# LAN multiplayer implementation

Status: **In progress. There is no host/join menu or playable multiplayer session
in the application yet.** This document describes the implemented foundation,
not a completed user-facing feature. The approved task remains active in PLAN.md.

## Implemented foundation

- `Platform::NetworkSocket` owns nonblocking native TCP handles. IPv4/IPv6 numeric
  connections, a dual-stack listener, bounded reads/writes and closure/error
  handling are isolated inside the native adapter. Windows links Winsock.
- `Lan::Decoder` parses fragmented/coalesced TCP frames. Protocol v1 uses a
  little-endian, 20-byte header (magic, protocol, type, length, sequence), a 1 MiB
  payload limit, a 4 MiB connection queue limit and 128 ready-frame limit.
  Scalars reject non-finite values; strings reject NUL and invalid UTF-8.
- `Lan::Host` / `Lan::Client` establish independent control and chunk streams on
  one listener port (default 25565). A random session token binds the chunk stream
  to an admitted control peer. Rooms include the host in their 2–8 player limit;
  simultaneous duplicate identities, incompatible game/generation/content
  signatures and invalid credentials are rejected before admission.
- Handshakes expire after 10 seconds; peers have a 15-second inactivity deadline
  and five-second heartbeats. Application frames use separate monotonically
  increasing counters per channel. Network polling has a per-connection 256 KiB
  I/O allowance per direction. These APIs perform no game or GPU mutation.
- `Lan::ProfileStore` stores local identity and credential plus checked,
  atomically replaced per-world player sidecars. Profile v1 contains independent
  dimension positions, bed spawn, game mode, survival values and inventory. It
  does not change save v18, block IDs or generation output. Admission validation
  only reads existing profiles; callers save a new profile after actual admission.
- `InventoryTransaction` applies logical slot commands to authoritative storage,
  cursor, crafting and optional container data. Sequence/revision checks reject
  replays and stale windows; rejected operations leave inventory state unchanged.
  Creative access, equipment, furnace slot restrictions and recipe inputs are
  checked before committing. Returned/drop contents are explicit results.
  Authority-side close handles disconnect/death even if client sequences are
  exhausted or the old container is no longer valid.
- World streaming accepts up to seven guest interests with radii 2–16 chunks,
  bounded by the existing shared load/generation/retirement windows. CPU
  simulation and local rendered chunk lists are independent. Guest progress
  cannot satisfy the host's rendering/loading gate. The application does not
  currently submit guest interests.

## Integration still required

1. Replace the single-player session ownership with per-dimension simulation and
   separate player runtime state, including independent sleep/death/fishing.
2. Connect authority-side movement/action validation and multi-player mob targeting,
   pickup, combat and explosions. Bind inventory windows and trading to requests
   rather than the current UI's direct writes.
3. Define and implement actual player/entity/environment/chunk payloads, initial
   loading, revision recovery, interest subscriptions, LOD edits, interpolation
   and local movement prediction. Reserved message types are not implementations.
4. Connect profiles to admission, autosave and disconnect; add host-only command
   permissions, PvP configuration and the gameplay-plugin exclusion.
5. Implement DNS-SD discovery, host/join/cancel/room settings, nicknames, chat,
   remote player rendering, localization, mobile permissions and lifecycle.
6. Verify playable multi-process and cross-device sessions. Native adapter code
   alone does not establish Windows/Apple/Android runtime compatibility.

## Regression coverage

The `lan_protocol_and_tcp` CTest uses real loopback sockets for framing,
connection closure, paired stream admission, room capacity, incompatibility and
large chunk-channel transfer alongside control messages. It also checks profile
round trips, corruption, credential mismatch and unsafe profile identities.

`authoritative_inventory_transactions` checks replay rejection, atomic invalid
operations, shared-container races, furnace output restrictions, crafting and
creative permission. World orchestration covers distant positive/negative guest
interests, shared loading budgets, local progress and retirement after departure.

Socket-restricted execution environments must run the real TCP test with local
socket access. A denied socket is a failed/unrun integration check, never a pass.
