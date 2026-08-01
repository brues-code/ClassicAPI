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
// `frame:GetAttribute` to 1.12 as native methods on the base Frame registry
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
// is first set we install a chained OnClick on that frame (protected, because
// OnClick is Button-only); it reads `unit` + `type1`/`type2`/`type` at click
// time and performs the action, then runs any previously-set handler. Verbs:
// `target`, `assist` (target the unit's target), `focus`. No global hook, no
// per-frame handler on frames that never asked for one.
//
// Ordering: because it chains the handler present when `type*` is set, set the
// `type` attribute AFTER the frame's own `OnClick` (real addons configure
// attributes after building the widget). The frame must be a Button registered
// for the relevant clicks (`RegisterForClicks`) — left is the Button default;
// right needs `RegisterForClicks("RightButtonUp")`, which real unit frames do.
//
// NOT DONE: `OnAttributeChanged` as a real SetScript-able handler — needs a
// base-frame script-name resolver co-hook (the `Tooltip::SetEvents` analog).

#include "Game.h"
#include "Offsets.h"
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

// Private keys on the frame's own Lua table. The leading control byte can't be
// produced by a lowercased user attribute name, so these never collide with a
// real attribute: kAttrKey holds the attribute subtable, kClickWiredKey latches
// the one-time OnClick install.
constexpr const char kAttrKey[] = "\1ClassicAPIAttributes";
constexpr const char kClickWiredKey[] = "\1ClassicAPIClickWired";

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

// Set the player's target to `guid` (the engine's own target-by-GUID setter).
void TargetGuid(uint64_t guid) {
    if (guid == 0)
        return;
    reinterpret_cast<void(__fastcall *)(uint64_t *)>(
        static_cast<uintptr_t>(Offsets::FUN_TARGET_BY_GUID))(&guid);
}

// The GUID a unit is currently targeting (its UNIT_FIELD_TARGET), or 0.
uint64_t UnitTargetGuid(uint64_t guid) {
    if (guid == 0)
        return 0;
    auto resolve = reinterpret_cast<void *(__fastcall *)(int, const char *, uint32_t,
                                                         uint32_t, int)>(
        static_cast<uintptr_t>(Offsets::FUN_OBJECT_RESOLVE_BY_GUID));
    auto *obj = static_cast<uint8_t *>(
        resolve(Offsets::OBJ_TYPE_UNIT, "ClassicAPI", static_cast<uint32_t>(guid),
                static_cast<uint32_t>(guid >> 32), 0x6e));
    if (obj == nullptr)
        return 0;
    auto *fields = *reinterpret_cast<uint8_t *const *>(
        obj + Offsets::OFF_CGUNIT_OBJECT_FIELDS);
    if (fields == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(fields + Offsets::OFF_UNIT_FIELD_TARGET);
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

// The chained OnClick handler. Upvalues: 1 = previous handler (or nil), 2 = the
// frame (captured at install time, so we don't depend on the `this` global).
int __fastcall OnClick_c(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(2)); // frame
    const int fi = Game::Lua::GetTop(L);
    if (Game::Lua::Type(L, fi) == Game::Lua::TYPE_TABLE) {
        char unit[128];
        if (CopyAttr(L, fi, "unit", unit, sizeof unit)) {
            // Which button — from the OnClick `arg1` global.
            char btn[32] = {0};
            Game::Lua::PushString(L, "arg1");
            Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
            if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING) {
                const char *b = Game::Lua::ToString(L, -1);
                if (b)
                    BoundedCopy(btn, b, sizeof btn);
            }
            Game::Lua::SetTop(L, fi); // drop arg1, keep frame

            const char *tkey = EqI(btn, "RightButton") ? "type2"
                               : EqI(btn, "LeftButton") ? "type1"
                                                        : "type";
            char verb[32] = {0};
            if (CopyAttr(L, fi, tkey, verb, sizeof verb) ||
                CopyAttr(L, fi, "type", verb, sizeof verb)) {
                const uint64_t guid = ResolveToken(unit);
                if (guid != 0) {
                    if (EqI(verb, "target"))
                        TargetGuid(guid);
                    else if (EqI(verb, "assist"))
                        TargetGuid(UnitTargetGuid(guid));
                    else if (EqI(verb, "focus"))
                        Unit::Focus::Set(guid);
                    // togglemenu / spell / macro not backported yet.
                }
            }
        }
    }
    Game::Lua::SetTop(L, top);
    ChainOld(L);
    return 0;
}

