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

// Tooltip layout that 4.3.4 has and 1.12 lacks: `SetAnchorType`,
// `GetMinimumWidth`, `GetPadding`, and the `ANCHOR_TOP` / `ANCHOR_BOTTOM`
// anchor types.
//
// 1.12 already keeps all of the state these touch: `SetOwner` stores the
// anchor type and its offsets, `SetMinimumWidth` the minimum width, and
// `SetPadding` the padding (see `OFF_TOOLTIP_ANCHOR_TYPE` and its neighbours).
// The methods read and write those same fields, as 4.3.4's script methods do.
//
// `SetAnchorType(type [, x [, y]])` re-anchors a tooltip that already has an
// owner — FrameXML's compare-tooltip code slides the tooltip with it when its
// shopping tooltips would run off the screen. It is the second half of
// `SetOwner`: the same offsets conversion, the same anchor store, then the
// engine's own position routine with force set. The type is only stored when
// the tooltip has an owner, and an unknown name means `ANCHOR_LEFT`, both as in
// 4.3.4 (and as in 1.12's `SetOwner`, which does the same with names).
//
// `GetPadding` returns the stored value as it is, in the engine's internal
// layout units — what 4.3.4 returns. It is not the number given to
// `SetPadding`, which is converted on the way in.
//
// ANCHOR_TOP / ANCHOR_BOTTOM. 1.12's `SetOwner` has nine anchor names and sends
// every other name to `ANCHOR_LEFT`, so an addon asking for `ANCHOR_TOP` quietly
// got the wrong side. Its position routine (FUN_TOOLTIP_RESET_POSITION) is,
// for the six corner anchors, 4.3.4's `ResetPosition` line for line; the two
// missing anchors are the same one-`SetPoint` shape — TOP puts the tooltip's
// bottom edge on the owner's top edge, BOTTOM its top edge on the owner's
// bottom edge. So:
//
//   - they get codes of our own (TOOLTIP_ANCHOR_TOP / _BOTTOM) the engine
//     never stores, so nothing else in the engine has an opinion about them:
//     its fade treats every code but CURSOR as "fade", and `GetAnchorType`'s
//     default is "ANCHOR_NONE";
//   - the position routine is hooked, and only ever acts on those two codes,
//     mirroring the engine's own steps (clear the frame's points, then one
//     `SetPoint`). Everything else goes straight to the original. It is the
//     one routine both `SetOwner` and the tooltip's layout pass position
//     through, so the anchor survives a re-layout;
//   - `SetOwner` and `GetAnchorType` are re-registered in front of the engine's
//     (the most recent registration wins), because the name chain lives inside
//     the Lua method. `SetOwner` hands the engine `ANCHOR_NONE` for the new
//     names — which leaves the offsets, owner and hide to the engine — and sets
//     the code afterwards.
//
// `ANCHOR_CURSOR_RIGHT` is not provided: it needs its own per-frame
// follow-the-mouse step, and no installed addon asks for it.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Tooltip::Layout {

