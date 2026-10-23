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

// Frame attributes + unit-frame mouseover/click backport.
//
// Backports `frame:SetAttribute` / `frame:SetAttributeNoHandler` /
// `frame:ClearAttribute` / `frame:GetAttribute` to 1.12 as native methods on the base Frame registry
// (every frame gets them, like the modern clients). Vanilla has no attribute
// system at all — it arrived in 2.0 with secure frames. Attributes are just a
// per-frame, case-insensitive key→value store (verified from the 3.3.5
// implementation: a name→Lua-value map plus a getter with prefix/name/suffix
// wildcard precedence). We store the map in the Lua registry keyed by the frame
// (registry[frame]) — invisible to Lua script and tamper-proof, like retail's
// C-side attribute store (see PushSub).
//
// The headline use — SecureUnitButton behavior, minus the secure wrapper (1.12
// has no combat lockdown or taint, so protected actions just work):
//
//     unitFrame:SetAttribute("unit", "party1")   -- hover → mouseover = party1
//     unitFrame:SetAttribute("type1", "target")  -- left-click → target party1
//
// ── Mouseover (needs C — the engine's mouseover is a GUID slot) ──
//
// Installing OnEnter/OnLeave would be clobbered by an addon's own
// `SetScript("OnEnter", …)` (pfUI sets `unit` before its scripts), and there's
// nothing to read from Lua anyway — the mouseover unit is an engine global. So
// we mirror 4.3.4's CGWorldFrame::OnLayerUpdate: once per frame (the shared
// WorldTick) read the engine's mouse-focus frame
// (`*(*VAR_UI_CONTEXT_PTR + OFF_UI_CONTEXT_MOUSE_FOCUS)`, the
// CFrameScriptObject* `GetMouseFocus` returns), ask FrameXML's
// `SecureButton_GetModifiedUnit` for its unit (see ModifiedUnit), and hand
// that to the engine's FULL mouseover setter (FUN_00492890) — 1:1 with
// hovering the unit's 3D model: model highlight, UPDATE_MOUSEOVER_UNIT, the
// GUID slot, and the mouseover tooltip. The tooltip is a deliberate
// difference: 4.3.4 shows it only when the world frame has the focus.
// Stomp-proof: the engine's own 3D-hover setter is event-driven, so nothing
// overwrites the slot while the cursor is over UI.
//
// ── Clicks (the frame's own OnClick — scoped, native) ──
//
// Clicks are events, not state, so they belong on the frame's OnClick — but
// ONLY on frames that opt in with a `type` attribute. When a `type*` attribute
// is set we install a chained OnClick on that frame (protected, because OnClick
// is Button-only). It mirrors retail's SecureActionButton_OnClick: resolve ONE
// verb per click from the modifier/button-qualified `type` attribute, perform
// it, done. No global hook, no handler on frames that never asked for one.
//
// Resolution (retail-style): the attribute is `[prefix]type[suffix]`, where the
// prefix is the held modifiers ("alt-"/"ctrl-"/"shift-") and the suffix is the
// button number (1=Left, 2=Right, …). So `type1`, `shift-type1`, `type` all
// resolve via ReadModAttr's precedence. Verbs: `target` (with the engine's
// default-interaction precedence — pending spell / cursor item cast/drop on the
// unit instead of switching target), `assist`, `focus`, `spell` (reads the
// `spell` attribute and casts it: with a `unit` attribute, on that unit via our
// native C_Spell.CastAtUnit — the unit's GUID goes straight to the cast
// dispatcher, no target juggling, and ground-target spells land at its feet;
// with no `unit`, on the current target via the plain CastSpellByName), `item`
// (uses the `item`
// attribute — a name/itemID/link via C_Item.UseItemByName with the unit as the
// use target, or a "bag slot" string like "0 1" via UseContainerItem; the
// deprecated `bag`/`slot` attributes also work), `macro` (from the
// `macrotext`/`macro` attribute — prefers an addon RunMacro, else runs natively
// via the stock ChatEdit_ParseText), `stopcasting`, `menu`/`togglemenu` (pops
// the standard unit dropdown at the cursor via the addon's
// ClassicAPI_ToggleUnitMenu, or cancels spell-targeting when a spell is on the
// cursor), `action` (UseAction of the `action` slot), `pet` (CastPetAction of
// the `action` index), `click` (forwards to the frame in the `clickbutton`
// attribute via delegate:Click), and any other verb as a custom action — a
// function stored as a raw field on the frame named by the verb, called as
// func(self, unit, button). This is the complete secure-button verb set, so the
// !!!ClassicAPI addon ships no parallel Lua dispatcher. Actions call ordinary
// Lua globals where the entry
// is the engine's (TargetUnit, IsAltKeyDown, …); where we own a C++ module the
// dispatch goes straight to it, no Lua round-trip (Spell::AtUnit for `spell`,
// Unit::Focus for `focus`). All run under the engine's protected OnClick.
//
// One verb per click is the point: because we own the click when a verb
// resolves (and only then chain the previous handler), a configured `type1` no
// longer runs *alongside* the addon's own conditional OnClick — that double
// dispatch (our fixed "target" + pfUI's ClickAction) was the conflict. An addon
// expresses all its click behavior as attributes; unconfigured clicks fall
// through to its OnClick (e.g. a right-click menu).
//
// Ordering / clobbering: it chains the handler present when `type*` is set, so
// set `type*` AFTER the frame's own `OnClick`. Addons that re-`SetScript`
// `OnClick` later (pfUI does on every raid relayout) replace our closure — so
// re-set `type*` after re-SetScript to recover. Re-wiring is safe: WireOnClick
// re-wraps a clobbered (Lua) handler but skips its own C closure, so it never
// double-chains. The frame must be a Button registered for the relevant clicks
// (`RegisterForClicks`) — left is the Button default; right needs
// `RegisterForClicks("RightButtonUp")`, which real unit frames do.
//
// `OnAttributeChanged` is a real SetScript-able handler: SetAttribute (not
// SetAttributeNoHandler) fires it as `(self, name, value)` with the vanilla
// `this` / `arg1` / `arg2` globals set too (see FireAttributeChanged). The
// script itself is a `Frame::ExtraScripts` script.

#include "frame/Attributes.h"
#include "frame/ExtraScripts.h"
#include "frame/ScriptArgs.h"

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "cursor/Info.h"
#include "macro/Execute.h"
#include "object/Resolve.h"
#include "spell/AtUnit.h"
#include "spell/Placement.h"
#include "tick/WorldTick.h"
#include "unit/Focus.h"
#include "unit/Identity.h"
#include "unit/Tooltip.h"
#include "xml/FrameLoad.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Frame::Attributes {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

