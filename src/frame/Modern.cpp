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

// Modern Region/Frame method backports.
//
// Modern addon ports routinely call these methods unconditionally —
// they're the C-level frame surface later clients take for granted.
// This module supplies the ones vanilla lacks (surfaced via
// AI_VoiceOver's modern code path, but generic to every port):
//
//   - `region:SetPoint("RIGHT")` — the one-arg form. Vanilla's parser
//     accepts every other modern form but errors when arg 3 is fully
//     omitted; a co-hook on Script_SetPoint normalizes it to
//     `(point, 0, 0)` (parent-relative, zero offsets — the exact modern
//     semantic).
//   - `region:SetSize(w, h)` / `region:GetSize()` — registered on the
//     REGION base method registry, so they resolve on frames, buttons,
//     textures, fontstrings — everything.
//   - `region:IsMouseOver([top, bottom, left, right])` — cursor-in-rect
//     test with optional per-edge offsets, also on the REGION registry.
//   - `region:GetRect()` — left, bottom, width, height, composed from the
//     engine's Region getters. Also on the REGION registry.
//   - `region:IsDragging()` — true while the region is the frame currently
//     being moved/sized. Reads the engine's global drag target.
//   - `frame:SetShown(shown)` — Show/Hide are per-branch script functions
//     (each with its own object typecheck), so SetShown is registered
//     separately on Frame / Texture / FontString with matching delegates.
//   - `frame:SetResizeBounds(minW, minH [, maxW, maxH])` — the modern
//     rename of vanilla's SetMinResize/SetMaxResize pair.
//   - `frame:HookScript(scriptType, handler)` — absent in vanilla
//     entirely. Chains after any existing handler and invokes `handler`
//     with modern positional args (see the implementation comment).
//   - `frame:GetEffectiveAlpha()` — own alpha × the parent chain's, the
//     product WoW composites at render time. Frame-scoped (matches
//     GetAlpha); walks the parents via the engine's own getters.
//
// Implementation style: each new method delegates to the ENGINE's own
// Script_* implementations by reshaping the Lua stack and calling them
// (`int __fastcall(void *L)`, self at index 1) — no re-derived setters,
// so pixel/UI-scale conversion, object resolution, and type checking are
// all the engine's own code. Values are read out BEFORE any stack
// reshaping.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Frame::Modern {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

int CallScript(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// ---- SetSize / GetSize (Region base — all region types) -------------------

// `region:SetSize(width, height)` → SetWidth + SetHeight.
int __fastcall Script_SetSize(void *L) {
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: region:SetSize(width, height)");
        return 0;
    }
    const double h = Game::Lua::ToNumber(L, 3);
    Game::Lua::SetTop(L, 2); // (self, width)
    CallScript(Offsets::FUN_SCRIPT_REGION_SETWIDTH, L);
    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushNumber(L, h);
    CallScript(Offsets::FUN_SCRIPT_REGION_SETHEIGHT, L);
    return 0;
}

// `region:GetSize()` → width, height.
int __fastcall Script_GetSize(void *L) {
    Game::Lua::SetTop(L, 1);                            // (self)
    CallScript(Offsets::FUN_SCRIPT_REGION_GETWIDTH, L); // (self, w)
    CallScript(Offsets::FUN_SCRIPT_REGION_GETHEIGHT, L); // (self, w, h)
    return 2;
}

// ---- IsMouseOver (Region base — all region types) --------------------------

// `region:IsMouseOver([topOffset, bottomOffset, leftOffset, rightOffset])` —
// true when the cursor is within the region's rect, each edge optionally
// shifted by the matching offset (added to that edge, modern semantics:
// +top/-bottom/-left/+right enlarges). Vanilla addons have always done this
// in Lua as MouseIsOver(); we register it as the native Region method later
// clients expose.
//
// The math mirrors the engine exactly: GetLeft/Right/Top/Bottom (Region
// registry, all types) and GetCursorPosition both return values scaled by the
// same k = FUN_0041ad70()*DAT_007ffd68 factor, but the rect getters also
// divide by the region's effective scale — so dividing the cursor by that same
// effective scale (read from obj+0x7C, the field GetLeft/GetEffectiveScale
// themselves read) puts both in the region's logical space. A getter returns
// nil for an unpositioned region → we report false.

// Calls a Region getter that returns one number; false if it pushed nil/none.
bool CallRegionNumber(void *L, uintptr_t fn, double *out) {
    Game::Lua::SetTop(L, 1); // (self)
    CallScript(fn, L);       // (self, value | nil)
    if (!Game::Lua::IsNumber(L, 2))
        return false;
    *out = Game::Lua::ToNumber(L, 2);
    return true;
}