// Installs OnClick as a closure over (previous handler, frame). Operates on
// self at stack index 1. Raises if `self` isn't a Button (OnClick is a Button
// script) — callers run it under PCall.
int __fastcall WireOnClick(void *L) {
    Game::Lua::SetTop(L, 1);                             // (self)
    Game::Lua::PushString(L, "OnClick");                 // (self, name)
    CallScript(Offsets::FUN_SCRIPT_FRAME_GETSCRIPT, L);  // (self, name, old)
    if (Game::Lua::GetTop(L) < 3)                        // defensive
        Game::Lua::PushNil(L);
    Game::Lua::SetTop(L, 3);                             // (self, name, old|nil)
    Game::Lua::PushValue(L, 1);                          // (self, name, old, self)
    Game::Lua::PushCClosure(L, &OnClick_c, 2);           // upvalues (old, self)
    CallScript(Offsets::FUN_SCRIPT_FRAME_SETSCRIPT, L);
    return 0;
}

// Returns true if the frame's OnClick was already wired by us; otherwise
// latches the flag and returns false.
bool ClickAlreadyWired(void *L, int frameIdx) {
    const int top = Game::Lua::GetTop(L);
    bool already = false;
    if (PushSub(L, frameIdx, true)) {        // [.., sub]
        const int si = Game::Lua::GetTop(L);
        Game::Lua::PushString(L, kClickWiredKey);
        Game::Lua::RawGet(L, si);            // [.., sub, flag]
        already = Game::Lua::ToBoolean(L, -1) != 0;
        Game::Lua::SetTop(L, si);            // [.., sub]
        if (!already) {
            Game::Lua::PushString(L, kClickWiredKey);
            Game::Lua::PushBoolean(L, 1);
            Game::Lua::RawSet(L, si);        // sub[wired] = true
        }
    }
    Game::Lua::SetTop(L, top);
    return already;
}

// ---- the methods -----------------------------------------------------------

// Enables the frame's mouse so it can become the mouse-focus (a bare frame
// otherwise never registers as hovered). Operates on self at index 1.
void EnableFrameMouse(void *L) {
    Game::Lua::SetTop(L, 1); // (self)
    Game::Lua::PushBoolean(L, 1);
    CallScript(Offsets::FUN_SCRIPT_FRAME_ENABLEMOUSE, L);
}

int DoSet(void *L) { // (self, name, value)
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
    // A `type*` attribute makes the frame a clickable unit button: install a
    // chained OnClick once (PCall-guarded — OnClick is Button-only). The
    // handler reads the attributes fresh at click time, so setting type1 then
    // type2 needs no re-wire.
    else if ((std::strcmp(lname, "type1") == 0 || std::strcmp(lname, "type2") == 0 ||
              std::strcmp(lname, "type") == 0) &&
             isString && !ClickAlreadyWired(L, 1)) {
        Game::Lua::PushCClosure(L, &WireOnClick, 0);
        Game::Lua::PushValue(L, 1); // arg = self
        Game::Lua::PCall(L, 1, 0, 0); // ignore errors (non-Button frame)
    }
    return 0;
}

int __fastcall Script_SetAttribute(void *L) { return DoSet(L); }
int __fastcall Script_SetAttributeNoHandler(void *L) { return DoSet(L); }

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
