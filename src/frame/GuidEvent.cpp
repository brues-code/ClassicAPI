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

// `frame:RegisterGUIDEvent(event, guid, ...)`, `frame:UnregisterGUIDEvent(event)`
// and `frame:IsGUIDEventRegistered(event)` — unit events by GUID. A ClassicAPI
// extension: the 3.80.2 client's RegisterUnitEvent takes unit tokens only and
// drops anything else, and ClassicAPI's keeps that.
//
// A GUID registration is kept apart from the engine's event registrations. The
// frame does not join the event's subscriber chain, so RegisterEvent,
// RegisterUnitEvent, UnregisterEvent and IsEventRegistered never see it, and
// the engine's own dispatch never reaches it: nampower's raw-GUID unit events,
// which go to every frame on the chain, cannot arrive twice. Unit::Signal
// fires the unit events to these listeners itself, through Deliver, once per
// event for the unit, with arg1 the GUID.
//
// A registered GUID's unit is watched (Unit::TokenEvents keeps a registration
// bit on it), so its field changes fire unit events as a token's do. The
// registrations die with the frame (UnregisterAllEvents, which the frame's
// destructor runs), and on /reload and at glue boot.

#include "GuidEvent.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "frame/UnitEvent.h"
#include "guid/Guid.h"
#include "unit/TokenEvents.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

namespace Frame::GuidEvent {

namespace {

// One frame's GUIDs for one event, in the order given.
struct Registration {
    int slot;
    std::vector<uint64_t> guids; // never empty
};
std::unordered_map<const void *, std::vector<Registration>> g_byFrame;

// The fire-time index: every (frame, event) that registered a GUID.
struct Listener {
    void *frame;
    int slot;
};
std::unordered_map<uint64_t, std::vector<Listener>> g_byGuid;

Registration *Find(const void *frame, int slot) {
    auto it = g_byFrame.find(frame);
    if (it == g_byFrame.end())
        return nullptr;
    for (Registration &r : it->second)
        if (r.slot == slot)
            return &r;
    return nullptr;
}

bool Contains(const std::vector<uint64_t> &guids, uint64_t guid) {
    return std::find(guids.begin(), guids.end(), guid) != guids.end();
}

// A GUID's first listener starts the watch on its unit, its last stops it:
// the index changes before the refresh, which reads it.
void AddListener(uint64_t guid, void *frame, int slot) {
    auto [it, first] = g_byGuid.try_emplace(guid);
    it->second.push_back(Listener{frame, slot});
    if (first)
        Unit::TokenEvents::Refresh(guid);
}

void RemoveListener(uint64_t guid, const void *frame, int slot) {
    auto it = g_byGuid.find(guid);
    if (it == g_byGuid.end())
        return;
    std::vector<Listener> &v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Listener &l) { return l.frame == frame && l.slot == slot; }),
            v.end());
    if (v.empty()) {
        g_byGuid.erase(it);
        Unit::TokenEvents::Refresh(guid);
    }
}

// Makes `guids` the frame's GUIDs for the event in `slot` (none drops the
// registration). True when that changed anything.
bool Set(void *frame, int slot, std::vector<uint64_t> guids) {
    Registration *r = Find(frame, slot);
    const std::vector<uint64_t> old = r != nullptr ? r->guids : std::vector<uint64_t>{};
    if (old == guids)
        return false;
    for (const uint64_t g : old)
        if (!Contains(guids, g))
            RemoveListener(g, frame, slot);
    for (const uint64_t g : guids)
        if (!Contains(old, g))
            AddListener(g, frame, slot);
    // The listener changes above run no script, so `r` is still good.
    if (!guids.empty()) {
        if (r != nullptr)
            r->guids = std::move(guids);
        else
            g_byFrame[frame].push_back(Registration{slot, std::move(guids)});
        return true;
    }
    std::vector<Registration> &regs = g_byFrame[frame];
    regs.erase(std::remove_if(regs.begin(), regs.end(),
                              [&](const Registration &x) { return x.slot == slot; }),
               regs.end());
    if (regs.empty())
        g_byFrame.erase(frame);
    return true;
}

void DropFrame(const void *frame) {
    auto it = g_byFrame.find(frame);
    if (it == g_byFrame.end())
        return;
    const std::vector<Registration> regs = std::move(it->second);
    g_byFrame.erase(it);
    for (const Registration &r : regs)
        for (const uint64_t g : r.guids)
            RemoveListener(g, frame, r.slot);
}

// ---- Lua methods --------------------------------------------------------------

