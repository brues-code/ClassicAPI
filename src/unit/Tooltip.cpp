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
#include "aura/Data.h"
#include "guid/Guid.h"
#include "object/Resolve.h"

#include <cstdint>

namespace Unit::Tooltip {

// The 1-based index the engine's `SetUnitBuff` / `SetUnitDebuff` would need to
// land on `slot`: one plus the slots before it in ITS range (0..31 or 32..47)
// that pass the engine's tooltip visibility gate — the exact walk
// `FUN_00534AC0` / `FUN_00534E30` perform (verified by decompile).
static int EngineIndexForSlot(const uint8_t *unit, int slot) {
    const int home =
        slot < Offsets::UNIT_AURA_BUFF_COUNT ? 0 : Offsets::UNIT_AURA_BUFF_COUNT;
    int index = 1;
    for (int s = home; s < slot; ++s) {
        if (Aura::Data::IsSlotTooltipVisible(unit, s))
            ++index;
    }
    return index;
}

// `GameTooltip:SetUnitAura(unit, index, [filter])` — modern unified-aura
// method. 1.12 has `SetUnitBuff` (slot 32) and `SetUnitDebuff` (slot 33), each
// indexing its own slot RANGE (0..31 / 32..47) under the engine's tooltip
// gate. `C_UnitAuras` indexes by the aura's polarity nibble instead (see
// Aura::Data), so the two index spaces diverge once the server has parked a
// debuff in a buff slot — and an index an addon got from `C_UnitAuras` must
// still open the right tooltip. So: resolve (index, filter) to the absolute
// slot in OUR space, then hand the engine the index THAT slot has within its
// own range under ITS gate, choosing SetUnitBuff or SetUnitDebuff by where the
// slot lives. The aura tooltip builder only ever receives spellId / level /
// stacks, so a debuff in a buff slot renders identically through SetUnitBuff.
//
// `filter` defaults to "HELPFUL" when omitted, matching modern behavior; only
// its HELPFUL/HARMFUL half is read (a PLAYER-filtered index is not an index
// into the plain list — pass an index from the same plain list, as FrameXML
// does). A unit with no live CGUnit (out-of-range groupmate) passes straight
// through: the engine reads the group array by range there, exactly as we do.
static int __fastcall Script_GameTooltipSetUnitAura(void *L) {
    // Args: self (table), unit (string), index (number), filter (optional string)
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetUnitAura(unit, index, [filter])");
        return 0;
    }
    if (!Game::Lua::IsString(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetUnitAura(unit, index, [filter])");
        return 0;
    }

    bool isHarmful = false;
    if (Game::Lua::Type(L, 4) == Game::Lua::TYPE_STRING) {
        const char *filter = Game::Lua::ToString(L, 4);
        // Case-insensitive prefix check for "HARM" — covers "HARMFUL"
        // (the canonical filter) and any reasonable variant. Anything
        // else, including missing/empty, falls through to the buff path.
        if (filter != nullptr) {
            const char a = filter[0], b = filter[1], c = filter[2], d = filter[3];
            isHarmful = (a == 'H' || a == 'h') && (b == 'A' || b == 'a') &&
                        (c == 'R' || c == 'r') && (d == 'M' || d == 'm');
        }
    }

    int index = static_cast<int>(Game::Lua::ToNumber(L, 3));
    bool useDebuffMethod = isHarmful;
    const auto *unit = static_cast<const uint8_t *>(
        Game::ResolveUnitToken(Game::Lua::ToString(L, 2)));
    if (unit != nullptr) {
        const int slot = Aura::Data::FindNthSlot(
            unit, index,
            isHarmful ? Aura::Data::Filter::Harmful : Aura::Data::Filter::Helpful);
        if (slot >= 0) {
            index = EngineIndexForSlot(unit, slot);
            useDebuffMethod = slot >= Offsets::UNIT_AURA_BUFF_COUNT;
        }
        // slot < 0: nothing at that index in our space; the raw index goes
        // through and the engine's own walk shows nothing either.
    }

    // Rebuild the args as (self, unit, engineIndex) — the engine methods take
    // exactly those three; the filter string is consumed here.
    Game::Lua::SetTop(L, 2);
    Game::Lua::PushNumber(L, static_cast<double>(index));

    using Script_t = int(__fastcall *)(void *L);
    auto fn = reinterpret_cast<Script_t>(
        useDebuffMethod ? Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_UNIT_DEBUFF
                        : Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_UNIT_BUFF);
    return fn(L);
}

// Vanilla 1.12 drops the unit-token string at the `Script_GameTooltip_SetUnit`
// boundary: the token is resolved to a 64-bit GUID via TokenToGUID, the GUID
// is passed to the inner `FUN_00529FE0` builder, and the original string is
// discarded. The builder writes the GUID into `[tooltip + 0x368]` / `+0x36C`
// and the shared `FUN_00530050` clear zeroes both at the start of every
// `SetX` call — same gating pattern HasSpell / HasItem use.
//
// Modern `GameTooltip:GetUnit()` returns `(name, unitToken)`, but the token
// can't be recovered exactly in vanilla — multiple unit tokens (`"target"`,
// `"focus"`, `"mouseover"` etc.) can point at the same GUID at any given
// moment, and the engine doesn't remember which one was used. The GUID is
// what addons actually need for cross-referencing (it threads through
// `UnitGUID`, the NameCache, item-link unique IDs, etc.); exposing it
// directly as `GetUnitGUID()` avoids inventing a faux token that wouldn't
// match the original Lua input anyway.

using ObjectGetName_t = const char *(__fastcall *)(void *obj, void *edx_unused, int *outFlags);

static const char *ResolveUnitName(uint32_t guidLo, uint32_t guidHi) {
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT,
                               (static_cast<uint64_t>(guidHi) << 32) | guidLo,
                               "GameTooltip:GetUnitGUID", 0x172);
    if (obj == nullptr)
        return nullptr;
    // `FUN_OBJECT_GET_NAME` is __thiscall — wire as __fastcall with the
    // ignored EDX slot. Returns a const char* (the canonical display
    // name for the object); falls back to engine `"UNKNOWNOBJECT"` /
    // `"Unknown Being"` literals when the unit's name isn't cached.
    auto getName = reinterpret_cast<ObjectGetName_t>(Offsets::FUN_OBJECT_GET_NAME);
    return getName(obj, nullptr, nullptr);
}

