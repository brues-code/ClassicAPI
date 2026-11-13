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
//   - `region:SetSize(w, h)` / `region:GetSize([ignoreRect])` — registered
//     on the REGION base method registry, so they resolve on frames,
//     buttons, textures, fontstrings — everything.
//   - `region:GetWidth([ignoreRect])` / `region:GetHeight([ignoreRect])` —
//     the engine's getters with the later argument. The engine answers the
//     explicit size, and the rect's extent only when that size is 0; with
//     ignoreRect a 0 stays 0. The engine leaves the layout scale in a rect
//     extent, and vanilla addons are tuned to that, so it stays.
//   - `region:GetUnscaledWidth([ignoreRect])` / `GetUnscaledHeight` /
//     `GetUnscaledSize` — 4.3.4's getters, which divide a rect extent by the
//     layout scale (see PushUnscaledExtent). The ported retail Lua calls
//     these where retail calls GetWidth / GetHeight / GetSize.
//   - `region:IsMouseOver([top, bottom, left, right])` — cursor-in-rect
//     test with optional per-edge offsets, also on the REGION registry.
//   - `region:GetRect()` — left, bottom, width, height, composed from the
//     engine's edge getters and the unscaled extents. Also on the REGION
//     registry.
//   - `region:GetScaledRect()` — GetRect times the effective scale, the
//     rect in UIParent-independent screen units.
//   - `region:IsDragging()` — true while the region is the frame currently
//     being moved/sized. Reads the engine's global drag target.
//   - `frame:SetShown(shown)` — Show/Hide are per-branch script functions
//     (each with its own object typecheck), so SetShown is registered
//     separately on Frame / Texture / FontString with matching delegates.
//   - `frame:SetResizeBounds(minW, minH [, maxW, maxH])` — the modern
//     rename of vanilla's SetMinResize/SetMaxResize pair.
//   - `frame:HookScript(scriptType, handler)` — absent in vanilla
//     entirely. 4.3.4's: chains after any existing handler through the
//     same wrapper `hooksecurefunc` installs.
//   - `texture:GetScale()` / `GetEffectiveScale()`, and the fontstring pair
//     — Frame has the engine's own. A region carries no scale of its own;
//     the frame's scale setter hands each region the frame's effective
//     scale as its layout scale (OFF_REGION_EFFECTIVE_SCALE, benilla-re
//     ui/visibility "Scale"), so the effective scale is that and the scale
//     is 1.
//   - `frame:GetEffectiveAlpha()` — the alpha the frame is drawn with:
//     the byte at OFF_FRAME_ALPHA, which `Frame::Alpha` keeps at own ×
//     the parent's drawn alpha. Frame-scoped (matches GetAlpha).
//
// Implementation style: each new method delegates to the ENGINE's own
// Script_* implementations by reshaping the Lua stack and calling them
// (`int __fastcall(void *L)`, self at index 1) — no re-derived setters,
// so pixel/UI-scale conversion, object resolution, and type checking are
// all the engine's own code. Values are read out BEFORE any stack
// reshaping.

#include "Game.h"
#include "HookSecureFunc.h"
#include "Offsets.h"
#include "frame/Layout.h"
#include "frame/ScriptBindings.h"

#include <cstdint>

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

using LayoutExtent_t = float(__thiscall *)(void *layout);

// The explicit size of `self` in layout `slot`; 0 when none is set.
float ExplicitExtent(void *self, int slot) {
    void *layout = Frame::Layout::Of(self);
    auto *vtable = Game::Read<uintptr_t *>(layout, 0);
    return reinterpret_cast<LayoutExtent_t>(vtable[slot / sizeof(uintptr_t)])(layout);
}

// Pushes the engine getter's answer for `self` (index 1), or 0 when
// ignoreRect is set and the explicit size in `slot` is 0 — the one case the
// engine answers from the rect instead.
void PushEngineExtent(void *L, bool ignoreRect, int slot, uintptr_t getter) {
    if (ignoreRect) {
        void *self = Game::Lua::ResolveRegion(L);
        if (self == nullptr)
            return;
        if (ExplicitExtent(self, slot) == 0.0f) {
            Game::Lua::PushNumber(L, 0.0);
            return;
        }
    }
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushValue(L, 1);
    CallScript(getter, L); // (.., self, value)
    Game::Lua::Remove(L, top + 1);
}

