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
#include "guid/Guid.h"
#include "object/Resolve.h"

#include <cstdint>

namespace GameObject::Tooltip {

// Vanilla 1.12 has no Lua-callable `GameTooltip:SetGameObject` method — the only
// time the tooltip displays gameobject info is the in-world mouseover, where
// the engine's hover handler at FUN_00492890 dispatches to the gameobject
// tooltip populator at FUN_0052AA20. That populator stashes the GO GUID into
// `+0x370/+0x374` on the tooltip frame (right between the unit and item GUID
// slots) and the shared per-tooltip Clear (FUN_00530050) zeroes the same slot
// on every subsequent `SetX` call — same gating pattern Has/GetUnitGUID,
// Has/GetItem, and Has/GetSpell use.

using GameObjectGetName_t = const char *(__fastcall *)(void *gameObject);

// `GameTooltip:GetGameObject()` → (name, id, guid) for whichever gameobject
// the tooltip is currently displaying, or nothing if it isn't showing one.
// Matches the modern `(name, link, itemID)` shape of `GetItem` — `id` is the
// gameobject's template/entry (same key `gameobjectcache.wdb` uses), `guid`
// is the canonical `"0xHHHHHHHHLLLLLLLL"` format.
//
// Entry ID lives at the CGGameObject_C's instance block `+0x0C` — same
// layout items use (see CLAUDE.md "Item lookups → CGItem field layout").
// Verified via FUN_005F7C40 (the gameobject's create-from-packet path),
// which passes `[guidBlock + 0x0C]` as the cache-lookup key. Reading
// directly off the instance block (rather than via the cache record) works
// even before `gameobjectcache.wdb` has loaded the record, which matches
// how the engine's own builder paths behave.
//
// `name` is whatever `FUN_005F8100` returns — the cached display name once
// the GameObjectStats_C record has been queried, or the empty string
// before then. We surface the empty string verbatim rather than nil so
// addons get a single, predictable return shape regardless of cache state.
//
// Returns nothing only for: tooltip not showing a gameobject (GUID slot
// zero), or the gameobject has left the object manager (visibility window
// closed) and can't be re-resolved.
static int __fastcall Script_GameTooltipGetGameObject(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:GetGameObject()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return 0;

    const auto *base = static_cast<const uint8_t *>(tooltipObj);
    const uint32_t guidLo =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_LO);
    const uint32_t guidHi =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_HI);
    if (guidLo == 0 && guidHi == 0)
        return 0;

    void *obj = Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT,
                               (static_cast<uint64_t>(guidHi) << 32) | guidLo,
                               "GameTooltip:GetGameObject", 0x172);
    if (obj == nullptr)
        return 0;

    auto *instanceBlock = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(obj) + 0x08);
    const uint32_t entryID = (instanceBlock != nullptr)
        ? *reinterpret_cast<const uint32_t *>(instanceBlock + 0x0C)
        : 0;

    auto getName = reinterpret_cast<GameObjectGetName_t>(Offsets::FUN_GAMEOBJECT_GET_NAME);
    const char *name = getName(obj);

    Game::Lua::PushString(L, name != nullptr ? name : "");
    Game::Lua::PushNumber(L, static_cast<double>(entryID));

    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    return 3;
}

// `GameTooltip:HasGameObject()` — boolean companion to `GetGameObject`.
// Returns true iff the tooltip is currently showing a gameobject (i.e., the
// stored GUID is non-zero, which the shared tooltip-clear zeroes on every
// new `SetX` call). True regardless of whether the gameobject is still
// resolvable through the object manager — gating purely on the
// tooltip-frame slot keeps this predicate cheap and side-effect-free.
static int __fastcall Script_GameTooltipHasGameObject(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:HasGameObject()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const auto *base = static_cast<const uint8_t *>(tooltipObj);
    const uint32_t guidLo =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_LO);
    const uint32_t guidHi =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_HI);
    Game::Lua::PushBool(L, guidLo != 0 || guidHi != 0);
    return 1;
}

static const Game::Lua::FrameMethodEntry g_methods[] = {
    {"GetGameObject", &Script_GameTooltipGetGameObject},
    {"HasGameObject", &Script_GameTooltipHasGameObject},
};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace GameObject::Tooltip
