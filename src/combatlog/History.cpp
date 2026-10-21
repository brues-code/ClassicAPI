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

// Kept entries and filters — 4.3.4's s_entries, s_filters, s_currentEntry,
// CombatLogFilter and the Lua surface of UnitCombatLog_C_Scripts.cpp.
//
// Every entry is kept, oldest first. A new one lets go of the oldest when it
// is older than combatLogRetentionTime seconds (GetCombatLogEntry's recycle,
// one per new entry). 4.3.4 also holds entries whose names are still being
// asked for (s_pendingEntries), which the stepping functions stop before;
// here every entry is sent at once, so none is held.
//
// A filter is an OR term: an entry matches when any filter matches, or when
// there is none. Within a filter the events, source, target and spell must
// all match. A unit is matched by GUID, or by its flags against a mask that
// must name an affiliation, a reaction, a controller and a type, unless it
// names a special bit (CombatLogFlagsMatch). 4.3.4 compares spell names with
// SStrCmpUTF8I; 1.12 has only SStrCmpI, which folds ASCII letters.
//
// The log ends with the world (UnitCombatLogShutdown, from
// UnitCombatClientShutdown): leaving for the login screen clears both lists.

#include "combatlog/History.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace CombatLog::History {

namespace {

// The mask CombatLog_Object_IsA and the filters treat as complete: any
// special bit, or one bit from each of the four groups.
constexpr uint32_t SPECIAL_MASK = 0xFFFF0000;
constexpr uint32_t AFFILIATION_MASK = 0xF;
constexpr uint32_t REACTION_MASK = 0xF0;
constexpr uint32_t CONTROL_MASK = 0x300;
constexpr uint32_t TYPE_MASK = 0xFC00;

constexpr int kMaxToken = 128;
constexpr int kStormMaxStr = 0x7FFFFFFF;

struct Filter {
    uint64_t events = ~0ull; // bit per Subevent
    uint64_t sourceGuid = 0;
    uint64_t destGuid = 0;
    uint32_t sourceFlags = 0;
    uint32_t sourceRaidFlags = 0;
    uint32_t destFlags = 0;
    uint32_t destRaidFlags = 0;
    int spellId = 0;
    bool spellNamed = false;
    std::string spellName;
};

std::deque<Entry> g_entries;
int g_current = -1; // s_currentEntry, as an index into g_entries
std::vector<Filter> g_filters;
CVar::Factory::Handle g_retention = nullptr;

constexpr int kDefaultRetention = 300;

using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);
using Tokenize_t = void(__stdcall *)(char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);
using LooseBool_t = int(__fastcall *)(void *L, int idx, int dflt);

int StrCmpI(const char *a, const char *b) {
    return reinterpret_cast<SStrCmpI_t>(static_cast<uintptr_t>(Offsets::FUN_SSTR_CMP_I))(
        a, b, kStormMaxStr);
}

bool ArgFlag(void *L, int idx) {
    return reinterpret_cast<LooseBool_t>(
               static_cast<uintptr_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE))(L, idx, 0) != 0;
}

bool Complete(uint32_t matched) {
    return (matched & AFFILIATION_MASK) && (matched & REACTION_MASK) && (matched & CONTROL_MASK) &&
           (matched & TYPE_MASK);
}

// CombatLogFlagsMatch.
bool FlagsMatch(uint32_t flags, uint32_t mask, uint32_t raidFlags, uint32_t raidMask) {
    const uint32_t matched = flags & mask;
    return (matched & SPECIAL_MASK) || (raidFlags & raidMask) || Complete(matched);
}

// 4.3.4's StringToGUID: an optional 0x, then up to 16 hex digits, stopping
// at the first other character.
uint64_t StringToGuid(const char *s) {
    if (s == nullptr)
        return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    uint64_t value = 0;
    for (int i = 0; i < 16; ++i) {
        const char c = *s++;
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            break;
        value = (value << 4) | static_cast<uint64_t>(digit);
    }
    return value;
}

