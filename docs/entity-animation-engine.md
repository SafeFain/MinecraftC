# Flexible entity animation engine

MinecraftC now drives its eight glTF mobs through versioned JSON action graphs
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

Generator version 2 emits eight deterministic GLBs and adjacent version-1
action graphs. Walk cycles use closed three-key loops and species-specific
phases: diagonal quadrupeds, opposite biped limbs, chicken legs/wings, and four
alternating spider leg groups.

Zombie and spider impacts occur at 0.30 seconds, skeleton arrows release at
0.45 seconds, and blastlings explode at 1.00 second. Range and line of sight
are checked again at the event; skeleton aim is refreshed at release. Save
format version 8 is unchanged because mixer and pending-action state are
transient.

## Validation

Release build, 24/24 CTest, deterministic asset regeneration, installed asset
enumeration, whitespace checking, and xvfb OpenGL startup passed. Manual
in-world review of all eight animations remains an unperformed visual check.