int CallScript(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// ---- small string helpers --------------------------------------------------

void LowerCopy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; ++i)
        dst[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(src[i])));
    dst[i] = '\0';
}

using Common::BoundedCopy;

// dst = tolower(a .. b .. c). Truncates at n-1.
void Compose3Lower(char *dst, size_t n, const char *a, const char *b, const char *c) {
    size_t i = 0;
    const char *parts[3] = {a, b, c};
    for (const char *s : parts)
        for (; s && *s && i + 1 < n; ++s)
            dst[i++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*s)));
    dst[i] = '\0';
}

// True if `lname` (already lowercase) is a click "type" action attribute: an
// optional modifier prefix (alt-/ctrl-/shift-, any order/combo) followed by
// "type" and an optional button digit — i.e. anything the click resolver
// (ButtonSuffix + ReadModAttr) reads as a verb selector: type, type1..type5,
// shift-type1, alt-ctrl-type2, … . This is the exact set WireOnClick must
// (re)install for; keying the wiring off it (rather than a fixed
// type/type1/type2 list) means a middle-click (type3) or modifier-only config
// still installs the chained OnClick.
bool IsTypeAttr(const char *lname) {
    const char *p = lname;
    for (bool advanced = true; advanced;) {
        if (std::strncmp(p, "alt-", 4) == 0)        p += 4;
        else if (std::strncmp(p, "ctrl-", 5) == 0)  p += 5;
        else if (std::strncmp(p, "shift-", 6) == 0) p += 6;
        else                                        advanced = false;
    }
    if (std::strncmp(p, "type", 4) != 0)
        return false;
    p += 4;
    if (p[0] == '\0')
        return true;                                     // "type"
    return p[0] >= '1' && p[0] <= '5' && p[1] == '\0';   // "typeN"
}

// ---- attribute storage (in the Lua registry, keyed by the frame) -----------

// Pushes the frame's attribute subtable onto the stack. Stored as
// registry[frame] in the Lua registry (LUA_REGISTRYINDEX) rather than as a
// field on the frame's own table — the registry is C-side and has no _G path,
// so the store is invisible to `pairs(frame)` and can't be read or tampered
// with from Lua, matching how retail keeps attributes C-side. With create=true,
// lazily creates and stores it. Returns true iff a subtable is left on top;
// false (nothing left on the stack) when absent and !create. 1.12 frames are
// immortal, so the strong registry key never leaks.
bool PushSub(void *L, int frameIdx, bool create) {
    Game::Lua::PushValue(L, frameIdx);                  // [.., frame]  (key)
    Game::Lua::RawGet(L, Game::Lua::REGISTRY_INDEX);    // [.., sub|nil]
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return true;                                    // [.., sub]
    Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1);     // drop the nil -> [..]
    if (!create)
        return false;
    Game::Lua::NewTable(L);                             // [.., sub]
    Game::Lua::PushValue(L, frameIdx);                  // [.., sub, frame]  (key)
    Game::Lua::PushValue(L, -2);                        // [.., sub, frame, sub]  (value)
    Game::Lua::RawSet(L, Game::Lua::REGISTRY_INDEX);    // registry[frame] = sub -> [.., sub]
    return true;
}

// Copies string attribute `lname` (already lowercase) of the frame at
// frameIdx into buf. Returns true iff a string value was found. Restores the
// stack.
bool CopyAttr(void *L, int frameIdx, const char *lname, char *buf, size_t n) {
    const int top = Game::Lua::GetTop(L);
    bool ok = false;
    if (PushSub(L, frameIdx, false)) {       // [.., sub]
        const int si = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, lname);
        Game::Lua::RawGet(L, si);            // [.., sub, val]
        const int t = Game::Lua::Type(L, -1);
        if (t == Game::Lua::TYPE_STRING || t == Game::Lua::TYPE_NUMBER) {
            // lua_tostring coerces a number to its digits in place — on our
            // temporary stack copy, so the stored attribute is untouched. This
            // lets a numeric attribute value (e.g. SetAttribute("spell", 12345)
            // or a numeric itemID) read as a string.
            const char *s = Game::Lua::ToString(L, -1);
            if (s) {
                BoundedCopy(buf, s, n);
                ok = true;
            }
        }
    }
    Game::Lua::SetTop(L, top);
    return ok;
}

// If the frame's attribute subtable has a non-nil value for keyLower, leave that
// value on the stack top and return true; otherwise restore the stack and
// return false.
bool TryPushValue(void *L, int frameIdx, const char *keyLower) {
    const int top = Game::Lua::GetTop(L);
    if (PushSub(L, frameIdx, false)) {       // [.., sub]
        const int si = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, keyLower);
        Game::Lua::RawGet(L, si);            // [.., sub, val]
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL) {
            Game::Lua::Remove(L, si);        // [.., val]
            return true;
        }
    }
    Game::Lua::SetTop(L, top);
    return false;
}

// Unit token → GUID goes through `Unit::Identity::GuidForToken` (engine
// resolver + `"player"` fast-path, handling our focus / nameplateN / raw-GUID
// extensions). For recognized tokens it never raises; unit attributes are
// tokens by contract.

// ---- Lua-global call helpers (used by the click dispatcher) -----------------
//
// The click dispatch mirrors retail's SecureActionButton_OnClick: it's C. It
// performs engine actions by calling ordinary Lua globals (TargetUnit,
// IsAltKeyDown, …) and our own actions by calling the C++ module directly
// (Spell::AtUnit, Unit::Focus). Both run under the engine's protected OnClick
// invocation, so a Lua error inside one is caught there (no crash).

// The plain global-call primitives (push-if-function, call with no/one arg)
// live in Game::Lua now (PushGlobalFunction / CallGlobal / CallGlobalString) —
// shared with the other modules that dispatch to FrameXML globals by name. The
// two shapes below are specific enough to stay here.

// _G[name]() -> boolean (false if the global isn't a function). Distinct from
// Game::Lua::CallGlobal in that it returns the call's boolean RESULT (used for
// the IsAltKeyDown/IsShiftKeyDown modifier probes).
bool CallBoolGlobal(void *L, const char *name) {
    const int top = Game::Lua::GetTop(L);
    bool r = false;
    if (Game::Lua::PushGlobalFunction(L, name)) {
        Game::Lua::Call(L, 0, 1);
        r = Game::Lua::ToBoolean(L, -1) != 0;
    }
    Game::Lua::SetTop(L, top);
    return r;
}