// `IsMouseOver` and `IsDragging` go on the REGION base registry, which every
// region-family type inherits from — so a frame, texture or fontstring self
// reaches them legitimately, and a Region-only gate would reject the very
// callers the base registration exists to serve. Accepting the four ids
// states that set directly instead of resting on an assumption about which
// classes the engine's `IsA` treats as Regions. Both methods only read
// Region-base state, so the wider set is sound. Non-raising: each reports
// false for an unusable self, the contract they already had.
constexpr uintptr_t kRegionFamilyTypeIds[] = {
    Offsets::VAR_REGION_LUA_TYPE_ID,
    Offsets::VAR_FRAME_LUA_TYPE_ID,
    Offsets::VAR_TEXTURE_LUA_TYPE_ID,
    Offsets::VAR_FONTSTRING_LUA_TYPE_ID,
};

void *ResolveRegionFamily(void *L) {
    return Game::Lua::ResolveTypedObjectAny(
        L, 1, kRegionFamilyTypeIds,
        static_cast<int>(sizeof(kRegionFamilyTypeIds) / sizeof(kRegionFamilyTypeIds[0])),
        /*raiseError=*/false);
}

int __fastcall Script_IsMouseOver(void *L) {
    // Offsets first — the stack gets reshaped by the delegated getters.
    const double offTop = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : 0.0;
    const double offBottom = Game::Lua::IsNumber(L, 3) ? Game::Lua::ToNumber(L, 3) : 0.0;
    const double offLeft = Game::Lua::IsNumber(L, 4) ? Game::Lua::ToNumber(L, 4) : 0.0;
    const double offRight = Game::Lua::IsNumber(L, 5) ? Game::Lua::ToNumber(L, 5) : 0.0;

    void *obj = ResolveRegionFamily(L);
    if (obj == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const float effScale = Game::Read<float>(obj, Offsets::OFF_REGION_EFFECTIVE_SCALE);

    double left, right, top, bottom;
    if (effScale == 0.0f ||
        !CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETLEFT, &left) ||
        !CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETRIGHT, &right) ||
        !CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETTOP, &top) ||
        !CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETBOTTOM, &bottom)) {
        Game::Lua::PushBool(L, false); // no resolved rect
        return 1;
    }

    Game::Lua::SetTop(L, 0);
    CallScript(Offsets::FUN_SCRIPT_GETCURSORPOSITION, L); // (x, y)
    const double x = Game::Lua::ToNumber(L, 1) / effScale;
    const double y = Game::Lua::ToNumber(L, 2) / effScale;

    left += offLeft;
    right += offRight;
    top += offTop;
    bottom += offBottom;

    const bool inside = x >= left && x <= right && y >= bottom && y <= top;
    Game::Lua::PushBool(L, inside);
    return 1;
}

// ---- GetRect (Region base — all region types) ------------------------------

// `region:GetRect()` → left, bottom, width, height. Composed from the
// engine's own Region getters (so all UI-scale conversion is its code).
// Matches retail: returns nothing for a region with no resolved rect —
// gated on GetLeft, which pushes nil when the position isn't resolved (a
// size-only region with no anchor has width/height but no rect).
int __fastcall Script_GetRect(void *L) {
    double left, bottom, width, height;
    if (!CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETLEFT, &left))
        return 0;
    CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETBOTTOM, &bottom);
    CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETWIDTH, &width);
    CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETHEIGHT, &height);
    Game::Lua::SetTop(L, 1);
    Game::Lua::PushNumber(L, left);
    Game::Lua::PushNumber(L, bottom);
    Game::Lua::PushNumber(L, width);
    Game::Lua::PushNumber(L, height);
    return 4;
}

// ---- IsDragging (Region base — all region types) ---------------------------

// `region:IsDragging()` — true while this region is the frame currently
// being moved/sized (StartMoving/StartSizing active, before
// StopMovingOrSizing). The engine tracks a single drag target in the global
// UI context at +0xCFC; StopMovingOrSizing itself gates on
// `context+0xCFC == self`, which is exactly this predicate. Reading the
// global context (rather than self+0xA0) keeps it correct for non-frame
// regions — a texture/fontstring self can never match the frame stored
// there, so it reports false, as it should.
int __fastcall Script_IsDragging(void *L) {
    void *self = ResolveRegionFamily(L);
    void *ctx = Game::Read<void *>(static_cast<uintptr_t>(Offsets::VAR_UI_CONTEXT_PTR));
    void *dragTarget =
        ctx != nullptr
            ? Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_DRAG_TARGET)
            : nullptr;
    Game::Lua::PushBool(L, self != nullptr && self == dragTarget);
    return 1;
}

