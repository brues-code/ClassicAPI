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

// `C_CVar.GetCVarBitfield(name, index)` / `C_CVar.SetCVarBitfield(name, index,
// value)` — treat a cvar's numeric value as a bitfield and read or write one
// bit of it, so a single cvar carries many independent booleans. Upstream this
// backs the record of which tutorial popups a player has dismissed: one cvar,
// one bit per popup, persisted by the config machinery for free.
//
// The index is 1-based. Blizzard types it `luaIndex` rather than `number`,
// which is what settles that.
//
// WIDTH. The value is parsed and rewritten as a uint64 in C++, never handed
// through a Lua number, so all 64 bits are exact — Lua 5.0's doubles are not in
// the path. The valid index range is 1..64 because that is the width of the
// type the arithmetic runs in, not a limit chosen here.
//
// Setting refuses, returning false, in the cases Blizzard's own documentation
// names as requirements: an unknown cvar, a read-only one, and an index out of
// range. Both of the first two are things this client can genuinely answer --
// the lookup is the same filtering one behind C_CVar.DoesCVarExist, so a cvar
// preserved out of Config.wtf without being implemented is refused here exactly
// as it is by every other Lua cvar function. Nothing here is ever secure, which
// is the remaining documented requirement.
//
// Writing goes through CVar::Factory::SetString rather than the engine setter
// directly: that wrapper already declares FUN_SET_CVAR_VALUE as the __thiscall
// it is, and getting that wrong corrupts the stack silently rather than
// crashing. It also means a bitfield write fires the cvar's change callback and
// marks the config dirty, exactly as SetCVar would.

#include "Factory.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdio>

namespace CVar::Bitfield {

namespace {

// The arithmetic type's own width, which is what bounds a valid index.
constexpr int kBitCount = 64;

// Parses the cvar's value as an unsigned base-10 integer. A value that is not a
// number reads as 0, which is how an unset bitfield behaves anyway: every bit
// clear. Stops at the first non-digit rather than rejecting, so a value with
// trailing text still yields its leading number.
uint64_t ValueOf(CVar::Factory::Handle cvar) {
    const char *s = CVar::Factory::GetString(cvar);
    if (s == nullptr)
        return 0;
    while (*s == ' ' || *s == '\t')
        ++s;
    uint64_t v = 0;
    for (; *s >= '0' && *s <= '9'; ++s) {
        const uint64_t digit = static_cast<uint64_t>(*s - '0');
        if (v > (UINT64_MAX - digit) / 10)
            return UINT64_MAX; // saturate rather than wrap
        v = v * 10 + digit;
    }
    return v;
}

// True when `index` names a bit this can address. 1-based.
bool IndexInRange(double index) {
    return index >= 1.0 && index <= static_cast<double>(kBitCount) &&
           index == static_cast<double>(static_cast<int>(index));
}

// `C_CVar.GetCVarBitfield(name, index) -> value`
//
// nil for an unknown cvar or an index out of range, matching the contract's
// nilable return, so "no such bit" stays distinguishable from "bit is false".
int __fastcall Script_GetCVarBitfield(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const double index = Game::Lua::ToNumber(L, 2);
    CVar::Factory::Handle cvar = CVar::Factory::Find(Game::Lua::ToString(L, 1));
    if (cvar == nullptr || !IndexInRange(index)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint64_t bit = uint64_t{1} << (static_cast<int>(index) - 1);
    Game::Lua::PushBool(L, (ValueOf(cvar) & bit) != 0);
    return 1;
}

// `C_CVar.SetCVarBitfield(name, index, value) -> success`
int __fastcall Script_SetCVarBitfield(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const double index = Game::Lua::ToNumber(L, 2);
    CVar::Factory::Handle cvar = CVar::Factory::Find(Game::Lua::ToString(L, 1));
    if (cvar == nullptr || !IndexInRange(index)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint32_t flags = *reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(cvar) + Offsets::OFF_CVAR_FLAGS);
    if ((flags & Offsets::CVAR_FLAG_READ_ONLY) != 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    const uint64_t bit = uint64_t{1} << (static_cast<int>(index) - 1);
    const uint64_t before = ValueOf(cvar);
    const uint64_t after = Game::Lua::ToBoolean(L, 3) != 0 ? (before | bit) : (before & ~bit);
    if (after != before) {
        char buf[24]; // 20 digits for a uint64 at most, plus the terminator
        std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(after));
        CVar::Factory::SetString(cvar, buf);
    }
    Game::Lua::PushBool(L, true);
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarBitfield", &Script_GetCVarBitfield);
    Game::Lua::RegisterTableFunction("C_CVar", "SetCVarBitfield", &Script_SetCVarBitfield);
}

const Game::ModuleAutoRegister _autoreg{&Register};
const Game::GlueModuleAutoRegister _glueAutoreg{&Register};

} // namespace

} // namespace CVar::Bitfield
