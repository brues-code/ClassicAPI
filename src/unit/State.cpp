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

// Player state queries that modern WoW exposes as no-arg globals
// (`IsMounted`, `IsStealthed`, etc.) but vanilla 1.12 doesn't bind
// to Lua. The underlying data IS in the engine — broadcast in
// UpdateFields for some, tracked locally for others — we just
// read it directly off the player descriptor / object.

#include "Game.h"
#include "Offsets.h"
#include "spell/Cancel.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::State {

namespace {

// `FUN_UNIT_IS_OUTDOORS(unit) -> uint`, nonzero = outdoors (see Offsets.h).
// The thunk tail-jumps into the WMO query, so eax carries the result even
// though a decompiler renders the thunk itself as `void`.
using IsOutdoorsQuery_t = uint32_t(__fastcall *)(const void *unit);

// Tri-state indoor/outdoor read for the local player: 1 = outdoors,
// 0 = indoors, -1 = unknown (no resolvable player object, e.g. pre-world).
int PlayerOutdoorState() {
    auto *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return -1;
    auto fn = reinterpret_cast<IsOutdoorsQuery_t>(
        static_cast<uintptr_t>(Offsets::FUN_UNIT_IS_OUTDOORS));
    return fn(player) != 0 ? 1 : 0;
}

uint32_t PlayerMovementFlags() {
    auto *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(
        player + Offsets::OFF_PLAYER_MOVEMENT_FLAGS);
}

uint32_t MountDisplayID(const uint8_t *desc) {
    if (desc == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_MOUNTDISPLAYID);
}

// `IsMounted()` — true iff the player is currently mounted. Reads
// `UNIT_FIELD_MOUNTDISPLAYID` from the descriptor; non-zero means
// the engine is rendering a mount under the player.
int __fastcall Script_IsMounted(void *L) {
    auto *desc = Unit::Identity::PlayerDescriptor();
    Game::Lua::PushBool(L, MountDisplayID(desc) != 0);
    return 1;
}

// `Dismount()` — drops the player's current mount: CMSG_CANCEL_AURA for
// the buff that applies `SPELL_AURA_MOUNTED`, the same dismount the
// autoDismount cvar uses (1.12 has no CMSG_CANCEL_MOUNT_AURA).
//
// Vanilla 1.12 doesn't ship `Dismount` or any equivalent; mods that
// wanted programmatic dismount had to call `CancelPlayerBuff` on the
// right buff index by walking `GetPlayerBuff` themselves.
//
// No-op if the player isn't mounted.
int __fastcall Script_Dismount(void *L) {
    if (MountDisplayID(Unit::Identity::PlayerDescriptor()) != 0)
        Spell::Cancel::CancelPlayerAura(Offsets::SPELL_AURA_MOUNTED);
    return 0;
}

// `IsStealthed()` — true iff the player is currently stealthed
// (Rogue Stealth / Druid Prowl / Night Elf Shadowmeld / etc.). Reads
// the sneak flag in `UNIT_FIELD_BYTES_1` byte 3 (mask
// `UNIT_BYTES_1_STEALTH_FLAG`) — the same broadcast bit the client's
// render path uses to draw a stealthed unit semi-transparent.
//
// Derived via `_classicapi_DiffAll`: a Prowl toggle from a Cat-Form
// baseline flips byte 3 `0x00 -> 0x02` and touches nothing else but
// aura-array slot metadata, so this is the one standalone descriptor
// bit that means "stealthed" (the earlier `0x17C & 0x02` read was
// actually AURALEVELS and only worked by coincidence). Being a unit
// anim-tier flag rather than a class field, it covers every stealth
// source and rank without a spellID list or an aura scan.
int __fastcall Script_IsStealthed(void *L) {
    auto *desc = Unit::Identity::PlayerDescriptor();
    bool stealthed = false;
    if (desc != nullptr) {
        const uint32_t bytes1 = *reinterpret_cast<const uint32_t *>(
            desc + Offsets::OFF_UNIT_FIELD_BYTES_1);
        stealthed = (bytes1 & Offsets::UNIT_BYTES_1_STEALTH_FLAG) != 0;
    }
    Game::Lua::PushBool(L, stealthed);
    return 1;
}

// `IsFalling()` — true iff the player is currently mid-fall or
// jumping. Reads the local CGPlayer movement-flags word at
// `+0x9E8` and tests the FALLING / FALLING_FAR bits. Modern
// `IsFalling` returns true for the entire airtime of a jump — we
// match by accepting either bit (FALLING during the active jump,
// FALLING_FAR for sustained falls).
int __fastcall Script_IsFalling(void *L) {
    const uint32_t flags = PlayerMovementFlags();
    const uint32_t fallMask = Offsets::MOVEFLAG_FALLING |
                              Offsets::MOVEFLAG_FALLING_FAR;
    Game::Lua::PushBool(L, (flags & fallMask) != 0);
    return 1;
}

// `IsSwimming()` — true iff the player is currently swimming
// (in liquid, with the swim animation/control set). Single-bit
// check on the SWIMMING flag in the local movement-flags word.
int __fastcall Script_IsSwimming(void *L) {
    const uint32_t flags = PlayerMovementFlags();
    Game::Lua::PushBool(L, (flags & Offsets::MOVEFLAG_SWIMMING) != 0);
    return 1;
}

// `IsAssistingRitual()` — true iff the player is currently clicking
// a warlock summoning portal (the channel-on-GameObject state where
// movement breaks the channel and there's no castbar UI in vanilla).
//
// Distinct from spell channeling: this fires for the *participant*
// who clicked the portal, not the warlock who cast Ritual of
// Summoning. The warlock's own channel doesn't set this state.
//
// Detection: the engine writes the warlock's channel spell ID (698 =
// Ritual of Summoning) into the clicker's UNIT_CHANNEL_SPELL, AND
// sets a CGPlayer-local pointer at +0xB4 to the portal GameObject.
// Either signal alone is ambiguous:
//   - UNIT_CHANNEL_SPELL alone matches the warlock's own channel
//     (and any real spell channel — fishing, Mind Flay, etc.).
//   - The +0xB4 pointer alone matches any spell cast that targets a
//     GameObject (notably mining, which is a regular cast on an ore
//     node — desc[0x228] stays 0).
// The conjunction is portal-clicker-specific in 1.12.
//
// Local-player only: the +0xB4 pointer lives on CGPlayer, not in
// the broadcast UpdateField descriptor, so we can't read it for
// `target` / `party*` / etc. Modern WoW has no exact analog —
// vanilla's summoning ritual has no UI surface, which is what
// motivates this function.
int __fastcall Script_IsAssistingRitual(void *L) {
    auto *player = Unit::Identity::PlayerObject();
    if (player == nullptr) {
        Game::Lua::PushBool(L, 0);
        return 1;
    }
    auto *desc = Unit::Identity::PlayerDescriptor();
    if (desc == nullptr) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const uint32_t channelSpell = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
    const void *castGameObject = *reinterpret_cast<const void *const *>(
        player + Offsets::OFF_CGPLAYER_CAST_GAMEOBJECT_PTR);
    Game::Lua::PushBoolean(L,
        (channelSpell != 0 && castGameObject != nullptr));
    return 1;
}

// `IsIndoors()` — 1 iff the local player is under a WMO roof (inside a
// building / cave / instance interior), else nil. Recomputed live from the
// engine's WMO geometry query, so it flips as you cross an interior
// threshold — not tied to the zone/area. nil before the player object
// exists (pre-world). Vanilla 1.12 ships no binding for this.
//
int __fastcall Script_IsIndoors(void *L) {
    if (PlayerOutdoorState() == 0)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// `IsOutdoors()` — 1 iff the local player is outdoors (open sky, or not
// inside a WMO interior), else nil. Exact complement of `IsIndoors` for a
// resolvable player; both return nil pre-world. See `Script_IsIndoors`.
int __fastcall Script_IsOutdoors(void *L) {
    if (PlayerOutdoorState() == 1)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsMounted", &Script_IsMounted);
    Game::Lua::RegisterGlobalFunction("IsIndoors", &Script_IsIndoors);
    Game::Lua::RegisterGlobalFunction("IsOutdoors", &Script_IsOutdoors);
    Game::Lua::RegisterGlobalFunction("Dismount", &Script_Dismount);
    Game::Lua::RegisterGlobalFunction("IsStealthed", &Script_IsStealthed);
    Game::Lua::RegisterGlobalFunction("IsFalling", &Script_IsFalling);
    Game::Lua::RegisterGlobalFunction("IsSwimming", &Script_IsSwimming);
    Game::Lua::RegisterGlobalFunction("IsAssistingRitual",
                                       &Script_IsAssistingRitual);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::State
