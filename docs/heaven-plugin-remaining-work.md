# Heaven plugin status and remaining validation

Updated 2026-10-19 from current source and dated task records. Acceptance results
below retain their original baselines and do not certify other platforms/devices.

## Implemented

`official_heaven` is a default-enabled compiled-in provider. Its independent v9
core owns terrain, batch/region generation, multilayer LOD, structures/loot, seeded
spawn, Skyway/Starstep actions, content semantics, localization, materials, models,
particles, sky policy and music. The host supplies generic versioned SDK services.
Disabling the provider removes its registered content after restart, while stable
serialized IDs remain reserved. See [installation and SDK](plugins.md) and
[developer guide](plugin-development.md).

Worlds require save v21/global v20. Ordinary plugin mismatch refusal is strict;
local worlds can now use explicit staged recovery with a complete backup by
default, or explicitly discard that backup. The older survey's blanket
“no recovery” statements no longer describe this behavior. LAN remains strict.

## Validation boundaries

- The [P1 acceptance record](heaven-p1-validation.md) records a Windows Release
  build, 84/84 CTest and real Vulkan checks at its original baseline.
- The [dated survey](history/heaven-migration-survey.md) records a later refusal
  snapshot failure. Subsequent local recovery validation recorded a full 84/84
  run before final journal hardening, followed by focused tests; consult the local
  task archive for exact runs. These historical results do not establish behavior
  at a different baseline.
- Native Linux/macOS/Android/iOS builds, packaged execution, physical mobile
  lifecycle/discovery and additional GPUs require their own evidence. CI resource
  checks do not establish device execution.
- Remaining fixed numeric IDs, legacy save-directory layout and test-only
  historical query adapters are compatibility responsibilities. They are not
  missing provider migration work.

The [original estimate](heaven-plugin-assessment.md) and historical survey are
retained for provenance. Their task estimates and completed P0/P1 lists are not
an active implementation plan. Independent reference and performance methodology
remain in the [P1 validation record](heaven-p1-validation.md).
