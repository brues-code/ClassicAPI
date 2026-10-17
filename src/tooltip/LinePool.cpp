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

// Lift GameTooltip's 30-line cap (TODO #97) — pure C++, no addon Lua.
//
// Vanilla `GameTooltip:AddLine` (FUN_00530270) silently drops any line
// past `numLinesAllocated`, and that count is just "how many contiguous
// `<name>TextLeftN`/`TextRightN` FontStrings the FrameXML template
// declared" — established once by the tooltip's XML-OnLoad region scan
// (FUN_00529650, vtable slot 9). Stat-heavy tooltips (pfUI's eqcompare
// block) lose their tail past line 30.
//
// We grow the pool the same way 3.3.5's AddLine grows on demand, but at
// scan time instead of per line: co-hook the scan, and after the engine
// has set up the template's 30 lines, create `TextLeft/Right 31..60` in
// C++ (mirroring Script_CreateFontString + SetFontObject + SetPoint) and
// append them to the tooltip's three line arrays. This runs once per
// GameTooltip-family frame at creation (and per `/reload`), so it
// auto-covers GameTooltip, ShoppingTooltip1/2, ItemRefTooltip,
// WorldMapTooltip, … with no frame list and no `AddLine` hook.
//
// Everything the grow needs — parent frame, the previous line (for its
// anchor target + font), and the array data pointers — comes straight
// from the just-populated tooltip, so there's no `_G` name lookup. The
// new lines are still *named* (`FUN_REGION_SET_NAME`) so addons that read
// `_G[name.."TextLeft"..i]` keep working and a re-run of the scan
// recounts them idempotently. See TODO #97 for the full RE trail.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdio>

namespace Tooltip::LinePool {

namespace {

// Grow every GameTooltip-family frame to this many lines. pfUI's worst
// case is ~32; 60 leaves headroom without an unbounded pool.
constexpr uint32_t kMaxLines = 60;

// --- engine entry points (all __thiscall; see Offsets.h) -------------------
using GetName_t = const char *(__thiscall *)(void *self);
using PoolAlloc_t = void *(__thiscall *)(void *pool, int zeroInit, const char *tag, int line);
using FontStringCtor_t = void *(__thiscall *)(void *mem, void *parent, int layer, int sublayer);
using SetName_t = void(__thiscall *)(void *region, const char *name);
using SetFont_t = void(__thiscall *)(void *fontHolder, void *fontObject);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using ReallocArray_t = void(__thiscall *)(void *descriptor, uint32_t newCount);

// The scan we co-hook: __thiscall(tooltip, node, flag). Hooked via the
// standard __fastcall(ecx, edx, …) shim — __thiscall passes `this` in ECX
// and the rest on the stack, so the trailing stack args line up and the
// phantom `edx` is harmless.
using Scan_t = void(__fastcall *)(void *tooltip, void *edx, void *node, int flag);
Scan_t g_scanOriginal = nullptr;

uint8_t *DescData(uint8_t *tooltip, int descOffset) {
    return *reinterpret_cast<uint8_t **>(tooltip + descOffset + 8);
}

// Reads a FontString's current font object (holder+4). Used to copy the
// previous line's font onto a new one so it renders identically.
void *FontObjectOf(void *fontString) {
    if (fontString == nullptr)
        return nullptr;
    return Game::Read<void *>(fontString, Offsets::OFF_FONTSTRING_FONT_OBJECT);
}

// Builds one CSimpleFontString: alloc → ctor(parent) → name → font (copied
// from `fontSource`) → anchor. Mirrors Script_CreateFontString's core plus
// the SetFontObject / SetPoint the Lua caller would do. Returns nullptr on
// allocation failure.
void *CreateLine(void *parent, const char *name, void *fontSource, int point,
                 void *relRegion, int relPoint, float x, float y) {
    auto alloc = reinterpret_cast<PoolAlloc_t>(Offsets::FUN_REGION_POOL_ALLOC);
    void *mem = alloc(reinterpret_cast<void *>(Offsets::VAR_SIMPLEFONTSTRING_POOL), 0,
                      reinterpret_cast<const char *>(Offsets::VAR_SIMPLEFONTSTRING_CLASS_TAG), -2);
    if (mem == nullptr)
        return nullptr;

    auto ctor = reinterpret_cast<FontStringCtor_t>(Offsets::FUN_SIMPLEFONTSTRING_CTOR);
    void *fs = ctor(mem, parent, Offsets::DRAWLAYER_ARTWORK, 1);
    if (fs == nullptr)
        return nullptr;
    auto *fsBytes = reinterpret_cast<uint8_t *>(fs);

    auto setName = reinterpret_cast<SetName_t>(Offsets::FUN_REGION_SET_NAME);
    setName(fs, name);

    void *fontObject = FontObjectOf(fontSource);
    if (fontObject != nullptr) {
        auto setFont = reinterpret_cast<SetFont_t>(Offsets::FUN_FONTSTRING_SET_FONT);
        setFont(fsBytes + Offsets::OFF_FONTSTRING_FONT_HOLDER, fontObject);
    }

    if (relRegion != nullptr) {
        auto setPoint = reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT);
        setPoint(fsBytes + Offsets::OFF_REGION_ANCHOR, point,
                 reinterpret_cast<uint8_t *>(relRegion) + Offsets::OFF_REGION_ANCHOR,
                 relPoint, Game::PixelToInternal(x), Game::PixelToInternal(y), 1);
    }
    return fs;
}

// Appends one (left, right) line pair to the tooltip's three parallel
// arrays and bumps `numLinesAllocated` — the C++ equivalent of the
// engine's own FUN_0061fe30. Reallocs each array via its {count,cap,data}
// descriptor (the reallocator preserves `cap` old elements; we set the new
// cap to match, as the scan does).
void AppendLine(uint8_t *tooltip, void *left, void *right) {
    auto reallocPtr = reinterpret_cast<ReallocArray_t>(Offsets::FUN_TOOLTIP_REALLOC_PTR_ARRAY);
    auto reallocInt = reinterpret_cast<ReallocArray_t>(Offsets::FUN_TOOLTIP_REALLOC_INT_ARRAY);

    const uint32_t index =
        Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES_ALLOC);
    const uint32_t newCount = index + 1;

