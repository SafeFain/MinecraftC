#pragma once

#include "game/Item.h"
#include "renderer/RenderDevice.h"

// All tool geometry is in grip-local coordinates. Bow parts share unit cuboids
// so changing charge only changes transforms, never GPU allocations.
enum class HeldToolPart { Body, BowUpperInner, BowUpperOuter,
    BowLowerInner, BowLowerOuter, StringUpper, StringLower, Arrow };
struct HeldToolRange {
    HeldToolPart part = HeldToolPart::Body;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};
struct HeldToolModel {
    MeshData mesh;
    std::vector<HeldToolRange> ranges;
};
struct HeldItemUseState {
    bool bowCharging = false;
    float bowCharge = 0.0f;
    float shieldRaise = 0.0f;
};

bool hasHeldToolModel(ItemId id, ToolKind tool);
HeldToolModel buildHeldToolModel(ItemId id, ToolKind tool, ToolTier tier);
TextureData buildHeldToolTexture();
HeldItemUseState advanceHeldItemUseState(HeldItemUseState previous,
    bool charging, float charge, bool blocking, float dt);
glm::mat4 heldToolPartTransform(HeldToolPart part, float charge);
glm::mat4 heldToolGripTransform(ItemId id, ToolKind tool, bool firstPerson);