namespace {

using ResetPosition_t = void(__fastcall *)(void *tooltip, void *edx, int force);
using ClearAllPoints_t = void(__fastcall *)(void *layout);
using SetPoint_t = void(__thiscall *)(void *anchor, int point, void *relAnchor, int relPoint,
                                      float x, float y, int flag);
using Script_t = int(__fastcall *)(void *L);

ResetPosition_t g_resetPositionOriginal = nullptr;

struct AnchorName {
    const char *name;
    int code;
};

// 1.12's SetOwner name chain, in its order, then the two names we add.
const AnchorName kAnchors[] = {
    {"ANCHOR_LEFT", Offsets::TOOLTIP_ANCHOR_LEFT},
    {"ANCHOR_RIGHT", Offsets::TOOLTIP_ANCHOR_RIGHT},
    {"ANCHOR_BOTTOMRIGHT", Offsets::TOOLTIP_ANCHOR_BOTTOMRIGHT},
    {"ANCHOR_BOTTOMLEFT", Offsets::TOOLTIP_ANCHOR_BOTTOMLEFT},
    {"ANCHOR_TOPRIGHT", Offsets::TOOLTIP_ANCHOR_TOPRIGHT},
    {"ANCHOR_TOPLEFT", Offsets::TOOLTIP_ANCHOR_TOPLEFT},
    {"ANCHOR_CURSOR", Offsets::TOOLTIP_ANCHOR_CURSOR},
    {"ANCHOR_NONE", Offsets::TOOLTIP_ANCHOR_NONE},
    {"ANCHOR_PRESERVE", Offsets::TOOLTIP_ANCHOR_PRESERVE},
    {"ANCHOR_TOP", Offsets::TOOLTIP_ANCHOR_TOP},
    {"ANCHOR_BOTTOM", Offsets::TOOLTIP_ANCHOR_BOTTOM},
};

// The anchor code for `name`, or -1 when it is none of ours — one the engine's
// own SetOwner would treat as `ANCHOR_LEFT`.
int FindAnchor(const char *name) {
    for (const AnchorName &anchor : kAnchors) {
        if (_stricmp(name, anchor.name) == 0)
            return anchor.code;
    }
    return -1;
}

bool IsAddedAnchor(int code) {
    return code == Offsets::TOOLTIP_ANCHOR_TOP || code == Offsets::TOOLTIP_ANCHOR_BOTTOM;
}

// The position routine. For TOP / BOTTOM, what the engine's switch does for a
// corner anchor: clear the frame's points, then anchor to the owner. The engine
// returns early without an owner, and clears whenever the type is not NONE.
void __fastcall ResetPosition_h(void *self, void *edx, int force) {
    auto *tooltip = static_cast<uint8_t *>(self);
    const int type = Game::Read<int>(tooltip, Offsets::OFF_TOOLTIP_ANCHOR_TYPE);
    auto *owner = Game::Read<void *>(tooltip, Offsets::OFF_TOOLTIP_OWNER);
    if (owner == nullptr || !IsAddedAnchor(type)) {
        g_resetPositionOriginal(self, edx, force);
        return;
    }

    void *layout = tooltip + Offsets::OFF_FRAME_LAYOUT_SUBOBJECT;
    reinterpret_cast<ClearAllPoints_t>(Offsets::FUN_LAYOUT_CLEAR_ALL_POINTS)(layout);

    const bool top = type == Offsets::TOOLTIP_ANCHOR_TOP;
    reinterpret_cast<SetPoint_t>(Offsets::FUN_REGION_SET_POINT)(
        layout, top ? Offsets::FRAMEPOINT_BOTTOM : Offsets::FRAMEPOINT_TOP, owner,
        top ? Offsets::FRAMEPOINT_TOP : Offsets::FRAMEPOINT_BOTTOM,
        Game::Read<float>(tooltip, Offsets::OFF_TOOLTIP_OFFSET_X),
        Game::Read<float>(tooltip, Offsets::OFF_TOOLTIP_OFFSET_Y), 1);
}

const Game::HookAutoRegister _resetPositionHook{
    Offsets::FUN_TOOLTIP_RESET_POSITION, reinterpret_cast<void *>(&ResetPosition_h),
    reinterpret_cast<void **>(&g_resetPositionOriginal)};

// Through the engine's entry, so the hook above sees it.
void ResetPosition(void *tooltip) {
    reinterpret_cast<ResetPosition_t>(Offsets::FUN_TOOLTIP_RESET_POSITION)(tooltip, nullptr, 1);
}

int __fastcall Script_GameTooltipSetAnchorType(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetAnchorType( anchorType [,Xoffset] [,Yoffset] )");
        return 0;
    }
    auto *tooltip = static_cast<uint8_t *>(self);

    const float x = Game::Lua::IsNumber(L, 3)
                        ? Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 3)))
                        : 0.0f;
    const float y = Game::Lua::IsNumber(L, 4)
                        ? Game::PixelToInternal(static_cast<float>(Game::Lua::ToNumber(L, 4)))
                        : 0.0f;
    Game::Ref<float>(tooltip, Offsets::OFF_TOOLTIP_OFFSET_X) = x;
    Game::Ref<float>(tooltip, Offsets::OFF_TOOLTIP_OFFSET_Y) = y;

    if (Game::Read<uintptr_t>(tooltip, Offsets::OFF_TOOLTIP_OWNER) != 0) {
        const int code = FindAnchor(Game::Lua::ToString(L, 2));
        Game::Ref<int>(tooltip, Offsets::OFF_TOOLTIP_ANCHOR_TYPE) =
            code >= 0 ? code : Offsets::TOOLTIP_ANCHOR_LEFT;
    }

    ResetPosition(self);
    return 0;
}

