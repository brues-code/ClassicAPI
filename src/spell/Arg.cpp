// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#include "spell/Arg.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Spell::Arg {

namespace {

using NameToSpellID_t = int(__fastcall *)(const char *name, int *outIsPet);

} // namespace

Resolved Resolve(void *L, int idx) {
    Resolved out{0, nullptr};
    if (Game::Lua::IsNumber(L, idx)) {
        out.spellID = static_cast<int>(Game::Lua::ToNumber(L, idx));
        return out;
    }
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING) {
        return out;
    }
    const char *s = Game::Lua::ToString(L, idx);
    if (s == nullptr) {
        return out;
    }
    if (const char *m = std::strstr(s, "spell:")) {
        out.spellID = std::atoi(m + 6);
        return out;
    }
    const int numeric = std::atoi(s);
    if (numeric > 0) {
        out.spellID = numeric;
    } else {
        out.name = s;
    }
    return out;
}

int NameToSpellID(const char *name, int *outIsPet) {
    if (outIsPet != nullptr)
        *outIsPet = 0;
    if (name == nullptr || *name == '\0')
        return 0;
    auto fn = reinterpret_cast<NameToSpellID_t>(
        static_cast<uintptr_t>(Offsets::FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID));
    int isPet = 0;
    const int spellID = fn(name, &isPet);
    if (outIsPet != nullptr)
        *outIsPet = isPet;
    return spellID;
}

int ResolveSpellID(void *L, int idx) {
    Resolved r = Resolve(L, idx);
    if (r.spellID > 0)
        return r.spellID;
    if (r.name != nullptr)
        return NameToSpellID(r.name);
    return 0;
}

} // namespace Spell::Arg