// _G[table][method](a, b) — two string args (null → nil, since PushString maps
// NULL to nil), no results. Returns true iff the method resolved to a function
// and got called. Used when the target is our own C_* function but the call must
// cross the Lua boundary — e.g. C_Item.UseItemByName, whose bag walk does
// SetTop(L, 0); going through lua_call gives it its own stack frame so that reset
// clears only its frame, not the dispatcher's (a direct C++ call would wipe our
// stack — the reason `spell`, which is stack-free, calls Spell::AtUnit directly
// while `item` routes through here).
bool CallTableFunc2Str(void *L, const char *table, const char *method,
                       const char *a, const char *b) {
    const int top = Game::Lua::GetTop(L);
    bool called = false;
    Game::Lua::PushString(L, table);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);   // _G[table]
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        const int t = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, method);
        Game::Lua::GetTable(L, t);                       // _G[table][method]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION) {
            Game::Lua::PushString(L, a);
            Game::Lua::PushString(L, b);
            Game::Lua::Call(L, 2, 0);
            called = true;
        }
    }
    Game::Lua::SetTop(L, top);
    return called;
}

// ---- mouse-focus poll (drives the native mouseover slot) -------------------

// Frames whose mouse we enabled when their `unit` attribute was first set
// (a bare frame never becomes the mouse focus otherwise). Keyed by the C
// object. Single-threaded (Lua + WorldTick on the main thread).
std::unordered_set<const void *> g_unitFrames;

// The last GUID *we* wrote into the mouseover slot. We only touch the slot on a
// change of our own value, so when the cursor isn't over a unit frame (guid 0)
// we never clobber the engine's own 3D mouseover.
uint64_t g_lastSet = 0;

const void *CurrentMouseFocus() {
    const void *ctx = Game::Read<const void *>(
        static_cast<uintptr_t>(Offsets::VAR_UI_CONTEXT_PTR));
    if (ctx == nullptr)
        return nullptr;
    return Game::Read<const void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);
}

// The engine's real mouseover setter — `__stdcall(guidLo, guidHi, prevLo,
// prevHi)`. Highlights the unit's model, builds the mouseover tooltip, fires
// UPDATE_MOUSEOVER_UNIT, and writes the GUID slot; pass 0,0 for the optional
// previous, and (0,0,0,0) to clear. Same address `Unit::Mouseover` co-hooks, so
// calling it here goes through that hook (loss events keep firing correctly).
void CallEngineMouseover(uint64_t guid) {
    reinterpret_cast<void(__stdcall *)(uint32_t, uint32_t, uint32_t, uint32_t)>(
        static_cast<uintptr_t>(Offsets::FUN_SET_MOUSEOVER_UNIT))(
        static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32), 0, 0);
}

// True when the GUID resolves to a live unit/player object (loaded, in range).
// False for an offline or far out-of-range party/raid member — the case the
// engine mouseover setter skips the tooltip for. Same resolver Unit::Mouseover
// uses for its unit test.
bool GuidHasLiveUnit(uint64_t guid) {
    if (guid == 0)
        return false;
    constexpr int kUnitOrPlayerMask =
        (1 << Offsets::OBJECT_TYPE_UNIT) | (1 << Offsets::OBJECT_TYPE_PLAYER);
    return Object::ByGuid(kUnitOrPlayerMask, guid, "ClassicAPI", 0) != nullptr;
}

// Build the mouseover tooltip for a unit with no live object, straight from the
// party/raid roster. FUN_00529FE0 is the same unit-tooltip builder SetUnit and
// the engine mouseover use; with no object it fills name/level/class + the
// "Offline" line from the roster (verified: `GameTooltip:SetUnit("party1")`
// works for an offline member). The engine's setter (FUN_00492890) gates this
// call on a live object, so offline members get nothing — we do it here.
void BuildRosterUnitTooltip(uint64_t guid) {
    void *tooltip = Game::Read<void *>(
        static_cast<uintptr_t>(Offsets::VAR_GAMETOOLTIP_OBJECT_PTR));
    if (tooltip == nullptr)
        return;

    // Fire OnTooltipSetDefaultAnchor first, exactly as the engine mouseover
    // setter (FUN_00492890) does before it builds — FrameXML's handler sets
    // the tooltip's owner + anchor, which is what actually shows it. Without
    // this, a rebuild after the tooltip was hidden on the previous leave has
    // no owner and never re-appears (only a subsequent live-unit hover, which
    // takes the engine's own anchor path, would). Use the self-contained
    // invoker FUN_FRAME_INVOKE_SCRIPT, safe to call from the WorldTick.
    const int anchorHandler = Game::Read<int>(
        tooltip, Offsets::OFF_TOOLTIP_SET_DEFAULT_ANCHOR_HANDLER);
    if (anchorHandler != 0)
        reinterpret_cast<void(__fastcall *)(int, void *)>(
            static_cast<uintptr_t>(Offsets::FUN_FRAME_INVOKE_SCRIPT))(anchorHandler,
                                                                      tooltip);

    uint32_t packed[2] = {static_cast<uint32_t>(guid),
                          static_cast<uint32_t>(guid >> 32)};
    reinterpret_cast<void(__thiscall *)(void *, uint32_t *)>(
        static_cast<uintptr_t>(Offsets::FUN_GAMETOOLTIP_BUILD_UNIT))(tooltip, packed);
}

// The unit a hovered frame stands for — 4.3.4's CGWorldFrame::OnLayerUpdate:
// for the mouse-focus frame it pcalls the FrameXML function
// `SecureButton_GetModifiedUnit(frame, macroRunning)` under the engine's
// script error handler and uses the token it returns, so `unitsuffix` and
// modifier-qualified `unit` attributes count. 4.3.4 asks protected frames;
// 1.12 has no protection, so the analog is a frame that has attributes at
// all. macroRunning is nil while hovering (no macro is executing), so the
// call passes the frame alone. Returns "" for no unit.
std::string ModifiedUnit(void *L, void *frame) {
    std::string token;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushObject(L, frame);                     // [frame]
    const int fi = Game::Lua::GetTop(L);
    if (PushSub(L, fi, /*create*/ false)) {               // [frame, attrs]
        Game::Lua::SetTop(L, fi);
        int errIdx = 0;
        const int errRef = Game::Read<int>(
            static_cast<uintptr_t>(Offsets::VAR_FRAMESCRIPT_ERROR_HANDLER_REF));
        if (errRef > 0) {
            Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, errRef);
            errIdx = Game::Lua::GetTop(L);
        }
        Game::Lua::PushString(L, "SecureButton_GetModifiedUnit");
        Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION) {
            Game::Lua::PushValue(L, fi);
            if (Game::Lua::PCall(L, 1, 1, errIdx) == 0 &&
                Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING)
                token = Game::Lua::ToString(L, -1);
        }
    }
    Game::Lua::SetTop(L, top);
    return token;
}

