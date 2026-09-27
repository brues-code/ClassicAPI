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

#include "Tooltip.h"

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "guid/Guid.h"
#include "object/Resolve.h"
#include "unit/Identity.h"
#include "unit/TokenResolve.h"

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

// The unit a tooltip shows lives in the engine as a GUID only: the builder
// `FUN_GAMETOOLTIP_BUILD_UNIT` writes it to `[tooltip + 0x368]` / `+0x36C`, and
// the shared clear `FUN_00530050` zeroes both at the start of every `SetX` —
// same gating pattern HasSpell / HasItem use. The token string is discarded
// by `Script_GameTooltip_SetUnit` right after FUN_TOKEN_TO_GUID resolves it.
//
// `GetUnit()` needs that token, and the GUID can't give it back (`"target"`
// and `"raid3"` can name one GUID at once). So we keep it where it enters.
// The builder has exactly three engine callers (xrefs, verified):
//
//   - `Script_GameTooltip_SetUnit` (0x005349B0) — the Lua token. We
//     re-register `SetUnit` in front of it (most recent registration wins,
//     as for `SetAction`) and stage the token as PENDING for that tooltip.
//   - `FUN_00492890` — the world mouseover setter; stores the mouseover GUID,
//     then builds into GameTooltip. The unit IS "mouseover".
//   - `FUN_004919d0` — rebuilds GameTooltip in place, only when the unit
//     changed is the one it already shows (guid == [+0x368]). Keeps its token.
//
// `Frame::Attributes` drives the engine's mouseover setter (and, for a member
// with no live object, the builder directly) when the cursor rests on a unit
// frame. It stages the FRAME'S token around those calls, because "mouseover"
// would name nothing there: the engine resolves the "mouseover" token only
// while the unit has a live object (FUN_00515970's mouseover branch returns 0
// otherwise), unlike "partyN", which reads the roster.
//
// The token binds inside the builder (BeforeBuild, from the one co-hook
// Tooltip::SetEvents owns), not after SetUnit returns: OnTooltipSetUnit fires
// from within the builder, and its handler must already see the new token.
//
// Known gap: a tooltip kept visible on unit X via SetUnit("target") while the
// cursor enters X in the world takes the mouseover build with the same GUID
// it already shows, which reads as a refresh and keeps "target". Leaving the
// owning frame normally hides the tooltip (the clear zeroes the GUID) first.

