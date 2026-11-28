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

// Event callbacks, as the 3.80.2 client keeps them (one CallbackEventRegistry
// per callback event, FUN_140ae8960 and its seven siblings):
//   - Only the eight callback events take callbacks (FUN_1405c3f10, a fixed
//     list, matched without regard to case). Any other name raises
//     "<Function> Attempt to register unknown event "<name>"" with the
//     binary's own wording (Unregister's says "register" too).
//   - A callback is a Lua function, which gets a container of its own, or a
//     C_FunctionContainers container (Event::FunctionContainer). Anything else
//     raises "invalid callback" at its argument. Two registrations match only
//     when they hold the same container (comparator 0x140af8ec0), so a plain
//     function registered twice runs twice, and UnregisterEventCallback with a
//     plain function removes nothing (it still returns true).
//   - A callback gets its owner and the event's values, never the event name:
//     nil for a global registration, the frame's table for a frame one. It
//     runs protected under the error handler. Frame OnEvent handlers run
//     first, then the global callbacks, then the frame ones.
//   - A registration or removal made while the event fires is queued and
//     applied when that fire ends (FUN_140aeec20 / FUN_140d409b0).
//   - The unit forms take exactly one unit token, and only MINIMAP_PING takes
//     them. For a fire with a unit, the plain callbacks run, then the ones for
//     that unit.
//   - Frames have RegisterEventCallback and RegisterUnitEventCallback (true
//     unless refused) and no unregister method of their own: UnregisterEvent
//     and UnregisterAllEvents remove the frame's callbacks too. A hidden frame
//     still gets them. A frame that is destroyed loses them here (3.80.2 keeps
//     the entry; this client would call through a freed frame).
//   - Every callback is dropped when the Lua state goes (a /reload).
// Not here: the taint report 3.80.2 appends to its errors, and its gate that
// refuses some of these in restricted contexts (ADDON_ACTION_FORBIDDEN); this
// client has neither.

#include "event/Callback.h"

#include "baselib/Ascii.h"
#include "Game.h"
#include "Offsets.h"
#include "event/FunctionContainer.h"
#include "frame/Destruction.h"
#include "frame/UnitEvent.h"
#include "profile/AddOnProfiler.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Event::Callback {

namespace {

using Event::FunctionContainer::Container;

constexpr const char *kNames[ID_COUNT] = {
    "MINIMAP_PING",
    "COMBAT_LOG_APPLY_FILTER_SETTINGS",
    "ENCOUNTER_STATE_CHANGED",
    "TOOLTIP_SHOW_ITEM_COMPARISON",
    "COMBAT_LOG_EVENT",
    "COMBAT_LOG_EVENT_UNFILTERED",
    "COMBAT_LOG_EVENT_INTERNAL_UNFILTERED",
    "COMBAT_LOG_REFILTER_ENTRIES",
};

int IdForName(const char *name) {
    if (name == nullptr)
        return -1;
    for (int i = 0; i < ID_COUNT; ++i)
        if (Ascii::EqualCI(name, kNames[i]))
            return i;
    return -1;
}

struct Entry {
    Container *c;
    void *frame;      // nullptr for a global registration
    std::string unit; // empty for a plain registration
    bool dead = false;