void MouseoverTick() {
    void *focus = const_cast<void *>(CurrentMouseFocus());

    uint64_t guid = 0;
    std::string token;
    if (focus != nullptr) {
        if (void *L = Game::Lua::State()) {
            token = ModifiedUnit(L, focus);
            if (!token.empty())
                guid = Unit::Identity::GuidForToken(token.c_str()); // live — follows target changes
        }
    }

    if (guid != g_lastSet) {
        // The tooltip built below is for the frame's unit, so `GetUnit()`
        // answers with the frame's token — "mouseover" would not resolve for
        // a member with no live object (see Unit::Tooltip).
        if (guid != 0)
            Unit::Tooltip::StageToken(
                Game::Read<void *>(static_cast<uintptr_t>(Offsets::VAR_GAMETOOLTIP_OBJECT_PTR)),
                token.c_str());
        CallEngineMouseover(guid); // sets slot + highlight + tooltip + event
        // Offline / far out-of-range party or raid member: the setter wrote the
        // GUID slot but skipped the tooltip (no live object). Build it from the
        // roster so hovering the frame still shows name / level / "Offline".
        if (guid != 0 && !GuidHasLiveUnit(guid))
            BuildRosterUnitTooltip(guid);
        Unit::Tooltip::ClearStagedToken();
        g_lastSet = guid;
    }
}

// ---- click dispatch (the frame's own chained OnClick) ----------------------

// Runs the chained-onto previous handler (upvalue 1), vanilla-style with no
// args (it reads the `this`/`arg1` globals the engine set for this invocation).
void ChainOld(void *L) {
    if (Game::Lua::Type(L, Game::Lua::UpvalueIndex(1)) == Game::Lua::TYPE_FUNCTION) {
        Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(1));
        Game::Lua::Call(L, 0, 0);
    }
}

// Builds the modifier prefix ("alt-"/"ctrl-"/"shift-", in that order — retail's
// order) from the live key state, e.g. "alt-shift-". Empty when none held.
void BuildModifierPrefix(void *L, char *buf, size_t n) {
    buf[0] = '\0';
    size_t i = 0;
    auto add = [&](const char *s) {
        for (const char *p = s; *p && i + 1 < n; ++p)
            buf[i++] = *p;
        buf[i] = '\0';
    };
    if (CallBoolGlobal(L, "IsAltKeyDown"))     add("alt-");
    if (CallBoolGlobal(L, "IsControlKeyDown")) add("ctrl-");
    if (CallBoolGlobal(L, "IsShiftKeyDown"))   add("shift-");
}

// Button name -> attribute suffix (retail's convention: the button number).
const char *ButtonSuffix(const char *btn) {
    if (Ascii::EqualCI(btn, "RightButton"))  return "2";
    if (Ascii::EqualCI(btn, "MiddleButton")) return "3";
    if (Ascii::EqualCI(btn, "Button4"))      return "4";
    if (Ascii::EqualCI(btn, "Button5"))      return "5";
    return "1"; // LeftButton / unknown
}

// Resolves a modified attribute into `buf`. Precedence (practical subset of the
// GetAttribute wildcard rules): prefix..name..suffix (e.g. "shift-type1"),
// then name..suffix ("type1"), then name ("type"). So a plain `type1` still
// applies under any modifier unless a modifier-specific attribute overrides it.
bool ReadModAttr(void *L, int fi, const char *prefix, const char *name,
                 const char *suffix, char *buf, size_t n) {
    char key[128];
    Compose3Lower(key, sizeof key, prefix, name, suffix);
    if (CopyAttr(L, fi, key, buf, n)) return true;
    Compose3Lower(key, sizeof key, "", name, suffix);
    if (CopyAttr(L, fi, key, buf, n)) return true;
    Compose3Lower(key, sizeof key, "", name, "");
    return CopyAttr(L, fi, key, buf, n);
}

// Reads attribute `keyLower` as an int (stored as a number, or a numeric
// string) into *out. Leaves the stack as it found it. Returns true on success.
bool ReadAttrInt(void *L, int fi, const char *keyLower, int *out) {
    bool ok = false;
    if (TryPushValue(L, fi, keyLower)) {                 // value left on top
        const int t = Game::Lua::Type(L, -1);
        if (t == Game::Lua::TYPE_NUMBER) {
            *out = static_cast<int>(Game::Lua::ToNumber(L, -1));
            ok = true;
        } else if (t == Game::Lua::TYPE_STRING) {
            const char *s = Game::Lua::ToString(L, -1);
            char *end = nullptr;
            const long v = s ? std::strtol(s, &end, 10) : 0;
            if (s && end != s) {
                *out = static_cast<int>(v);
                ok = true;
            }
        }
        Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1);  // pop the peeked value
    }
    return ok;
}

// Numeric analog of ReadModAttr (prefix..name..suffix precedence).
bool ReadModAttrInt(void *L, int fi, const char *prefix, const char *name,
                    const char *suffix, int *out) {
    char key[128];
    Compose3Lower(key, sizeof key, prefix, name, suffix);
    if (ReadAttrInt(L, fi, key, out)) return true;
    Compose3Lower(key, sizeof key, "", name, suffix);
    if (ReadAttrInt(L, fi, key, out)) return true;
    Compose3Lower(key, sizeof key, "", name, "");
    return ReadAttrInt(L, fi, key, out);
}

// Parses a "bag slot" string (two integers, e.g. "0 1") into bag/slot. Returns
// false unless the whole string is exactly two integers, so a normal item name
// or a bare itemID ("12345") falls through to the name path.
bool ParseBagSlot(const char *s, int *bag, int *slot) {
    char *end = nullptr;
    const long b = std::strtol(s, &end, 10);
    if (end == s) return false;                          // not a leading number
    const char *p = end;
    const long sl = std::strtol(p, &end, 10);
    if (end == p) return false;                          // only one number
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') return false;                      // trailing junk
    *bag = static_cast<int>(b);
    *slot = static_cast<int>(sl);
    return true;
}

// _G[name](a, b) — two number args, no results. Returns true iff called.
bool CallGlobalNum2(void *L, const char *name, double a, double b) {
    const int top = Game::Lua::GetTop(L);
    const bool called = Game::Lua::PushGlobalFunction(L, name);
    if (called) {
        Game::Lua::PushNumber(L, a);
        Game::Lua::PushNumber(L, b);
        Game::Lua::Call(L, 2, 0);
    }
    Game::Lua::SetTop(L, top);
    return called;
}

// _G[name](a) — one number arg, no results. Returns true iff called.
bool CallGlobalNum1(void *L, const char *name, double a) {
    const int top = Game::Lua::GetTop(L);
    const bool called = Game::Lua::PushGlobalFunction(L, name);
    if (called) {
        Game::Lua::PushNumber(L, a);
        Game::Lua::Call(L, 1, 0);
    }
    Game::Lua::SetTop(L, top);
    return called;
}

