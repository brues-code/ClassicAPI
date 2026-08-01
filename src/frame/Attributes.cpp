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
// wildcard precedence). We store the map on the frame's own Lua table under a
// private key, so it's collected with the frame.
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
// ground-target spells land at the unit's feet), `macro` (from the
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
#include "cursor/Info.h"
#include "spell/AtCursor.h"
#include "spell/AtUnit.h"
#include "tick/WorldTick.h"
#include "unit/Focus.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace Frame::Attributes {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);

int CallScript(uintptr_t fn, void *L) {
    return reinterpret_cast<ScriptFn_t>(fn)(L);
}

// Private key on the frame's own Lua table for the attribute subtable. The
// leading control byte can't be produced by a lowercased user attribute name,
// so it never collides with a real attribute.
constexpr const char kAttrKey[] = "\1ClassicAPIAttributes";

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

// ASCII case-insensitive full-string equality.
bool EqI(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b)))
            return false;
    return *a == *b;
}

// ---- attribute storage (on the frame's own Lua table) ----------------------

// Pushes frame[kAttrKey] (the attribute subtable) onto the stack. With
// create=true, lazily creates and stores it. Returns true iff a subtable is
// left on top; false (nothing left on the stack) when absent and !create.
bool PushSub(void *L, int frameIdx, bool create) {
    Game::Lua::PushValue(L, frameIdx);      // [.., frame]
    const int f = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, kAttrKey);      // [.., frame, key]
    Game::Lua::RawGet(L, f);                 // [.., frame, sub|nil]
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        Game::Lua::Remove(L, f);             // [.., sub]
        return true;
    }
    Game::Lua::SetTop(L, f);                 // [.., frame]   (drop the nil)
    if (!create) {
        Game::Lua::SetTop(L, f - 1);         // [..]          (drop frame)
        return false;
    }
    Game::Lua::NewTable(L);                   // [.., frame, sub]
    Game::Lua::PushString(L, kAttrKey);       // [.., frame, sub, key]
    Game::Lua::PushValue(L, f + 1);           // [.., frame, sub, key, sub]
    Game::Lua::RawSet(L, f);                  // frame[key] = sub -> [.., frame, sub]
    Game::Lua::Remove(L, f);                  // [.., sub]
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

// If frame[kAttrKey][keyLower] is non-nil, leave that value on the stack top
// and return true; otherwise restore the stack and return false.
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

// ---- engine primitives -----------------------------------------------------

// Resolve a unit token/GUID to a GUID via the hooked resolver (handles
// player/target/party/raid plus our focus / nameplateN / raw-GUID extensions).
// For recognized tokens this never raises; unit attributes are tokens by
// contract.
uint64_t ResolveToken(const char *token) {
    return reinterpret_cast<uint64_t(__fastcall *)(const char *)>(
        static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID))(token);
}

// ---- Lua-global call helpers (used by the click dispatcher) -----------------
//
// The click dispatch mirrors retail's SecureActionButton_OnClick: it's C. It
// performs engine actions by calling ordinary Lua globals (TargetUnit,
// IsAltKeyDown, …) and our own actions by calling the C++ module directly
// (Spell::AtUnit, Unit::Focus). Both run under the engine's protected OnClick
// invocation, so a Lua error inside one is caught there (no crash).

// Pushes _G[name]; leaves it on the stack and returns true only if it's a
// function (otherwise pops it and returns false).
bool PushGlobalFunc(void *L, const char *name) {
    Game::Lua::PushString(L, name);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION)
        return true;
    Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1);
    return false;
}

// _G[name]() -> boolean (false if the global isn't a function).
bool CallBoolGlobal(void *L, const char *name) {
    const int top = Game::Lua::GetTop(L);
    bool r = false;
    if (PushGlobalFunc(L, name)) {
        Game::Lua::Call(L, 0, 1);
        r = Game::Lua::ToBoolean(L, -1) != 0;
    }
    Game::Lua::SetTop(L, top);
    return r;
}