// CombatLogFilter::SetFilterData's event list: names split on spaces and
// commas, matched without case; no list is every event.
uint64_t EventBits(const char *events) {
    if (events == nullptr)
        return ~0ull;
    std::string buffer = events;
    char *cursor = buffer.data();
    uint64_t bits = 0;
    char token[kMaxToken];
    do {
        token[0] = '\0';
        reinterpret_cast<Tokenize_t>(static_cast<uintptr_t>(Offsets::FUN_STORM_STR_TOKENIZE))(
            &cursor, token, sizeof token, " ,", nullptr);
        if (token[0] == '\0')
            break;
        for (int i = 0; i < EVENT_COUNT; ++i) {
            if (StrCmpI(token, Name(i)) == 0) {
                bits |= 1ull << i;
                break;
            }
        }
    } while (*cursor != '\0');
    return bits;
}

// CombatLogFilter::MatchesFilter.
bool Matches(const Filter &f, const Entry &e) {
    if (e.event < 0 || e.event >= EVENT_COUNT || (f.events & (1ull << e.event)) == 0)
        return false;
    if (f.sourceGuid != 0) {
        if (e.source != f.sourceGuid)
            return false;
    } else if (!FlagsMatch(e.sourceFlags, f.sourceFlags, e.sourceRaidFlags, f.sourceRaidFlags)) {
        return false;
    }
    if (f.destGuid != 0) {
        if (e.dest != f.destGuid)
            return false;
    } else if (!FlagsMatch(e.destFlags, f.destFlags, e.destRaidFlags, f.destRaidFlags)) {
        return false;
    }
    if (f.spellId != 0)
        return e.spellId == f.spellId;
    if (!f.spellNamed)
        return true;
    const char *name = e.spellPrefix ? SpellName(SpellRecord(e.spellId)) : nullptr;
    return name != nullptr && StrCmpI(name, f.spellName.c_str()) == 0;
}

bool Matches(const Entry &e, bool all) { return all || MatchesFilter(e); }

int Retention() { return CVar::Factory::GetInt(g_retention, kDefaultRetention); }

// RecycleCombatLogEntry on the oldest entry: the current entry moves on to
// the next one.
void DropOldest() {
    g_entries.pop_front();
    if (g_current > 0)
        --g_current;
    else if (g_current == 0 && g_entries.empty())
        g_current = -1;
}

void PushCurrent(void *L) {
    if (g_current >= 0)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
}

int Count() { return static_cast<int>(g_entries.size()); }

int __fastcall Script_CombatLogResetFilter(void *) {
    g_filters.clear();
    return 0;
}

int __fastcall Script_CombatLogAddFilter(void *L) {
    using namespace Game::Lua;
    Filter f;
    f.events = EventBits(ToString(L, 1));
    // The source (2) and target (3): flags, a GUID, or anything else for all.
    for (int idx = 2; idx <= 3; ++idx) {
        uint32_t flags = 0;
        uint64_t guid = 0;
        switch (Type(L, idx)) {
        case TYPE_NUMBER:
            flags = static_cast<uint32_t>(static_cast<int64_t>(ToNumber(L, idx)));
            if ((flags & SPECIAL_MASK) == 0 && !Complete(flags)) {
                Error(L, idx == 2 ? "CombatLogAddFilter(): incomplete filter for srcMask"
                                  : "CombatLogAddFilter(): incomplete filter for dstMask");
                return 0;
            }
            break;
        case TYPE_STRING:
            guid = StringToGuid(ToString(L, idx));
            break;
        default:
            flags = 0xFFFFFFFF;
            break;
        }
        (idx == 2 ? f.sourceGuid : f.destGuid) = guid;
        (idx == 2 ? f.sourceFlags : f.destFlags) = guid != 0 ? 0 : flags;
    }
    switch (Type(L, 4)) {
    case TYPE_NUMBER:
        f.spellId = static_cast<int>(static_cast<int64_t>(ToNumber(L, 4)));
        break;
    case TYPE_STRING:
        if (const char *name = ToString(L, 4)) {
            f.spellNamed = true;
            f.spellName = name;
        }
        break;
    default:
        break;
    }
    if (f.spellId != 0) {
        f.spellNamed = false;
        f.spellName.clear();
    }
    g_filters.push_back(std::move(f));
    return 0;
}

int __fastcall Script_CombatLogGetRetentionTime(void *L) {
    Game::Lua::PushNumber(L, Retention());
    return 1;
}

int __fastcall Script_CombatLogSetRetentionTime(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: CombatLogSetRetentionTime(seconds)");
        return 0;
    }
    CVar::Factory::SetInt(g_retention, static_cast<int>(Game::Lua::ToNumber(L, 1)));
    return 0;
}