namespace {

constexpr int kMaxTokenLen = 64; // longer than any real token ("0x" + 16 hex = 18)
constexpr const char *kMouseoverToken = "mouseover";

struct Record {
    void *tooltip;
    uint32_t guid[2];
    char token[kMaxTokenLen];
};

// Few tooltips, same reasoning and lifetime as Tooltip::SetEvents' cells: the
// objects are recreated on every /reload, so the table is dropped at teardown.
constexpr int kMaxTooltips = 32;
Record g_records[kMaxTooltips];
int g_recordCount = 0;

// The token the next build of `g_pendingTooltip` binds (StageToken). Matched
// by tooltip, so a stale stage (the engine raised past our reset) can never
// bind to another tooltip's build; every stager also resets it on entry.
void *g_pendingTooltip = nullptr;
char g_pendingToken[kMaxTokenLen];

void CopyToken(char *dst, const char *src) {
    int i = 0;
    for (; i < kMaxTokenLen - 1 && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

Record *Find(const void *tooltip) {
    for (int i = 0; i < g_recordCount; ++i)
        if (g_records[i].tooltip == tooltip)
            return &g_records[i];
    return nullptr;
}

Record *FindOrCreate(void *tooltip) {
    if (Record *r = Find(tooltip))
        return r;
    if (g_recordCount >= kMaxTooltips)
        return nullptr;
    Record &r = g_records[g_recordCount++];
    r.tooltip = tooltip;
    return &r;
}

void Bind(void *tooltip, const uint32_t *guid, const char *token) {
    Record *r = FindOrCreate(tooltip);
    if (r == nullptr)
        return;
    r->guid[0] = guid[0];
    r->guid[1] = guid[1];
    CopyToken(r->token, token);
}

void PrepareForReload() {
    g_recordCount = 0;
    g_pendingTooltip = nullptr;
}

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace

void StageToken(void *tooltip, const char *token) {
    g_pendingTooltip = nullptr;
    if (tooltip == nullptr || token == nullptr)
        return;
    CopyToken(g_pendingToken, token);
    g_pendingTooltip = tooltip;
}

void ClearStagedToken() { g_pendingTooltip = nullptr; }

void BeforeBuild(void *tooltip, const uint32_t *guid) {
    if (tooltip == nullptr || guid == nullptr)
        return;
    if (g_pendingTooltip == tooltip) {
        g_pendingTooltip = nullptr;
        Bind(tooltip, guid, g_pendingToken);
        return;
    }
    // Not from our SetUnit: FUN_004919d0's in-place refresh keeps the bound
    // token; every other caller is a mouseover build.
    const auto *base = static_cast<const uint8_t *>(tooltip);
    const uint32_t liveLo = Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_UNIT_GUID_LO);
    const uint32_t liveHi = Game::Read<uint32_t>(base, Offsets::OFF_TOOLTIP_UNIT_GUID_HI);
    const Record *r = Find(tooltip);
    if (r != nullptr && liveLo == guid[0] && liveHi == guid[1] &&
        r->guid[0] == guid[0] && r->guid[1] == guid[1])
        return;
    Bind(tooltip, guid, kMouseoverToken);
}

// `GameTooltip:SetUnit(unit)` — the engine's own method, with the token
// staged for BeforeBuild. Anything we can't vouch for goes to the engine with
// the untouched stack so it raises its own errors: a non-tooltip self, a
// non-string arg, or a string the engine's resolver would reject (its raise
// would longjmp past our reset, so we never stage one).
static int __fastcall Script_GameTooltipSetUnit(void *L) {
    using Script_t = int(__fastcall *)(void *L);
    const auto engine = reinterpret_cast<Script_t>(Offsets::FUN_SCRIPT_GAMETOOLTIP_SET_UNIT);

    ClearStagedToken();
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE || !Game::Lua::IsString(L, 2))
        return engine(L);
    void *tooltip = Game::Lua::ResolveTooltip(L, 1, /*raiseError*/ false);
    const char *token = Game::Lua::ToString(L, 2);
    if (tooltip == nullptr || token == nullptr || !Unit::TokenResolve::IsUnitToken(token))
        return engine(L);

    StageToken(tooltip, token);
    const int n = engine(L);
    ClearStagedToken(); // a token naming nothing never reaches the builder
    return n;
}

using ObjectGetName_t = const char *(__fastcall *)(void *obj, void *edx_unused, int *outFlags);

// The name `UnitName` would give for this GUID, in `Script_UnitName`'s order
// (0x00517020): the live object, else — no object, e.g. an offline or far
// party/raid member — the group roster slot's inline name, else the player
// NameCache. The unit tooltip builder draws its header from the same sources.
static const char *ResolveUnitName(uint32_t guidLo, uint32_t guidHi) {
    uint64_t guid = (static_cast<uint64_t>(guidHi) << 32) | guidLo;
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "GameTooltip:GetUnit", 0x172);
    if (obj == nullptr) {
        using SlotLookup_t = const uint8_t *(__fastcall *)(const uint64_t *guid);
        if (const uint8_t *slot = reinterpret_cast<SlotLookup_t>(
                Offsets::FUN_GROUP_MEMBER_SLOT_LOOKUP)(&guid))
            return Game::Ptr<const char>(slot, Offsets::OFF_GROUP_MEMBER_SLOT_NAME);
        return Unit::Identity::NameForGuid(guid);
    }
    // `FUN_OBJECT_GET_NAME` is __thiscall — wire as __fastcall with the
    // ignored EDX slot. Returns a const char* (the canonical display
    // name for the object); falls back to engine `"UNKNOWNOBJECT"` /
    // `"Unknown Being"` literals when the unit's name isn't cached.
    auto getName = reinterpret_cast<ObjectGetName_t>(Offsets::FUN_OBJECT_GET_NAME);
    return getName(obj, nullptr, nullptr);
}

// `GameTooltip:GetUnitGUID()` → (name, guidString) for whichever unit
// the tooltip is currently displaying, or nothing if it isn't showing
// a unit. Same shape as `GetUnit()` with the GUID in the token's place — the
// GUID outlives a token like "mouseover", which stops naming the unit as soon
// as the cursor moves on.
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

// `GameTooltip:GetUnit()` → (name, unitToken) for whichever unit the tooltip
// is showing, or nothing if it isn't showing one. `unitToken` is the string
// given to `SetUnit`, or "mouseover" for the world-mouseover tooltip. The
// token is only returned while its bound GUID is still the one the tooltip
// shows; nil otherwise.
static int __fastcall Script_GameTooltipGetUnit(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: GameTooltip:GetUnit()");
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

    if (const char *name = ResolveUnitName(guidLo, guidHi))
        Game::Lua::PushString(L, name);
    else
        Game::Lua::PushNil(L);

    const Record *r = Find(tooltipObj);
    if (r != nullptr && r->guid[0] == guidLo && r->guid[1] == guidHi)
        Game::Lua::PushString(L, r->token);
    else
        Game::Lua::PushNil(L);
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
    {"SetUnit", &Script_GameTooltipSetUnit},
    {"GetUnit", &Script_GameTooltipGetUnit},
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
