# Portable block shapes

Copy this folder into user-data `mods/block_shapes_data/`, enable it in **Plugins**,
then restart. See [plugin development](../../../docs/plugin-development.md).
It works wherever data plugins are supported, including Android and iOS.

Demonstrates slabs/stairs, a normal door/button, a door that villagers can open
but players cannot, and an 80-tick button that arrows cannot trigger. Slabs merge
into stone. Buttons use the game's existing local door power rules.

The translucent box block renders the union of two overlapping boxes while
colliding only within its lower quarter-volume and selecting across the full
cube. A second block has an explicitly empty collision group with a full
selection box. All groups use `[minX,minY,minZ,maxX,maxY,maxZ]` coordinates.
