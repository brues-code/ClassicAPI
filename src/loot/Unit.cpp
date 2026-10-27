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

#include "../Game.h"
#include "../Offsets.h"
#include "../guid/Guid.h"
#include "../object/Resolve.h"
#include "Request.h"

#include <cstdint>

namespace Loot::Unit {

namespace {

// `C_Loot.LootUnit(guid)` — initiates a loot session against the unit
// identified by `guid` (a hex-string as returned by `UnitGUID` /
// `C_Loot.GetNearbyLootableUnits`). The engine handles release-prior-
// loot, packet construction, and dispatch; the loot window appears
// asynchronously via the regular `LOOT_OPENED` event once the server's
// `SMSG_LOOT_RESPONSE` lands.
//
// `useDistanceCheck` is hardcoded to `0` so the engine doesn't auto-
// walk the player toward a corpse that happens to be just out of
// range — which would be a surprise side-effect for an AoE-loot
// caller looping over `GetNearbyLootableUnits`. The server still
// enforces real range; OOR requests just don't get a response.
//
// Returns nothing. Silent no-op when:
//   - The object manager is NULL (glue / character-select).
//   - The GUID doesn't resolve to a visible `TYPEMASK_UNIT` object
//     (target despawned, out of render range, wrong typemask).
//   - The local player pointer can't be resolved.
//   - The player is mounted (as a right-click on a corpse does nothing).
// Errors only on Lua usage problems (missing arg, unparseable GUID).
int __fastcall Script_LootUnit(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Loot.LootUnit(guid)");
        return 0;
    }
    const char *guidStr = Game::Lua::ToString(L, 1);
    uint64_t guid = 0;
    if (!Guid::Parse(guidStr, &guid) || guid == 0) {
        Game::Lua::Error(L, "Usage: C_Loot.LootUnit(guid) — unparseable GUID");
        return 0;
    }

    // Object manager NULL pre-login; bail silently rather than
    // crashing inside the resolver.
    if (*reinterpret_cast<void *volatile *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr)
        return 0;

    void *target = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (target == nullptr)
        return 0;

    // `ResolveUnitToken("player")` gives the canonical CGPlayer_C the
    // engine uses for `__thiscall` calls — distinct from the global
    // at `VAR_LOCAL_PLAYER_PTR` (see the comment on that offset).
    void *player = Game::ResolveUnitToken("player");
    if (player == nullptr)
        return 0;

    Request::Send(player, target, guid);
    return 0;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_Loot", "LootUnit", &Script_LootUnit);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Loot::Unit
