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
// `frame:IsEventRegistered(event)` (registered, unit…).
//
// Vanilla owns everything except the filter, so the filter is all this adds:
//
//   - Chain membership stays the engine's. RegisterUnitEvent delegates to the
//     engine's own `Script_RegisterEvent` (stack cut to (self, event)), so the
//     frame lands on the event's subscriber chain through the same code every
//     RegisterEvent uses — DllMain's RegisterEvent hook included. Membership
//     is read back from the frame's own registered-event list, the one
//     Frame::UnregisterEvent searches.
//   - The filter is stored here, keyed by frame, and dropped exactly where the
//     engine drops the frame's subscriber node: co-hooks on the C++ helpers
//     `Frame::UnregisterEvent` (FUN_FRAME_UNREGISTER_EVENT) and
//     `Frame::UnregisterAllEvents` (FUN_FRAME_UNREGISTER_ALL_EVENTS — also the
//     CScriptObject destructor's path, so frame destruction is covered). A
//     plain RegisterEvent clears it too (OnPlainRegister, from DllMain's hook
//     on `Frame::RegisterEvent`).
//   - The check is one gate on the per-frame OnEvent fire. The dispatcher
//     FUN_FIRE_EVENT sets `_G.event`, walks the chain, and per frame calls the
//     runner FUN_FRAME_RUN_SCRIPT_WITH_CONTEXT(frame, frame+OFF_FRAME_ONEVENT_SLOT,
//     fmt, va). We subscribe to that runner through `Frame::RunnerHook` (it is
//     shared with Frame::ClickEvents — one MinHook per address), and when the
//     frame has a filter for the event being dispatched and arg1 matches none
//     of its tokens, we return without invoking the handler. Suppressing at the
//     runner also suppresses HookScript chains and Frame::ScriptArgs' positional
//     path — they all live downstream of it, which is what retail does too.
//     Event::Custom::FireValues walks the chain itself and never reaches that
//     runner, so it asks `Accepts` (UnitEvent.h) for each frame instead.
//
// Semantics follow the 3.80.2 Classic client (Script_RegisterUnitEvent
// 0x1428CF3D0, listener add 0x1405C4240, IsEventRegistered 0x140A88A80):
//
//   - A registration holds one unit list, and every register overwrites it:
//     RegisterEvent (or RegisterUnitEvent with no valid unit) makes it plain,
//     RegisterUnitEvent makes it filtered. The return is true when the call
//     made a registration or changed its units, false when nothing changed.
//   - Units are read from argument 3 up to the first argument that is not a
//     string or number. Each is looked up case-insensitively in the client's
//     unit-token table (0x1427053B0; no compound tokens such as party1target)
//     and stored as the table's lowercase name; unknown tokens are dropped.
//   - Units are allowed only on the client's unit events (the hash set in
//     0x140AD89A0 plus UNIT_SCALING_INFO_UPDATE); any other event with a unit
//     raises "Attempt to register unknown event", as an unknown event name
//     does. Errors carry the client's "<name>:RegisterUnitEvent(): " prefix
//     (0x1405AC570: the frame's name, else its type) and stop at 199 chars.
//
// Where we differ, all additive:
//
//   - UNIT COUNT. 3.80.2 packs at most four token ids into the listener entry.
//     We store strings in our own map, so every unit argument counts.
//   - TOKENS AND EVENTS. ClassicAPI's own tokens (`pettarget`, `markN`,
//     `totemN`) join the token table, and 1.12's own unit events that 3.80.2
//     replaced (UNIT_RAGE, UNIT_MAXMANA, …) plus ClassicAPI's
//     (UNIT_AURA_DELTA, UNIT_AREA_CHANGED) join the event list.
//
// Filter rule: the filter applies only when the event's first argument is a
// string (every unit event fires as "%s" + token). 3.80.2 fires each unit
// event once per token the unit has and matches that token; 1.12 also fires
// once per token with the token as arg1, so matching arg1 is the same test.

#include "UnitEvent.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/RunnerHook.h"
#include "totem/Tracker.h"
#include "unit/RaidTarget.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frame::UnitEvent {