// "<name>:<method>(): <message>", the name being the frame's, else its type, as
// RegisterUnitEvent's errors read.
void FormatError(char (&out)[Game::Lua::kClientErrorSize], void *frame, const char *method,
                 const char *fmt, ...) {
    char message[Game::Lua::kClientErrorSize];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof message, fmt, args);
    va_end(args);
    const char *name = Game::Lua::ObjectName(frame);
    Game::Lua::FormatClientMethodError(out, name != nullptr ? name : Game::Lua::FrameObjectType(frame),
                                       method, message);
}

// The slot of the unit event named at stack index 2, or -1 for a name that is
// no unit event. Claims ClassicAPI's own events first, as a RegisterEvent does:
// a GUID registration never runs the engine's.
int UnitEventSlot(void *L) {
    const char *name = Frame::UnitEvent::CanonicalUnitEvent(Game::Lua::ToString(L, 2));
    if (name == nullptr)
        return -1;
    Event::Custom::RetryClaims();
    return Event::Custom::LookupByName(name);
}

constexpr const char kRegisterUsage[] = "Usage: (\"event\", \"guid1\" [, \"guid2\", ...])";

// The body of RegisterGUIDEvent: pushes the return and gives true, or fills
// `error` and gives false; the caller raises it once this scope's vector is
// gone (a Lua error never runs its destructor).
bool RegisterGUIDEvent(void *L, void *frame, char (&error)[Game::Lua::kClientErrorSize]) {
    const int slot = UnitEventSlot(L);
    if (slot < 0) {
        FormatError(error, frame, "RegisterGUIDEvent", "Attempt to register unknown event \"%s\"",
                    Game::Lua::ToString(L, 2));
        return false;
    }
    // GUIDs: from index 3 to the first value that is not a string, each once,
    // in their order. A string that is no GUID is dropped.
    std::vector<uint64_t> guids;
    for (int idx = 3, top = Game::Lua::GetTop(L); idx <= top && Game::Lua::IsString(L, idx); ++idx) {
        uint64_t g = 0;
        if (Guid::Parse(Game::Lua::ToString(L, idx), &g) && g != 0 && !Contains(guids, g))
            guids.push_back(g);
    }
    if (guids.empty()) {
        FormatError(error, frame, "RegisterGUIDEvent", "%s", kRegisterUsage);
        return false;
    }
    Game::Lua::PushBool(L, Set(frame, slot, std::move(guids)));
    return true;
}

// `frame:RegisterGUIDEvent(event, guid, ...)` → registered: true when the call
// made the registration or changed its GUIDs. The GUIDs replace the frame's
// earlier ones for the event.
int __fastcall Script_RegisterGUIDEvent(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    char error[Game::Lua::kClientErrorSize];
    if (!Game::Lua::IsString(L, 2))
        FormatError(error, frame, "RegisterGUIDEvent", "%s", kRegisterUsage);
    else if (RegisterGUIDEvent(L, frame, error))
        return 1;
    Game::Lua::Error(L, "%s", error);
    return 0;
}

// `frame:UnregisterGUIDEvent(event)` — drops the frame's GUIDs for the event.
int __fastcall Script_UnregisterGUIDEvent(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::ArgError(L, 2, "Usage: self:UnregisterGUIDEvent(eventName)");
        return 0;
    }
    if (const int slot = UnitEventSlot(L); slot >= 0)
        Set(frame, slot, {});
    return 0;
}

// `frame:IsGUIDEventRegistered(event)` → isRegistered, guid…: the GUIDs in
// their order, or a nil second return.
int __fastcall Script_IsGUIDEventRegistered(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::ArgError(L, 2,
                            "Usage: local isRegistered, (guids)* = self:IsGUIDEventRegistered(eventName)");
        return 0;
    }
    const int slot = UnitEventSlot(L);
    const Registration *r = slot >= 0 ? Find(frame, slot) : nullptr;
    const int count = r != nullptr ? static_cast<int>(r->guids.size()) : 0;
    Game::Lua::PushBool(L, r != nullptr);
    if (r == nullptr || Game::Lua::CheckStack(L, count) == 0) {
        Game::Lua::PushNil(L);
        return 2;
    }
    char buf[Guid::STRING_SIZE];
    for (const uint64_t g : r->guids)
        Game::Lua::PushString(L, Guid::FormatAsString(g, buf, sizeof buf));
    return 1 + count;
}

// ---- registration -----------------------------------------------------------------

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"RegisterGUIDEvent", &Script_RegisterGUIDEvent},
    {"UnregisterGUIDEvent", &Script_UnregisterGUIDEvent},
    {"IsGUIDEventRegistered", &Script_IsGUIDEventRegistered},
};

