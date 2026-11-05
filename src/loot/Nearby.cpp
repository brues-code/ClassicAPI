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

namespace Loot::Nearby {

namespace {

// `ClntObjMgrEnumVisibleObjects(callback, context)` — walks the engine's
// visible-object table. The callback shape's `void *unusedEdx` slot
// absorbs the second `__fastcall` register so MSVC lays out the trailing
// `uint64_t guid` on the stack exactly where the engine puts it. The
// callback returns non-zero to keep iterating; zero stops the walk early.
using ClntObjMgrEnumVisibleObjectsCallback_t = int(__fastcall *)(void *ctx,
                                                                  void *unusedEdx,
                                                                  uint64_t guid);
using ClntObjMgrEnumVisibleObjects_t =
    int(__fastcall *)(ClntObjMgrEnumVisibleObjectsCallback_t cb, void *ctx);

// Only `TYPEMASK_UNIT` carries lootable-mob state in 1.12 — dead
// creatures stay as CGCreature_C objects with the lootable bit flipped
// on, rather than transitioning to a separate corpse object. Dead
// players become CGCorpse_C (`TYPEMASK_CORPSE`) but those aren't
// lootable by other players in vanilla — no reason to walk them here.

struct ScanCtx {
    void *L;
    void *player; // resolved once outside the iterator
    int idx;            // 1-based slot of the next entry to append
};

int __fastcall ScanCallback(ScanCtx *ctx, void * /*unusedEdx*/, uint64_t guid) {
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (obj == nullptr)
        return 1; // GUID isn't a unit; continue

    if (!Request::IsLootable(obj))
        return 1; // unit isn't lootable to us; continue

    if (!Request::InReach(ctx->player, obj))
        return 1; // out of click-loot range; continue

    char guidBuf[Guid::STRING_SIZE];
    Game::Lua::PushNumber(ctx->L, static_cast<double>(ctx->idx));
    Game::Lua::NewTable(ctx->L);
    Game::Lua::SetFieldString(ctx->L, "guid",
                              Guid::FormatAsString(guid, guidBuf, sizeof guidBuf));
    Game::Lua::SetTable(ctx->L, -3);
    ++ctx->idx;
    return 1;
}

// `C_Loot.GetNearbyLootableUnits()` — returns a 1-indexed array of
// `{ guid = "0x..." }` subtables, one per visible unit that is (a)
// flagged DYNFLAG_LOOTABLE by the server and (b) inside the engine's
// own interact range (the distance threshold that lights up the
// right-click-loot cursor). Empty table pre-login, when no lootable
// units are in range, or when we don't have loot rights on visible
// corpses (the server only sets the bit for clients with rights).
int __fastcall Script_GetNearbyLootableUnits(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    // The engine derefs the object manager unconditionally on iterator
    // entry. NULL during glue / character-select; bail with an empty
    // table rather than crashing.
    if (*reinterpret_cast<void *volatile *>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr)
        return 1;

    // Resolve the local player once — the canonical CGPlayer_C the
    // engine's own interact-range checks use, distinct from the
    // `VAR_LOCAL_PLAYER_PTR` global. Bail if unavailable.
    void *player = Game::ResolveUnitToken("player");
    if (player == nullptr)
        return 1;

    auto Enum = reinterpret_cast<ClntObjMgrEnumVisibleObjects_t>(
        Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
    ScanCtx ctx{L, player, 1};
    Enum(reinterpret_cast<ClntObjMgrEnumVisibleObjectsCallback_t>(&ScanCallback),
         &ctx);
    return 1;
}

void Register() {
    Game::Lua::RegisterTableFunction("C_Loot", "GetNearbyLootableUnits",
                                     &Script_GetNearbyLootableUnits);
}

} // namespace

static const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace Loot::Nearby
