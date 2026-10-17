// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// Completes the engine's per-tooltip clear, FUN_GAMETOOLTIP_CLEAR
// (FUN_00530050), in the two places it leaves state behind. All 14
// Set*/builder paths route through this clear (verified via its xrefs), so one
// co-hook covers every way a tooltip is reset. The hook runs no Lua.
//
// 1. Item GUID. The clear zeroes the unit (+0x368), gameobject (+0x370),
//    itemID (+0x398) and spell (+0x39c) identity fields — but NOT the item
//    GUID (+0x380/+0x384) that the item builder writes for CGItem tooltips
//    (SetBagItem / SetInventoryItem / …). The engine never reads that GUID as
//    "current item" (the builder always overwrites it before use), but
//    GameTooltip:GetItem / HasItem / IsEquippedItem read it (via
//    Item::TooltipItem::CurrentID's GUID fallback, which exists because CGItem
//    tooltips leave the itemID field 0). So after the tooltip switched to
//    content that sets no item — SetUnitAura (→ FUN_0052f880), SetEquipmentSet
//    (→ SetText), etc. — the stale GUID made GetItem resolve the previous
//    item. The item builder calls the clear before writing the GUID, so
//    zeroing it here leaves only a real item build to set it.
//
// 2. The line below a texture. FUN_GAMETOOLTIP_ADD_TEXTURE (behind
//    GameTooltip:AddTexture) re-anchors the textured line N and the line
//    below it, N+1, each at the template's 2px gap (VAR_TOOLTIP_LINE_GAP).
//    The clear undoes both but re-anchors line N+1 with a (0, 0) offset
//    (0x00530180: `push 1; push ebx; push ebx; push 6` with ebx = 0), so that
//    line sat 2px too close on every later tooltip from that frame. Here the
//    line goes back to the template's TOPLEFT → BOTTOMLEFT (0, gap). The clear
//    zeroes the texture count, so the textured lines are read before the
//    original runs.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Tooltip::Clear {

namespace {

using Clear_t = void(__fastcall *)(void *self);
Clear_t g_clearOriginal = nullptr;

using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);

constexpr uint32_t kMaxTextures = 3;

// The engine's own condition for touching line N+1 (both in the texture
// routine and in the clear): it is inside the pool and has a fontstring.
void RestoreLineBelow(uint8_t *tooltip, uint32_t line) {
    const uint32_t pool = Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES_ALLOC);
    if (line + 1 >= pool)
        return;
    auto **left = Game::Read<uint8_t **>(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC + 8);
    if (left == nullptr || left[line] == nullptr || left[line + 1] == nullptr)
        return;
    const float gap = Game::PixelToInternal(Game::Read<float>(Offsets::VAR_TOOLTIP_LINE_GAP));
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        left[line + 1] + Offsets::OFF_REGION_ANCHOR, Offsets::FRAMEPOINT_TOPLEFT,
        left[line] + Offsets::OFF_REGION_ANCHOR, Offsets::FRAMEPOINT_BOTTOMLEFT, 0.0f, gap, 1);
}

// True when the texture routine has re-anchored `line` since the clear: a
// texture now on that line or on the one above it.
bool Reanchored(const uint8_t *tooltip, uint32_t line) {
    uint32_t count = Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_TEXTURES);
    if (count > kMaxTextures)
        count = kMaxTextures;
    const auto *lines = Game::Ptr<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTURE_LINES);
    for (uint32_t i = 0; i < count; ++i)
        if (lines[i] == line || lines[i] + 1 == line)
            return true;
    return false;
}

void __fastcall Clear_h(void *self) {
    if (self == nullptr) {
        g_clearOriginal(self);
        return;
    }
    auto *tooltip = static_cast<uint8_t *>(self);
    uint32_t numTextures = Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_TEXTURES);
    if (numTextures > kMaxTextures)
        numTextures = kMaxTextures;
    uint32_t textureLines[kMaxTextures] = {};
    for (uint32_t i = 0; i < numTextures; ++i)
        textureLines[i] = Game::Ptr<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTURE_LINES)[i];

    // The original ends by running OnTooltipCleared, which may add textures
    // again; lines it re-anchored keep the routine's anchors.
    g_clearOriginal(self);

    Game::Ref<uint64_t>(tooltip, Offsets::OFF_TOOLTIP_ITEM_GUID_LO) = 0;
    for (uint32_t i = 0; i < numTextures; ++i)
        if (!Reanchored(tooltip, textureLines[i] + 1))
            RestoreLineBelow(tooltip, textureLines[i]);
}

static const Game::HookAutoRegister _hook{
    Offsets::FUN_GAMETOOLTIP_CLEAR,
    reinterpret_cast<void *>(&Clear_h),
    reinterpret_cast<void **>(&g_clearOriginal)};

} // namespace

} // namespace Tooltip::Clear