// ---- SetShown (per-branch Show/Hide delegates) -----------------------------

// `object:SetShown(shown)` — any falsy value hides, truthy shows,
// matching modern semantics.
template <uintptr_t ShowFn, uintptr_t HideFn>
int __fastcall Script_SetShown(void *L) {
    const bool shown = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1); // (self) — Show/Hide take no args
    CallScript(shown ? ShowFn : HideFn, L);
    return 0;
}

// ---- GetEffectiveAlpha (Frame) ---------------------------------------------

// `frame:GetEffectiveAlpha()` — the frame's own alpha times every
// ancestor's alpha, i.e. the product WoW composites at render time (fading
// UIParent fades all children). Vanilla exposes only GetAlpha (own alpha);
// we walk self → parent → … via the engine's own GetAlpha (Frame) and
// GetParent (Region), so no field offsets or scale math are re-derived. The
// iteration cap is a defensive guard against a pathological chain (WoW frame
// trees are shallow; the loop normally ends when GetParent returns nil at
// the top of the tree).
int __fastcall Script_GetEffectiveAlpha(void *L) {
    Game::Lua::SetTop(L, 1); // (obj) = self
    double eff = 1.0;
    for (int guard = 0; guard < 128; ++guard) {
        CallScript(Offsets::FUN_SCRIPT_FRAME_GETALPHA, L); // (obj, alpha)
        eff *= Game::Lua::ToNumber(L, 2);
        Game::Lua::SetTop(L, 1);                             // (obj)
        CallScript(Offsets::FUN_SCRIPT_REGION_GETPARENT, L); // (obj, parent|nil)
        if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NIL)
            break;
        Game::Lua::Remove(L, 1); // drop obj → (parent) becomes index 1
    }
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushNumber(L, eff);
    return 1;
}

// ---- SetResizeBounds (Frame) -----------------------------------------------

// `frame:SetResizeBounds(minWidth, minHeight [, maxWidth, maxHeight])` —
// modern rename of the vanilla SetMinResize/SetMaxResize pair.
int __fastcall Script_SetResizeBounds(void *L) {
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: frame:SetResizeBounds(minWidth, "
                            "minHeight [, maxWidth, maxHeight])");
        return 0;
    }
    const bool haveMax = Game::Lua::IsNumber(L, 4) && Game::Lua::IsNumber(L, 5);
    const double maxW = haveMax ? Game::Lua::ToNumber(L, 4) : 0.0;
    const double maxH = haveMax ? Game::Lua::ToNumber(L, 5) : 0.0;
    Game::Lua::SetTop(L, 3); // (self, minW, minH)
    CallScript(Offsets::FUN_SCRIPT_FRAME_SETMINRESIZE, L);
    if (haveMax) {
        Game::Lua::SetTop(L, 1); // (self)
        Game::Lua::PushNumber(L, maxW);
        Game::Lua::PushNumber(L, maxH);
        CallScript(Offsets::FUN_SCRIPT_FRAME_SETMAXRESIZE, L);
    }
    return 0;
}

// ---- HookScript (Frame) ----------------------------------------------------

// `frame:HookScript(scriptType, handler)` — vanilla has no HookScript at
// all. Chains onto any existing handler: the old handler runs first,
// vanilla-style (no args — it reads the `this`/`event`/`argN` globals like
// every 1.12 handler), then `handler` is invoked MODERN-style with
// positional arguments `(frame, event, arg1..arg9)` for OnEvent and
// `(frame, arg1..arg9)` otherwise — mirroring the compat convention
// modern addon ports expect (and ship themselves when they detect a
// legacy client). The chain is a C closure over (handler, old, isOnEvent).

// Pushes _G[name].
void PushGlobal(void *L, const char *name) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
}

// The chained handler the engine actually invokes (with no args, like any
// vanilla script handler). Upvalues: 1 = hook handler, 2 = old handler
// (or nil), 3 = isOnEvent (number). Errors inside lua_call propagate to
// the engine's own protected script dispatch.
int __fastcall HookThunk(void *L) {
    if (Game::Lua::Type(L, Game::Lua::UpvalueIndex(2)) ==
        Game::Lua::TYPE_FUNCTION) {
        Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(2));
        Game::Lua::Call(L, 0, 0);
    }
    const bool isOnEvent =
        Game::Lua::ToNumber(L, Game::Lua::UpvalueIndex(3)) != 0;
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(1));
    PushGlobal(L, "this");
    if (isOnEvent)
        PushGlobal(L, "event");
    static const char *const kArgs[] = {"arg1", "arg2", "arg3", "arg4",
                                        "arg5", "arg6", "arg7", "arg8",
                                        "arg9"};
    for (const char *a : kArgs)
        PushGlobal(L, a);
    Game::Lua::Call(L, (isOnEvent ? 2 : 1) + 9, 0);
    return 0;
}