// 4.3.4's Script_GetWidth / Script_GetHeight for `self` (index 1): the
// explicit size from layout `slot`, or when it is 0 and ignoreRect is not set,
// the rect's span over the layout scale; pushed in script units. 1.12's own
// getters (FUN_SCRIPT_REGION_GETWIDTH / _GETHEIGHT) leave out the division,
// so an anchored region under a UI scale reads scaled: a 160-high track at
// scale 0.91 answered 145.6 while its GetTop - GetBottom was 160. Their other
// steps, and the edge getters' division, are the engine's.
void PushUnscaledExtent(void *L, bool ignoreRect, int slot, bool vertical) {
    void *self = Game::Lua::ResolveRegion(L);
    if (self == nullptr)
        return;
    float extent = ExplicitExtent(self, slot);
    if (extent == 0.0f && !ignoreRect) {
        float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // {bottom, left, top, right}
        if (Frame::Layout::GetResolvedRect(self, rect))
            extent = (vertical ? rect[2] - rect[0] : rect[3] - rect[1]) /
                     Game::Read<float>(self, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    }
    Game::Lua::PushNumber(L, Game::InternalToPixel(extent));
}

// `region:GetWidth([ignoreRect])` / `region:GetHeight([ignoreRect])`.
int __fastcall Script_GetWidth(void *L) {
    const bool ignoreRect = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1);
    PushEngineExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_WIDTH,
                     Offsets::FUN_SCRIPT_REGION_GETWIDTH);
    return 1;
}

int __fastcall Script_GetHeight(void *L) {
    const bool ignoreRect = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1);
    PushEngineExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_HEIGHT,
                     Offsets::FUN_SCRIPT_REGION_GETHEIGHT);
    return 1;
}

// `region:GetSize([ignoreRect])` → width, height.
int __fastcall Script_GetSize(void *L) {
    const bool ignoreRect = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1); // (self)
    PushEngineExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_WIDTH,
                     Offsets::FUN_SCRIPT_REGION_GETWIDTH);
    PushEngineExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_HEIGHT,
                     Offsets::FUN_SCRIPT_REGION_GETHEIGHT);
    return 2;
}

// `region:GetUnscaledWidth([ignoreRect])` / `GetUnscaledHeight` /
// `GetUnscaledSize`.
int __fastcall Script_GetUnscaledWidth(void *L) {
    const bool ignoreRect = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1);
    PushUnscaledExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_WIDTH, false);
    return 1;
}

int __fastcall Script_GetUnscaledHeight(void *L) {
    const bool ignoreRect = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1);
    PushUnscaledExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_HEIGHT, true);
    return 1;
}

int __fastcall Script_GetUnscaledSize(void *L) {
    const bool ignoreRect = Game::Lua::ToBoolean(L, 2) != 0;
    Game::Lua::SetTop(L, 1); // (self)
    PushUnscaledExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_WIDTH, false);
    PushUnscaledExtent(L, ignoreRect, Offsets::VTBL_LAYOUT_GET_HEIGHT, true);
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
// Region-base state, so the wider set is sound. A bad self raises the
// engine's own "this" errors, as 4.3.4's FrameScript_GetObjectThis does.
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
        /*raiseError=*/true);
}

int __fastcall Script_IsMouseOver(void *L) {
    // Offsets first — the stack gets reshaped by the delegated getters.
    const double offTop = Game::Lua::IsNumber(L, 2) ? Game::Lua::ToNumber(L, 2) : 0.0;
    const double offBottom = Game::Lua::IsNumber(L, 3) ? Game::Lua::ToNumber(L, 3) : 0.0;
    const double offLeft = Game::Lua::IsNumber(L, 4) ? Game::Lua::ToNumber(L, 4) : 0.0;
    const double offRight = Game::Lua::IsNumber(L, 5) ? Game::Lua::ToNumber(L, 5) : 0.0;

    void *obj = ResolveRegionFamily(L);
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

    // Strict, as 4.3.4's CScriptRegion::IsMouseOver: an edge is outside.
    const bool inside = x > left && x < right && y > bottom && y < top;
    Game::Lua::PushBool(L, inside);
    return 1;
}

// ---- GetRect (Region base — all region types) ------------------------------