namespace {

// A null name never matches (the event-table walk can hand one over).
bool NameIs(const char *a, const char *b) {
    return a != nullptr && b != nullptr && Ascii::EqualCI(a, b);
}

// ---- the token table -----------------------------------------------------------
//
// 3.80.2's unit tokens (id→name 0x142702EB0, 361 ids), plus ClassicAPI's.

constexpr const char *kSingleTokens[] = {
    "player",    "pet",        "vehicle",     "mouseover",       "target",
    "targettarget", "softenemy", "softfriend", "softinteract",   "focus",
    "focustarget", "anytarget", "anyinteract", "anyleftinteract", "anyenemy",
    "anyfriend", "npc",        "questnpc",
    // ClassicAPI's
    "pettarget",
};

struct TokenFamily {
    const char *prefix;
    int count; // valid suffixes are 1..count
};

// Longer prefixes need no ordering: the suffix must be all digits, so
// "raidpet1" never matches "raid".
constexpr TokenFamily kNumberedTokens[] = {
    {"raid", 40},         {"raidpet", 40},       {"party", 4},
    {"partypet", 4},      {"nameplate", 150},    {"boss", 5},
    {"arena", 5},         {"arenapet", 5},       {"spectateda", 15},
    {"spectatedb", 15},   {"spectatedpeta", 15}, {"spectatedpetb", 15},
    {"commentator", 30},
    // ClassicAPI's
    {"mark", Unit::RaidTarget::kMarkCount}, {"totem", Totem::Tracker::kSlotCount},
};

// The table's name for `s` (lowercase), or false when `s` is not a token.
bool CanonicalToken(const char *s, std::string *out) {
    for (const char *t : kSingleTokens)
        if (Ascii::EqualCI(s, t)) {
            *out = t;
            return true;
        }
    for (const TokenFamily &f : kNumberedTokens) {
        if (!Ascii::StartsWithCI(s, f.prefix))
            continue;
        const char *d = s + std::strlen(f.prefix);
        if (*d < '1' || *d > '9')
            continue; // no suffix, or a leading zero
        int n = 0;
        for (; *d >= '0' && *d <= '9' && n <= f.count; ++d)
            n = n * 10 + (*d - '0');
        if (*d != '\0' || n > f.count)
            continue;
        *out = f.prefix;
        *out += std::to_string(n);
        return true;
    }
    return false;
}

// ---- the unit-event list --------------------------------------------------------

constexpr const char *kUnitEvents[] = {
    // 3.80.2's (0x140AD89A0's hash set, plus UNIT_SCALING_INFO_UPDATE)
    "ARENA_CROWD_CONTROL_SPELL_UPDATE", "HONOR_XP_UPDATE", "INCOMING_RESURRECT_CHANGED",
    "INCOMING_SUMMON_CHANGED", "KNOWN_TITLES_UPDATE", "LOSS_OF_CONTROL_ADDED",
    "LOSS_OF_CONTROL_UPDATE", "NAME_PLATE_UNIT_BEHIND_CAMERA_CHANGED", "PARTY_MEMBER_DISABLE",
    "PARTY_MEMBER_ENABLE", "PLAYER_DAMAGE_DONE_MODS", "PLAYER_FLAGS_CHANGED",
    "PLAYER_GAINS_VEHICLE_DATA", "PLAYER_GUILD_UPDATE", "PLAYER_LOSES_VEHICLE_DATA",
    "PLAYER_MAX_LEVEL_UPDATE", "PLAYER_PVP_KILLS_CHANGED", "PLAYER_PVP_RANK_CHANGED",
    "PLAYER_SPECIALIZATION_CHANGED", "PLAYER_TARGET_SET_ATTACKING", "PLAYER_TRIAL_XP_UPDATE",
    "PLAYER_XP_UPDATE", "PVP_TIMER_UPDATE", "READY_CHECK_CONFIRM", "UNIT_ABSORB_AMOUNT_CHANGED",
    "UNIT_ARENA_COOLDOWNS_UPDATE", "UNIT_ATTACK", "UNIT_ATTACK_POWER", "UNIT_ATTACK_SPEED",
    "UNIT_AURA", "UNIT_AURA_BLOCKED", "UNIT_AURA_BLOCK_LIST_CLEARED",
    "UNIT_CLASSIFICATION_CHANGED", "UNIT_COMBAT", "UNIT_CONNECTION", "UNIT_CTR_OPTIONS",
    "UNIT_DAMAGE", "UNIT_DEFENSE", "UNIT_DISPLAYPOWER", "UNIT_DISTANCE_CHECK_UPDATE",
    "UNIT_ENTERED_VEHICLE", "UNIT_ENTERING_VEHICLE", "UNIT_EXITED_VEHICLE",
    "UNIT_EXITING_VEHICLE", "UNIT_FACTION", "UNIT_FLAGS", "UNIT_FORM_CHANGED", "UNIT_HAPPINESS",
    "UNIT_HEALTH", "UNIT_HEALTH_FREQUENT", "UNIT_HEAL_ABSORB_AMOUNT_CHANGED",
    "UNIT_HEAL_PREDICTION", "UNIT_INVENTORY_CHANGED", "UNIT_IN_RANGE_UPDATE", "UNIT_LEVEL",
    "UNIT_MANA", "UNIT_MAXHEALTH", "UNIT_MAXPOWER", "UNIT_MAX_HEALTH_MODIFIERS_CHANGED",
    "UNIT_MODEL_CHANGED", "UNIT_NAME_UPDATE", "UNIT_OTHER_PARTY_CHANGED", "UNIT_PET",
    "UNIT_PET_EXPERIENCE", "UNIT_PET_TRAINING_POINTS", "UNIT_PHASE", "UNIT_PORTRAIT_UPDATE",
    "UNIT_POWER_BAR_HIDE", "UNIT_POWER_BAR_SHOW", "UNIT_POWER_BAR_TIMER_UPDATE",
    "UNIT_POWER_FREQUENT", "UNIT_POWER_UPDATE", "UNIT_QUEST_LOG_CHANGED", "UNIT_RANGEDDAMAGE",
    "UNIT_RANGED_ATTACK_POWER", "UNIT_RESISTANCES", "UNIT_SCALING_INFO_UPDATE",
    "UNIT_SPELLCAST_CHANNEL_START", "UNIT_SPELLCAST_CHANNEL_STOP",
    "UNIT_SPELLCAST_CHANNEL_UPDATE", "UNIT_SPELLCAST_DELAYED", "UNIT_SPELLCAST_EMPOWER_START",
    "UNIT_SPELLCAST_EMPOWER_STOP", "UNIT_SPELLCAST_EMPOWER_UPDATE", "UNIT_SPELLCAST_FAILED",
    "UNIT_SPELLCAST_FAILED_QUIET", "UNIT_SPELLCAST_INTERRUPTED",
    "UNIT_SPELLCAST_INTERRUPTIBLE", "UNIT_SPELLCAST_NOT_INTERRUPTIBLE",
    "UNIT_SPELLCAST_RETICLE_CLEAR", "UNIT_SPELLCAST_RETICLE_TARGET", "UNIT_SPELLCAST_SENT",
    "UNIT_SPELLCAST_START", "UNIT_SPELLCAST_STOP", "UNIT_SPELLCAST_SUCCEEDED",
    "UNIT_SPELL_DIMINISH_CATEGORY_STATE_UPDATED", "UNIT_SPELL_HASTE", "UNIT_STATS",
    "UNIT_TARGET", "UNIT_TARGETABLE_CHANGED", "UNIT_THREAT_LIST_UPDATE",
    "UNIT_THREAT_SITUATION_UPDATE",
    // 1.12's own unit events that 3.80.2 replaced
    "UNIT_DYNAMIC_FLAGS", "UNIT_ENERGY", "UNIT_FOCUS", "UNIT_LOYALTY", "UNIT_MAXENERGY",
    "UNIT_MAXFOCUS", "UNIT_MAXHAPPINESS", "UNIT_MAXMANA", "UNIT_MAXRAGE", "UNIT_RAGE",
    // ClassicAPI's
    "UNIT_AURA_DELTA", "UNIT_AREA_CHANGED",
};

bool IsUnitEvent(const char *eventName) {
    for (const char *e : kUnitEvents)
        if (Ascii::EqualCI(eventName, e))
            return true;
    return false;
}

// One filtered registration — the unit list 3.80.2 keeps on the event's
// listener entry for this frame. `event` is the CANONICAL name copied from the
// event-table entry, so the fire-time compare against the dispatcher-set
// `_G.event` (which is that same entry name) is a plain strcmp.
struct Filter {
    std::string event;
    std::vector<std::string> units; // never empty; a filter with no unit is no filter
};

// Keyed by frame. Entries die with the frame's subscriber node (the two
// unregister co-hooks), on a plain register, and on /reload (PrepareForReload).
std::unordered_map<const void *, std::vector<Filter>> g_filters;

// Set while RegisterUnitEvent runs the engine's RegisterEvent, so the plain
// register hook leaves the filter to it.
bool g_inUnitRegister = false;

// ---- the frame's own event list ----------------------------------------------
//
// Membership is read from the script object's registered-event list (the
// event-table indices it listens to), the structure Frame::UnregisterEvent
// searches — a walk over the frame's handful of registrations instead of the
// ~400-entry event table plus a subscriber chain.

uint32_t EventCount(const void *frame) {
    return Game::Read<uint32_t>(frame, Offsets::OFF_SCRIPTOBJECT_EVENT_COUNT);
}

// Name of the frame's `i`-th registration.
const char *EventNameAt(const void *frame, uint32_t i) {
    const uint32_t *indices =
        Game::Read<const uint32_t *>(frame, Offsets::OFF_SCRIPTOBJECT_EVENT_INDICES);
    auto *base = Game::Read<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_EVENT_TABLE_BASE_PTR));
    return Game::Read<const char *>(base + indices[i] * Offsets::EVENT_ENTRY_STRIDE,
                                    Offsets::OFF_EVENT_ENTRY_NAME);
}