// `SetOwner` with the two added names: the engine's own method with
// `ANCHOR_NONE` in their place (it hides the tooltip, stores the owner and the
// offsets, and positions nothing), then the real code and a position pass. Any
// other call goes to the engine untouched, so its errors stay its own.
int __fastcall Script_GameTooltipSetOwner(void *L) {
    const auto engine = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_OWNER);
    const int code =
        Game::Lua::IsString(L, 3) ? FindAnchor(Game::Lua::ToString(L, 3)) : -1;
    void *self = IsAddedAnchor(code) ? Game::Lua::ResolveTooltip(L, 1, /*raiseError*/ false)
                                     : nullptr;
    if (self == nullptr)
        return engine(L);

    Game::Lua::Remove(L, 3);
    Game::Lua::PushString(L, "ANCHOR_NONE");
    Game::Lua::Insert(L, 3);
    const int n = engine(L);

    auto *tooltip = static_cast<uint8_t *>(self);
    if (Game::Read<uintptr_t>(tooltip, Offsets::OFF_TOOLTIP_OWNER) != 0) {
        Game::Ref<int>(tooltip, Offsets::OFF_TOOLTIP_ANCHOR_TYPE) = code;
        ResetPosition(self);
    }
    return n;
}

// The engine's `GetAnchorType` sends a code it does not know to
// "ANCHOR_NONE", so the two added ones are named here.
int __fastcall Script_GameTooltipGetAnchorType(void *L) {
    void *self = Game::Lua::ResolveTooltip(L, 1, /*raiseError*/ false);
    if (self != nullptr) {
        const int code =
            Game::Read<int>(static_cast<const uint8_t *>(self), Offsets::OFF_TOOLTIP_ANCHOR_TYPE);
        if (IsAddedAnchor(code)) {
            Game::Lua::PushString(L, code == Offsets::TOOLTIP_ANCHOR_TOP ? "ANCHOR_TOP"
                                                                         : "ANCHOR_BOTTOM");
            return 1;
        }
    }
    return reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_GET_ANCHOR_TYPE)(L);
}

// The width `SetMinimumWidth` set, and the second value 4.3.4 returns for its
// force flag. 1.12 has no such flag, so a tooltip is never forced.
int __fastcall Script_GameTooltipGetMinimumWidth(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    Game::Lua::PushNumber(
        L, Game::Read<float>(static_cast<const uint8_t *>(self), Offsets::OFF_TOOLTIP_MIN_WIDTH));
    Game::Lua::PushNil(L);
    return 2;
}

int __fastcall Script_GameTooltipGetPadding(void *L) {
    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;
    Game::Lua::PushNumber(
        L, Game::Read<float>(static_cast<const uint8_t *>(self), Offsets::OFF_TOOLTIP_PADDING));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetAnchorType", &Script_GameTooltipSetAnchorType},
    {"GetMinimumWidth", &Script_GameTooltipGetMinimumWidth},
    {"GetPadding", &Script_GameTooltipGetPadding},
    {"SetOwner", &Script_GameTooltipSetOwner},
    {"GetAnchorType", &Script_GameTooltipGetAnchorType},
};

const Game::Doc::Field kSetAnchorTypeArgs[] = {
    Game::Doc::Req("anchorType", "string", "An anchor name such as \"ANCHOR_RIGHT\"."),
    Game::Doc::Opt("xOffset", "number", "0", "Horizontal offset in pixels."),
    Game::Doc::Opt("yOffset", "number", "0", "Vertical offset in pixels."),
};
const Game::Doc::Function kSetAnchorType{
    "Changes how the tooltip is anchored to its owner, and moves it there.", kSetAnchorTypeArgs, {}};

const Game::Doc::Field kGetMinimumWidthRets[] = {
    Game::Doc::Req("width", "number", "The minimum width set by SetMinimumWidth."),
    Game::Doc::Opt("force", "number", nullptr, "Always nil."),
};
const Game::Doc::Function kGetMinimumWidth{"Returns the minimum width of the tooltip.", {},
                                           kGetMinimumWidthRets};

const Game::Doc::Field kGetPaddingRets[] = {
    Game::Doc::Req("padding", "number", "The tooltip padding in layout units."),
};
const Game::Doc::Function kGetPadding{"Returns the padding of the tooltip.", {}, kGetPaddingRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetAnchorType", &kSetAnchorType},
    {"GetMinimumWidth", &kGetMinimumWidth},
    {"GetPadding", &kGetPadding},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Tooltip::Layout