// `region:GetRect()` → left, bottom, width, height. Composed from the
// engine's edge getters and the unscaled extents (the engine's GetWidth /
// GetHeight leave the layout scale in a rect-derived size).
// Matches retail: returns nothing for a region with no resolved rect —
// gated on GetLeft, which pushes nil when the position isn't resolved (a
// size-only region with no anchor has width/height but no rect).
int __fastcall Script_GetRect(void *L) {
    double left, bottom, width, height;
    if (!CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETLEFT, &left))
        return 0;
    CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETBOTTOM, &bottom);
    CallRegionNumber(L, reinterpret_cast<uintptr_t>(&Script_GetUnscaledWidth), &width);
    CallRegionNumber(L, reinterpret_cast<uintptr_t>(&Script_GetUnscaledHeight), &height);
    Game::Lua::SetTop(L, 1);
    Game::Lua::PushNumber(L, left);
    Game::Lua::PushNumber(L, bottom);
    Game::Lua::PushNumber(L, width);
    Game::Lua::PushNumber(L, height);
    return 4;
}

// `region:GetScaledRect()` → GetRect's four values times the effective scale.
int __fastcall Script_GetScaledRect(void *L) {
    void *self = ResolveRegionFamily(L);
    if (self == nullptr)
        return 0;
    const double scale = Game::Read<float>(self, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    double left, bottom, width, height;
    if (!CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETLEFT, &left))
        return 0;
    CallRegionNumber(L, Offsets::FUN_SCRIPT_REGION_GETBOTTOM, &bottom);
    CallRegionNumber(L, reinterpret_cast<uintptr_t>(&Script_GetUnscaledWidth), &width);
    CallRegionNumber(L, reinterpret_cast<uintptr_t>(&Script_GetUnscaledHeight), &height);
    Game::Lua::SetTop(L, 1);
    Game::Lua::PushNumber(L, left * scale);
    Game::Lua::PushNumber(L, bottom * scale);
    Game::Lua::PushNumber(L, width * scale);
    Game::Lua::PushNumber(L, height * scale);
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
    Game::Lua::PushBool(L, self == dragTarget);
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

// `frame:GetEffectiveAlpha()` — the alpha the frame is drawn with, the byte
// its regions and model read (fading UIParent fades all children). With
// `Frame::Alpha` that byte is own × the parent's drawn alpha, truncated per
// level, so this is 3.3.5's Script_GetEffectiveAlpha (0x0049F900):
// `(m_alpha * m_parentAlpha / 255) / 255.0`. The engine's GetAlpha runs first
// for its `self` type check, which errors on a non-frame.
int __fastcall Script_GetEffectiveAlpha(void *L) {
    Game::Lua::SetTop(L, 1);                           // (self)
    CallScript(Offsets::FUN_SCRIPT_FRAME_GETALPHA, L); // (self, own)
    const void *frame = Game::Lua::ResolveObject(L, 1);
    Game::Lua::SetTop(L, 0);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::Read<uint8_t>(frame, Offsets::OFF_FRAME_ALPHA) / 255.0);
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

// `frame:HookScript("type", function)` — 4.3.4's
// FrameScript_Object::HookScript, less taint:
//   * no script set: the hook becomes the script itself, so GetScript
//     returns it;
//   * otherwise the script becomes the hooksecurefunc wrapper over (old,
//     hook) — 4.3.4 installs the same FrameScript_HookWrapper for both. The
//     old handler runs first with the script's arguments, then the hook with
//     the same arguments under the engine's script error handler.
// The arguments are whatever the dispatch passes. Frame::ScriptArgs looks
// inside the wrapper and passes positional args when either side declares
// parameters; the engine sets the this/event/argN globals for vanilla
// handlers either way. Script lookup, the unknown-script error and storage
// stay the engine's own GetScript / SetScript, whose texts match 4.3.4's.
//
// The retail third argument, `bindingType`, hooks one of the script's
// intrinsic bindings instead (Frame::ScriptBindings); without it the hook
// goes on the extrinsic binding, which GetScript / SetScript read and write.

int __fastcall Script_HookScript(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2) ||
        Game::Lua::Type(L, 3) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: %s:HookScript(\"type\", function)",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }

    if (Game::Lua::IsNumber(L, 4)) {
        const int binding = static_cast<int>(Game::Lua::ToNumber(L, 4));
        if (binding == Frame::ScriptBindings::INTRINSIC_PRECALL ||
            binding == Frame::ScriptBindings::INTRINSIC_POSTCALL) {
            if (!Frame::ScriptBindings::HookIntrinsic(
                    L, frame, static_cast<Frame::ScriptBindings::Binding>(binding)))
                Game::Lua::Error(L, "%s doesn't have a \"%s\" script",
                                 Game::Lua::ObjectDisplayName(frame), Game::Lua::ToString(L, 2));
            return 0;
        }
    }

    Game::Lua::SetTop(L, 3);                            // (self, type, hook)
    CallScript(Offsets::FUN_SCRIPT_FRAME_GETSCRIPT, L); // (.., old or nil)
    if (Game::Lua::Type(L, 4) == Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Insert(L, 3);         // (self, type, old, hook)
        HookSecureFunc::PushWrapper(L);  // (self, type, wrapper)
    } else {
        Game::Lua::SetTop(L, 3);         // (self, type, hook)
    }
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

// ---- Texture / FontString scale -----------------------------------------------

int __fastcall Script_GetRegionScale(void *L) {
    if (ResolveRegionFamily(L) == nullptr)
        return 0;
    Game::Lua::PushNumber(L, 1.0);
    return 1;
}

int __fastcall Script_GetRegionEffectiveScale(void *L) {
    void *self = ResolveRegionFamily(L);
    if (self == nullptr)
        return 0;
    Game::Lua::PushNumber(L, Game::Read<float>(self, Offsets::OFF_REGION_EFFECTIVE_SCALE));
    return 1;
}

// ---- Button normal-state font ------------------------------------------------

// `button:SetNormalFontObject(font)` / `button:GetNormalFontObject()` - the
// names 3.0 gave vanilla's SetTextFontObject / GetTextFontObject (the font of
// the button's normal state; the Highlight / Disabled pair kept their names).
// Pure delegation, so arguments, validation and errors are the engine's own.
int __fastcall Script_SetNormalFontObject(void *L) {
    return CallScript(Offsets::FUN_SCRIPT_BUTTON_SET_TEXT_FONT_OBJECT, L);
}

int __fastcall Script_GetNormalFontObject(void *L) {
    return CallScript(Offsets::FUN_SCRIPT_BUTTON_GET_TEXT_FONT_OBJECT, L);
}

// ---- Registration ----------------------------------------------------------

const Game::Lua::FrameMethodEntry g_regionMethods[] = {
    {"SetSize", &Script_SetSize},
    {"GetSize", &Script_GetSize},
    {"GetWidth", &Script_GetWidth},
    {"GetHeight", &Script_GetHeight},
    {"GetUnscaledSize", &Script_GetUnscaledSize},
    {"GetUnscaledWidth", &Script_GetUnscaledWidth},
    {"GetUnscaledHeight", &Script_GetUnscaledHeight},
    {"IsMouseOver", &Script_IsMouseOver},
    {"GetRect", &Script_GetRect},
    {"GetScaledRect", &Script_GetScaledRect},
    {"IsDragging", &Script_IsDragging},
};

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"SetShown", &Script_SetShown<Offsets::FUN_SCRIPT_FRAME_SHOW,
                                  Offsets::FUN_SCRIPT_FRAME_HIDE>},
    {"SetResizeBounds", &Script_SetResizeBounds},
    {"HookScript", &Script_HookScript},
    {"GetEffectiveAlpha", &Script_GetEffectiveAlpha},
};