// The event-table name of the frame's registration for `eventName`
// (case-insensitive, as the engine matches), or null when it has none.
// Walked from the end, as Frame::UnregisterEvent walks it.
const char *RegisteredName(const void *frame, const char *eventName) {
    for (uint32_t i = EventCount(frame); i-- > 0;)
        if (const char *name = EventNameAt(frame, i); NameIs(name, eventName))
            return name;
    return nullptr;
}

// ---- filter map --------------------------------------------------------------

Filter *FindFilter(const void *frame, const char *eventName) {
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return nullptr;
    for (Filter &f : it->second)
        if (NameIs(f.event.c_str(), eventName))
            return &f;
    return nullptr;
}

// True when a filter was removed.
bool EraseFilter(const void *frame, const char *eventName) {
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return false;
    std::vector<Filter> &v = it->second;
    bool erased = false;
    for (size_t i = 0; i < v.size(); ++i) {
        if (NameIs(v[i].event.c_str(), eventName)) {
            v.erase(v.begin() + static_cast<ptrdiff_t>(i));
            erased = true;
            break;
        }
    }
    if (v.empty())
        g_filters.erase(it);
    return erased;
}

// ---- Lua methods --------------------------------------------------------------

// The client's "<name>:RegisterUnitEvent(): <message>" (0x1405AC570), the name
// being the frame's, else its type.
void FormatError(char (&out)[Game::Lua::kClientErrorSize], void *frame, const char *fmt, ...) {
    char message[Game::Lua::kClientErrorSize];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof message, fmt, args);
    va_end(args);
    const char *name = Game::Lua::ObjectName(frame);
    Game::Lua::FormatClientMethodError(out, name != nullptr ? name : Game::Lua::FrameObjectType(frame),
                                       "RegisterUnitEvent", message);
}