    reallocPtr(tooltip + Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC, newCount);
    Game::Ref<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC + 4) = newCount;
    reallocPtr(tooltip + Offsets::OFF_GAMETOOLTIP_TEXTRIGHT_DESC, newCount);
    Game::Ref<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTRIGHT_DESC + 4) = newCount;
    reallocInt(tooltip + Offsets::OFF_GAMETOOLTIP_WRAPFLAG_DESC, newCount);
    Game::Ref<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_WRAPFLAG_DESC + 4) = newCount;

    reinterpret_cast<void **>(DescData(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC))[index] = left;
    reinterpret_cast<void **>(DescData(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTRIGHT_DESC))[index] = right;
    reinterpret_cast<uint32_t *>(DescData(tooltip, Offsets::OFF_GAMETOOLTIP_WRAPFLAG_DESC))[index] = 0;

    Game::Ref<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES_ALLOC) = newCount;
}

// Grows `tooltip`'s line pool up to kMaxLines, creating + appending the
// missing `TextLeftN`/`TextRightN` pairs. No-op if the pool is empty
// (scan didn't populate) or already at/above the target.
void GrowPool(uint8_t *tooltip) {
    uint32_t count =
        Game::Read<uint32_t>(tooltip, Offsets::OFF_GAMETOOLTIP_NUM_LINES_ALLOC);
    if (count == 0 || count >= kMaxLines)
        return;

    auto vtable = *reinterpret_cast<void ***>(tooltip);
    auto getName = reinterpret_cast<GetName_t>(vtable[1]); // slot 1 = GetName
    const char *baseName = getName(tooltip);
    if (baseName == nullptr || baseName[0] == '\0')
        return;

    for (uint32_t index = count; index < kMaxLines; ++index) {
        // Previous line supplies both the anchor target and the font.
        void *prevLeft = reinterpret_cast<void **>(
            DescData(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTLEFT_DESC))[index - 1];
        void *prevRight = reinterpret_cast<void **>(
            DescData(tooltip, Offsets::OFF_GAMETOOLTIP_TEXTRIGHT_DESC))[index - 1];
        if (prevLeft == nullptr || prevRight == nullptr)
            break;

        char nameLeft[96];
        char nameRight[96];
        std::snprintf(nameLeft, sizeof nameLeft, "%sTextLeft%u", baseName, index + 1);
        std::snprintf(nameRight, sizeof nameRight, "%sTextRight%u", baseName, index + 1);

        // Left: TOPLEFT anchored to the previous left line's BOTTOMLEFT at
        // (0, -2) — the template's exact chain. Right: RIGHT anchored to
        // this line's own left LEFT at (40, 0).
        void *left = CreateLine(tooltip, nameLeft, prevLeft, Offsets::FRAMEPOINT_TOPLEFT,
                                prevLeft, Offsets::FRAMEPOINT_BOTTOMLEFT, 0.0f, -2.0f);
        if (left == nullptr)
            break;
        void *right = CreateLine(tooltip, nameRight, prevRight, Offsets::FRAMEPOINT_RIGHT,
                                 left, Offsets::FRAMEPOINT_LEFT, 40.0f, 0.0f);
        if (right == nullptr)
            break;

        AppendLine(tooltip, left, right);
    }
}

// Co-hook of the tooltip's XML-OnLoad region scan. Runs the original
// (which counts the template's 30 lines and sizes the arrays), then grows
// the pool. Guarded on the CGameTooltip vtable so we never touch a
// non-tooltip object.
void __fastcall Scan_h(void *tooltip, void *edx, void *node, int flag) {
    g_scanOriginal(tooltip, edx, node, flag);
    if (tooltip != nullptr &&
        *reinterpret_cast<uintptr_t *>(tooltip) == Offsets::VAR_GAMETOOLTIP_VTABLE)
        GrowPool(reinterpret_cast<uint8_t *>(tooltip));
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_GAMETOOLTIP_SETUP_LINES,
    reinterpret_cast<void *>(&Scan_h),
    reinterpret_cast<void **>(&g_scanOriginal)};

} // namespace

} // namespace Tooltip::LinePool
