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

#include "addons/Registry.h"

#include "Game.h"

#include <cstdio>
#include <cstring>

namespace Addons {

namespace {

using GetByIndex_t = void *(__fastcall *)(int idx0Based);
using GetAddOnCount_t = int(__fastcall *)();
using ResolveReqDeps_t = uint8_t *(__fastcall *)(const char *name);

} // namespace

// Either input is routed through the by-name hash lookup
// `FUN_ADDON_RESOLVE_REQ_DEPS` to reach the record.
// `FUN_ADDON_GET_BY_INDEX` returns a *name pointer*, NOT the record —
// reading record offsets off it lands in string memory and crashes (a
// numeric-path caller like pfUI's addon list hit exactly that). The
// resolver returns the RequiredDeps descriptor at `record+0x48`;
// subtract to recover the base.
const uint8_t *ResolveRecord(void *L, const char *usage) {
    const char *name;
    if (Game::Lua::IsNumber(L, 1)) {
        const int idx = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
        auto byIndex = reinterpret_cast<GetByIndex_t>(Offsets::FUN_ADDON_GET_BY_INDEX);
        name = static_cast<const char *>(byIndex(idx));
        if (name == nullptr) {
            auto count = reinterpret_cast<GetAddOnCount_t>(Offsets::FUN_ADDON_GET_COUNT);
            Game::Lua::Error(L, reinterpret_cast<const char *>(static_cast<uintptr_t>(
                                    Offsets::VAR_ADDON_INDEX_RANGE_ERROR_FMT)),
                             count());
            return nullptr;
        }
    } else if (Game::Lua::IsString(L, 1)) {
        name = Game::Lua::ToString(L, 1);
    } else {
        Game::Lua::Error(L, usage);
        return nullptr;
    }

    auto resolve = reinterpret_cast<ResolveReqDeps_t>(
        Offsets::FUN_ADDON_RESOLVE_REQ_DEPS);
    uint8_t *reqDesc = resolve(name);
    if (reqDesc == nullptr)
        return nullptr;
    return reqDesc - Offsets::OFF_ADDON_REQDEPS_DESC;
}

const uint8_t *ResolveAddOnArg(void *L, const char *usage, const char **outName) {
    const char *name = nullptr;
    const int type = Game::Lua::Type(L, 1);
    if (type == Game::Lua::TYPE_NUMBER) {
        const int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
        auto count = reinterpret_cast<GetAddOnCount_t>(Offsets::FUN_ADDON_GET_COUNT);
        auto byIndex = reinterpret_cast<GetByIndex_t>(Offsets::FUN_ADDON_GET_BY_INDEX);
        if (index >= 1 && index <= count())
            name = static_cast<const char *>(byIndex(index - 1));
        if (name == nullptr) {
            char msg[512];
            std::snprintf(msg, sizeof(msg),
                          "Invalid index %d. AddOn index must be in the range of 1 to %d - %s",
                          index, count(), usage);
            Game::Lua::ArgError(L, 1, msg);
            return nullptr;
        }
    } else if (type == Game::Lua::TYPE_STRING) {
        name = Game::Lua::ToString(L, 1);
    } else {
        Game::Lua::ArgError(L, 1, usage);
        return nullptr;
    }
    if (outName != nullptr)
        *outName = name;
    return FindRecord(name);
}

const uint8_t *FindRecord(const char *name) {
    if (name == nullptr)
        return nullptr;
    auto resolve = reinterpret_cast<ResolveReqDeps_t>(Offsets::FUN_ADDON_RESOLVE_REQ_DEPS);
    uint8_t *reqDesc = resolve(name);
    if (reqDesc == nullptr)
        return nullptr;
    return reqDesc - Offsets::OFF_ADDON_REQDEPS_DESC;
}

const char *RecordName(const uint8_t *record) {
    return *reinterpret_cast<const char *const *>(record + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
}

const char *CharacterArg(void *L, int idx, const char *usage) {
    const int type = Game::Lua::Type(L, idx);
    if (type == Game::Lua::TYPE_NONE || type == Game::Lua::TYPE_NIL)
        return nullptr;
    if (type != Game::Lua::TYPE_STRING && type != Game::Lua::TYPE_NUMBER) {
        Game::Lua::ArgError(L, idx, usage);
        return nullptr;
    }
    const char *character = Game::Lua::ToString(L, idx);
    if (character == nullptr)
        return nullptr;
    // 3.80.2 FUN_142a714f0, the parser for a string with no '-': a prefix of
    // sixteen zeros is the zero GUID, and so is WOWGUID_NULL.
    const size_t len = std::strlen(character);
    if (std::strncmp(character, "0000000000000000", len < 16 ? len : 16) == 0)
        return nullptr;
    if (_stricmp(character, "WOWGUID_NULL") == 0)
        return nullptr;
    return character;
}

} // namespace Addons