const Game::Lua::FrameMethodEntry g_buttonMethods[] = {
    {"SetNormalFontObject", &Script_SetNormalFontObject},
    {"GetNormalFontObject", &Script_GetNormalFontObject},
};

const Game::Lua::FrameMethodEntry g_textureMethods[] = {
    {"SetShown", &Script_SetShown<Offsets::FUN_SCRIPT_TEXTURE_SHOW,
                                  Offsets::FUN_SCRIPT_TEXTURE_HIDE>},
    {"GetScale", &Script_GetRegionScale},
    {"GetEffectiveScale", &Script_GetRegionEffectiveScale},
};

const Game::Lua::FrameMethodEntry g_fontStringMethods[] = {
    {"SetShown", &Script_SetShown<Offsets::FUN_SCRIPT_FONTSTRING_SHOW,
                                  Offsets::FUN_SCRIPT_FONTSTRING_HIDE>},
    {"GetScale", &Script_GetRegionScale},
    {"GetEffectiveScale", &Script_GetRegionEffectiveScale},
};

// ---- Documentation ---------------------------------------------------------

const Game::Doc::Field kSetSizeArgs[] = {
    Game::Doc::Req("x", "uiUnit"),
    Game::Doc::Req("y", "uiUnit"),
};
const Game::Doc::Function kSetSize{"Sets the width and the height of the region.", kSetSizeArgs,
                                   {}};