// The body of RegisterUnitEvent. Pushes the return and gives true, or fills
// `error` and gives false; the caller raises it once this scope's strings are
// gone (a Lua error never runs their destructors).
bool RegisterUnitEvent(void *L, void *frame, char (&error)[Game::Lua::kClientErrorSize]) {
    // The event name stays valid: index 2 survives the reshape below.
    const char *eventName = Game::Lua::ToString(L, 2);

    // Units: from index 3 to the first value that is not a string or number;
    // unknown tokens are dropped, the rest kept as the table's names.
    const int argTop = Game::Lua::GetTop(L);
    std::vector<std::string> units;
    std::string token;
    for (int idx = 3; idx <= argTop && Game::Lua::IsString(L, idx); ++idx)
        if (const char *s = Game::Lua::ToString(L, idx); s != nullptr && CanonicalToken(s, &token))
            units.push_back(token);

    if (!units.empty() && !IsUnitEvent(eventName)) {
        FormatError(error, frame, "Attempt to register unknown event \"%s\"", eventName);
        return false;
    }

    const uint32_t countBefore = EventCount(frame);

    // Chain membership is the engine's: (self, event) → Script_RegisterEvent.
    // Nothing about the event table is cached across this call — the
    // RegisterEvent hook in DllMain runs Event::Custom::RetryClaims inside it,
    // which can claim this very name into a free slot or move the table.
    Game::Lua::SetTop(L, 2);
    g_inUnitRegister = true;
    Game::Lua::CallScript(Offsets::FUN_SCRIPT_FRAME_REGISTEREVENT, L);
    g_inUnitRegister = false;

    // Not on the frame's list after the engine's register: an unknown event.
    const char *canonical = RegisteredName(frame, eventName);
    if (canonical == nullptr) {
        FormatError(error, frame, "Attempt to register unknown event \"%s\"", eventName);
        return false;
    }
    // The engine appends to the frame's list exactly when it joins a chain.
    bool changed = EventCount(frame) != countBefore;

    Filter *existing = FindFilter(frame, eventName);
    if (units.empty()) {
        // A plain register: the registration goes plain.
        if (existing != nullptr)
            changed |= EraseFilter(frame, eventName);
    } else if (existing == nullptr) {
        g_filters[frame].push_back(Filter{canonical, std::move(units)});
        changed = true;
    } else if (existing->units != units) {
        existing->units = std::move(units);
        changed = true;
    }

    Game::Lua::PushBool(L, changed);
    return true;
}

