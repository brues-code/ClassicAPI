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

// `frame:RegisterUnitEvent(event, ...units)` — register for an event but only
// receive it for the given unit tokens — plus the unit-aware
// `frame:IsEventRegistered(event)` (registered [, unit…]).
//
// Vanilla owns everything except the filter, so the filter is all this adds:
//
//   - Chain membership stays the engine's. RegisterUnitEvent delegates to the
//     engine's own `Script_RegisterEvent` (stack cut to (self, event)), so the
//     frame lands on the event's subscriber chain through the same code every
//     RegisterEvent uses — self typecheck, error text, and DllMain's
//     RegisterEvent hook included.
//   - The filter is stored here, keyed by frame, and dropped exactly where the
//     engine drops the frame's subscriber node: co-hooks on the C++ helpers
//     `Frame::UnregisterEvent` (FUN_FRAME_UNREGISTER_EVENT) and
//     `Frame::UnregisterAllEvents` (FUN_FRAME_UNREGISTER_ALL_EVENTS — also the
//     CScriptObject destructor's path, so frame destruction is covered).
//   - The check is one gate on the per-frame OnEvent fire. The dispatcher
//     FUN_FIRE_EVENT sets `_G.event`, walks the chain, and per frame calls the
//     runner FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT(frame, frame+OFF_FRAME_ONEVENT_SLOT,
//     fmt, va). We subscribe to that runner through `Frame::RunnerHook` (it is
//     shared with Frame::ClickEvents — one MinHook per address), and when the
//     frame has a filter for the event being dispatched and arg1 matches none
//     of its tokens, we return without invoking the handler. Suppressing at the
//     runner also suppresses HookScript chains and Frame::ScriptArgs' positional
//     path — they all live downstream of it, which is what retail does too.
//
// Semantics mirror Blizzard's 5.4.8 implementation (Ghidra-verified), where the
// filter lives ON the subscriber node and the node's kind is fixed when it is
// created: RegisterEvent on a filtered registration leaves the filter;
// RegisterUnitEvent on a plain registration stays plain; RegisterUnitEvent on a
// filtered registration replaces the units; RegisterUnitEvent with no usable
// unit is a plain RegisterEvent. Switching kinds takes UnregisterEvent first.
//
// Two places we deliberately don't mirror 5.4.8, both additive:
//
//   - UNIT COUNT. 5.4.8 reads exactly two units (a hardcoded two-iteration loop
//     over Lua indices 3 and 4) and folds each token's table index into a byte
//     of one dword on the node — `filter = filter * 0x100 + idx`. Retail later
//     filled that dword and documents "up to four units", which its 5.4.8
//     reader already anticipated by walking all four bytes generically. We
//     store strings in our own map, so there is no dword to fill and no reason
//     to pick a number: every argument past the event name that is a string is
//     taken as a unit. Retail's four are covered by definition.
//   - TOKEN VALIDATION. 5.4.8 looks each unit up in a static canonical token
//     table (case-sensitive) and drops unknowns; we keep the string and compare
//     case-insensitively, because ClassicAPI's own tokens (`focus`,
//     `nameplateN`, `markN`) are not in the engine's table.
//
// Filter rule: the filter applies only when the event's first argument is a
// string (every UNIT_* event fires as "%s" + token). A first spec that isn't
// `%s`, or a no-arg event (those go through FUN_FIRE_EVENT_NO_ARGS → a runner
// we never see), fires as with RegisterEvent — one rule that holds on every
// dispatch path, whose failure mode is an extra event rather than a lost one.

#include "Game.h"
#include "Offsets.h"
#include "frame/RunnerHook.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frame::UnitEvent {

namespace {

using ScriptFn_t = int(__fastcall *)(void *L);
// Storm SStrCmpI — `__stdcall`, RET 0xC (see Offsets.h; a __cdecl declaration
// drifts ESP by 12 per call).
using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);

int CallScript(uintptr_t fn, void *L) { return reinterpret_cast<ScriptFn_t>(fn)(L); }

bool EqualsIgnoreCase(const char *a, const char *b) {
    if (a == nullptr || b == nullptr)
        return false;
    auto cmp = reinterpret_cast<SStrCmpI_t>(static_cast<uintptr_t>(Offsets::FUN_SSTR_CMP_I));
    return cmp(a, b, 0x7FFFFFFF) == 0;
}

// One filtered registration — the unit filter 5.4.8 keeps on the event's
// subscriber node for this frame. `event` is the CANONICAL name copied from the
// event-table entry, so the fire-time compare against the dispatcher-set
// `_G.event` (which is that same entry name) is a plain strcmp.
struct Filter {
    std::string event;
    std::vector<std::string> units; // never empty; a filter with no unit is no filter
};

// Keyed by frame. Entries die with the frame's subscriber node (the two
// unregister co-hooks) and on /reload (PrepareForReload).
std::unordered_map<const void *, std::vector<Filter>> g_filters;

// ---- event table -----------------------------------------------------------