// `click` verb: the `clickbutton` attribute holds a FRAME (not a string).
// Resolve it with the same modified-key precedence as ReadModAttr, then forward
// the click via delegate:Click(button). Returns true iff a delegate was called.
bool ClickDelegate(void *L, int fi, const char *prefix, const char *suffix,
                   const char *button) {
    char key[128];
    Compose3Lower(key, sizeof key, prefix, "clickbutton", suffix);
    bool got = TryPushValue(L, fi, key);
    if (!got) {
        Compose3Lower(key, sizeof key, "", "clickbutton", suffix);
        got = TryPushValue(L, fi, key);
    }
    if (!got) {
        Compose3Lower(key, sizeof key, "", "clickbutton", "");
        got = TryPushValue(L, fi, key);
    }
    if (!got) return false;                          // no delegate configured

    const int di = Game::Lua::GetTop(L);             // the delegate value
    bool called = false;
    if (Game::Lua::Type(L, di) == Game::Lua::TYPE_TABLE) {
        Game::Lua::PushString(L, "Click");
        Game::Lua::GetTable(L, di);                  // delegate.Click
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION) {
            Game::Lua::PushValue(L, di);             // self = delegate
            Game::Lua::PushString(L, button);        // button name
            Game::Lua::Call(L, 2, 0);
            called = true;
        }
    }
    Game::Lua::SetTop(L, di - 1);                    // drop the delegate (+leftovers)
    return called;
}

// Custom verb: a function stored as a raw field on the frame's own table named
// by the verb (retail's escape hatch), invoked as func(self, unit, button).
// Returns true iff such a function existed and was called.
bool CallCustomAction(void *L, int fi, const char *verb, const char *unit,
                      const char *button) {
    if (Game::Lua::Type(L, fi) != Game::Lua::TYPE_TABLE) return false;
    Game::Lua::PushString(L, verb);
    Game::Lua::RawGet(L, fi);                        // frame[verb]
    bool called = false;
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION) {
        Game::Lua::PushValue(L, fi);                 // self
        Game::Lua::PushString(L, unit);              // unit (nil if null)
        Game::Lua::PushString(L, button);            // button name
        Game::Lua::Call(L, 3, 0);
        called = true;
    } else {
        Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1);
    }
    return called;
}

// Performs the resolved `verb` on `unit` (a token attribute value, may be null).
// `button` is the raw click-button name ("LeftButton", …), needed by the
// `click` and custom verbs. Returns true if it owned the click (so the chained
// handler is skipped).
bool DispatchVerb(void *L, int fi, const char *prefix, const char *suffix,
                  const char *verb, const char *unit, const char *button) {
    if (Ascii::EqualCI(verb, "target")) {
        if (!unit) return false;
        // `unit="none"` clears the target (retail's SecureActionButton behavior).
        if (Ascii::EqualCI(unit, "none")) {
            Game::Lua::CallGlobal(L, "ClearTarget");
            return true;
        }
        // Cursor / pending-spell take precedence, matching the engine's default
        // unit interaction — cast the pending spell / drop the item on the unit
        // instead of switching target. The two predicates are ours (direct C++);
        // the actions are engine-only, so they go through the Lua globals (which
        // are the engine's own entries, with token→GUID resolution).
        if (Spell::Placement::IsActive())
            Game::Lua::CallGlobalString(L, "SpellTargetUnit", unit);
        else if (Cursor::Info::HasItem())
            Game::Lua::CallGlobalString(L, "DropItemOnUnit", unit);
        else
            Game::Lua::CallGlobalString(L, "TargetUnit", unit);
        return true;
    }
    if (Ascii::EqualCI(verb, "assist")) {
        if (!unit) return false;
        Game::Lua::CallGlobalString(L, "AssistUnit", unit);
        return true;
    }
    if (Ascii::EqualCI(verb, "focus")) {
        if (!unit) return false;
        Unit::Focus::Set(Unit::Identity::GuidForToken(unit));
        return true;
    }
    if (Ascii::EqualCI(verb, "spell")) {
        // A numeric spell ID (modern addons do SetAttribute("spell", 12345),
        // stored as a Lua number; a numeric string is accepted too). Cast the
        // EXACT rank by ID through the spellbook-slot resolver — at the unit, or
        // with no unit on the current target (targetGuid 0). NOT name
        // resolution, which casts the highest known rank regardless of the rank
        // the caller asked for.
        int spellID;
        if (ReadModAttrInt(L, fi, prefix, "spell", suffix, &spellID)) {
            if (unit)
                Spell::AtUnit::CastByID(spellID, unit);
            else
                Spell::Placement::DispatchSpellCast(spellID);
            return true;
        }
        // Otherwise a spell name. With a `unit`, cast straight at it (GUID →
        // dispatcher, no target juggling; a ground-target spell lands at its
        // feet). With no `unit`, fall back to the plain global cast on the
        // current target — a `type="spell"` button that sets no `unit`
        // attribute (issue #18) works like `/cast <spell>`.
        char spell[128];
        if (!ReadModAttr(L, fi, prefix, "spell", suffix, spell, sizeof spell))
            return false;
        if (unit)
            Spell::AtUnit::CastByName(spell, unit, /*placeGroundSpell*/ true);
        else
            Game::Lua::CallGlobalString(L, "CastSpellByName", spell);
        return true;
    }
    if (Ascii::EqualCI(verb, "item")) {
        char item[128];
        int bag, slot;
        if (ReadModAttr(L, fi, prefix, "item", suffix, item, sizeof item)) {
            if (ParseBagSlot(item, &bag, &slot))
                CallGlobalNum2(L, "UseContainerItem", bag, slot);
            else
                CallTableFunc2Str(L, "C_Item", "UseItemByName", item, unit);
            return true;
        }
        // Deprecated form: no `item`, take the `bag` + `slot` attributes.
        if (ReadModAttrInt(L, fi, prefix, "bag", suffix, &bag) &&
            ReadModAttrInt(L, fi, prefix, "slot", suffix, &slot)) {
            CallGlobalNum2(L, "UseContainerItem", bag, slot);
            return true;
        }
        return false;
    }
    if (Ascii::EqualCI(verb, "macro")) {
        // Give the macro a unit to act on. Vanilla macros have no `@unit`
        // conditional, and unlike the `spell` verb (which feeds a GUID straight
        // to the cast dispatcher) we can't inject a target into arbitrary macro
        // text — so a plain `/cast Flash Heal` would hit the current target, not
        // the clicked unit. Emulate the classic Clique click-heal: snapshot the
        // current target, retarget the clicked unit, run the macro, then restore
        // the previous target so the click doesn't leave the player retargeted.
        // The macro's actions (/cast etc.) dispatch against the clicked unit
        // synchronously while it's selected, so restoring the target afterward
        // doesn't affect them.
        uint64_t prevTarget = 0;
        const bool swapped = unit != nullptr;
        if (swapped) {
            prevTarget =
                (static_cast<uint64_t>(Game::Read<volatile uint32_t>(
                     Offsets::VAR_CURRENT_SELECTION_GUID_HI))
                 << 32) |
                Game::Read<volatile uint32_t>(
                    Offsets::VAR_CURRENT_SELECTION_GUID_LO);
            Game::Lua::CallGlobalString(L, "TargetUnit", unit);
        }
        char macro[512];
        bool handled = false;
        // `macrotext` is raw macro text (the modern default). Run it as text
        // through the stock chat parser so each line dispatches via
        // SlashCmdList — that routes /cast etc. through any addon slash hooks
        // (e.g. SuperCleveRoidMacros' conditional /cast). A name-based RunMacro
        // (which is what SuperCleveRoidMacros ships) can't interpret text and
        // would silently resolve nothing.
        if (ReadModAttr(L, fi, prefix, "macrotext", suffix, macro, sizeof macro)) {
            Macro::Execute::Text(L, macro);
            handled = true;
        }
        // Deprecated `macro` form: a saved-macro name/index. Prefer an
        // addon-provided RunMacro (SuperCleveRoidMacros, pfUI, …) to run the
        // named macro's body; fall back to the stock parser when none exists.
        else if (ReadModAttr(L, fi, prefix, "macro", suffix, macro, sizeof macro)) {
            handled = Macro::Execute::Saved(L, macro);
        }
        // Restore the target the click swapped away from. `FUN_TARGET_BY_GUID`
        // validates the GUID resolves to a live unit and bails otherwise, so a
        // target that despawned mid-macro is dropped cleanly rather than
        // committed; a zero prior target means "no target" → ClearTarget.
        if (swapped) {
            if (prevTarget)
                reinterpret_cast<void(__fastcall *)(const uint64_t *)>(
                    Offsets::FUN_TARGET_BY_GUID)(&prevTarget);
            else
                Game::Lua::CallGlobal(L, "ClearTarget");
        }
        return handled;
    }
    if (Ascii::EqualCI(verb, "stop") || Ascii::EqualCI(verb, "stopcasting")) {
        Game::Lua::CallGlobal(L, "SpellStopCasting");
        return true;
    }
    if (Ascii::EqualCI(verb, "action")) {
        int slot;
        if (!ReadModAttrInt(L, fi, prefix, "action", suffix, &slot)) return false;
        CallGlobalNum1(L, "UseAction", slot);
        return true;
    }
    if (Ascii::EqualCI(verb, "pet")) {
        int index;
        if (!ReadModAttrInt(L, fi, prefix, "action", suffix, &index)) return false;
        CallGlobalNum1(L, "CastPetAction", index);
        return true;
    }
    if (Ascii::EqualCI(verb, "click")) {
        return ClickDelegate(L, fi, prefix, suffix, button);
    }
    if (Ascii::EqualCI(verb, "menu") || Ascii::EqualCI(verb, "togglemenu")) {
        // While a spell is on the cursor, cancel targeting instead of opening
        // the menu (retail's SecureUnitButton behavior).
        if (CallBoolGlobal(L, "SpellIsTargeting")) {
            Game::Lua::CallGlobal(L, "SpellStopTargeting");
            return true;
        }
        if (!unit) return false;
        // The unit dropdown is pure FrameXML work (UnitPopup + ToggleDropDown),
        // so it lives in the !!!ClassicAPI addon; we just pop it at the cursor.
        Game::Lua::CallGlobalString(L, "ClassicAPI_ToggleUnitMenu", unit);
        return true;
    }
    // Unknown verb → a custom action: a function stored as a raw field on the
    // frame named by the verb (retail's escape hatch). If none exists it isn't
    // handled here and the frame's own chained OnClick runs.
    return CallCustomAction(L, fi, verb, unit, button);
}