const Game::Doc::Field kIgnoreRectArgs[] = {
    Game::Doc::Opt("ignoreRect", "bool", "false",
                   "True returns only the set size, which is 0 for a region with none."),
};
const Game::Doc::Field kSizeRets[] = {
    Game::Doc::Req("width", "uiUnit"),
    Game::Doc::Req("height", "uiUnit"),
};
const Game::Doc::Field kWidthRets[] = {
    Game::Doc::Req("width", "uiUnit"),
};
const Game::Doc::Field kHeightRets[] = {
    Game::Doc::Req("height", "uiUnit"),
};
const Game::Doc::Function kGetSize{
    "Returns the width and the height of the region; a size from anchors is multiplied by the "
    "effective scale.",
    kIgnoreRectArgs, kSizeRets};
const Game::Doc::Function kGetWidth{
    "Returns the width of the region; a width from anchors is multiplied by the effective "
    "scale.",
    kIgnoreRectArgs, kWidthRets};
const Game::Doc::Function kGetHeight{
    "Returns the height of the region; a height from anchors is multiplied by the effective "
    "scale.",
    kIgnoreRectArgs, kHeightRets};
const Game::Doc::Function kGetUnscaledSize{
    "Returns the width and the height of the region in the units of GetTop and GetBottom.",
    kIgnoreRectArgs, kSizeRets, nullptr, true};
const Game::Doc::Function kGetUnscaledWidth{
    "Returns the width of the region in the units of GetLeft and GetRight.", kIgnoreRectArgs,
    kWidthRets, nullptr, true};
const Game::Doc::Function kGetUnscaledHeight{
    "Returns the height of the region in the units of GetTop and GetBottom.", kIgnoreRectArgs,
    kHeightRets, nullptr, true};

const Game::Doc::Field kIsMouseOverArgs[] = {
    Game::Doc::Opt("offsetTop", "uiUnit", "0", "Added to the top edge before the test."),
    Game::Doc::Opt("offsetBottom", "uiUnit", "0", "Added to the bottom edge."),
    Game::Doc::Opt("offsetLeft", "uiUnit", "0", "Added to the left edge."),
    Game::Doc::Opt("offsetRight", "uiUnit", "0", "Added to the right edge."),
};
const Game::Doc::Field kIsMouseOverRets[] = {
    Game::Doc::Req("isMouseOver", "bool", "False for a region with no position."),
};
const Game::Doc::Function kIsMouseOver{
    "Returns whether the cursor is inside the region's rectangle.", kIsMouseOverArgs,
    kIsMouseOverRets};

const Game::Doc::Field kRectRets[] = {
    Game::Doc::Req("left", "uiUnit"),
    Game::Doc::Req("bottom", "uiUnit"),
    Game::Doc::Req("width", "uiUnit"),
    Game::Doc::Req("height", "uiUnit"),
};
const Game::Doc::Function kGetRect{
    "Returns the region's left, bottom, width and height; returns nothing for a region with no "
    "position.",
    {}, kRectRets};
const Game::Doc::Function kGetScaledRect{
    "Returns the four values of GetRect multiplied by the effective scale; returns nothing for "
    "a region with no position.",
    {}, kRectRets};

const Game::Doc::Field kIsDraggingRets[] = {
    Game::Doc::Req("isDragging", "bool"),
};
const Game::Doc::Function kIsDragging{
    "Returns whether the region is the frame the mouse is moving or resizing now.", {},
    kIsDraggingRets};

const Game::Doc::Field kSetShownArgs[] = {
    Game::Doc::Opt("shown", "bool", "false"),
};
const Game::Doc::Function kSetShown{"Shows the object when shown is true and hides it otherwise.",
                                    kSetShownArgs, {}};

