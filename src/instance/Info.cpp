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

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"

#include <cstdint>

namespace Instance::Info {

namespace {

// Map.dbc instance-type enum, indexable from 0..4. Vanilla never uses
// slot 4 (arena), but the modern API string set includes it.
const char *const kTypeNames[] = {
    "none",   // 0 — open world
    "party",  // 1 — 5-man dungeon
    "raid",   // 2 — raid
    "pvp",    // 3 — battleground
    "arena",  // 4 — unused in vanilla
};

// Per-type player cap. Vanilla has no per-map cap stored client-side
// (MapDifficulty.dbc was a TBC addition), so we return the type's
// canonical max. ZG / AQ20 / WSG / AB return 40 instead of their true
// caps; same answer for any custom raid on private servers. Addons
// that need exact caps must supply their own table.
int CapForType(uint32_t type) {
    switch (type) {
        case 1: return 5;
        case 2: return 40;
        case 3: return 40;
        case 4: return 5;
        default: return 0;
    }
}

const uint8_t *MapRecord(uint32_t mapID) {
    return DBC::Record(Offsets::VAR_MAP_RECORDS, Offsets::VAR_MAP_COUNT, mapID);
}

} // namespace

// `GetInstanceInfo()` — returns the modern 9-tuple shape, with vanilla
// degeneracies for fields the 1.12 client doesn't track:
//   name, type, difficultyID, difficultyName, maxPlayers,
//   dynamicDifficulty, isDynamic, instanceID, instanceGroupSize
//
// - `name` / `type` come from Map.dbc.
// - `difficultyID` / `difficultyName` are always `1` / `"Normal"` (no
//   heroic mode pre-TBC; MapDifficulty.dbc doesn't exist).
// - `maxPlayers` is the type's canonical cap (see `CapForType`).
// - `dynamicDifficulty` is always `0` and `isDynamic` is always `false`
//   (dynamic difficulty was a Cataclysm-era addition).
// - `instanceID` is the current Map.dbc ID. (Modern docs call this
//   "instanceID" but it's really the mapID — both vanilla and modern
//   put the same value here.)
// - `instanceGroupSize` mirrors `maxPlayers` (no per-group-config
//   variants in vanilla).
static int __fastcall Script_GetInstanceInfo(void *L) {
    const uint32_t mapID = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_MAP_ID));

    const uint8_t *rec = MapRecord(mapID);
    if (rec == nullptr) {
        Game::Lua::PushString(L, "");
        Game::Lua::PushString(L, "none");
        Game::Lua::PushNumber(L, 1.0);
        Game::Lua::PushString(L, "Normal");
        Game::Lua::PushNumber(L, 0.0);
        Game::Lua::PushNumber(L, 0.0);
        Game::Lua::PushBool(L, false);
        Game::Lua::PushNumber(L, static_cast<double>(mapID));
        Game::Lua::PushNumber(L, 0.0);
        return 9;
    }

    const uint32_t type = *reinterpret_cast<const uint32_t *>(
        rec + Offsets::OFF_MAP_INSTANCE_TYPE);
    const uint32_t locale = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    const char *name = *reinterpret_cast<const char *const *>(
        rec + Offsets::OFF_MAP_NAME + locale * 4);

    const char *typeName =
        (type < sizeof(kTypeNames) / sizeof(kTypeNames[0])) ? kTypeNames[type]
                                                            : "none";
    const int cap = CapForType(type);

    Game::Lua::PushString(L, name != nullptr ? name : "");
    Game::Lua::PushString(L, typeName);
    Game::Lua::PushNumber(L, 1.0);
    Game::Lua::PushString(L, "Normal");
    Game::Lua::PushNumber(L, static_cast<double>(cap));
    Game::Lua::PushNumber(L, 0.0);
    Game::Lua::PushBool(L, false);
    Game::Lua::PushNumber(L, static_cast<double>(mapID));
    Game::Lua::PushNumber(L, static_cast<double>(cap));
    return 9;
}

namespace {

const Game::Doc::Field kGetInstanceInfoRets[] = {
    Game::Doc::Req("name", "cstring", "The name of the current map."),
    Game::Doc::Req("instanceType", "cstring",
                   "\"none\", \"party\", \"raid\", \"pvp\" or \"arena\"."),
    Game::Doc::Req("difficultyID", "number", "Always 1."),
    Game::Doc::Req("difficultyName", "cstring", "Always \"Normal\"."),
    Game::Doc::Req("maxPlayers", "number",
                   "5 for a dungeon, 40 for a raid or battleground, 0 in the open world; not "
                   "the instance's own limit."),
    Game::Doc::Req("dynamicDifficulty", "number", "Always 0."),
    Game::Doc::Req("isDynamic", "bool", "Always false."),
    Game::Doc::Req("instanceID", "number", "The map ID."),
    Game::Doc::Req("instanceGroupSize", "number", "Same as maxPlayers."),
};
const Game::Doc::Function kGetInstanceInfo{"Returns information about the current map.", {},
                                           kGetInstanceInfoRets, "InstanceGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInstanceInfo",
                                      &Script_GetInstanceInfo, &kGetInstanceInfo);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Instance::Info