// The chained OnClick handler. Upvalues: 1 = previous handler (or nil), 2 = the
// frame (captured at install time, so we don't depend on the `this` global).
// Resolves one verb per click from the (modifier/button) `type` attribute and
// performs it — retail's one-action-per-click model. Only chains the previous
// handler when we DIDN'T own the click, so a configured `type1` no longer runs
// alongside the addon's own OnClick (that double-dispatch was the conflict).
int __fastcall OnClick_c(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(2)); // frame
    const int fi = Game::Lua::GetTop(L);
    bool handled = false;
    if (Game::Lua::Type(L, fi) == Game::Lua::TYPE_TABLE) {
        char btn[32] = {0};
        Game::Lua::PushString(L, "arg1");
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING) {
            const char *b = Game::Lua::ToString(L, -1);
            if (b)
                BoundedCopy(btn, b, sizeof btn);
        }
        Game::Lua::SetTop(L, fi); // drop arg1, keep frame

        const char *suffix = ButtonSuffix(btn);
        char prefix[24];
        BuildModifierPrefix(L, prefix, sizeof prefix);

        char verb[32];
        if (ReadModAttr(L, fi, prefix, "type", suffix, verb, sizeof verb)) {
            char unit[128];
            const bool haveUnit =
                ReadModAttr(L, fi, prefix, "unit", suffix, unit, sizeof unit);
            handled = DispatchVerb(L, fi, prefix, suffix, verb,
                                   haveUnit ? unit : nullptr, btn);
        }
    }
    Game::Lua::SetTop(L, top);
    if (!handled)
        ChainOld(L); // let the frame's own OnClick handle unconfigured clicks
    return 0;
}

// Installs OnClick as a closure over (previous handler, frame). Operates on
// self at stack index 1. Raises if `self` isn't a Button (OnClick is a Button
// script) — callers run it under PCall.
//
// Self-healing against clobbering: addons re-`SetScript("OnClick", …)` their
// own handler (pfUI does it on every raid relayout via UpdateScripts), which
// replaces our closure. So we re-wire whenever a `type*` attribute is (re)set,
// but only when the current OnClick is a *Lua* handler (a real clobber) or nil
// — if it's already a C function it's our own closure, and re-wrapping would
// double-chain. `IsCFunction` cleanly distinguishes the two here (addon click
// handlers are Lua; the only C-function OnClick we ever see is ours).
int __fastcall WireOnClick(void *L) {
    Game::Lua::SetTop(L, 1);                             // (self)
    Game::Lua::PushString(L, "OnClick");                 // (self, name)
    CallScript(Offsets::FUN_SCRIPT_FRAME_GETSCRIPT, L);  // (self, name, old)
    if (Game::Lua::GetTop(L) < 3)                        // defensive
        Game::Lua::PushNil(L);
    Game::Lua::SetTop(L, 3);                             // (self, name, old|nil)
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_FUNCTION &&
        Game::Lua::IsCFunction(L, 3) != 0)
        return 0;                                        // already our closure
    Game::Lua::PushValue(L, 1);                          // (self, name, old, self)
    Game::Lua::PushCClosure(L, &OnClick_c, 2);           // upvalues (old, self)
    CallScript(Offsets::FUN_SCRIPT_FRAME_SETSCRIPT, L);
    return 0;
}

