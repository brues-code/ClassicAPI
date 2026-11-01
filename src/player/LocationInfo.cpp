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

// The `PlayerLocation`-taking `C_PlayerInfo` accessors:
//
//   C_PlayerInfo.GetName(playerLocation)       -> name
//   C_PlayerInfo.GetClass(playerLocation)      -> className, classFilename, classID
//   C_PlayerInfo.GetRace(playerLocation)       -> raceID
//   C_PlayerInfo.GetSex(playerLocation)        -> sex          (1 neutral / 2 male / 3 female)
//   C_PlayerInfo.IsConnected([playerLocation]) -> isConnected
//
// A `PlayerLocation` (see Util/PlayerLocation.lua) is a plain table carrying
// either a unit token (`.unit`) or a GUID string (`.guid`). We resolve it to
// a 64-bit GUID — unit tokens through the engine's token→GUID table
// (`FUN_TOKEN_TO_GUID`, which resolves group members in other zones too, not
// just synced CGUnits), GUID strings by parsing — and then read everything
// straight from the engine's own data structures. No `Script_*` Lua wrappers
// are called; each value comes from the same engine internals the codebase
// already uses elsewhere:
//
//   - GetName    -> resolve the GUID to a CGObject and call the polymorphic
//                   name getter `FUN_OBJECT_GET_NAME` (same path as
//                   `UnitNameFromGUID`).
//   - GetRace /  -> UNIT_FIELD_BYTES_0: the login-time mirror globals when the
//     GetClass /    location is the local player (valid before the descriptor
//     GetSex        spawns, matching `Script_UnitClass`'s own player path),
//                   otherwise the resolved unit's descriptor bytes. GetClass
//                   maps classID -> className/classFile via `ChrClasses.dbc`
//                   like `C_CreatureInfo.GetClassInfo`. GetSex reports the
//                   API's 2/3 (raw gender byte 0/1 + 2), matching
//                   `GetPlayerInfoByGUID`.
//   - IsConnected -> a resolvable CGObject means connected; otherwise the
//                   group-member roster's online bit (then slot presence),
//                   exactly what `Script_UnitIsConnected` checks.
//
// Only the `unit` and `guid` PlayerLocation kinds resolve on 1.12 — the chat
// line / who / battlefield-score / community / voice kinds have no
// client-side unit resolution. Unsupported or unresolvable locations return
// nil.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Names.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Player::LocationInfo {

namespace {

using TokenToGuid_t = uint64_t(__fastcall *)(const char *token);
using GroupLookup_t = void *(__fastcall *)(const uint64_t *guid);

// Reads a string field `t[field]` at table stack index `locIdx` into `out`.
// Returns true only for a non-empty string. Stack-neutral.
bool ReadStringField(void *L, int locIdx, const char *field, char *out, int size) {
    Game::Lua::PushString(L, field);
    Game::Lua::GetTable(L, locIdx); // pops key, pushes value
    bool ok = false;
    if (Game::Lua::IsString(L, -1)) {
        const char *s = Game::Lua::ToString(L, -1);
        if (s != nullptr && s[0] != '\0') {
            int i = 0;
            for (; s[i] != '\0' && i < size - 1; ++i)
                out[i] = s[i];
            out[i] = '\0';
            ok = true;
        }
    }
    Game::Lua::SetTop(L, -2); // pop value (or nil)
    return ok;
}

// Resolves the PlayerLocation table at `locIdx` to a 64-bit GUID. Unit tokens
// go through the engine token→GUID table (handles out-of-zone group members);
// GUID strings are parsed. Returns false for unsupported kinds or a token
// that maps to nothing.
bool LocationGuid(void *L, int locIdx, uint64_t *out) {
    char token[64];
    if (ReadStringField(L, locIdx, "unit", token, sizeof(token))) {
        auto toGuid = reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID);
        *out = toGuid(token);
        return *out != 0;
    }
    char guidStr[40];
    if (ReadStringField(L, locIdx, "guid", guidStr, sizeof(guidStr)))
        return Guid::Parse(guidStr, out) && *out != 0;
    return false;
}

// Resolves a GUID to its currently-synced CGUnit, or null if not resident.
const uint8_t *ResolveUnit(uint64_t guid) {
    return static_cast<const uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "ClassicAPI", 0));
}