// `frame:RegisterUnitEvent(event, ...units)` → registered.
int __fastcall Script_RegisterUnitEvent(void *L) {
    // Self first, as Script_RegisterEvent checks it: ResolveFrame raises the
    // engine's own error strings.
    void *frame = Game::Lua::ResolveFrame(L);
    char error[Game::Lua::kClientErrorSize];
    if (!Game::Lua::IsString(L, 2))
        FormatError(error, frame, "Usage: (\"event\", \"unit1\" [,\"unit2\", \"unit3\", \"unit4\"])");
    else if (RegisterUnitEvent(L, frame, error))
        return 1;
    Game::Lua::Error(L, "%s", error);
    return 0;
}

// `frame:IsEventRegistered(event)` → registered, unit…. Vanilla has
// RegisterEvent / UnregisterEvent but not the query; the membership check
// reads the frame's own event list, which the engine keeps in step with the
// subscriber chains. Like 3.80.2 there is always a second return: the units
// in their order, or nil.
int __fastcall Script_IsEventRegistered(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::ArgError(L, 2, "Usage: local isRegistered, (units)* = self:IsEventRegistered(eventName)");
        return 0;
    }
    const char *eventName = Game::Lua::ToString(L, 2);
    const bool registered = RegisteredName(frame, eventName) != nullptr;
    const Filter *f = registered ? FindFilter(frame, eventName) : nullptr;
    const int units = f != nullptr ? static_cast<int>(f->units.size()) : 0;
    Game::Lua::PushBool(L, registered);
    if (f == nullptr || Game::Lua::CheckStack(L, units) == 0) {
        // No units, or no room to push them: the second return is nil.
        Game::Lua::PushNil(L);
        return 2;
    }
    for (const std::string &u : f->units)
        Game::Lua::PushString(L, u.c_str());
    return 1 + units;
}

// ---- the gate ------------------------------------------------------------------

// The frame's filter for the event being dispatched. `eventName` is the
// event-table entry's own name, which Filter::event was copied from, so a
// plain strcmp matches it.
const Filter *DispatchFilter(const std::vector<Filter> &filters, const char *eventName) {
    for (const Filter &f : filters)
        if (std::strcmp(f.event.c_str(), eventName) == 0)
            return &f;
    return nullptr;
}

bool MatchesUnit(const Filter &f, const char *arg1) {
    if (arg1 != nullptr)
        for (const std::string &u : f.units)
            if (NameIs(arg1, u.c_str()))
                return true;
    return false;
}

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
    // The filter applies only when arg1 is a string — checked before touching
    // Lua, since it costs nothing.
    const char *arg1 = nullptr;
    if (!FirstArgIsString(fmt, varargs, &arg1))
        return false;

    // Which event? The dispatcher has just set `_G.event` to the entry's name.
    void *L = Game::Lua::State();
    if (L == nullptr)
        return false;
    Game::Lua::PushString(L, "event");
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    const char *ev = Game::Lua::ToString(L, -1);
    const Filter *match = ev != nullptr ? DispatchFilter(it->second, ev) : nullptr;
    Game::Lua::SetTop(L, -2);
    return match != nullptr && !MatchesUnit(*match, arg1); // no token matched — suppress
}

