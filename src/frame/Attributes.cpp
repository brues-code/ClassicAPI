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
// we mirror retail / SuperWoW's `SetMouseoverUnit`: watch the engine's
// mouse-focus frame (`*(*VAR_UI_CONTEXT_PTR + OFF_UI_CONTEXT_MOUSE_FOCUS)`, the
// CFrameScriptObject* `GetMouseFocus` returns) once per frame on the shared
// WorldTick, and when it's a `unit`-attributed frame call the engine's FULL
// mouseover setter (FUN_00492890) — 1:1 with hovering the unit's 3D model:
// model highlight, mouseover tooltip, UPDATE_MOUSEOVER_UNIT, and the GUID slot.
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
// `spell` attribute and casts it on the unit via our native C_Spell.CastAtUnit —
// the unit's GUID goes straight to the cast dispatcher, no target juggling, and
// ground-target spells land at the unit's feet), `item` (uses the `item`
// attribute — a name/itemID/link via C_Item.UseItemByName with the unit as the
// use target, or a "bag slot" string like "0 1" via UseContainerItem; the
// deprecated `bag`/`slot` attributes also work), `macro` (from the
// `macrotext`/`macro` attribute — prefers an addon RunMacro, else runs natively
// via the stock ChatEdit_ParseText), `stopcasting`, `menu`/`togglemenu` (pops
// the standard unit dropdown at the cursor via the addon's
// ClassicAPI_ToggleUnitMenu). Actions call ordinary Lua globals where the entry
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
// SetAttributeNoHandler) fires it with `this` = frame, `arg1` = name, `arg2` =
// value. Implemented by co-hooking the base-frame script-name resolver
// (`FUN_FRAME_SCRIPT_RESOLVER`) and handing out an external per-frame cell for
// that name — the `Tooltip::SetEvents` analog, applied to every frame.

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "cursor/Info.h"
#include "macro/Execute.h"
#include "object/Resolve.h"
#include "spell/AtCursor.h"
#include "spell/AtUnit.h"
#include "tick/WorldTick.h"
#include "unit/Focus.h"
#include "unit/Identity.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

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

