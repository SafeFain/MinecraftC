# Test contracts and execution

Run commands from the repository root. Use the configured build directory; the
examples use `build-local` (Windows multi-config builds also need `-C Release`).

```sh
ctest --test-dir build-local -L fast --output-on-failure
ctest --test-dir build-local -L worldgen --output-on-failure
ctest --test-dir build-local -L assets --output-on-failure
ctest --test-dir build-local -L vulkan --output-on-failure
ctest --test-dir build-local --output-on-failure
```

`fast` excludes the known expensive asset/regeneration, terrain and GPU checks;
it is a feedback subset, not a substitute for the complete suite. `slow`,
`assets`, `worldgen`, `integration` and `vulkan` are overlapping labels.
Every test has an explicit timeout. Asset regeneration and terrain checks retain
larger bounds rather than being silently omitted from the full suite.
Audio engine checks and Natural manifest/coverage checks are separate from their
complete regeneration tests, so the fast subset still exercises those components.

Desktop test builds register Vulkan readbacks by default. Explicitly configure
`-DMINECRAFTC_VULKAN_TESTS=OFF` for a CPU-only job; this excludes GPU coverage and
must be reported. Linux CI runs them with Xvfb and Mesa Lavapipe; Windows/macOS
CI explicitly select CPU/package coverage. Locally the runner skips only when
there is no display and no Xvfb; device, shader and readback errors fail. Vulkan
tests share the `vulkan_device` resource lock. Their runner has a 220-second
budget, including both default and Natural texture runs; CTest allows 240 seconds
so the runner can print a timeout diagnostic before the outer timeout.

## Files and failure diagnosis

`minecraftc_add_test` launches each invocation through `tests/run_test.py`.
Each child receives a unique short temporary root under `<build>/t`, independently
of inherited TEMP/TMP/TMPDIR. Working directories and explicit asset arguments are
preserved. This isolates legacy fixed-name fixtures and separate CTest processes.
Successful and skipped runs clean up; failed runs report and retain their root.
Directly invoking a test executable bypasses this isolation.

Filesystem-using C++ entrypoints report uncaught exceptions through TestSupport.
Filesystem exceptions include their operation, paths and OS error code. The test
runner separately identifies launch/directory errors as `INFRASTRUCTURE`, and
reports abnormal native exits by test name, decimal status and hexadecimal code.

A failed assertion does not automatically establish a production defect. Triage
must state which of these the evidence supports:

- Production violates an unchanged public behavior, compatibility or resource contract.
- The requirement intentionally changed; update its documented contract and tests together.
- The test overconstrains implementation, content order, tuning or wording; replace it
  with an assertion that still detects the original fault.
- Infrastructure prevented execution; repair the fixture/environment and rerun. Do not
  describe an access-denied crash or skipped GPU check as a passing assertion.

Record expected/actual behavior, the smallest reproducer and the failing command.
Do not change production just to satisfy an incidental snapshot, add sleeps/retries
to hide races, or claim a full run passed after only isolated reruns passed.

## Before adding or rewriting a test

Include these answers in the review or scenario comment:

| Question | Required evidence |
| --- | --- |
| What contract is protected? | A public behavior, format/protocol, ownership, bounded-work or generation invariant. |
| What fault can it detect? | A concrete broken behavior; use a mutation/negative control for coverage guards. |
| Is it already covered? | Name existing scenarios and explain the distinct gap. |
| Does it survive implementation changes? | Avoid private fields, container order, authored counts, shader source text and translation wording. |
| What does it cost? | Isolated fixtures, bounded waits, a suitable label/timeout and measured execution time. |

Exact assertions are appropriate when the number is the contract: frozen serialized
IDs, packet bounds, specified combat formulas, a fixture's round-trip cardinality,
or duplicate resource submissions in a controlled one-object scenario. Explain the
reason. A content catalog must instead prove membership, uniqueness and semantic
classification. It need not freeze where an item appears or how many items a tab has.
Test authored asset mappings against their metadata and controlled remapping fixtures.
Pure Arabic shaping fixtures test an algorithm; shipped translation sentences may
change without invalidating that algorithm.

## Coverage guard decisions

- API boundary scanner regressions introduce forbidden dependencies in `.cpp`,
  `.inl` and `.inc`, then verify CLI failure and useful locations. Comment/literal
  fixtures must pass; adapters have exact ownership rules. File-wide baseline
  exceptions are rejected. This is a lexical guard, not a C++ name-resolution proof.
- Natural GPU checks render independent default/enabled runs with the same scene,
  fixed lighting and an unchanged empty background. Opaque terrain, translucent
  terrain, dropped items, held tools and UI icons are isolated so one working
  consumer cannot hide another missing override. Changes require more than stray
  pixels; harness mutations verify each missing consumer is rejected.
- UI readbacks repeat unchanged controls, require a visible changed area in the
  expected region and reject changes outside it. HUD/orbs, glint and falling anvil
  are isolated. Synthetic negative controls cover missing draws, single pixels,
  unrelated draws, unstable backgrounds and invalid capture sizes. Registered
  Heaven grading is intentionally subtle: it uses exact stable controls with a
  multi-pixel change requirement rather than a screenshot snapshot.
- VulkanDiagnostics declares CPU-value capture/ray operations and history validity.
  Implementation and GPU ABI assertions live in the Vulkan adapter. The boundary
  guard rejects renderer-internal includes from both source and test drivers.
  Particle shader time can be frozen/reset for reproducible diagnostic captures;
  ordinary rendering keeps the presentation clock.
- Application loading checks progress bounds, monotonic completion and readiness,
  independently of progress weights and cache directories. Public LOD tests cover
  exact-cache deferral/resumption. Localized strings are compared with authored
  dictionaries/overrides; controlled grammar and Arabic fixtures test algorithms.
- One-chunk upload counts remain intentional ownership/deduplication guards:
  stale mesh work must not acquire GPU resources and each live replacement has
  one owner. They do not freeze a global streaming or content inventory.
- LAN LOD subscription identity/revision handling lives in the runtime-owned
  `LodSubscriptions` component. Controlled completion events test cancel/re-add,
  duplicate work, invalidation, backpressure and range pruning without sleeps,
  session friendship or a fixed global worker budget. Two compiled fault variants
  confirmed rejection of stale identity and premature pending-work release.
- LAN replica and plugin-generator LOD tests observe real mesh uploads and render
  submissions. Reference meshes come from public generation/mesh contracts;
  drivers no longer read resident tile/replica containers or write LOD selections.
  Exact cache reuse across approximation revisions, negative-coordinate cold edits,
  material replacement and coarse removals retain end-to-end coverage. Transient
  client cleanup compares the owned fixture's file set, not cache subdirectory names.
- Overworld terrain, structure placement, ore behavior, ecology/blueprints and
  mesh/material/light handoff run as separate CTests. Historical terrain fault
  seeds and region/singleton boundary comparisons are retained. The focused lapis
  case protects both host variants and the buried-only exposure gate at the four
  chunks adjoining the origin; full-generation equivalence covers determinism.

The original audit is a baseline, not a certification of the repaired tree.
The [104-row follow-up ledger](testing-audit-followup.csv) preserves its original
judgments/line references and distinguishes repaired, retained-contract and
pending-validation dispositions; these statuses are not a new A/B/C rating.
[Contract decisions](testing-contract-review.md) explain retained exact assertions,
coverage ownership and the public session composition boundary. Modified scenarios
must complete validation before their ledger status becomes implemented.
