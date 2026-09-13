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

// API documentation: the record of every Lua registration plus its
// descriptor, exported on demand as Blizzard `APIDocumentation` tables.
//
//   C_APIDocumentation.GetSystems()      -> sorted array of system Names
//   C_APIDocumentation.GetSystem(name)   -> one System table (Blizzard shape), or nil
//   _classicapi_UndocumentedAPI()        -> array of registrations lacking a
//                                           descriptor, and its count
//
// The registrars in Game.cpp call `Record*` (see Documentation.h). Recording
// runs once per Lua state — the first in-game pass and the first glue pass —
// because the set of registrations is static: every module registers the
// same names on every `/reload`. The recorded list is never materialized
// into Lua wholesale; `GetSystem` builds ONE system's table per call, which
// is how Blizzard's browser loads too (its generated data addon is
// LoadOnDemand). Eager export would create thousands of tables on every
// `/reload` for a feature used from a slash command.
//
// System grouping is derived, never authored, except for two strings:
//   - `C_Spell.X`            -> System "Spell", Namespace "C_Spell" (strip `C_`)
//   - `table.insert`         -> System "table", Namespace "table"
//   - a global               -> `doc->system` ("SpellGlobals"); a global whose
//                               descriptor names no system has no home and is
//                               listed by the coverage function
//   - a frame method         -> Blizzard's widget system for its registry
//                               ("SimpleTextureAPI", Type "ScriptObject")
//   - an enum / structure /
//     event                  -> the System named in its declaration
// A system's `Environment` is "All" when its functions were recorded in both
// Lua states, else "Game" or "Glue"; a function whose own environment is
// narrower than its system's gets a Documentation line saying so.

#include "api/Documentation.h"

#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"
#include "event/Custom.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Api::Documentation {

using Game::Doc::Field;
using Game::Doc::FieldList;
using Game::Doc::Function;
using Game::Doc::Method;
using Game::Doc::Structure;
// NOT `using Game::Doc::Event` — that name would shadow the global `Event`
// namespace for the whole file, so `Event::Custom::…` would resolve to the
// type. Same trap as `Time::Tick` vs the `Tick` namespace (see CLAUDE.md).
using EventDoc = Game::Doc::Event;

// Head of the self-registering `Structure` list. Constant-initialized, so it
// is valid before any `Structure` constructor runs, whatever the cross-TU
// static-init order.
const Structure *g_structureHead = nullptr;