// Bounded, always-terminated copy (avoids the C4996 strncpy/strcpy warnings).
void BoundedCopy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

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
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING) {
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

// frame CFrameScriptObject* → current `unit` token, for the mouse-focus poll.
// Keyed by the same pointer the engine stores at the mouse-focus slot. Single-
// threaded (Lua + WorldTick on the main thread), so no synchronization needed.
std::unordered_map<const void *, std::string> g_unitByFrame;

// The last GUID *we* wrote into the mouseover slot. We only touch the slot on a
// change of our own value, so when the cursor isn't over a unit frame (guid 0)
// we never clobber the engine's own 3D mouseover.
uint64_t g_lastSet = 0;

const void *CurrentMouseFocus() {
    const void *ctx = *reinterpret_cast<const void *const *>(
        static_cast<uintptr_t>(Offsets::VAR_UI_CONTEXT_PTR));
    if (ctx == nullptr)
        return nullptr;
    return *reinterpret_cast<const void *const *>(
        reinterpret_cast<const uint8_t *>(ctx) + Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);
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
    void *tooltip = *reinterpret_cast<void *const *>(
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
    const int anchorHandler = *reinterpret_cast<const int *>(
        reinterpret_cast<uint8_t *>(tooltip) +
        Offsets::OFF_TOOLTIP_SET_DEFAULT_ANCHOR_HANDLER);
    if (anchorHandler != 0)
        reinterpret_cast<void(__fastcall *)(int, void *)>(
            static_cast<uintptr_t>(Offsets::FUN_FRAME_INVOKE_SCRIPT))(anchorHandler,
                                                                      tooltip);

    uint32_t packed[2] = {static_cast<uint32_t>(guid),
                          static_cast<uint32_t>(guid >> 32)};
    reinterpret_cast<void(__thiscall *)(void *, uint32_t *)>(
        static_cast<uintptr_t>(Offsets::FUN_GAMETOOLTIP_BUILD_UNIT))(tooltip, packed);
}

void MouseoverTick() {
    const void *focus = CurrentMouseFocus();

    uint64_t guid = 0;
    if (focus != nullptr) {
        auto it = g_unitByFrame.find(focus);
        if (it != g_unitByFrame.end())
            guid = Unit::Identity::GuidForToken(
                it->second.c_str()); // live — follows target changes
    }

    if (guid != g_lastSet) {
        CallEngineMouseover(guid); // sets slot + highlight + tooltip + event
        // Offline / far out-of-range party or raid member: the setter wrote the
        // GUID slot but skipped the tooltip (no live object). Build it from the
        // roster so hovering the frame still shows name / level / "Offline".
        if (guid != 0 && !GuidHasLiveUnit(guid))
            BuildRosterUnitTooltip(guid);
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

// Performs the resolved `verb` on `unit` (a token attribute value, may be null).
// Returns true if it owned the click (so the chained handler is skipped).
bool DispatchVerb(void *L, int fi, const char *prefix, const char *suffix,
                  const char *verb, const char *unit) {
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
        if (Spell::AtCursor::IsPlacementActive())
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
        if (!unit) return false;
        char spell[128];
        if (!ReadModAttr(L, fi, prefix, "spell", suffix, spell, sizeof spell))
            return false;
        Spell::AtUnit::CastByName(spell, unit);
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
                (static_cast<uint64_t>(*reinterpret_cast<volatile uint32_t *>(
                     Offsets::VAR_CURRENT_SELECTION_GUID_HI))
                 << 32) |
                *reinterpret_cast<volatile uint32_t *>(
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
    if (Ascii::EqualCI(verb, "menu") || Ascii::EqualCI(verb, "togglemenu")) {
        if (!unit) return false;
        // The unit dropdown is pure FrameXML work (UnitPopup + ToggleDropDown),
        // so it lives in the !!!ClassicAPI addon; we just pop it at the cursor.
        Game::Lua::CallGlobalString(L, "ClassicAPI_ToggleUnitMenu", unit);
        return true;
    }
    // Unknown verb → not handled here; the chained handler runs.
    return false;
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
                                   haveUnit ? unit : nullptr);
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
// has no such script, so we make it SetScript/GetScript/HookScript-able the same
// way Tooltip::SetEvents adds OnTooltipSet* — co-hook the frame script-name
// resolver (FUN_FRAME_SCRIPT_RESOLVER, the base every frame type delegates to)
// and, for the one name the engine doesn't know, hand back an external 8-byte
// {handler, context} cell. Frames are immortal in 1.12 (no destroy), so a cell
// keyed by the frame's C object pointer never goes stale.
//
// 1.12 passes script args as globals, not params (see OnClick_c reading arg1),
// so we fire with `this` = frame (bound by the invoker), `arg1` = name and
// `arg2` = the new value: `function() local name, value = arg1, arg2 … end`.

struct AttrScriptSlot { uint32_t v[2]; }; // {handler, exec context}
std::unordered_map<const void *, AttrScriptSlot> g_attrHandlers;

uint32_t *AttrSlotFor(const void *frame, bool create) {
    auto it = g_attrHandlers.find(frame);
    if (it != g_attrHandlers.end())
        return it->second.v;
    if (!create)
        return nullptr;
    AttrScriptSlot &s = g_attrHandlers[frame]; // node-based → stable address
    s.v[0] = 0;
    s.v[1] = 0;
    return s.v;
}

// Resolver co-hook. The engine's __thiscall(frame, name) is modelled as
// __fastcall with a dummy edx — `name` lands on the first stack slot either way
// (same trick as Tooltip::SetEvents). The original returns 0 for a name it
// doesn't know; we then claim "OnAttributeChanged".
using FrameResolver_t = int(__fastcall *)(void *frame, void *edx, const char *name);
FrameResolver_t g_frameResolverOriginal = nullptr;

int __fastcall FrameResolver_h(void *frame, void *edx, const char *name) {
    const int slot = g_frameResolverOriginal(frame, edx, name);
    if (slot != 0) // a real base-frame / subtype script — leave it
        return slot;
    if (Ascii::EqualCI(name, "onattributechanged"))
        return reinterpret_cast<int>(AttrSlotFor(frame, /*create*/ true));
    return 0;
}

const Game::HookAutoRegister _attrResolverHook{
    Offsets::FUN_FRAME_SCRIPT_RESOLVER,
    reinterpret_cast<void *>(&FrameResolver_h),
    reinterpret_cast<void **>(&g_frameResolverOriginal)};

// Fire OnAttributeChanged on `frame`, with arg1 = name and arg2 = the new value
// (still at stack[3] in DoSet). Recursion-guarded so a handler that itself calls
// SetAttribute doesn't loop. The invoker (FUN_FRAME_INVOKE_SCRIPT) binds `this`
// and runs the handler under its own protected pcall; it doesn't restore the
// stack top, so snapshot/restore around it (as Tooltip::SetEvents does).
int g_firingAttr = 0;

using InvokeAttrScript_t = void(__fastcall *)(uint32_t handler, void *frame);

void FireAttributeChanged(void *L, void *frame, const char *name) {
    if (frame == nullptr || g_firingAttr > 0)
        return;
    uint32_t *slot = AttrSlotFor(frame, /*create*/ false);
    if (slot == nullptr || slot[0] == 0)
        return;

    const int savedTop = Game::Lua::GetTop(L);

    // Snapshot the caller's arg1/arg2 first. SetAttribute can be called from
    // inside a running script handler that still needs them, and the invoker
    // below neither saves the globals nor restores the stack top — so we save
    // (onto the stack, below anything the invoker pushes), overwrite, fire,
    // then put them back. 1.12 passes handler args as globals, not params.
    Game::Lua::PushString(L, "arg1");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);        // [.., oldArg1]
    Game::Lua::PushString(L, "arg2");
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);        // [.., oldArg1, oldArg2]
    const int oldArg1 = savedTop + 1;
    const int oldArg2 = savedTop + 2;

    Game::Lua::PushString(L, "arg1");
    Game::Lua::PushString(L, name);
    Game::Lua::RawSet(L, Game::Lua::GLOBALS_INDEX);          // _G.arg1 = name
    Game::Lua::PushString(L, "arg2");
    if (savedTop >= 3)
        Game::Lua::PushValue(L, 3);                          // the new value
    else
        Game::Lua::PushNil(L);
    Game::Lua::RawSet(L, Game::Lua::GLOBALS_INDEX);          // _G.arg2 = value

    ++g_firingAttr;
    reinterpret_cast<InvokeAttrScript_t>(Offsets::FUN_FRAME_INVOKE_SCRIPT)(slot[0], frame);
    --g_firingAttr;

    // Restore arg1/arg2 from the snapshot (the fired handler may also have
    // written them; the caller's values win).
    Game::Lua::PushString(L, "arg1");
    Game::Lua::PushValue(L, oldArg1);
    Game::Lua::RawSet(L, Game::Lua::GLOBALS_INDEX);
    Game::Lua::PushString(L, "arg2");
    Game::Lua::PushValue(L, oldArg2);
    Game::Lua::RawSet(L, Game::Lua::GLOBALS_INDEX);

    Game::Lua::SetTop(L, savedTop);
}

// ---- the methods -----------------------------------------------------------

// Enables the frame's mouse so it can become the mouse-focus (a bare frame
// otherwise never registers as hovered). Operates on self at index 1.
void EnableFrameMouse(void *L) {
    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushBoolean(L, 1);
    CallScript(Offsets::FUN_SCRIPT_FRAME_ENABLEMOUSE, L);
}

int DoSet(void *L, bool fireHandler) { // (self, name, value)
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:SetAttribute(\"name\", value)");
        return 0;
    }
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
        void *obj = Game::Lua::ResolveObject(L, 1);
        if (obj != nullptr) {
            if (isString) {
                const char *tok = Game::Lua::ToString(L, 3);
                const bool isNew = g_unitByFrame.find(obj) == g_unitByFrame.end();
                g_unitByFrame[obj] = tok ? tok : "";
                if (isNew)
                    EnableFrameMouse(L);
            } else {
                g_unitByFrame.erase(obj);
            }
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
        FireAttributeChanged(L, Game::Lua::ResolveObject(L, 1), lname);
    return 0;
}

int __fastcall Script_SetAttribute(void *L) { return DoSet(L, /*fireHandler*/ true); }
int __fastcall Script_SetAttributeNoHandler(void *L) { return DoSet(L, /*fireHandler*/ false); }

// `cleared = frame:ClearAttribute("name")` — retail (11.2.0) attribute removal;
// `cleared` is true iff the attribute was set (and is now gone). Routes through
// DoSet's nil-value path so it also drops a `unit` mouseover binding, but does
// NOT fire OnAttributeChanged — verified against retail (a live 12.0 test showed
// ClearAttribute firing no handler, unlike SetAttribute).
int __fastcall Script_ClearAttribute(void *L) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:ClearAttribute(\"name\")");
        return 0;
    }
    char lname[256];
    LowerCopy(lname, Game::Lua::ToString(L, 2), sizeof lname);

    const bool existed = TryPushValue(L, 1, lname); // leaves the value on top if set
    if (existed)
        Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1); // drop the peeked value

    Game::Lua::SetTop(L, 2);   // (self, name)
    Game::Lua::PushNil(L);     // (self, name, nil) → value = nil clears the key
    DoSet(L, /*fireHandler*/ false); // retail ClearAttribute fires no OnAttributeChanged

    Game::Lua::PushBool(L, existed);
    return 1;
}

int __fastcall Script_GetAttribute(void *L) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:GetAttribute(\"name\")");
        return 0;
    }
    // 3-arg form: (prefix, name, suffix) with wildcard precedence.
    if (Game::Lua::GetTop(L) >= 4 && Game::Lua::IsString(L, 3)) {
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

    char lname[256];
    LowerCopy(lname, Game::Lua::ToString(L, 2), sizeof lname);
    if (TryPushValue(L, 1, lname))
        return 1;
    Game::Lua::PushNil(L);
    return 1;
}

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

} // namespace Frame::Attributes