const Game::Doc::Field kSetResizeBoundsArgs[] = {
    Game::Doc::Req("minWidth", "uiUnit"),
    Game::Doc::Req("minHeight", "uiUnit"),
    Game::Doc::Opt("maxWidth", "uiUnit", nullptr, "Applied only together with maxHeight."),
    Game::Doc::Opt("maxHeight", "uiUnit"),
};
const Game::Doc::Function kSetResizeBounds{
    "Sets the smallest and largest size the frame can be resized to.", kSetResizeBoundsArgs,
    {}};

const Game::Doc::Field kHookScriptArgs[] = {
    Game::Doc::Req("scriptTypeName", "cstring"),
    Game::Doc::Req("script", "function",
                   "Runs after the current handler, with the same arguments."),
    Game::Doc::Opt("bindingType", "number", nullptr,
                   "LE_SCRIPT_BINDING_TYPE_INTRINSIC_PRECALL or "
                   "LE_SCRIPT_BINDING_TYPE_INTRINSIC_POSTCALL hooks that intrinsic script "
                   "instead."),
};
const Game::Doc::Function kHookScript{"Adds a function that runs after a script's handler.",
                                      kHookScriptArgs, {}};

const Game::Doc::Field kGetEffectiveAlphaRets[] = {
    Game::Doc::Req("effectiveAlpha", "number",
                   "The frame's alpha multiplied by the alpha of its parents, in steps of 1/255."),
};
const Game::Doc::Function kGetEffectiveAlpha{"Returns the alpha the frame is drawn with.", {},
                                             kGetEffectiveAlphaRets};

const Game::Doc::Field kFontArgs[] = {
    Game::Doc::Req("font", "SimpleFont"),
};
const Game::Doc::Field kFontRets[] = {
    Game::Doc::Opt("font", "SimpleFont"),
};
const Game::Doc::Function kSetNormalFontObject{
    "Sets the font of the button's text in its normal state.", kFontArgs, {}};
const Game::Doc::Function kGetNormalFontObject{
    "Returns the font of the button's text in its normal state.", {}, kFontRets};

const Game::Doc::Field kScaleRets[] = {
    Game::Doc::Req("scale", "number", "Always 1."),
};
const Game::Doc::Function kGetRegionScale{"Returns the region's own scale.", {}, kScaleRets};
const Game::Doc::Field kEffectiveScaleRets[] = {
    Game::Doc::Req("effectiveScale", "number"),
};
const Game::Doc::Function kGetRegionEffectiveScale{
    "Returns the effective scale of the frame that owns the region.", {}, kEffectiveScaleRets};

const Game::Doc::Method g_regionMethodDocs[] = {
    {"SetSize", &kSetSize},
    {"GetSize", &kGetSize},
    {"GetWidth", &kGetWidth},
    {"GetHeight", &kGetHeight},
    {"GetUnscaledSize", &kGetUnscaledSize},
    {"GetUnscaledWidth", &kGetUnscaledWidth},
    {"GetUnscaledHeight", &kGetUnscaledHeight},
    {"IsMouseOver", &kIsMouseOver},
    {"GetRect", &kGetRect},
    {"GetScaledRect", &kGetScaledRect},
    {"IsDragging", &kIsDragging},
};
const Game::Doc::Method g_frameMethodDocs[] = {
    {"SetShown", &kSetShown},
    {"SetResizeBounds", &kSetResizeBounds},
    {"HookScript", &kHookScript},
    {"GetEffectiveAlpha", &kGetEffectiveAlpha},
};
const Game::Doc::Method g_buttonMethodDocs[] = {
    {"SetNormalFontObject", &kSetNormalFontObject},
    {"GetNormalFontObject", &kGetNormalFontObject},
};
const Game::Doc::Method g_regionScaleMethodDocs[] = {
    {"SetShown", &kSetShown},
    {"GetScale", &kGetRegionScale},
    {"GetEffectiveScale", &kGetRegionEffectiveScale},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_regionMethods,
        g_regionMethodDocs);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        g_frameMethodDocs);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_BUTTON_METHOD_REGISTRY), g_buttonMethods,
        g_buttonMethodDocs);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_textureMethods,
        g_regionScaleMethodDocs);
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_METHOD_REGISTRY), g_fontStringMethods,
        g_regionScaleMethodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::Modern
