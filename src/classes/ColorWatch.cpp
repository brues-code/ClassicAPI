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

// Keeps the color globals ColorMixin objects, whoever writes to them: every
// standard color (`UI::ColorData`, NORMAL_FONT_COLOR and friends),
// ITEM_QUALITY_COLORS and RAID_CLASS_COLORS.
//
// FrameXML defines some of them as plain {r, g, b} tables; the addon gives
// them ColorMixin (`!!!ClassicAPI/Util/Color.lua`), and readers call methods
// on them (`RAID_CLASS_COLORS[class]:GetRGB()`). Addons replace them with
// their own plain copies, from file scope, from any event, or later still.
// Repairing on a list of events always left a window after the last one, and
// the next `:GetRGB()` in it errored (issue #150, twice).
//
// So this watches the writes themselves, on the engine's store
// (Table::Settable):
//   - `NAME = x` into the globals table, for each watched name, and
//   - `NAME[k] = x` into the table ITEM_QUALITY_COLORS or RAID_CLASS_COLORS
//     holds now,
// and after either one calls `CAPI_RepairColorGlobal(NAME)` in the addon, then,
// for RAID_CLASS_COLORS, refreshes Classes::ColorCache. The
// call goes out from inside the writer's assignment, the way a __newindex
// handler's would, so the next statement already sees ColorMixin colors.
// Before the addon has defined the function (FrameXML's own assignments) there
// is nothing to call; its load-time pass covers those.
//
// Writes the engine store never sees are not caught: rawset, and a change
// inside a color (`RAID_CLASS_COLORS.MAGE.r = 1` keeps the methods; so does
// `ITEM_QUALITY_COLORS[4].color = x`, one level further down). A write into an
// addon's own environment (setfenv) is not a write to the global, and nothing
// outside that addon reads it.

#include "Game.h"
#include "Offsets.h"
#include "classes/ColorCache.h"
#include "gc/Hooks.h"
#include "table/Border.h"
#include "table/Settable.h"
#include "ui/ColorData.h"

#include <ankerl/unordered_dense.h>

namespace Classes::ColorWatch {

namespace {

constexpr const char *kRepair = "CAPI_RepairColorGlobal";

// The globals whose entries are colors too: a write into the table one of
// them holds is watched as well.
struct ColorTable {
    const char *name;
    const void *table; // the table the global holds now, compared, never followed
    // Runs after the repair of any write to the global or into its table.
    void (*changed)(void *L);
};
ColorTable g_tables[] = {
    {"ITEM_QUALITY_COLORS", nullptr, nullptr},
    {"RAID_CLASS_COLORS", nullptr, &Classes::ColorCache::Refresh},
};

void Repaired(void *L, const ColorTable &watched) {
    if (watched.changed != nullptr)
        watched.changed(L);
}

// Per Lua state, set when the game state registers its functions. 5.0 interns
// every string, so a string key is one of the watched names exactly when its
// TString* is in `g_names`; a registry table keeps those strings alive.
const void *g_globals = nullptr;
ankerl::unordered_dense::map<const void *, const char *> g_names;

// The table the global `name` holds now (raw), or null.
const void *CurrentTable(void *L, const char *name) {
    Game::Lua::PushString(L, name);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    const void *table = Table::Border::TableAt(L, -1);
    Game::Lua::SetTop(L, -2);
    return table;
}

void Repair(void *L, const char *name) {
    Game::Lua::PushString(L, kRepair);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, -2);
        return;
    }
    Game::Lua::PushString(L, name);
    Game::Lua::Call(L, 1, 0);
}

void OnWrite(void *L, const Table::Settable::Write &write) {
    if (write.table == nullptr)
        return;
    for (const ColorTable &watched : g_tables) {
        if (write.table == watched.table) {
            Repair(L, watched.name);
            Repaired(L, watched);
            return;
        }
    }
    if (write.table != g_globals || write.keyType != Game::Lua::TYPE_STRING)
        return;
    const auto it = g_names.find(write.key);
    if (it == g_names.end())
        return;
    ColorTable *table = nullptr;
    for (ColorTable &watched : g_tables)
        if (watched.name == it->second) {
            watched.table = CurrentTable(L, watched.name);
            table = &watched;
        }
    Repair(L, it->second);
    if (table != nullptr)
        Repaired(L, *table);
}

// Interns `name`, keeps it in the anchor table at the top of the stack, and
// watches it.
void WatchName(void *L, const char *name) {
    Game::Lua::PushString(L, name);
    const void *string = Game::Read<const void *>(
        reinterpret_cast<Table::Border::IndexAcceptable_t>(Offsets::LUA_INDEX_ACCEPTABLE)(L, -1),
        Offsets::OFF_LUA_TVALUE_VALUE);
    Game::Lua::RawSetI(L, -2, static_cast<int>(g_names.size()) + 1);
    g_names.emplace(string, name);
}

void Watch() {
    void *L = Game::Lua::State();
    g_globals = Table::Border::TableAt(L, Game::Lua::GLOBALS_INDEX);
    Game::Lua::NewTable(L);
    for (ColorTable &watched : g_tables) {
        WatchName(L, watched.name);
        watched.table = CurrentTable(L, watched.name);
    }
    for (const auto &color : UI::ColorData::kColors)
        WatchName(L, color.baseTag);
    Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
}

void Forget() {
    g_globals = nullptr;
    g_names.clear();
    for (ColorTable &watched : g_tables)
        watched.table = nullptr;
}

// A table a global held can still be freed after a rawset replaced it; its
// address must not match the next table allocated there.
void OnFree(const void *object) {
    for (ColorTable &watched : g_tables)
        if (object == watched.table)
            watched.table = nullptr;
}

const Table::Settable::AutoSubscribe _write{&OnWrite};
const Gc::Hooks::AutoSubscribe _gc{nullptr, &OnFree};
const Game::ModuleAutoRegister _autoreg{&Watch};
const Game::ReloadAutoRegister _reload{&Forget};
const Game::GlueBootAutoRegister _glueBoot{&Forget};

} // namespace

} // namespace Classes::ColorWatch