// `GameTooltip:GetUnitGUID()` → (name, guidString) for whichever unit
// the tooltip is currently displaying, or nothing if it isn't showing
// a unit. Return order matches modern's `GameTooltip:GetUnit()` shape
// (name first), so addons porting from `local name, unit = ttip:GetUnit()`
// can swap to `GetUnitGUID` and keep their existing destructuring.
// `name` is the unit's display name (may be `"UNKNOWNOBJECT"` for a
// remote unit whose info hasn't been queried yet — same fallback the
// engine uses internally). `guidString` is the canonical
// `"0xHHHHHHHHLLLLLLLL"` format matching `UnitGUID(unit)`.
static int __fastcall Script_GameTooltipGetUnitGUID(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:GetUnitGUID()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr)
        return 0;

    const auto *base = static_cast<const uint8_t *>(tooltipObj);
    const uint32_t guidLo =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_UNIT_GUID_LO);
    const uint32_t guidHi =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_UNIT_GUID_HI);
    if (guidLo == 0 && guidHi == 0)
        return 0;

    const char *name = ResolveUnitName(guidLo, guidHi);
    if (name != nullptr)
        Game::Lua::PushString(L, name);
    else
        Game::Lua::PushNil(L);

    const uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(guid, buf, sizeof buf));
    return 2;
}

// `GameTooltip:HasUnit()` — boolean companion to `GetUnitGUID`. Returns
// true iff the tooltip is currently showing a unit (i.e., the stored
// GUID is non-zero, which the shared tooltip-clear zeroes on every
// new `SetX` call).
static int __fastcall Script_GameTooltipHasUnit(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:HasUnit()");
        return 0;
    }
    void *tooltipObj = Game::Lua::ResolveTooltip(L);
    if (tooltipObj == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const auto *base = static_cast<const uint8_t *>(tooltipObj);
    const uint32_t guidLo =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_UNIT_GUID_LO);
    const uint32_t guidHi =
        Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_UNIT_GUID_HI);
    Game::Lua::PushBool(L, guidLo != 0 || guidHi != 0);
    return 1;
}

static const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetUnitAura", &Script_GameTooltipSetUnitAura},
    {"GetUnitGUID", &Script_GameTooltipGetUnitGUID},
    {"HasUnit", &Script_GameTooltipHasUnit},
};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Unit::Tooltip