int __fastcall Script_CombatLogGetNumEntries(void *L) {
    const bool all = ArgFlag(L, 1);
    int count = 0;
    for (const Entry &e : g_entries) {
        if (Matches(e, all))
            ++count;
    }
    Game::Lua::PushNumber(L, count);
    return 1;
}

int __fastcall Script_CombatLogSetCurrentEntry(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: CombatLogSetCurrentEntry(index [,ignoreFilter])");
        return 0;
    }
    g_current = -1;
    int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const bool all = ArgFlag(L, 2);
    if (index <= 0) {
        // 0 is the newest entry, -1 the one before it.
        for (int i = Count() - 1; i >= 0; --i) {
            if (Matches(g_entries[i], all) && index++ == 0) {
                g_current = i;
                break;
            }
        }
    } else {
        for (int i = 0; i < Count(); ++i) {
            if (Matches(g_entries[i], all) && --index == 0) {
                g_current = i;
                break;
            }
        }
    }
    PushCurrent(L);
    return 1;
}

int __fastcall Script_CombatLogGetCurrentEntry(void *L) {
    if (g_current < 0 || Game::Lua::CheckStack(L, MAX_EVENT_VALUES) == 0)
        return 0;
    return PushEvent(L, g_entries[g_current]);
}

int __fastcall Script_CombatLogAdvanceEntry(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: CombatLogAdvanceEntry(count [,ignoreFilter])");
        return 0;
    }
    int count = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const bool all = ArgFlag(L, 2);
    if (count != 0) {
        // The walk starts at the current entry, which counts when it matches.
        int result = -1;
        if (count < 0) {
            for (int i = g_current; i >= 0; --i) {
                if (Matches(g_entries[i], all) && count++ == 0) {
                    result = i;
                    break;
                }
            }
        } else if (g_current >= 0) {
            for (int i = g_current; i < Count(); ++i) {
                if (Matches(g_entries[i], all) && count-- == 0) {
                    result = i;
                    break;
                }
            }
        }
        g_current = result;
    }
    PushCurrent(L);
    return 1;
}

int __fastcall Script_CombatLogClearEntries(void *) {
    g_entries.clear();
    g_current = -1;
    return 0;
}

