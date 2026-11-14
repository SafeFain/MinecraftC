# CTest audit contract decisions

This review preserves the original 104 audit judgments in
[the follow-up ledger](testing-audit-followup.csv). A repaired finding and a
justified retained assertion are different dispositions. Neither means that a
passing run proves every possible fault is covered. Validation results belong in
the task record; pending checks must not be reported as passed.

## Exact assertions that remain intentional

| Audit rows | Contract and fault model |
| --- | --- |
| 19, 20, 22, 104 | Seeded generation is versioned. Cave occupancy bands, the historical v11 village window (12 plains/2 desert), the v3 ore baseline (5781), and Heaven v9 altitude/landing policies detect known distribution or compatibility regressions. They are independent acceptance samples, not copies of the current generator formula. Changing output needs the generation-version decision required by AGENTS.md. BEDROCK returning AIR from the ore query means “no replacement”; it is a non-host exclusion check. |
| 23, 41, 65, 77 | The frozen pre-migration Heaven v9 source/content/particle/model exports are independent compatibility oracles. Hashes protect those references from accidental edits. Full voxels, point columns, fine/coarse LOD and provider-published content cover different adapter paths. Region pending blocks are now replayed before final-output comparison; an empty internal staging channel is not required. |
| 32, 38–44, 63, 64, 93, 98, 99 | Frozen serialized IDs, historical little-endian layouts, XP curves, recipe inputs/results, item durability, regeneration, hunger, split offspring and gameplay deadlines are observable mechanics or compatibility contracts. A test setting three inputs must verify exactly three inputs/results survive. The v25-to-legacy metadata converter explicitly guards its supported schema; v25 adds chunk redstone state and retains the v24 metadata layout. No C++ struct padding is used as the wire reference. |
| 15, 49, 52, 84, 87 | Warm animation evaluation has a no-allocation hot-path contract. The synchronous allocation probe surrounds only warmed mixer calls. The high-speed ray query ceiling guards work proportional to traversed cells. One immutable model load and shared graph identity protect actual cache ownership. The 100-mob fixture checks that its deliberately spawned population survives and receives navigation service. Repeated I/O-queue lifetimes reproduce a historical initialization-order race; the active-task drain check has entry barriers, bounded negative observation and a completion-order assertion. |
| 30, 90, 96 | GI packed bytes and update planes are the CPU/GPU transfer contract. Public cache counters test invalidation, deduplication, bounded work and retirement, not private container layout. Controlled one-chunk mesh uploads/releases check unique GPU ownership. Journal ceilings (512 per chunk, 32768 overall) are memory acceptance budgets; a smaller per-chunk history is allowed. |
| 36, 37, 57, 59, 61, 79, 81, 94 | Controlled fixture cardinalities, callback edges, atomic cursor/item counts, topological dependency order, remapped IDs and bounded queues test externally observable operations. They must not become global authored-content inventories. Pipeline tests resolve named shader dependencies; shape remapping uses the decoded state, not a copied +7 state offset. |
| 6, 16, 29, 35, 53, 72, 75 | Tile-seam, visible-effect and asset-size budgets are acceptance limits. GLB/PNG constants describe file formats; 64×64 skins describe the native skin authoring schema and <20 KiB is an embedded-image budget. Diagnostic substrings identify the offending resource/category without snapshotting whole prose. Native package compilation intentionally requires the configured native toolchain. GPU effects use repeated controls and spatial or brightness relationships, not a driver-specific screenshot. |
| 27, 31, 56, 88 | Controlled coordinate transforms, outward DPI rounding, collision increments, Java water travel, persisted settings and frame-rate invariance remain exact where they describe math or saved inputs. UI margins, preset tuning, tool vertex positions and catalog ordering are not frozen. |

## Coverage ownership and duplicate scenarios

- Weather unit checks own seeded state and precipitation. Command/rule integration
  owns `/weather`, paused weather and sleep behavior; SaveStore tests own weather
  round trips. Repeating all three in WeatherTests would duplicate coverage.
- ModelAsset tests own hierarchy composition, weight normalization and named clip
  lookup. ModelAnimation tests own interpolation/pose sampling. Registry tests own
  resource reuse and fallback events; event crossing is derived from the published
  action definition, with authoritative timing independently covered by combat tests.
- Multiplayer simulation checks multiple actor views and targeting/independent
  dimensions. AI tests own navigation/fairness; combat timing tests compare real,
  null and absent presentation and inject misleading presentation timings. These
  protect different integration failures.
- Catalog operations and staged recovery transactions are separate CTests.
  Recovery progress is driven to a deadline; it does not assume 100 internal steps.
- The no-plugin Nether scenario verifies absent optional registries and unavailable
  Heaven, in addition to running native generation. It is no longer only a missing
  fixture argument.
- Overworld terrain, structures, ores, ecology and mesh handoff are separate CTests.
  The focused lapis test protects ordinary/deepslate hosts and burial at the four
  origin boundary chunks. Existing region/singleton tests own whole-world determinism.

## Composition, isolation and failure handling

Session scenarios supply real workers, transports, dimension and guest factories
through GameSessionServices. Normal application construction still owns its
collaborators. Factories transfer ownership; external services must outlive the
session. Tests observe the collaborators they supplied and invoke actual loading,
interaction, save, fishing, portal and network operations. No GameSession, World or
LodTerrainSystem test friendship, private container getter or injected completion
queue remains. Live-world scenarios must not reload metadata as a state-setting
shortcut: that destroys inactive dimensions and transient entity IDs. Beds/anchors
use real interactions, and Heaven safe positions use a grounded save.

Faulted room closure forwards the original plugin fault to connected clients
while refusing profile/metadata writes. A generic socket-close message must not
erase that diagnostic; the LAN integration exercises this public shutdown path.

Each CTest receives its own writable TEMP/TMP/TMPDIR. Fixed child fixture names
therefore do not collide across tests or concurrent suite invocations. Standalone
animation, glTF, platform and catalog tests additionally reserve RAII directories.
Filesystem entrypoint catches and the launcher report operation/path/status; failed
roots are retained. Assertions still signal failure, and triage must distinguish
production violations, intentional requirement changes, overconstrained tests and
infrastructure errors as described in [testing.md](testing.md). An assertion cannot
infer a requirement change automatically.

Nonblocking network and streaming integration needs condition polling. Use a real
deadline and a short scheduling yield, or a worker barrier when the test controls
the work. Simulation tick counts are virtual time, not wall-clock sleeps. The
30 ms platform test releases a deliberately held sharing lock to exercise transient
atomic-replace retry behavior; it does not guess when a background operation ended.

All registered tests have timeouts and fast/slow labels; expensive asset regeneration
is distinct from runtime engine checks. Display tests have the Vulkan label/resource
lock, desktop registration enabled by default, and a Linux Xvfb/Lavapipe CI lane.
Windows/macOS CI explicitly select CPU/package checks. Local Windows GPU execution and
unexecuted remote/platform runs must be reported separately.

For new tests, the review questions and negative-control requirements in
[testing.md](testing.md#before-adding-or-rewriting-a-test) apply. The API scanner,
pixel comparisons and subscription-race regression have executable mutation
controls; passing positive fixtures alone does not validate a coverage guard.