// _G[name]() — no args, no results.
void CallGlobal(void *L, const char *name) {
    const int top = Game::Lua::GetTop(L);
    if (PushGlobalFunc(L, name))
        Game::Lua::Call(L, 0, 0);
    Game::Lua::SetTop(L, top);
}

// _G[name](arg) — one string arg, no results. Returns true iff the global was a
// function and got called (false when it's absent), so callers can fall back.
bool CallGlobalStr(void *L, const char *name, const char *arg) {
    const int top = Game::Lua::GetTop(L);
    const bool called = PushGlobalFunc(L, name);
    if (called) {
        Game::Lua::PushString(L, arg);
        Game::Lua::Call(L, 1, 0);
    }
    Game::Lua::SetTop(L, top);
    return called;
}

// ---- macrotext execution (stock ChatEdit_ParseText, no addon dependency) ----
//
// 1.12 has no `RunMacroText`/`RunMacro` global — those are addon shims (pfUI
// wraps `ChatEdit_ParseText`; SuperCleveRoidMacros ships its own `RunMacro`).
// To run macro text without depending on an addon, we replicate pfUI's shim in
// C: build a throwaway "edit box" whose `GetText` returns the line and whose
// every other method is a harmless no-op (via an `__index` metamethod), then
// hand it to the stock FrameXML `ChatEdit_ParseText(editBox, 1)` — the same
// path the real chat box uses to dispatch a slash command / send a line.

// GetText: returns upvalue(1), the captured line.
int __fastcall MacroGetText_c(void *L) {
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(1));
    return 1;
}

// A no-op standing in for any edit-box method the parser happens to call.
int __fastcall MacroNoop_c(void *) { return 0; }

// __index(tab, key): hand back the no-op so `editBox:AnyMethod()` is safe.
int __fastcall MacroIndex_c(void *L) {
    Game::Lua::PushCClosure(L, &MacroNoop_c, 0);
    return 1;
}

// Runs a single macro line through the stock chat parser.
void RunMacroLineC(void *L, const char *line, size_t len) {
    if (len == 0) return;
    const int top = Game::Lua::GetTop(L);

    Game::Lua::NewTable(L);                    // fake editBox
    const int obj = Game::Lua::GetTop(L);

    Game::Lua::PushString(L, "GetText");       // obj.GetText = closure over line
    Game::Lua::PushLString(L, line, static_cast<unsigned int>(len));
    Game::Lua::PushCClosure(L, &MacroGetText_c, 1);
    Game::Lua::SetTable(L, obj);

    Game::Lua::NewTable(L);                     // metatable { __index = noop }
    const int mt = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "__index");
    Game::Lua::PushCClosure(L, &MacroIndex_c, 0);
    Game::Lua::SetTable(L, mt);

    // No lua_setmetatable binding — use the Lua global.
    if (PushGlobalFunc(L, "setmetatable")) {
        Game::Lua::PushValue(L, obj);
        Game::Lua::PushValue(L, mt);
        Game::Lua::Call(L, 2, 0);
    }

    if (PushGlobalFunc(L, "ChatEdit_ParseText")) {
        Game::Lua::PushValue(L, obj);
        Game::Lua::PushNumber(L, 1);           // send = 1
        Game::Lua::Call(L, 2, 0);
    }

    Game::Lua::SetTop(L, top);
}