// ---- OnAttributeChanged script (settable via SetScript, like retail) --------
//
// Backport of the modern `OnAttributeChanged` widget script: SetAttribute (but
// NOT SetAttributeNoHandler) fires the frame's OnAttributeChanged handler. 1.12
// has no such script; Frame::ExtraScripts makes it SetScript/GetScript/
// HookScript-able on every frame.

const Frame::ExtraScripts::Script g_onAttributeChanged{"onattributechanged", Offsets::VAR_FRAME_LUA_TYPE_ID};

// Fire OnAttributeChanged on `frame` — 4.3.4's
// CSimpleFrame::RunOnAttributeChangedScript: the handler gets the lowercased
// name and the new value (still at stack[3] in DoSet) as real arguments,
// `(self, name, value)`, plus the vanilla `this`/`arg1`/`arg2` globals.
// RunWithValues carries arbitrary Lua values, which the engine's
// printf-style runners cannot, and restores the stack and globals.
//
// No recursion guard, as in 4.3.4: a handler that sets attributes — its own
// or another frame's — fires their handlers too. Secure headers and the
// state driver depend on that. A runaway loop ends at Lua's C-call limit as
// an ordinary, reported script error.
void FireAttributeChanged(void *L, void *frame, const char *name) {
    if (frame == nullptr)
        return;
    uint32_t *slot = g_onAttributeChanged.Slot(frame);
    if (slot == nullptr || slot[0] == 0)
        return;
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, name);
    if (top >= 3)
        Game::Lua::PushValue(L, 3); // the new value
    else
        Game::Lua::PushNil(L);
    Frame::ScriptArgs::RunWithValues(L, static_cast<int>(slot[0]), frame, top + 1, 2);
    Game::Lua::SetTop(L, top);
}

// ---- the methods -----------------------------------------------------------

// Enables the frame's mouse so it can become the mouse-focus (a bare frame
// otherwise never registers as hovered).
//
// Two properties, both deliberate:
//   * STACK-NEUTRAL. The old form did SetTop(L, 1) to set up (self, true) for
//     the engine setter, which DESTROYED the (name, value) at stack[2]/[3] that
//     FireAttributeChanged reads right after — handing any OnAttributeChanged
//     handler a nil `arg2`. We instead push the call at the top and restore the
//     caller's stack, so DoSet's (self, name, value) survives intact.
//   * PROTECTED. SetAttribute is Frame-scoped (VAR_FRAME_METHOD_REGISTRY), so
//     `self` is a real Frame and the setter's type check normally passes;
//     invoking through lua_pcall is belt-and-suspenders so a best-effort side
//     effect can never surface an error in the addon's SetAttribute. (Calling
//     via pcall is also what lets us pass the args at the callee's own stack
//     base without clobbering ours.)
void EnableFrameMouse(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushCClosure(
        L,
        reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_FRAME_ENABLEMOUSE),
        0);
    Game::Lua::PushValue(L, 1);   // self (the frame)
    Game::Lua::PushBoolean(L, 1); // enable = true
    Game::Lua::PCall(L, 2, 0, 0); // best-effort; swallow a transient type error
    Game::Lua::SetTop(L, top);    // restore the caller's stack
}

// (self, name, value). `usage` is the method's error text; 4.3.4 requires
// the value argument to be present (`lua_type(L, 3) != LUA_TNONE`), so
// `SetAttribute("x")` is an error while `SetAttribute("x", nil)` clears.
int DoSet(void *L, bool fireHandler, const char *usage) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2) || Game::Lua::GetTop(L) < 3) {
        Game::Lua::Error(L, usage, Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    Game::Lua::SetTop(L, 3);
    char lname[256];
    LowerCopy(lname, Game::Lua::ToString(L, 2), sizeof lname);

    if (PushSub(L, 1, true)) {               // [.., sub]
        const int si = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, lname);     // [.., sub, key]
        Game::Lua::PushValue(L, 3);          // [.., sub, key, value]  (nil if absent)
        Game::Lua::RawSet(L, si);            // sub[lname] = value
        Game::Lua::SetTop(L, si - 1);        // drop sub
    }

    const bool isString = Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING;

    // `unit` drives the mouseover binding: index it by the frame's C object
    // (the key the mouse-focus poll uses) and enable its mouse so it can be
    // hovered.
    if (std::strcmp(lname, "unit") == 0) {
        if (isString) {
            if (g_unitFrames.insert(frame).second)
                EnableFrameMouse(L);
        } else {
            g_unitFrames.erase(frame);
        }
    }
    // A `type*` attribute makes the frame a clickable unit button: (re)install
    // the chained OnClick (PCall-guarded — OnClick is Button-only). WireOnClick
    // is idempotent and self-healing, so calling it on every `type*` set both
    // avoids double-chaining and reinstalls after an addon clobbered our
    // OnClick — set `type*` again after re-SetScript to recover. The handler
    // reads all the attributes fresh at click time. IsTypeAttr covers every
    // form the resolver reads (type, typeN, modifier-prefixed), so a middle-
    // click or modifier-only binding still wires.
    else if (IsTypeAttr(lname) && isString) {
        Game::Lua::PushCClosure(L, &WireOnClick, 0);
        Game::Lua::PushValue(L, 1); // arg = self
        Game::Lua::PCall(L, 1, 0, 0); // ignore errors (non-Button frame)
    }

    if (fireHandler)
        FireAttributeChanged(L, frame, lname);
    return 0;
}

int __fastcall Script_SetAttribute(void *L) {
    return DoSet(L, /*fireHandler*/ true, "Usage: %s:SetAttribute(\"name\", value)");
}
int __fastcall Script_SetAttributeNoHandler(void *L) {
    return DoSet(L, /*fireHandler*/ false,
                 "Usage: %s:SetAttributeNoHandler(\"name\", value)");
}