int __fastcall Script_HookScript(void *L) {
    if (!Game::Lua::IsString(L, 2) ||
        Game::Lua::Type(L, 3) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: frame:HookScript(\"type\", function)");
        return 0;
    }
    const char *scriptName = Game::Lua::ToString(L, 2);
    const bool isOnEvent =
        scriptName != nullptr && std::strcmp(scriptName, "OnEvent") == 0;

    Game::Lua::SetTop(L, 3); // (self, name, handler)
    // GetScript reads self/name at 1/2 and pushes the current handler
    // (or nil) — extra stack entries above are ignored by its reads.
    CallScript(Offsets::FUN_SCRIPT_FRAME_GETSCRIPT, L); // (.., old)
    // Closure upvalues, in upvalue order: handler, old, isOnEvent.
    Game::Lua::PushValue(L, 3);                       // handler
    Game::Lua::PushValue(L, 4);                       // old
    Game::Lua::PushNumber(L, isOnEvent ? 1.0 : 0.0);  // flag
    Game::Lua::PushCClosure(L, &HookThunk, 3); // (self,name,handler,old,closure)
    Game::Lua::Remove(L, 4);                   // drop old
    Game::Lua::Remove(L, 3);                   // drop handler → (self,name,closure)
    CallScript(Offsets::FUN_SCRIPT_FRAME_SETSCRIPT, L);
    return 0;
}

// ---- SetPoint one-arg form (Script_SetPoint co-hook) -----------------------

// Vanilla's Script_SetPoint accepts every modern form except the bare
// one-arg call (`region:SetPoint("RIGHT")`) — a fully-omitted arg 3 is
// LUA_TNONE, which fails its string/table/number/nil validation and
// raises the usage error. Normalize to the vanilla (point, x, y) form
// with zero offsets, which anchors relative to the parent at the same
// point — exactly the modern one-arg semantic. All other call shapes
// pass through untouched.
ScriptFn_t g_origSetPoint = nullptr;

int __fastcall SetPoint_h(void *L) {
    if (Game::Lua::GetTop(L) == 2 && Game::Lua::IsString(L, 2)) {
        Game::Lua::PushNumber(L, 0.0);
        Game::Lua::PushNumber(L, 0.0);
    }
    return g_origSetPoint(L);
}

const Game::HookAutoRegister _setPointHook{
    Offsets::FUN_SCRIPT_REGION_SETPOINT,
    reinterpret_cast<void *>(&SetPoint_h),
    reinterpret_cast<void **>(&g_origSetPoint)};

// ---- Registration ----------------------------------------------------------

const Game::Lua::FrameMethodEntry g_regionMethods[] = {
    {"SetSize", &Script_SetSize},
    {"GetSize", &Script_GetSize},
    {"IsMouseOver", &Script_IsMouseOver},
    {"GetRect", &Script_GetRect},
    {"IsDragging", &Script_IsDragging},
};

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"SetShown", &Script_SetShown<Offsets::FUN_SCRIPT_FRAME_SHOW,
                                  Offsets::FUN_SCRIPT_FRAME_HIDE>},
    {"SetResizeBounds", &Script_SetResizeBounds},
    {"HookScript", &Script_HookScript},
    {"GetEffectiveAlpha", &Script_GetEffectiveAlpha},
};

const Game::Lua::FrameMethodEntry g_textureMethods[] = {
    {"SetShown", &Script_SetShown<Offsets::FUN_SCRIPT_TEXTURE_SHOW,
                                  Offsets::FUN_SCRIPT_TEXTURE_HIDE>},
};

const Game::Lua::FrameMethodEntry g_fontStringMethods[] = {
    {"SetShown", &Script_SetShown<Offsets::FUN_SCRIPT_FONTSTRING_SHOW,
                                  Offsets::FUN_SCRIPT_FONTSTRING_HIDE>},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY),
        g_regionMethods,
        static_cast<int>(sizeof(g_regionMethods) / sizeof(g_regionMethods[0])));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY),
        g_frameMethods,
        static_cast<int>(sizeof(g_frameMethods) / sizeof(g_frameMethods[0])));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY),
        g_textureMethods,
        static_cast<int>(sizeof(g_textureMethods) / sizeof(g_textureMethods[0])));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY),
        g_fontStringMethods,
        static_cast<int>(sizeof(g_fontStringMethods) /
                         sizeof(g_fontStringMethods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::Modern