// The live event-table entry whose name matches `eventName` (case-insensitive,
// as Frame::RegisterEvent matches), or nullptr for an unknown event.
const uint8_t *FindEventEntry(const char *eventName) {
    if (eventName == nullptr)
        return nullptr;
    const uint32_t count = Game::Read<uint32_t>(
        static_cast<uintptr_t>(Offsets::VAR_EVENT_TABLE_COUNT));
    auto *base = Game::Read<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_EVENT_TABLE_BASE_PTR));
    if (count == 0 || base == nullptr)
        return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = base + i * Offsets::EVENT_ENTRY_STRIDE;
        const char *name = Game::Read<const char *>(entry, Offsets::OFF_EVENT_ENTRY_NAME);
        if (name != nullptr && EqualsIgnoreCase(name, eventName))
            return entry; // names are unique
    }
    return nullptr;
}

// Is `frame` on `entry`'s subscriber chain — the membership walk
// Frame::RegisterEvent performs before appending.
bool ChainHas(const uint8_t *entry, const void *frame) {
    uintptr_t node = Game::Read<uintptr_t>(entry, Offsets::OFF_EVENT_ENTRY_HEAD);
    while ((node & 1) == 0 && node != 0) {
        if (Game::Read<const void *>(node + Offsets::OFF_EVENT_NODE_FRAME) == frame)
            return true;
        node = Game::Read<uintptr_t>(node + Offsets::OFF_EVENT_NODE_NEXT);
    }
    return false;
}

// ---- filter map --------------------------------------------------------------

Filter *FindFilter(const void *frame, const char *eventName) {
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return nullptr;
    for (Filter &f : it->second)
        if (EqualsIgnoreCase(f.event.c_str(), eventName))
            return &f;
    return nullptr;
}

void EraseFilter(const void *frame, const char *eventName) {
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return;
    std::vector<Filter> &v = it->second;
    for (size_t i = 0; i < v.size(); ++i) {
        if (EqualsIgnoreCase(v[i].event.c_str(), eventName)) {
            v.erase(v.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    if (v.empty())
        g_filters.erase(it);
}

// ---- Lua methods --------------------------------------------------------------

// `frame:RegisterUnitEvent(event, ...units)` → registered.
int __fastcall Script_RegisterUnitEvent(void *L) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:RegisterUnitEvent(\"event\" [, \"unit1\", ...])");
        return 0;
    }
    void *frame = Game::Lua::ResolveObject(L, 1);
    if (frame == nullptr) {
        // Not a frame — hand it to the engine so it raises its own error, so
        // the message stays the engine's rather than a copy of it.
        Game::Lua::SetTop(L, 2);
        return CallScript(Offsets::FUN_SCRIPT_FRAME_REGISTEREVENT, L);
    }

    // Read everything BEFORE reshaping the stack for the delegate — including
    // the top, which the reshape destroys. Like 5.4.8 we take only real strings
    // as units (a number is not coerced) and silently skip anything else.
    const std::string eventName = Game::Lua::ToString(L, 2);
    const int argTop = Game::Lua::GetTop(L);
    std::vector<std::string> units;
    for (int idx = 3; idx <= argTop; ++idx) {
        if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
            continue;
        const char *s = Game::Lua::ToString(L, idx);
        if (s != nullptr && *s != '\0')
            units.push_back(s);
    }
    const uint8_t *entry = FindEventEntry(eventName.c_str());
    const bool wasRegistered = entry != nullptr && ChainHas(entry, frame);

    // Chain membership is the engine's: (self, event) → Script_RegisterEvent.
    Game::Lua::SetTop(L, 2);
    CallScript(Offsets::FUN_SCRIPT_FRAME_REGISTEREVENT, L);

    // 5.4.8 node semantics (see the header). No usable unit → this was a plain
    // RegisterEvent and an existing filter is left alone; an unknown event has
    // no node to attach a filter to.
    if (entry != nullptr && !units.empty()) {
        if (Filter *f = FindFilter(frame, eventName.c_str())) {
            f->units = units; // filtered → replace the units
        } else if (!wasRegistered) {
            Filter fresh;
            fresh.event = Game::Read<const char *>(entry, Offsets::OFF_EVENT_ENTRY_NAME);
            fresh.units = units;
            g_filters[frame].push_back(fresh);
        }
        // else: already registered plainly → stays plain
    }

    Game::Lua::PushBool(L, entry != nullptr && ChainHas(entry, frame));
    return 1;
}

// `frame:IsEventRegistered(event)` → registered [, unit…]. Vanilla has
// RegisterEvent / UnregisterEvent but not the query; the membership check is
// the same chain walk Frame::RegisterEvent performs, so the answer is exactly
// what the dispatcher sees. Units come back in registration order.
int __fastcall Script_IsEventRegistered(void *L) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:IsEventRegistered(\"event\")");
        return 0;
    }
    void *frame = Game::Lua::ResolveObject(L, 1);
    const char *eventName = Game::Lua::ToString(L, 2);
    const uint8_t *entry = (frame != nullptr) ? FindEventEntry(eventName) : nullptr;
    const bool registered = entry != nullptr && ChainHas(entry, frame);
    Game::Lua::PushBool(L, registered);
    if (!registered)
        return 1;
    const Filter *f = FindFilter(frame, eventName);
    if (f == nullptr)
        return 1;
    if (Game::Lua::CheckStack(L, static_cast<int>(f->units.size()) + 1) == 0)
        return 1; // can't grow the stack — report registered, drop the units
    int pushed = 1;
    for (const std::string &u : f->units) {
        Game::Lua::PushString(L, u.c_str());
        ++pushed;
    }
    return pushed;
}