// ---- unregister co-hooks (the filter dies with the node) ------------------------

// `__thiscall(frame, eventName)` mimicked as `__fastcall(ecx, edx_unused, …)`.
using UnregisterEvent_t = void(__fastcall *)(void *frame, void *edx, const char *eventName);
UnregisterEvent_t g_origUnregisterEvent = nullptr;

void __fastcall UnregisterEvent_h(void *frame, void *edx, const char *eventName) {
    if (frame != nullptr && !g_filters.empty())
        EraseFilter(frame, eventName);
    g_origUnregisterEvent(frame, edx, eventName);
    if (frame != nullptr && eventName != nullptr)
        for (AutoUnregister *u = AutoUnregister::head; u != nullptr; u = u->next)
            u->fn(frame, eventName);
}

// `__fastcall(frame)`; the Lua wrapper has dead `(0)` calls after lua_error, so
// tolerate a null frame.
using UnregisterAll_t = void(__fastcall *)(void *frame, void *edx);
UnregisterAll_t g_origUnregisterAll = nullptr;

void __fastcall UnregisterAllEvents_h(void *frame, void *edx) {
    if (frame != nullptr && !g_filters.empty())
        g_filters.erase(frame);
    g_origUnregisterAll(frame, edx);
    if (frame != nullptr)
        for (AutoUnregister *u = AutoUnregister::head; u != nullptr; u = u->next)
            u->fn(frame, nullptr);
}

// ---- registration -----------------------------------------------------------------

const Game::Lua::FrameMethodEntry g_frameMethods[] = {
    {"RegisterUnitEvent", &Script_RegisterUnitEvent},
    {"IsEventRegistered", &Script_IsEventRegistered},
};

const Game::Doc::Field kRegisterUnitEventArgs[] = {
    Game::Doc::Req("eventName", "cstring"),
    Game::Doc::Vararg("UnitToken",
                      "The script runs only when the event's unit is one of these; with none, "
                      "this is RegisterEvent."),
};
const Game::Doc::Field kRegisterUnitEventRets[] = {
    Game::Doc::Req("registered", "bool"),
};
const Game::Doc::Function kRegisterUnitEvent{
    "Registers the frame for an event that its OnEvent script receives only for the given "
    "units.",
    kRegisterUnitEventArgs, kRegisterUnitEventRets};

const Game::Doc::Field kIsEventRegisteredArgs[] = {
    Game::Doc::Req("eventName", "cstring"),
};
const Game::Doc::Field kIsEventRegisteredRets[] = {
    Game::Doc::Req("isRegistered", "bool"),
    Game::Doc::Vararg("UnitToken", "The units given to RegisterUnitEvent, in their order."),
};
const Game::Doc::Function kIsEventRegistered{
    "Returns whether the frame is registered for an event.",
    kIsEventRegisteredArgs, kIsEventRegisteredRets};

const Game::Doc::Method g_frameMethodDocs[] = {
    {"RegisterUnitEvent", &kRegisterUnitEvent},
    {"IsEventRegistered", &kIsEventRegistered},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_frameMethods,
        g_frameMethodDocs);
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

bool CanonicalUnitToken(const char *s, std::string *out) {
    return s != nullptr && CanonicalToken(s, out);
}

void OnPlainRegister(const void *frame, const char *eventName) {
    if (!g_inUnitRegister && frame != nullptr && eventName != nullptr && !g_filters.empty())
        EraseFilter(frame, eventName);
}

bool Accepts(const void *frame, const char *eventName, const char *arg1) {
    if (g_filters.empty() || frame == nullptr || eventName == nullptr)
        return true;
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return true;
    const Filter *match = DispatchFilter(it->second, eventName);
    return match == nullptr || MatchesUnit(*match, arg1);
}

const std::vector<std::string> *Units(const void *frame, const char *eventName) {
    if (g_filters.empty() || frame == nullptr || eventName == nullptr)
        return nullptr;
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return nullptr;
    const Filter *match = DispatchFilter(it->second, eventName);
    return match != nullptr ? &match->units : nullptr;
}

} // namespace Frame::UnitEvent