    bool Same(const Entry &o) const { return c == o.c && frame == o.frame && unit == o.unit; }
};

struct Op {
    bool add;
    Entry entry; // holds one reference on its container
};

struct Registry {
    std::vector<Entry> entries; // each holds one reference on its container
    std::vector<Op> pending;
    int depth = 0;
};

Registry g_registries[ID_COUNT];

void Insert(Registry &r, Entry &&e) {
    for (const Entry &x : r.entries)
        if (x.Same(e)) {
            Event::FunctionContainer::Release(e.c);
            return;
        }
    r.entries.push_back(std::move(e));
}

void Erase(Registry &r, const Entry &e) {
    for (auto it = r.entries.begin(); it != r.entries.end(); ++it)
        if (it->Same(e)) {
            Event::FunctionContainer::Release(it->c);
            r.entries.erase(it);
            return;
        }
}

void Add(Id id, Container *c, void *frame, const char *unit) {
    Registry &r = g_registries[id];
    Event::FunctionContainer::Retain(c);
    Entry e{c, frame, unit != nullptr ? unit : ""};
    if (r.depth > 0)
        r.pending.push_back(Op{true, std::move(e)});
    else
        Insert(r, std::move(e));
}

void Remove(Id id, const Entry &e) {
    Registry &r = g_registries[id];
    if (r.depth > 0) {
        Event::FunctionContainer::Retain(e.c);
        r.pending.push_back(Op{false, e});
    } else {
        Erase(r, e);
    }
}

void ApplyPending(Registry &r) {
    std::vector<Op> ops = std::move(r.pending);
    r.pending.clear();
    for (Op &op : ops) {
        if (op.add) {
            Insert(r, std::move(op.entry));
        } else {
            Erase(r, op.entry);
            Event::FunctionContainer::Release(op.entry.c);
        }
    }
}

// Removes every registration `frame` made for `id`. With `dying`, they stop
// running at once, also in a fire that is under way.
void RemoveFrame(Id id, void *frame, bool dying) {
    Registry &r = g_registries[id];
    std::vector<Entry> matches;
    for (Entry &e : r.entries)
        if (e.frame == frame) {
            if (dying)
                e.dead = true;
            matches.push_back(e);
        }
    // A registration queued during a fire is removed after it lands.
    for (const Op &op : r.pending)
        if (op.add && op.entry.frame == frame)
            matches.push_back(op.entry);
    for (const Entry &e : matches)
        Remove(id, e);
}

void Invoke(void *L, const Entry &e, int firstArg, int nargs) {
    if (e.dead)
        return;
    const int top = Game::Lua::GetTop(L);
    const int errfunc = Game::Lua::PushErrorHandler(L);
    if (!Event::FunctionContainer::PushFunction(L, e.c)) {
        Game::Lua::SetTop(L, top);
        return;
    }
    if (e.frame != nullptr)
        Game::Lua::PushObject(L, e.frame);
    else
        Game::Lua::PushNil(L);
    for (int i = 0; i < nargs; ++i)
        Game::Lua::PushValue(L, firstArg + i);
    {
        const Profile::AddOnProfiler::Scope profile(Event::FunctionContainer::Owner(e.c));
        Game::Lua::PCall(L, nargs + 1, 0, errfunc);
    }
    Game::Lua::SetTop(L, top);
}

// The entries vector does not change during a fire: every change waits in
// `pending` until the outermost fire of this event ends.
void RunPass(void *L, Registry &r, const char *unit, int firstArg, int nargs) {
    for (int framePass = 0; framePass < 2; ++framePass)
        for (size_t i = 0; i < r.entries.size(); ++i) {
            const Entry &e = r.entries[i];
            if ((e.frame != nullptr) != (framePass == 1))
                continue;
            if (unit == nullptr ? !e.unit.empty() : !Ascii::EqualCI(e.unit.c_str(), unit))
                continue;
            Invoke(L, e, firstArg, nargs);
        }
}

// ---- argument readers -------------------------------------------------------------

const char *NameArg(void *L, int idx) {
    return Game::Lua::IsString(L, idx) ? Game::Lua::ToString(L, idx) : nullptr;
}

bool IsCallbackValue(void *L, int idx) { return Event::FunctionContainer::IsCallbackArg(L, idx); }

// Copies the table's name for the unit at `idx` into `out`; false for a value
// that is not a unit token.
bool UnitArg(void *L, int idx, char (&out)[32]) {
    const char *s = NameArg(L, idx);
    std::string token;
    if (s == nullptr || !Frame::UnitEvent::CanonicalUnitToken(s, &token) ||
        token.size() >= sizeof(out))
        return false;
    std::snprintf(out, sizeof(out), "%s", token.c_str());
    return true;
}

// ---- globals ----------------------------------------------------------------------

// Raises "<fn> Attempt to <verb> unknown event "<name>"".
void UnknownEvent(void *L, const char *fn, const char *verb, const char *name) {
    Game::Lua::Error(L, "%s Attempt to %s unknown event \"%s\"", fn, verb,
                     name != nullptr ? name : "");
}

int GlobalRegister(void *L, const char *fn, const char *verb, bool add, bool withUnit) {
    const char *name = NameArg(L, 1);
    const int id = IdForName(name);
    if (id < 0 || (withUnit && id != MINIMAP_PING)) {
        UnknownEvent(L, fn, verb, name);
        return 0;
    }
    if (!IsCallbackValue(L, 2)) {
        Game::Lua::ArgError(L, 2, "invalid callback");
        return 0;
    }
    char unit[32] = "";
    if (withUnit && !UnitArg(L, 3, unit)) {
        UnknownEvent(L, fn, verb, name);
        return 0;
    }
    Container *c = Event::FunctionContainer::FromCallbackArg(L, 2);
    if (add)
        Add(static_cast<Id>(id), c, nullptr, unit);
    else
        Remove(static_cast<Id>(id), Entry{c, nullptr, unit});
    Event::FunctionContainer::Release(c);
    Game::Lua::PushBool(L, true);
    return 1;
}

int __fastcall Script_RegisterEventCallback(void *L) {
    return GlobalRegister(L, "RegisterEventCallback", "register", true, false);
}

int __fastcall Script_UnregisterEventCallback(void *L) {
    return GlobalRegister(L, "UnregisterEventCallback", "register", false, false);
}

int __fastcall Script_RegisterUnitEventCallback(void *L) {
    return GlobalRegister(L, "RegisterUnitEventCallback", "register", true, true);
}

int __fastcall Script_UnregisterUnitEventCallback(void *L) {
    return GlobalRegister(L, "UnregisterUnitEventCallback", "unregister", false, true);
}

// ---- frame methods ----------------------------------------------------------------

int FrameRegister(void *L, const char *fn, bool withUnit) {
    void *frame = Game::Lua::ResolveFrame(L);
    const char *name = NameArg(L, 2);
    const int id = IdForName(name);
    char unit[32] = "";
    const bool known = id >= 0 && (!withUnit || id == MINIMAP_PING);
    if (known && !IsCallbackValue(L, 3)) {
        Game::Lua::ArgError(L, 3, "invalid callback");
        return 0;
    }
    if (!known || (withUnit && !UnitArg(L, 4, unit))) {
        char message[160];
        std::snprintf(message, sizeof(message), "Attempt to register unknown event \"%s\"",
                      name != nullptr ? name : "");
        const char *frameName = Game::Lua::ObjectName(frame);
        Game::Lua::RaiseClientMethodError(
            L, frameName != nullptr ? frameName : Game::Lua::FrameObjectType(frame), fn, message);
        return 0;
    }
    Container *c = Event::FunctionContainer::FromCallbackArg(L, 3);
    Add(static_cast<Id>(id), c, frame, unit);
    Event::FunctionContainer::Release(c);
    Game::Lua::PushBool(L, true);
    return 1;
}

int __fastcall Script_FrameRegisterEventCallback(void *L) {
    return FrameRegister(L, "RegisterEventCallback", false);
}

int __fastcall Script_FrameRegisterUnitEventCallback(void *L) {
    return FrameRegister(L, "RegisterUnitEventCallback", true);
}

// frame:UnregisterEvent(name) / UnregisterAllEvents() also drop the frame's
// callbacks (3.80.2's cleanup closures, FUN_1405bc450 / FUN_1405bc160).
void OnUnregister(void *frame, const char *eventName) {
    if (eventName == nullptr) {
        for (int id = 0; id < ID_COUNT; ++id)
            RemoveFrame(static_cast<Id>(id), frame, false);
        return;
    }
    const int id = IdForName(eventName);
    if (id >= 0)
        RemoveFrame(static_cast<Id>(id), frame, false);
}

void OnFrameDestroyed(void *frame) {
    for (int id = 0; id < ID_COUNT; ++id)
        RemoveFrame(static_cast<Id>(id), frame, true);
}

// ---- C_EventUtils.IsCallbackEvent -------------------------------------------------

int __fastcall Script_IsCallbackEvent(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::ArgError(L, 1,
                            "Usage: local isCallbackEvent = C_EventUtils.IsCallbackEvent(eventName)");
        return 0;
    }
    Game::Lua::PushBool(L, IdForName(Game::Lua::ToString(L, 1)) >= 0);
    return 1;
}