int __fastcall Script_CombatLog_Object_IsA(void *L) {
    const uint32_t flags =
        static_cast<uint32_t>(static_cast<int64_t>(Game::Lua::ToNumber(L, 1))) &
        static_cast<uint32_t>(static_cast<int64_t>(Game::Lua::ToNumber(L, 2)));
    if ((flags & SPECIAL_MASK) != 0 || Complete(flags))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

using Game::Doc::Opt;
using Game::Doc::Req;
using Game::Doc::Vararg;

const Game::Doc::Field kAddFilterArgs[] = {
    Opt("events", "string", nullptr,
        "Subevent names, divided by spaces or commas. With no list, every subevent matches."),
    Opt("source", "number", nullptr,
        "A COMBATLOG_OBJECT_* mask, or a GUID string. With nothing, every source matches."),
    Opt("dest", "number", nullptr,
        "A COMBATLOG_OBJECT_* mask, or a GUID string. With nothing, every target matches."),
    Opt("spell", "number", nullptr, "A spell ID, or a spell name."),
};
const Game::Doc::Field kIgnoreFilterArg[] = {
    Opt("ignoreFilter", "bool", "false"),
};
const Game::Doc::Field kSetCurrentArgs[] = {
    Req("index", "number",
        "From 1, counted from the oldest entry. From 0 down, counted back from the newest."),
    Opt("ignoreFilter", "bool", "false"),
};
const Game::Doc::Field kAdvanceArgs[] = {
    Req("count", "number", "The number of entries to move. A negative count moves back."),
    Opt("ignoreFilter", "bool", "false"),
};
const Game::Doc::Field kSecondsArg[] = {Req("seconds", "number")};
const Game::Doc::Field kSecondsRet[] = {Req("seconds", "number")};
const Game::Doc::Field kNumRet[] = {Req("numEntries", "number")};
const Game::Doc::Field kFoundRet[] = {Opt("found", "number", nullptr, "1, or nil.")};
const Game::Doc::Field kEntryRets[] = {
    Vararg("CombatLogEventInfo", "The same values as CombatLogGetCurrentEventInfo."),
};
const Game::Doc::Field kIsAArgs[] = {
    Req("flags", "number", "The COMBATLOG_OBJECT_* flags of a unit."),
    Req("mask", "number", "A COMBATLOG_OBJECT_* mask."),
};
const Game::Doc::Field kIsARet[] = {Opt("isA", "number", nullptr, "1, or nil.")};

constexpr const char *kSystem = "CombatLogGlobals";

const Game::Doc::Function kResetFilterDoc{"Removes all combat log filters.", {}, {}, kSystem};
const Game::Doc::Function kAddFilterDoc{"Adds a filter to the combat log.", kAddFilterArgs, {},
                                        kSystem};
const Game::Doc::Function kGetRetentionDoc{
    "Returns how long the combat log keeps its entries.", {}, kSecondsRet, kSystem};
const Game::Doc::Function kSetRetentionDoc{"Sets how long the combat log keeps its entries.",
                                           kSecondsArg, {}, kSystem};
const Game::Doc::Function kNumEntriesDoc{"Returns the number of kept entries that match the filter.",
                                         kIgnoreFilterArg, kNumRet, kSystem};
const Game::Doc::Function kSetCurrentDoc{"Selects a kept combat log entry.", kSetCurrentArgs,
                                         kFoundRet, kSystem};
const Game::Doc::Function kGetCurrentDoc{"Returns the values of the selected combat log entry.",
                                         {}, kEntryRets, kSystem};
const Game::Doc::Function kAdvanceDoc{"Moves the selection to a later or earlier entry.",
                                      kAdvanceArgs, kFoundRet, kSystem};
const Game::Doc::Function kClearEntriesDoc{"Removes all kept combat log entries.", {}, {},
                                           kSystem};
const Game::Doc::Function kIsADoc{"Returns whether a unit's flags match a mask.", kIsAArgs,
                                  kIsARet, kSystem};

void RegisterLuaFunctions() {
    g_retention = CVar::Factory::Register("combatLogRetentionTime", "300");
    using Game::Lua::RegisterGlobalFunction;
    RegisterGlobalFunction("CombatLogResetFilter", &Script_CombatLogResetFilter, &kResetFilterDoc);
    RegisterGlobalFunction("CombatLogAddFilter", &Script_CombatLogAddFilter, &kAddFilterDoc);
    RegisterGlobalFunction("CombatLogSetRetentionTime", &Script_CombatLogSetRetentionTime,
                           &kSetRetentionDoc);
    RegisterGlobalFunction("CombatLogGetRetentionTime", &Script_CombatLogGetRetentionTime,
                           &kGetRetentionDoc);
    RegisterGlobalFunction("CombatLogGetNumEntries", &Script_CombatLogGetNumEntries,
                           &kNumEntriesDoc);
    RegisterGlobalFunction("CombatLogSetCurrentEntry", &Script_CombatLogSetCurrentEntry,
                           &kSetCurrentDoc);
    RegisterGlobalFunction("CombatLogGetCurrentEntry", &Script_CombatLogGetCurrentEntry,
                           &kGetCurrentDoc);
    RegisterGlobalFunction("CombatLogAdvanceEntry", &Script_CombatLogAdvanceEntry, &kAdvanceDoc);
    RegisterGlobalFunction("CombatLogClearEntries", &Script_CombatLogClearEntries,
                           &kClearEntriesDoc);
    RegisterGlobalFunction("CombatLog_Object_IsA", &Script_CombatLog_Object_IsA, &kIsADoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// UnitCombatLogReset: the glue pass runs on each return to the login screen.
void Reset() {
    g_entries.clear();
    g_current = -1;
    g_filters.clear();
}

const Game::GlueModuleAutoRegister _gluereg{&Reset};

} // namespace

void Record(const Entry &entry) {
    if (!g_entries.empty() &&
        static_cast<int32_t>(entry.timeMs - 1000u * static_cast<uint32_t>(Retention()) -
                             g_entries.front().timeMs) >= 0)
        DropOldest();
    g_entries.push_back(entry);
}

bool MatchesFilter(const Entry &entry) {
    if (g_filters.empty())
        return true;
    for (const Filter &f : g_filters) {
        if (Matches(f, entry))
            return true;
    }
    return false;
}

} // namespace CombatLog::History