namespace {

enum Kind : uint8_t {
    KIND_GLOBAL,           // RegisterGlobalFunction / RegisterGlueFunction / RegisterGlobalAlias
    KIND_TABLE,            // RegisterTableFunction
    KIND_FRAME_METHOD,     // RegisterFrameMethods, one per entry
    KIND_ENUM,             // RegisterIntegerEnum
    KIND_ORPHAN_METHOD_DOC // a Doc::Method naming no entry in its table
};

struct Entry {
    Kind kind;
    unsigned env;          // Env bits the registration was recorded in
    const char *table;     // KIND_TABLE: the namespace; KIND_ENUM: the parent ("Enum")
    const char *name;      // registered name / method name / enum sub-name
    const void *registry;  // frame methods only
    const Function *doc;   // nullptr = undocumented
    const char *enumSystem;
    const Game::Lua::EnumIntegerEntry *enumEntries;
    int enumCount;
};

std::vector<Entry> g_entries;
Env g_pass = ENV_NONE;
unsigned g_sealed = 0;
int g_recordedThisPass = 0;

constexpr const char *kDebugPrefix = "_classicapi_";

bool IsDebugName(const char *name) {
    return std::strncmp(name, kDebugPrefix, std::strlen(kDebugPrefix)) == 0;
}

bool SameText(const char *a, const char *b) {
    if (a == nullptr || b == nullptr)
        return a == b;
    return std::strcmp(a, b) == 0;
}

bool SameTextNoCase(const char *a, const char *b) {
    if (a == nullptr || b == nullptr)
        return a == b;
    return _stricmp(a, b) == 0;
}

Entry *Find(Kind kind, const char *table, const char *name, const void *registry) {
    for (Entry &e : g_entries) {
        if (e.kind == kind && e.registry == registry && SameText(e.table, table) &&
            SameText(e.name, name))
            return &e;
    }
    return nullptr;
}

void Record(Kind kind, const char *table, const char *name, const void *registry,
            const Function *doc) {
    if (g_pass == ENV_NONE || name == nullptr)
        return;
    ++g_recordedThisPass;
    if (Entry *e = Find(kind, table, name, registry)) {
        e->env |= g_pass;
        if (e->doc == nullptr)
            e->doc = doc;
        return;
    }
    Entry e{};
    e.kind = kind;
    e.env = g_pass;
    e.table = table;
    e.name = name;
    e.registry = registry;
    e.doc = doc;
    g_entries.push_back(e);
}

// ---- System resolution ------------------------------------------------------

// Blizzard's names for the widget-method systems, keyed by the engine
// registry each `FrameMethodEntry[]` is bound to.
struct WidgetSystem {
    uintptr_t registry;
    const char *system;
};
const WidgetSystem kWidgetSystems[] = {
    {Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY, "GameTooltipAPI"},
    {Offsets::VAR_REGION_METHOD_REGISTRY, "ScriptRegionAPI"},
    {Offsets::VAR_FRAME_METHOD_REGISTRY, "SimpleFrameAPI"},
    {Offsets::VAR_TEXTURE_METHOD_REGISTRY, "SimpleTextureAPI"},
    {Offsets::VAR_FONTSTRING_METHOD_REGISTRY, "SimpleFontStringAPI"},
    {Offsets::VAR_EDITBOX_METHOD_REGISTRY, "SimpleEditBoxAPI"},
    {Offsets::VAR_MODEL_METHOD_REGISTRY, "SimpleModelAPI"},
};

const char *WidgetSystemFor(const void *registry) {
    for (const WidgetSystem &w : kWidgetSystems)
        if (reinterpret_cast<const void *>(w.registry) == registry)
            return w.system;
    return nullptr;
}

// "C_Spell" -> "Spell"; anything else is its own system name.
const char *SystemNameForTable(const char *table) {
    if (table[0] == 'C' && table[1] == '_' && table[2] != '\0')
        return table + 2;
    return table;
}

// The System an entry contributes a function to, or nullptr when it has
// none (an undocumented global, a frame method on an unmapped registry).
const char *SystemOf(const Entry &e) {
    switch (e.kind) {
    case KIND_TABLE:
        return SystemNameForTable(e.table);
    case KIND_GLOBAL:
        return e.doc != nullptr ? e.doc->system : nullptr;
    case KIND_FRAME_METHOD:
        return WidgetSystemFor(e.registry);
    case KIND_ENUM:
        return e.enumSystem;
    default:
        return nullptr;
    }
}

struct SystemInfo {
    const char *name;
    const char *ns;        // nullptr for global and widget systems
    const char *type;      // "System" or "ScriptObject"
    unsigned env;          // Env bits; 0 when nothing recorded (structures only)
};

// Merge one entry's contribution into the system list.
void AddSystem(std::vector<SystemInfo> &systems, const char *name, const char *ns,
               const char *type, unsigned env) {
    if (name == nullptr)
        return;
    for (SystemInfo &s : systems) {
        if (SameTextNoCase(s.name, name)) {
            if (s.ns == nullptr)
                s.ns = ns;
            if (type != nullptr)
                s.type = type;
            s.env |= env;
            return;
        }
    }
    systems.push_back(SystemInfo{name, ns, type != nullptr ? type : "System", env});
}

std::vector<SystemInfo> CollectSystems() {
    std::vector<SystemInfo> systems;
    for (const Entry &e : g_entries) {
        switch (e.kind) {
        case KIND_TABLE:
            AddSystem(systems, SystemNameForTable(e.table), e.table, "System", e.env);
            break;
        case KIND_GLOBAL:
            AddSystem(systems, SystemOf(e), nullptr, "System", e.env);
            break;
        case KIND_FRAME_METHOD:
            AddSystem(systems, SystemOf(e), nullptr, "ScriptObject", e.env);
            break;
        case KIND_ENUM:
            AddSystem(systems, e.enumSystem, nullptr, nullptr, 0);
            break;
        default:
            break;
        }
    }
    for (const Structure *s = g_structureHead; s != nullptr; s = s->next)
        AddSystem(systems, s->system, nullptr, nullptr, 0);
    const int events = Event::Custom::ReservedCount();
    for (int i = 0; i < events; ++i) {
        const EventDoc *doc = Event::Custom::ReservedDoc(i);
        if (doc != nullptr)
            AddSystem(systems, doc->system, nullptr, nullptr, 0);
    }
    return systems;
}

const SystemInfo *FindSystem(const std::vector<SystemInfo> &systems, const char *query) {
    for (const SystemInfo &s : systems)
        if (SameTextNoCase(s.name, query) || SameTextNoCase(s.ns, query))
            return &s;
    return nullptr;
}

const char *EnvironmentName(unsigned env) {
    if (env == (ENV_GAME | ENV_GLUE) || env == 0)
        return "All";
    return env == ENV_GLUE ? "Glue" : "Game";
}

// ---- Export builders (each leaves exactly what its comment says) -------------

namespace Lua = Game::Lua;

// Sets `Documentation = { line1 [, line2 [, line3]] }` on the table at
// stack[-1]; no-op when every line is null.
void SetDocumentation(void *L, const char *l1, const char *l2 = nullptr,
                      const char *l3 = nullptr) {
    if (l1 == nullptr && l2 == nullptr && l3 == nullptr)
        return;
    Lua::PushString(L, "Documentation");
    Lua::NewTable(L);
    int n = 0;
    for (const char *line : {l1, l2, l3}) {
        if (line == nullptr)
            continue;
        Lua::PushNumber(L, static_cast<double>(++n));
        Lua::PushString(L, line);
        Lua::SetTable(L, -3);
    }
    Lua::SetTable(L, -3);
}

// A Lua literal from a descriptor's `defaultValue`, pushed as the value it
// denotes: `true`/`false` -> boolean, a quoted string -> string, else number.
void PushDefault(void *L, const char *literal) {
    if (std::strcmp(literal, "true") == 0) {
        Lua::PushBool(L, true);
    } else if (std::strcmp(literal, "false") == 0) {
        Lua::PushBool(L, false);
    } else if (literal[0] == '"') {
        const size_t len = std::strlen(literal);
        Lua::PushLString(L, literal + 1, static_cast<unsigned>(len >= 2 ? len - 2 : 0));
    } else {
        Lua::PushNumber(L, std::strtod(literal, nullptr));
    }
}

// Pushes one field record: { Name, Type, Nilable [, Default] [, Documentation] }.
void PushField(void *L, const Field &f) {
    Lua::NewTable(L);
    Lua::SetFieldString(L, "Name", f.name);
    Lua::SetFieldString(L, "Type", f.type);
    Lua::SetFieldBool(L, "Nilable", f.nilable);
    if (f.defaultValue != nullptr) {
        Lua::PushString(L, "Default");
        PushDefault(L, f.defaultValue);
        Lua::SetTable(L, -3);
    }
    SetDocumentation(L, f.doc);
}

// Sets `stack[-1][key] = { field records }`; no-op for an empty list.
void SetFieldArray(void *L, const char *key, FieldList list) {
    if (list.count <= 0)
        return;
    Lua::PushString(L, key);
    Lua::NewTable(L);
    for (int i = 0; i < list.count; ++i) {
        Lua::PushNumber(L, static_cast<double>(i + 1));
        PushField(L, list.data[i]);
        Lua::SetTable(L, -3);
    }
    Lua::SetTable(L, -3);
}

constexpr const char *kExtensionNote = "ClassicAPI extension - not part of the Blizzard API.";
constexpr const char *kGlueOnlyNote = "Available only on the glue screen.";
constexpr const char *kGameOnlyNote = "Available only in-game.";
// An undocumented function lists with no arguments, which is indistinguishable
// from a function that genuinely takes none. Say which it is, so an empty
// signature is never read as a real one.
constexpr const char *kUndocumentedNote =
    "Not documented yet. Its arguments and return values are not listed here.";

// Pushes one function record. An undocumented entry still lists by name so
// `/classicapi <System> list` shows the whole surface; its shape arrives with
// the descriptor.
void PushFunction(void *L, const Entry &e, unsigned systemEnv) {
    Lua::NewTable(L);
    Lua::SetFieldString(L, "Name", e.name);
    Lua::SetFieldString(L, "Type", "Function");
    const char *envNote = nullptr;
    if (e.env != systemEnv && e.env != 0)
        envNote = (e.env == ENV_GLUE) ? kGlueOnlyNote : kGameOnlyNote;
    if (e.doc == nullptr) {
        SetDocumentation(L, kUndocumentedNote, envNote);
        return;
    }
    SetDocumentation(L, e.doc->summary, e.doc->extension ? kExtensionNote : nullptr, envNote);
    SetFieldArray(L, "Arguments", e.doc->args);
    SetFieldArray(L, "Returns", e.doc->rets);
}

// Pushes { Name, Type = "Enumeration", NumValues, MinValue, MaxValue,
//          Fields = { { Name, Type = <enum>, EnumValue } } }.
void PushEnumeration(void *L, const Entry &e) {
    int minValue = 0, maxValue = 0;
    for (int i = 0; i < e.enumCount; ++i) {
        const int v = e.enumEntries[i].value;
        if (i == 0 || v < minValue)
            minValue = v;
        if (i == 0 || v > maxValue)
            maxValue = v;
    }
    Lua::NewTable(L);
    Lua::SetFieldString(L, "Name", e.name);
    Lua::SetFieldString(L, "Type", "Enumeration");
    Lua::SetFieldNumber(L, "NumValues", e.enumCount);
    Lua::SetFieldNumber(L, "MinValue", minValue);
    Lua::SetFieldNumber(L, "MaxValue", maxValue);
    Lua::PushString(L, "Fields");
    Lua::NewTable(L);
    for (int i = 0; i < e.enumCount; ++i) {
        Lua::PushNumber(L, static_cast<double>(i + 1));
        Lua::NewTable(L);
        Lua::SetFieldString(L, "Name", e.enumEntries[i].key);
        Lua::SetFieldString(L, "Type", e.name);
        Lua::SetFieldNumber(L, "EnumValue", e.enumEntries[i].value);
        Lua::SetTable(L, -3);
    }
    Lua::SetTable(L, -3);
}

// Pushes { Name, Type = "Structure" [, Documentation], Fields = { … } }.
void PushStructure(void *L, const Structure &s) {
    Lua::NewTable(L);
    Lua::SetFieldString(L, "Name", s.name);
    Lua::SetFieldString(L, "Type", "Structure");
    SetDocumentation(L, s.summary);
    SetFieldArray(L, "Fields", s.fields);
}

// "LOSS_OF_CONTROL_ADDED" -> "LossOfControlAdded" (Blizzard's event Name).
void PascalCase(const char *literal, char *out, size_t cap) {
    size_t o = 0;
    bool upper = true;
    for (const char *p = literal; *p != '\0' && o + 1 < cap; ++p) {
        if (*p == '_') {
            upper = true;
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(*p);
        out[o++] = static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
        upper = false;
    }
    out[o] = '\0';
}

// Pushes { Name, Type = "Event", LiteralName [, Documentation] [, Payload] }.
void PushEvent(void *L, const char *literal, const EventDoc &doc) {
    char name[128];
    PascalCase(literal, name, sizeof name);
    Lua::NewTable(L);
    Lua::SetFieldString(L, "Name", name);
    Lua::SetFieldString(L, "Type", "Event");
    Lua::SetFieldString(L, "LiteralName", literal);
    SetDocumentation(L, doc.summary);
    SetFieldArray(L, "Payload", doc.payload);
}

// Pushes the whole System table for `sys`.
void PushSystem(void *L, const SystemInfo &sys) {
    Lua::NewTable(L);
    Lua::SetFieldString(L, "Name", sys.name);
    Lua::SetFieldString(L, "Type", sys.type);
    if (sys.ns != nullptr)
        Lua::SetFieldString(L, "Namespace", sys.ns);
    Lua::SetFieldString(L, "Environment", EnvironmentName(sys.env));

    Lua::PushString(L, "Functions");
    Lua::NewTable(L);
    int n = 0;
    for (const Entry &e : g_entries) {
        if (e.kind != KIND_GLOBAL && e.kind != KIND_TABLE && e.kind != KIND_FRAME_METHOD)
            continue;
        if (!SameTextNoCase(SystemOf(e), sys.name) || IsDebugName(e.name))
            continue;
        Lua::PushNumber(L, static_cast<double>(++n));
        PushFunction(L, e, sys.env);
        Lua::SetTable(L, -3);
    }
    Lua::SetTable(L, -3);

    Lua::PushString(L, "Events");
    Lua::NewTable(L);
    n = 0;
    const int events = Event::Custom::ReservedCount();
    for (int i = 0; i < events; ++i) {
        const EventDoc *doc = Event::Custom::ReservedDoc(i);
        if (doc == nullptr || !SameTextNoCase(doc->system, sys.name))
            continue;
        Lua::PushNumber(L, static_cast<double>(++n));
        PushEvent(L, Event::Custom::ReservedName(i), *doc);
        Lua::SetTable(L, -3);
    }
    Lua::SetTable(L, -3);

    Lua::PushString(L, "Tables");
    Lua::NewTable(L);
    n = 0;
    for (const Structure *s = g_structureHead; s != nullptr; s = s->next) {
        if (!SameTextNoCase(s->system, sys.name))
            continue;
        Lua::PushNumber(L, static_cast<double>(++n));
        PushStructure(L, *s);
        Lua::SetTable(L, -3);
    }
    for (const Entry &e : g_entries) {
        if (e.kind != KIND_ENUM || !SameTextNoCase(e.enumSystem, sys.name))
            continue;
        Lua::PushNumber(L, static_cast<double>(++n));
        PushEnumeration(L, e);
        Lua::SetTable(L, -3);
    }
    Lua::SetTable(L, -3);
}

// ---- Coverage ---------------------------------------------------------------

std::vector<std::string> Undocumented() {
    std::vector<std::string> out;
    const std::vector<SystemInfo> systems = CollectSystems();
    for (const Entry &e : g_entries) {
        if (IsDebugName(e.name))
            continue;
        switch (e.kind) {
        case KIND_GLOBAL:
            if (e.doc == nullptr) {
                out.push_back(e.name);
            } else if (e.doc->system == nullptr) {
                out.push_back(std::string(e.name) + " (doc has no system)");
            } else if (const SystemInfo *s = FindSystem(systems, e.doc->system);
                       s != nullptr && s->ns != nullptr) {
                out.push_back(std::string(e.name) + " (system '" + e.doc->system +
                              "' is namespaced)");
            }
            break;
        case KIND_TABLE:
            if (e.doc == nullptr)
                out.push_back(std::string(e.table) + "." + e.name);
            break;
        case KIND_FRAME_METHOD: {
            const char *sys = WidgetSystemFor(e.registry);
            if (sys == nullptr)
                out.push_back(std::string(e.name) + " (method registry not mapped)");
            else if (e.doc == nullptr)
                out.push_back(std::string(sys) + ":" + e.name);
            break;
        }
        case KIND_ORPHAN_METHOD_DOC: {
            const char *sys = WidgetSystemFor(e.registry);
            out.push_back(std::string(sys != nullptr ? sys : "?") + ":" + e.name +
                          " (doc for unregistered method)");
            break;
        }
        case KIND_ENUM:
            if (e.enumSystem == nullptr)
                out.push_back(std::string(e.table) + "." + e.name);
            break;
        }
    }
    const int events = Event::Custom::ReservedCount();
    for (int i = 0; i < events; ++i)
        if (Event::Custom::ReservedDoc(i) == nullptr)
            out.push_back(std::string("event ") + Event::Custom::ReservedName(i));
    return out;
}

// ---- Lua surface ------------------------------------------------------------

int __fastcall Script_GetSystems(void *L) {
    std::vector<SystemInfo> systems = CollectSystems();
    std::sort(systems.begin(), systems.end(), [](const SystemInfo &a, const SystemInfo &b) {
        return _stricmp(a.name, b.name) < 0;
    });
    Lua::NewTable(L);
    for (size_t i = 0; i < systems.size(); ++i) {
        Lua::PushNumber(L, static_cast<double>(i + 1));
        Lua::PushString(L, systems[i].name);
        Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_GetSystem(void *L) {
    if (!Lua::IsString(L, 1)) {
        Lua::Error(L, "Usage: C_APIDocumentation.GetSystem(name)");
        return 0;
    }
    const char *query = Lua::ToString(L, 1);
    const std::vector<SystemInfo> systems = CollectSystems();
    const SystemInfo *sys = FindSystem(systems, query);
    if (sys == nullptr)
        return 0; // nil
    Lua::CheckStack(L, 32);
    PushSystem(L, *sys);
    return 1;
}

int __fastcall Script_UndocumentedAPI(void *L) {
    const std::vector<std::string> names = Undocumented();
    Lua::NewTable(L);
    for (size_t i = 0; i < names.size(); ++i) {
        Lua::PushNumber(L, static_cast<double>(i + 1));
        Lua::PushString(L, names[i].c_str());
        Lua::SetTable(L, -3);
    }
    Lua::PushNumber(L, static_cast<double>(names.size()));
    return 2;
}

const Field kGetSystemsRets[] = {
    Game::Doc::Req("systems", "table", "Array of system Names, sorted without regard to case."),
};
const Function kGetSystems{
    "Returns the name of every documented system.",
    {}, kGetSystemsRets, nullptr, true};

const Field kGetSystemArgs[] = {
    Game::Doc::Req("system", "string", "A system Name or Namespace; case does not matter."),
};
const Field kGetSystemRets[] = {
    Game::Doc::Opt("system", "table", nullptr,
                   "The system in Blizzard's APIDocumentation table shape, or nil when unknown."),
};
const Function kGetSystem{
    "Returns one system's documentation table.",
    kGetSystemArgs, kGetSystemRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_APIDocumentation", "GetSystems", &Script_GetSystems,
                                     &kGetSystems);
    Game::Lua::RegisterTableFunction("C_APIDocumentation", "GetSystem", &Script_GetSystem,
                                     &kGetSystem);
    Game::Lua::RegisterGlobalFunction("_classicapi_UndocumentedAPI", &Script_UndocumentedAPI);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// ---- Recording (called from Game.cpp) ----------------------------------------

void BeginPass(Env env) {
    g_pass = (g_sealed & env) != 0 ? ENV_NONE : env;
    g_recordedThisPass = 0;
}

void EndPass() {
    if (g_pass == ENV_NONE)
        return;
    g_sealed |= g_pass;
    const Env done = g_pass;
    g_pass = ENV_NONE;
    const std::vector<std::string> missing = Undocumented();
    Debug::Log::Printf("[doc] %s pass: %d registrations recorded, %d undocumented",
                       done == ENV_GLUE ? "glue" : "game", g_recordedThisPass,
                       static_cast<int>(missing.size()));
}

void RecordGlobal(const char *name, const Function *doc) {
    Record(KIND_GLOBAL, nullptr, name, nullptr, doc);
}

void RecordTable(const char *table, const char *name, const Function *doc) {
    Record(KIND_TABLE, table, name, nullptr, doc);
}

void RecordFrameMethods(const void *registry, const Game::Lua::FrameMethodEntry *table,
                        int count, const Method *docs, int docCount) {
    if (g_pass == ENV_NONE)
        return;
    for (int i = 0; i < count; ++i) {
        const Function *doc = nullptr;
        for (int d = 0; d < docCount; ++d) {
            if (std::strcmp(docs[d].name, table[i].name) == 0) {
                doc = docs[d].doc;
                break;
            }
        }
        Record(KIND_FRAME_METHOD, nullptr, table[i].name, registry, doc);
    }
    for (int d = 0; d < docCount; ++d) {
        bool matched = false;
        for (int i = 0; i < count && !matched; ++i)
            matched = std::strcmp(docs[d].name, table[i].name) == 0;
        if (!matched)
            Record(KIND_ORPHAN_METHOD_DOC, nullptr, docs[d].name, registry, docs[d].doc);
    }
}

void RecordEnum(const char *parent, const char *sub, const Game::Lua::EnumIntegerEntry *entries,
                int count, const char *system) {
    if (g_pass == ENV_NONE || sub == nullptr)
        return;
    ++g_recordedThisPass;
    if (Entry *e = Find(KIND_ENUM, parent, sub, nullptr)) {
        e->env |= g_pass;
        if (e->enumSystem == nullptr)
            e->enumSystem = system;
        return;
    }
    Entry e{};
    e.kind = KIND_ENUM;
    e.env = g_pass;
    e.table = parent;
    e.name = sub;
    e.enumSystem = system;
    e.enumEntries = entries;
    e.enumCount = count;
    g_entries.push_back(e);
}

} // namespace Api::Documentation

// The Structure constructor lives here so the list head is private to this
// TU; declared in Game.h beside the other descriptor types.
Game::Doc::Structure::Structure(const char *n, const char *s, FieldList f, const char *sum)
    : name(n), system(s), fields(f), summary(sum), next(Api::Documentation::g_structureHead) {
    Api::Documentation::g_structureHead = this;
}