const Game::Doc::Field kRegisterArgs[] = {
    Game::Doc::Req("eventName", "cstring"),
    Game::Doc::Vararg("WOWGUID", "The units the script hears the event for."),
};
const Game::Doc::Field kRegisterRets[] = {
    Game::Doc::Req("registered", "bool"),
};
const Game::Doc::Function kRegister{
    "Registers the frame for a unit event that its OnEvent script receives for the units with "
    "the given GUIDs, with the GUID as the unit. The GUIDs replace the frame's earlier ones for "
    "the event.",
    kRegisterArgs, kRegisterRets, nullptr, true};

const Game::Doc::Field kEventArg[] = {
    Game::Doc::Req("eventName", "cstring"),
};
const Game::Doc::Function kUnregister{
    "Unregisters the frame from a unit event that it registered by GUID.", kEventArg, {},
    nullptr, true};

const Game::Doc::Field kIsRegisteredRets[] = {
    Game::Doc::Req("isRegistered", "bool"),
    Game::Doc::Vararg("WOWGUID", "The GUIDs given to RegisterGUIDEvent, in their order."),
};
const Game::Doc::Function kIsRegistered{
    "Returns whether the frame is registered for a unit event by GUID.", kEventArg,
    kIsRegisteredRets, nullptr, true};

const Game::Doc::Method g_frameMethodDocs[] = {
    {"RegisterGUIDEvent", &kRegister},
    {"UnregisterGUIDEvent", &kUnregister},
    {"IsGUIDEventRegistered", &kIsRegistered},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        g_frameMethodDocs);
}

// UnregisterAllEvents, also the frame destructor's path.
void OnUnregister(void *frame, const char *eventName) {
    if (eventName == nullptr && !g_byFrame.empty())
        DropFrame(frame);
}

// Every frame is gone (and the event table rebuilt): forget them all, and stop
// watching their units.
void PrepareForReload() {
    std::vector<uint64_t> watched;
    watched.reserve(g_byGuid.size());
    for (const auto &[guid, listeners] : g_byGuid)
        watched.push_back(guid);
    g_byFrame.clear();
    g_byGuid.clear();
    for (const uint64_t g : watched)
        Unit::TokenEvents::Refresh(g);
}

bool StillListening(void *frame, void *ctx) {
    const auto *key = static_cast<const std::pair<int, uint64_t> *>(ctx);
    const Registration *r = Find(frame, key->first);
    return r != nullptr && Contains(r->guids, key->second);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};
const Game::GlueBootAutoRegister _glueBootReg{&PrepareForReload};
const Frame::UnitEvent::AutoUnregister _unregSub{&OnUnregister};

} // namespace

bool Has(int slot, uint64_t guid) {
    if (g_byGuid.empty())
        return false;
    auto it = g_byGuid.find(guid);
    if (it == g_byGuid.end())
        return false;
    for (const Listener &l : it->second)
        if (l.slot == slot)
            return true;
    return false;
}

bool HasAny(int slot) {
    for (const auto &[guid, listeners] : g_byGuid)
        for (const Listener &l : listeners)
            if (l.slot == slot)
                return true;
    return false;
}

bool Watched(uint64_t guid) { return !g_byGuid.empty() && g_byGuid.count(guid) != 0; }

std::vector<uint64_t> Guids(int slot) {
    std::vector<uint64_t> out;
    for (const auto &[guid, listeners] : g_byGuid)
        for (const Listener &l : listeners)
            if (l.slot == slot) {
                out.push_back(guid);
                break;
            }
    return out;
}

void Deliver(int slot, uint64_t guid, Push push, void *ctx) {
    auto it = g_byGuid.find(guid);
    if (it == g_byGuid.end())
        return;
    // A snapshot: a handler can register and unregister. StillListening asks
    // again at each frame's turn.
    std::vector<void *> frames;
    for (const Listener &l : it->second)
        if (l.slot == slot)
            frames.push_back(l.frame);
    void *L = Game::Lua::State();
    if (frames.empty() || L == nullptr || Game::Lua::CheckStack(L, 16) == 0)
        return;
    const int top = Game::Lua::GetTop(L);
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    const int nargs = 1 + push(L, ctx);
    std::pair<int, uint64_t> key{slot, guid};
    Event::Custom::FireValuesTo(slot, L, top + 1, nargs, frames, &StillListening, &key);
    Game::Lua::SetTop(L, top);
}

} // namespace Frame::GuidEvent