// Reads a UNIT_FIELD_BYTES_0 byte: the login-time mirror global when `guid` is
// the local player (works before the descriptor spawns), otherwise the
// resolved unit's descriptor byte. Returns -1 when the unit isn't resident.
int UnitByte(uint64_t guid, uintptr_t playerMirror, uint32_t descByteOffset) {
    if (guid == Unit::Identity::PlayerGuid())
        return *reinterpret_cast<const uint8_t *>(playerMirror);
    const uint8_t *unit = ResolveUnit(guid);
    if (unit == nullptr)
        return -1;
    auto *desc = *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_CGUNIT_OBJECT_FIELDS);
    if (desc == nullptr)
        return -1;
    return *reinterpret_cast<const uint8_t *>(desc + descByteOffset);
}

int __fastcall Script_GetName(void *L) {
    uint64_t guid = 0;
    if (!LocationGuid(L, 1, &guid))
        return 0;
    const uint8_t *unit = ResolveUnit(guid);
    if (unit == nullptr)
        return 0;
    const char *name = Unit::Identity::NameForUnit(unit);
    if (name == nullptr || name[0] == '\0')
        return 0;
    Game::Lua::PushString(L, name);
    return 1;
}

int __fastcall Script_GetRace(void *L) {
    uint64_t guid = 0;
    if (!LocationGuid(L, 1, &guid))
        return 0;
    const int raceID = UnitByte(guid, Offsets::VAR_PLAYER_RACE_BYTE,
                                Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
    if (raceID <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(raceID));
    return 1;
}

int __fastcall Script_GetClass(void *L) {
    uint64_t guid = 0;
    if (!LocationGuid(L, 1, &guid))
        return 0;
    const int classID = UnitByte(guid, Offsets::VAR_PLAYER_CLASS_BYTE,
                                 Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    if (classID <= 0)
        return 0;
    // className / classFile from ChrClasses.dbc — same reads as
    // C_CreatureInfo.GetClassInfo(classID).
    const char *classFile = DBC::ClassToken(static_cast<uint32_t>(classID));
    if (classFile == nullptr)
        return 0; // classID with no ChrClasses row
    const char *className = DBC::ClassName(static_cast<uint32_t>(classID));

    Game::Lua::PushString(L, className); // NULL → nil via lua_pushstring
    Game::Lua::PushString(L, classFile);
    Game::Lua::PushNumber(L, static_cast<double>(classID));
    return 3;
}

int __fastcall Script_GetSex(void *L) {
    uint64_t guid = 0;
    if (!LocationGuid(L, 1, &guid))
        return 0;
    const int gender = UnitByte(guid, Offsets::VAR_PLAYER_SEX_BYTE,
                                Offsets::OFF_UNIT_DESCRIPTOR_SEX_BYTE);
    if (gender < 0)
        return 0;
    // UNIT_FIELD_BYTES_0 stores 0 = male / 1 = female; the API (and UnitSex)
    // reports 2 = male / 3 = female. Same +2 remap as GetPlayerInfoByGUID.
    Game::Lua::PushNumber(L, static_cast<double>(gender + 2));
    return 1;
}

int __fastcall Script_IsConnected(void *L) {
    // `playerLocation` is optional — with no table arg it defaults to the
    // local player (always connected while playing).
    uint64_t guid = 0;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE) {
        if (!LocationGuid(L, 1, &guid))
            return 0;
    } else {
        guid = Unit::Identity::PlayerGuid();
    }
    if (guid == 0)
        return 0;

    bool connected = ResolveUnit(guid) != nullptr; // a synced object is online
    if (!connected) {
        // Not resident — consult the group-member roster, mirroring
        // Script_UnitIsConnected: the stats block's online bit, else slot
        // presence (a member with no stats block yet still counts as present).
        auto stats = reinterpret_cast<GroupLookup_t>(
            Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP)(&guid);
        if (stats != nullptr) {
            connected = (*reinterpret_cast<const uint8_t *>(
                             static_cast<const uint8_t *>(stats) +
                             Offsets::OFF_GROUP_MEMBER_STATUS_FLAGS) &
                         Offsets::GROUP_MEMBER_STATUS_ONLINE) != 0;
        } else {
            connected = reinterpret_cast<GroupLookup_t>(
                            Offsets::FUN_GROUP_MEMBER_SLOT_LOOKUP)(&guid) != nullptr;
        }
    }
    Game::Lua::PushBool(L, connected);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetName", &Script_GetName);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetClass", &Script_GetClass);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetRace", &Script_GetRace);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetSex", &Script_GetSex);
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "IsConnected", &Script_IsConnected);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Player::LocationInfo