// ---- the gate ------------------------------------------------------------------

// True (and `*out` = arg1) when the runner's first conversion spec is `%s` —
// the unit token of every unit event. Anything else means arg1 isn't a string
// and the filter does not apply.
bool FirstArgIsString(const char *fmt, const void *varargs, const char **out) {
    if (fmt == nullptr || varargs == nullptr)
        return false;
    const char *p = std::strchr(fmt, '%');
    if (p == nullptr || p[1] != 's')
        return false;
    *out = *static_cast<const char *const *>(varargs);
    return true;
}

// Frame::RunnerHook interceptor. Returns true ONLY to suppress a fire (the
// engine runner is then never called for it); every other path returns false
// and the fire proceeds untouched. Touches no Lua code: `_G.event` is read with
// RawGet so an `__index` metatable on _G can never run script here, and no
// reference into g_filters outlives the call.
bool OnRun(void *frame, uint32_t *slotPtr, const char *fmt, void *varargs) {
    // Only the dispatcher's OnEvent fire passes the OnEvent slot.
    if (frame == nullptr ||
        slotPtr != reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(frame) +
                                                Offsets::OFF_FRAME_ONEVENT_SLOT))
        return false;
    // Frames without filters pay one probe and leave.
    if (g_filters.empty())
        return false;
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return false;

    // Which event? The dispatcher has just set `_G.event` to the entry's name.
    void *L = Game::Lua::State();
    if (L == nullptr)
        return false;
    Game::Lua::PushString(L, "event");
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    const char *ev = Game::Lua::ToString(L, -1);
    const Filter *match = nullptr;
    if (ev != nullptr) {
        for (const Filter &f : it->second) {
            if (std::strcmp(f.event.c_str(), ev) == 0) {
                match = &f;
                break;
            }
        }
    }
    Game::Lua::SetTop(L, -2);
    if (match == nullptr)
        return false;

    // The filter applies only when arg1 is a string.
    const char *arg1 = nullptr;
    if (!FirstArgIsString(fmt, varargs, &arg1))
        return false;
    if (arg1 != nullptr) {
        for (const std::string &u : match->units)
            if (EqualsIgnoreCase(arg1, u.c_str()))
                return false;
    }
    return true; // no token matched — suppress
}

// ---- unregister co-hooks (the filter dies with the node) ------------------------

// `__thiscall(frame, eventName)` mimicked as `__fastcall(ecx, edx_unused, …)`.
using UnregisterEvent_t = void(__fastcall *)(void *frame, void *edx, const char *eventName);
UnregisterEvent_t g_origUnregisterEvent = nullptr;

void __fastcall UnregisterEvent_h(void *frame, void *edx, const char *eventName) {
    if (frame != nullptr && !g_filters.empty())
        EraseFilter(frame, eventName);
    g_origUnregisterEvent(frame, edx, eventName);
}

// `__fastcall(frame)`; the Lua wrapper has dead `(0)` calls after lua_error, so
// tolerate a null frame.
using UnregisterAll_t = void(__fastcall *)(void *frame, void *edx);
UnregisterAll_t g_origUnregisterAll = nullptr;

void __fastcall UnregisterAllEvents_h(void *frame, void *edx) {
    if (frame != nullptr && !g_filters.empty())
        g_filters.erase(frame);
    g_origUnregisterAll(frame, edx);
}

// ---- registration -----------------------------------------------------------------

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"RegisterUnitEvent", &Script_RegisterUnitEvent},
    {"IsEventRegistered", &Script_IsEventRegistered},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        static_cast<int>(sizeof(g_frameMethods) / sizeof(g_frameMethods[0])));
}

// Frames survive /reload but the engine rebuilds the event table with empty
// chains and every frame re-registers from scratch, so the filters start over
// too — and a recycled frame pointer can never inherit a stale one.
void PrepareForReload() { g_filters.clear(); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
const Frame::RunnerHook::AutoSubscribe _runnerSub{&OnRun};

const Game::HookAutoRegister _hookUnregister{
    Offsets::FUN_FRAME_UNREGISTER_EVENT, reinterpret_cast<void *>(&UnregisterEvent_h),
    reinterpret_cast<void **>(&g_origUnregisterEvent)};
const Game::HookAutoRegister _hookUnregisterAll{
    Offsets::FUN_FRAME_UNREGISTER_ALL_EVENTS,
    reinterpret_cast<void *>(&UnregisterAllEvents_h),
    reinterpret_cast<void **>(&g_origUnregisterAll)};

} // namespace

} // namespace Frame::UnitEvent