// ---- documentation ----------------------------------------------------------------

constexpr const char *kSystem = "FrameScript";

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("eventName", "cstring", "A callback event; C_EventUtils.IsCallbackEvent."),
    Game::Doc::Req("callback", "EventCallbackType",
                   "A function, or a container from C_FunctionContainers.CreateCallback. It "
                   "gets nil, then the event's values."),
};
const Game::Doc::Field kUnitArgs[] = {
    Game::Doc::Req("eventName", "cstring", "MINIMAP_PING; no other event takes a unit."),
    Game::Doc::Req("callback", "EventCallbackType",
                   "A function, or a container from C_FunctionContainers.CreateCallback."),
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kOkRets[] = {
    Game::Doc::Req("success", "bool"),
};
const Game::Doc::Function kRegisterEventCallback{
    "Calls a function each time a callback event fires.", kArgs, kOkRets, kSystem};
const Game::Doc::Function kUnregisterEventCallback{
    "Stops a callback that RegisterEventCallback registered. Only a container can be "
    "removed: a plain function makes a new registration each time.",
    kArgs, kOkRets, kSystem};
const Game::Doc::Function kRegisterUnitEventCallback{
    "Calls a function each time a callback event fires for a unit.", kUnitArgs, kOkRets,
    kSystem};
const Game::Doc::Function kUnregisterUnitEventCallback{
    "Stops a callback that RegisterUnitEventCallback registered.", kUnitArgs, kOkRets,
    kSystem};

const Game::Doc::Field kFrameArgs[] = {
    Game::Doc::Req("eventName", "cstring", "A callback event; C_EventUtils.IsCallbackEvent."),
    Game::Doc::Req("cb", "FrameEventCallbackType",
                   "A function, or a container from C_FunctionContainers.CreateCallback. It "
                   "gets the frame, then the event's values."),
};
const Game::Doc::Field kFrameUnitArgs[] = {
    Game::Doc::Req("eventName", "cstring", "MINIMAP_PING; no other event takes a unit."),
    Game::Doc::Req("cb", "FrameEventCallbackType"),
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kFrameRets[] = {
    Game::Doc::Req("registered", "bool"),
};
const Game::Doc::Function kFrameRegisterEventCallback{
    "Calls a function each time a callback event fires, with the frame as its first "
    "argument. UnregisterEvent and UnregisterAllEvents remove it.",
    kFrameArgs, kFrameRets};
const Game::Doc::Function kFrameRegisterUnitEventCallback{
    "Calls a function each time a callback event fires for a unit, with the frame as its "
    "first argument.",
    kFrameUnitArgs, kFrameRets};

const Game::Doc::Field kIsArgs[] = {
    Game::Doc::Req("eventName", "cstring"),
};
const Game::Doc::Field kIsRets[] = {
    Game::Doc::Req("isCallbackEvent", "bool",
                   "True for an event that RegisterEventCallback takes. The name is not "
                   "case-sensitive."),
};
const Game::Doc::Function kIsCallbackEvent{
    "Returns whether an event is delivered to event callbacks.", kIsArgs, kIsRets};

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"RegisterEventCallback", &Script_FrameRegisterEventCallback},
    {"RegisterUnitEventCallback", &Script_FrameRegisterUnitEventCallback},
};
const Game::Doc::Method g_frameMethodDocs[] = {
    {"RegisterEventCallback", &kFrameRegisterEventCallback},
    {"RegisterUnitEventCallback", &kFrameRegisterUnitEventCallback},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("RegisterEventCallback", &Script_RegisterEventCallback,
                                      &kRegisterEventCallback);
    Game::Lua::RegisterGlobalFunction("UnregisterEventCallback", &Script_UnregisterEventCallback,
                                      &kUnregisterEventCallback);
    Game::Lua::RegisterGlobalFunction("RegisterUnitEventCallback",
                                      &Script_RegisterUnitEventCallback,
                                      &kRegisterUnitEventCallback);
    Game::Lua::RegisterGlobalFunction("UnregisterUnitEventCallback",
                                      &Script_UnregisterUnitEventCallback,
                                      &kUnregisterUnitEventCallback);
    Game::Lua::RegisterTableFunction("C_EventUtils", "IsCallbackEvent", &Script_IsCallbackEvent,
                                     &kIsCallbackEvent);
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY),
                                    g_frameMethods, g_frameMethodDocs);
}

void PrepareForReload() {
    for (Registry &r : g_registries) {
        for (Entry &e : r.entries)
            Event::FunctionContainer::Release(e.c);
        for (Op &op : r.pending)
            Event::FunctionContainer::Release(op.entry.c);
        r.entries.clear();
        r.pending.clear();
        r.depth = 0;
    }
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Frame::UnitEvent::AutoUnregister _unregister{&OnUnregister};
const Frame::Destruction::AutoSubscribe _destruction{&OnFrameDestroyed};

} // namespace

bool HasCallbacks(Id id) { return !g_registries[id].entries.empty(); }

void Fire(Id id, void *L, int firstArg, int nargs, const char *unit) {
    Registry &r = g_registries[id];
    if (L == nullptr || r.entries.empty())
        return;
    if (Game::Lua::CheckStack(L, nargs + 4) == 0)
        return;
    ++r.depth;
    RunPass(L, r, nullptr, firstArg, nargs);
    if (unit != nullptr)
        RunPass(L, r, unit, firstArg, nargs);
    if (--r.depth == 0)
        ApplyPending(r);
}

} // namespace Event::Callback