// `cleared = frame:ClearAttribute("name")` — retail (11.2.0) attribute removal;
// `cleared` is true iff the attribute was set (and is now gone). Routes through
// DoSet's nil-value path so it also drops a `unit` mouseover binding, but does
// NOT fire OnAttributeChanged — verified against retail (a live 12.0 test showed
// ClearAttribute firing no handler, unlike SetAttribute).
int __fastcall Script_ClearAttribute(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:ClearAttribute(\"name\")",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    char lname[256];
    LowerCopy(lname, Game::Lua::ToString(L, 2), sizeof lname);

    const bool existed = TryPushValue(L, 1, lname); // leaves the value on top if set
    if (existed)
        Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1); // drop the peeked value

    Game::Lua::SetTop(L, 2);   // (self, name)
    Game::Lua::PushNil(L);     // (self, name, nil) → value = nil clears the key
    DoSet(L, /*fireHandler*/ false, ""); // retail ClearAttribute fires no OnAttributeChanged

    Game::Lua::PushBool(L, existed);
    return 1;
}

// 4.3.4's Script_GetAttribute. The 3-arg form `(prefix, name, suffix)` is
// taken only with exactly those three arguments and a string name; a nil
// prefix or suffix counts as empty. Lookup order: prefix..name..suffix,
// "*"..name..suffix, prefix..name.."*", "*"..name.."*", then name.
int __fastcall Script_GetAttribute(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    if (Game::Lua::GetTop(L) == 4 && Game::Lua::IsString(L, 3)) {
        const char *pfx = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "";
        const char *nm = Game::Lua::ToString(L, 3);
        const char *sfx = Game::Lua::IsString(L, 4) ? Game::Lua::ToString(L, 4) : "";
        char key[256];
        Compose3Lower(key, sizeof key, pfx, nm, sfx);
        if (TryPushValue(L, 1, key)) return 1;
        Compose3Lower(key, sizeof key, "*", nm, sfx);
        if (TryPushValue(L, 1, key)) return 1;
        Compose3Lower(key, sizeof key, pfx, nm, "*");
        if (TryPushValue(L, 1, key)) return 1;
        Compose3Lower(key, sizeof key, "*", nm, "*");
        if (TryPushValue(L, 1, key)) return 1;
        Compose3Lower(key, sizeof key, "", nm, "");
        if (TryPushValue(L, 1, key)) return 1;
        Game::Lua::PushNil(L);
        return 1;
    }
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:GetAttribute(\"name\")",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }

    char lname[256];
    LowerCopy(lname, Game::Lua::ToString(L, 2), sizeof lname);
    if (TryPushValue(L, 1, lname))
        return 1;
    Game::Lua::PushNil(L);
    return 1;
}

// ---- XML <Attributes> --------------------------------------------------------

// 4.3.4's CSimpleFrame::LoadXML_Attributes, for each `<Attributes>` child of
// a frame element:
//
//     <Attributes>
//         <Attribute name="unit" value="player"/>
//         <Attribute name="count" type="number" value="3"/>
//     </Attributes>
//
// `type` is nil / boolean / number / string (the default), converted with the
// loader's own helpers (FUN_XML_ATTR_TO_BOOL, FUN_SSTR_TO_FLOAT). Like 4.3.4
// it stores without firing OnAttributeChanged — here by calling our own
// SetAttributeNoHandler, so a `unit` or `type*` set from XML has the same
// effect as one set from Lua. It runs after the engine has loaded the whole
// node (Xml::FrameLoad), so the element's <Scripts> are already in place, and
// inherited templates are loaded first, so the frame's own values win.
// Problems are the engine's load warnings, with 4.3.4's texts.
void LoadXmlAttributes(void *L, void *frame, const void *attributes, void *status) {
    using ToFloat_t = float(__stdcall *)(const char *);

    const int top = Game::Lua::GetTop(L);
    int frameIdx = 0;
    for (const void *e = Xml::FirstChild(attributes); e != nullptr; e = Xml::NextSibling(e)) {
        const char *frameName = Game::Lua::ObjectDisplayName(frame);
        if (!Xml::TagIs(e, "Attribute")) {
            Xml::Warn(status, "Frame %s: Unknown attributes element %s", frameName, Xml::Tag(e));
            continue;
        }
        const char *name = Xml::Attr(e, "name");
        if (name == nullptr) {
            Xml::Warn(status, "Frame %s: unnamed attribute element", frameName);
            continue;
        }
        const char *type = Xml::Attr(e, "type");
        if (type == nullptr)
            type = "string";
        const char *value = Xml::Attr(e, "value");
        const bool isNil = Ascii::EqualCI(type, "nil");
        if (value == nullptr && !isNil) {
            Xml::Warn(status, "Frame %s: attribute element named %s missing value",
                      frameName, name);
            continue;
        }

        if (frameIdx == 0) {
            if (!Xml::PushWrapper(L, frame))
                break;
            frameIdx = Game::Lua::GetTop(L);
        }
        Game::Lua::PushCClosure(L, &Script_SetAttributeNoHandler, 0);
        Game::Lua::PushValue(L, frameIdx);
        Game::Lua::PushString(L, name);
        if (isNil)
            Game::Lua::PushNil(L);
        else if (Ascii::EqualCI(type, "boolean"))
            Game::Lua::PushBool(L, Game::StringToFlag(value));
        else if (Ascii::EqualCI(type, "number"))
            Game::Lua::PushNumber(L, reinterpret_cast<ToFloat_t>(Offsets::FUN_SSTR_TO_FLOAT)(value));
        else
            Game::Lua::PushString(L, value);
        Game::Lua::PCall(L, 3, 0, 0);
        Game::Lua::SetTop(L, frameIdx);
    }
    Game::Lua::SetTop(L, top);
}

void OnFrameNode(void *frame, const void *node, void *status) {
    void *L = nullptr;
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c)) {
        if (!Xml::TagIs(c, "Attributes"))
            continue;
        if (L == nullptr && (L = Game::Lua::State()) == nullptr)
            return;
        LoadXmlAttributes(L, frame, c, status);
    }
}

const Xml::FrameLoad::AutoSubscribe _frameNode{&OnFrameNode};

// ---- registration ----------------------------------------------------------

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"SetAttribute", &Script_SetAttribute},
    {"SetAttributeNoHandler", &Script_SetAttributeNoHandler},
    {"ClearAttribute", &Script_ClearAttribute},
    {"GetAttribute", &Script_GetAttribute},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY),
        g_frameMethods,
        static_cast<int>(sizeof(g_frameMethods) / sizeof(g_frameMethods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Tick::WorldTick::AutoSubscribe _tick{&MouseoverTick};

} // namespace

// Drop the per-frame map before a /reload (or logout) resets the Lua state.
// It is keyed by the frame's C object pointer, which the allocator recycles
// across a reload: g_unitFrames records which frames already had their mouse
// enabled, and a stale entry at a reused address would skip enabling a new
// frame's. (The OnAttributeChanged cells are dropped by Frame::ExtraScripts.)
void PrepareForReload() {
    g_unitFrames.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace Frame::Attributes
