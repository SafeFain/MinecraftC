# Flexible entity animation engine

MinecraftC now drives its ten glTF mobs through versioned JSON action graphs
and a general layered animation mixer.

## Runtime behavior

- Action graphs declare semantic bindings, any number of ordered layers,
  override/additive blending, node masks, clip timing, fades, priorities,
  queues, and named timeline events.
- Per-entity mixers advance during game updates. Rendering evaluates the final
  local pose, composes joint matrices, and retains main-thread GPU ownership.
- Invalid graphs fall back to a built-in graph. Hostile attack events retain
  safe default timings even when their visual clip or graph is unavailable.
- Locomotion uses accepted autonomous X/Z displacement. Knockback does not
  start walking, blocked movement stops it, and the last valid facing direction
  is retained while idle.

## Assets and combat

Generator version 5 emits ten deterministic GLBs and adjacent version-1
action graphs. Walk cycles use closed three-key loops and species-specific
phases: diagonal quadrupeds, opposite biped limbs, chicken legs/wings, and four
alternating spider leg groups.

Zombie and spider impacts occur at 0.30 seconds, skeleton arrows release at
0.45 seconds, and blastlings explode at 1.00 second. Range and line of sight
are checked again at the event; skeleton aim is refreshed at release. Mixer and
pending-action state remain transient and add no fields to the current save
format version 12.

## Validation

Current regression coverage includes the full CTest suite, deterministic asset
regeneration, installed asset enumeration, whitespace checking, and xvfb Vulkan
startup. Manual in-world review of all ten animations remains an unperformed
visual check. Historical test totals are retained in the corresponding task and
merge records rather than frozen into this current design document.
