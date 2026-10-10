# LAN multiplayer implementation

Status: **Implemented and validated locally on Linux/software Vulkan.
Platform build and device coverage are distinguished below.**

## Playing on a LAN

1. Load a compatible world, open Pause → Open to LAN, choose the port and room
   capacity, then open the room. The default port is TCP 25565, capacity includes
   the host, and PvP starts disabled. The host can change PvP or close the room.
2. On another installation, choose LAN Multiplayer from the main menu. Refresh
   the room list, enter a nickname, then select a compatible room with space.
   Direct connection accepts the host's numeric IPv4 or IPv6 address and port;
   IPv6 link-local addresses use a numeric scope suffix such as `%5`.
3. Wait for the selected terrain, lighting and meshes to finish loading. Escape,
   gamepad Back or the Cancel button leaves a connecting/loading session. Use
   the ordinary gameplay controls and chat. Administrative commands belong to
   the host and retain the world's normal cheats permission.

Discovery uses `_minecraftc._tcp.local.` and multicast UDP 5353. A room uses two
TCP connections on the chosen host port, separating control from terrain transfer.
Devices need a mutually reachable local network; a numeric direct connection is
available when multicast discovery is unavailable. Host pause keeps the room
running. Sending the host application to the background saves players and closes
the room; sending a client to the background leaves its session.

The local user-data `lan-identity` file supplies a persistent identity/credential.
The host keeps guest profiles under each world's `players` directory. Rejoining
restores that guest's inventory, health, bed spawn and dimension positions.
Protocol/game/generation incompatibilities and mismatching gameplay plugins reject
admission. Matching gameplay plugins are supported; visual plugins may differ. The vanilla host save stays at v18 with generation
v20 and Heaven v9; guest sidecars use their independent profile v3 palette format (v1/v2 remain readable).

## Implemented foundation

- `Platform::NetworkSocket` owns nonblocking native TCP handles. IPv4/IPv6 numeric
  connections, a dual-stack listener, bounded reads/writes and closure/error
  handling are isolated inside the native adapter. Windows links Winsock.
- `Lan::Decoder` parses fragmented/coalesced TCP frames. Protocol v3 uses a
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
  atomically replaced per-world player sidecars. Profile v2 retains v1 reading and adds held cursor/crafting contents. It contains independent
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
  cannot satisfy the host's rendering/loading gate. GameSession submits interests for independent guest players and retains occupied
  dimensions while the host travels.

## Current application integration

Main menu → LAN Multiplayer lists discovered rooms and offers direct numeric
IPv4/IPv6 address, port and nickname entry. Desktop uses pinned mjansson/mdns;
Apple uses Bonjour and Android uses NSD. Native services and platform metadata
are implemented; physical/cross-platform validation remains outstanding.
In a loaded world, Pause → Open to LAN opens/closes a 2–8-player room and controls
PvP (off by default). Gameplay plugins must match across participants; see [plugin LAN compatibility and SDK](plugins.md#lan-multiplayer-and-abi-1-extension). Host pause and
loading keep room simulation/polling active. Connection/loading can be cancelled by Escape, gamepad Back or a pointer/touch button. Room chat uses authoritative names, bounded UTF-8 messages and per-player rate limits. Join/leave notices are localized. Background closes a host room or
leaves a joined room; disconnected clients return to the main menu.

Each dimension retains its world, entities, environment and save ownership.
Guest movement intent is simulated with server elapsed time; client movement is
predicted and corrected. Server snapshots carry private owner storage/crafting,
public other-player views and clocks/weather. Near terrain uses bounded RLE
snapshots and revision deltas; clients derive lighting/meshes and request full
recovery on a revision gap. They do not generate/edit/save authoritative near chunks.

Inventory screens submit logical gestures, serialize one pending request at a
time and wait for acknowledgement. Window IDs, inventory/container fingerprints,
permissions, reach, destruction and dimension changes reject stale requests.
Authority handles death/disconnect cursor cleanup, personal profiles, fishing,
per-player bed occupancy/respawn/travel and per-dimension sleep percentages.
Player melee/sweep and arrows respect room PvP and identify the shooter separately
from entity IDs. Host-only administrative commands reject guest local execution.

Entity snapshots carry at most 512 live and 128 dying entities per player interest,
including item stacks and villager appearance/quotes. Replicas interpolate visual
instances and never run authoritative entity AI. Other players share model GPU
assets but retain separate animations, held-item use and fishing lines. Trade
requests bind inventory/quote revisions and recheck server reach and sight. Live
game rules and difficulty update clients after bootstrap.

Seeded distant terrain has an owned temporary cache. Selected LOD tile requests
receive host persistent edits, including cold saved chunks, with versioned
invalidations and stale-result rejection. Eight client requests, 4096 subscriptions
and two global server worker jobs bound this lane. Subscription identities reject
late results after cancel/re-add; superseded CPU replacements never cross the
GPU upload boundary. Authoritative near terrain
still never generates locally. Global roster includes both dimensions. Feedback
events provide combat, fishing, explosions, lightning and interactive block sounds;
owner mining and held-item visuals are replicated. LAN labels cover all ten languages.

## Platform validation

Linux Release and the explicit `vulkan_lan_smoke` target build locally. Independent
host/client processes pass full terrain loading, authoritative movement and 100
joined Vulkan frames at 960×640 and 360×640 under Xvfb/llvmpipe. Captures show the
remote host and held sword, villager, dropped item, terrain and roster. This is
software Vulkan coverage; physical GPU and separate-device latency are unmeasured.

Real TCP regression covers near recovery, private inventory/window/trade races,
chat, mixed mob/item/arrow/TNT presentation, cold negative and coarse distant edits,
subscription cancellation, stale pending/in-flight meshes, maximum LOD selection,
guest-only Heaven terrain loading, reconnect, destroyed-bed fallback and manual/
immediate respawn. The complete final CTest suite passed 63/63 in 537.51s,
including real local DNS-SD without skips. The last host-loading persistence
check also passed; host saves guest profiles and terrain while changing dimensions.

| Platform | Local evidence | Remaining coverage |
|---|---|---|
| Linux | Release build, real loopback TCP/mDNS, joined software Vulkan | Physical GPUs and separate devices |
| Windows | MinGW TCP/discovery object compilation and session/protocol syntax | Full MSVC build, Windows runtime/device discovery |
| Android | SDK 35 Java compilation, NDK arm64 API 29 native/session/SDL-entry syntax | Gradle APK and device lifecycle/discovery |
| macOS/iOS | Bonjour implementation and service/privacy bundle metadata; earlier public-header API check | Apple SDK builds and device permission/discovery |

An Android SDK/NDK is available locally; Gradle and a wrapper are unavailable.
An Apple SDK/device is unavailable on this Linux host. Local syntax checks are
not packaged application or cross-device execution tests. Movement prediction
uses correction against received authority state; it does not rewind/replay an
input history. Desktop discovery re-enumerates interfaces when refreshed.

## Reproducing the joined Vulkan check

Build the optional scene, then run its orchestrator on a display:

```bash
cmake --build build-local --target vulkan_lan_smoke -j2
python3 tools/lan_vulkan_smoke.py --binary build-local/vulkan_lan_smoke --assets assets
```

On Linux without a display, prefix the Python command with `xvfb-run -a` and set
`SDL_VIDEODRIVER=x11`; select the installed Vulkan driver as appropriate. Add
`--width 360 --height 640` for portrait coverage. Each run creates an isolated
temporary directory containing host/client logs, saves, movement completion and
`joined.ppm`; `--output <directory>` retains runs beneath a chosen evidence root.

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