// Runs macro text one line at a time — a real macro is line-delimited, and a
// single ChatEdit_ParseText call only dispatches one command.
void RunMacroTextC(void *L, const char *text) {
    if (!text) return;
    for (const char *p = text; *p;) {
        const char *nl = p;
        while (*nl && *nl != '\n') ++nl;
        RunMacroLineC(L, p, static_cast<size_t>(nl - p));
        p = (*nl == '\n') ? nl + 1 : nl;
    }
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

void MouseoverTick() {
    const void *focus = CurrentMouseFocus();

    uint64_t guid = 0;
    if (focus != nullptr) {
        auto it = g_unitByFrame.find(focus);
        if (it != g_unitByFrame.end())
            guid = ResolveToken(it->second.c_str()); // live — follows target changes
    }

    if (guid != g_lastSet) {
        CallEngineMouseover(guid); // sets slot + highlight + tooltip + event
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
    if (EqI(btn, "RightButton"))  return "2";
    if (EqI(btn, "MiddleButton")) return "3";
    if (EqI(btn, "Button4"))      return "4";
    if (EqI(btn, "Button5"))      return "5";
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

// Performs the resolved `verb` on `unit` (a token attribute value, may be null).
// Returns true if it owned the click (so the chained handler is skipped).
bool DispatchVerb(void *L, int fi, const char *prefix, const char *suffix,
                  const char *verb, const char *unit) {
    if (EqI(verb, "target")) {
        if (!unit) return false;
        // Cursor / pending-spell take precedence, matching the engine's default
        // unit interaction — cast the pending spell / drop the item on the unit
        // instead of switching target. The two predicates are ours (direct C++);
        // the actions are engine-only, so they go through the Lua globals (which
        // are the engine's own entries, with token→GUID resolution).
        if (Spell::AtCursor::IsPlacementActive())
            CallGlobalStr(L, "SpellTargetUnit", unit);
        else if (Cursor::Info::HasItem())
            CallGlobalStr(L, "DropItemOnUnit", unit);
        else
            CallGlobalStr(L, "TargetUnit", unit);
        return true;
    }
    if (EqI(verb, "assist")) {
        if (!unit) return false;
        CallGlobalStr(L, "AssistUnit", unit);
        return true;
    }
    if (EqI(verb, "focus")) {
        if (!unit) return false;
        Unit::Focus::Set(ResolveToken(unit));
        return true;
    }
    if (EqI(verb, "spell")) {
        if (!unit) return false;
        char spell[128];
        if (!ReadModAttr(L, fi, prefix, "spell", suffix, spell, sizeof spell))
            return false;
        Spell::AtUnit::CastByName(spell, unit);
        return true;
    }
    if (EqI(verb, "macro")) {
        char macro[512];
        if (!ReadModAttr(L, fi, prefix, "macrotext", suffix, macro, sizeof macro) &&
            !ReadModAttr(L, fi, prefix, "macro", suffix, macro, sizeof macro))
            return false;
        // Prefer an addon-provided RunMacro (SuperCleveRoidMacros, pfUI, …) — it
        // handles named macros and extended macro text; fall back to the stock
        // ChatEdit_ParseText path when no RunMacro global is present.
        if (!CallGlobalStr(L, "RunMacro", macro))
            RunMacroTextC(L, macro);
        return true;
    }
    if (EqI(verb, "stop") || EqI(verb, "stopcasting")) {
        CallGlobal(L, "SpellStopCasting");
        return true;
    }
    if (EqI(verb, "menu") || EqI(verb, "togglemenu")) {
        if (!unit) return false;
        // The unit dropdown is pure FrameXML work (UnitPopup + ToggleDropDown),
        // so it lives in the !!!ClassicAPI addon; we just pop it at the cursor.
        CallGlobalStr(L, "ClassicAPI_ToggleUnitMenu", unit);
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
    if (EqI(name, "onattributechanged"))
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
    Game::Lua::PushString(L, "arg1");
    Game::Lua::PushString(L, name);
    Game::Lua::RawSet(L, Game::Lua::GLOBALS_INDEX);           // _G.arg1 = name
    Game::Lua::PushString(L, "arg2");
    if (savedTop >= 3)
        Game::Lua::PushValue(L, 3);
    else
        Game::Lua::PushNil(L);
    Game::Lua::RawSet(L, Game::Lua::GLOBALS_INDEX);           // _G.arg2 = value

    ++g_firingAttr;
    reinterpret_cast<InvokeAttrScript_t>(Offsets::FUN_FRAME_INVOKE_SCRIPT)(slot[0], frame);
    --g_firingAttr;
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
    // reads all the attributes fresh at click time.
    else if ((std::strcmp(lname, "type1") == 0 || std::strcmp(lname, "type2") == 0 ||
              std::strcmp(lname, "type") == 0) &&
             isString) {
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
