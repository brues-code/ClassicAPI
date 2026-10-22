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

// The four-element totem bar (`GetTotemInfo`, `GetTotemTimeLeft`,
// `TargetTotem`, `GameTooltip:SetTotem`, `PLAYER_TOTEM_UPDATE`), backported
// to 3.3.5 parity. 3.3.5 keeps `CGGameUI::m_totems[4]` — {guid, name,
// duration, startTime, icon} per slot — filled by SMSG_TOTEM_CREATED and
// emptied by `CGGameUI::DestroyTotem` when the totem's unit object is
// disabled. Vanilla has neither the packet nor the array (no 1.12 server
// sends SMSG_TOTEM_CREATED or handles CMSG_TOTEM_DESTROYED — checked
// vmangos and tortoise-wow), so we fill the same record from what 1.12
// does have:
//
//   * SLOT is read straight from the totem summon spell's `Spell.dbc`
//     effect: 87/88/89/90 = SUMMON_TOTEM_SLOT1..4 = Fire/Earth/Water/Air,
//     so `slot = effect - 86`. No hardcoded spell table; Turtle custom
//     totems self-classify.
//   * DURATION is the summon spell's `SpellDuration.dbc` value (via the
//     engine's own duration helper, so player duration mods apply).
//   * ICON is the summon spell's SpellIcon path — what 3.3.5 resolves from
//     the packet's spellId.
//   * GUID is the totem creature itself: the summon's `EffectMiscValue`
//     (creature entry) + `CreatedBy == player`, found by a throttled
//     object-manager scan after the cast. 3.3.5 gets it from the packet.
//   * NAME is `CGUnit_C::GetUnitName` on that creature, re-read every tick
//     — 3.3.5 sets `m_name` the same way from PostInit and the creature /
//     name query callbacks, firing PLAYER_TOTEM_UPDATE each time.
//
// Detection: the player's totem cast fires SMSG_SPELL_GO, which
// `Aura::Source` already co-hooks; it calls `OnPlayerSpellGo` here (before
// its aura gate, since a summon applies no aura).
//
// Removal mirrors 3.3.5: once the GUID is known, the slot empties when that
// unit object stops resolving (3.3.5: `CGUnit_C::Disable` → DestroyTotem —
// death, recall, despawn at expiry, or leaving visibility). A slot whose
// creature was never seen falls back to its duration. Leaving the world
// empties every slot without an event (3.3.5 `CGGameUI::EnterWorld`, skipped
// on a UI reload — the local player survives a reload, so we key on it).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "guid/Guid.h"
#include "item/Count.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "tooltip/Lines.h"
#include "time/Clock.h"
#include "ui/ColorData.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Totem::Tracker {

namespace {

// SUMMON_TOTEM_SLOT1..4 spell effects (verified against Spell.dbc: Searing
// 87/Fire, Stoneskin 88/Earth, Healing Stream 89/Water, Windfury 90/Air).
constexpr int kEffectSummonTotemSlot1 = 87;
constexpr int kEffectSummonTotemSlot4 = 90;
constexpr int kSlots = 4;


// Re-scan the object manager for totem existence at most this often.
constexpr uint32_t kScanIntervalMs = 250;

struct Slot {
    bool active;          // 3.3.5's `m_guid != 0` — a totem is down in this slot
    uint32_t spellID;
    uint32_t entry;       // summoned creature-template ID (to find the creature)
    uint64_t guid;        // the totem creature; 0 until the scan finds it
    uint64_t staleGuid;   // the slot's previous totem — never adopted by the scan
    const char *name;     // CGUnit_C::GetUnitName of `guid`; nullptr until found
    const char *icon;     // SpellIcon path of the summon spell
    uint32_t startMs;     // FUN_OS_TICKCOUNT_MS (shares GetTime's epoch × 1000)
    uint32_t durationMs;
};

Slot g_slots[kSlots] = {};
uint32_t g_lastScanMs = 0;

// Per-slot totem tool item ID (Fire/Earth/Water/Air), read from the totem
// spell's `Totem[0]` field — the required TOOL the spell lists (Earth
// Totem, Fire Totem, …), NOT a consumed reagent, and NOT hardcoded. Filled
// from the player's spellbook on SPELLS_CHANGED and refreshed at cast; 0 =
// not yet resolved. `g_spellsChangedId` caches the event id.
uint32_t g_toolItem[kSlots] = {};
int g_spellsChangedId = -1;

using Time::Clock::NowMs;

// The totem tool item a spell requires (`Totem[0]`), or 0.
uint32_t TotemToolItem(const uint8_t *rec) {
    const int32_t t = Game::Read<int32_t>(rec, Offsets::OFF_SPELL_RECORD_TOTEM);
    return t > 0 ? static_cast<uint32_t>(t) : 0;
}

// Fill any still-unknown slot tool items by scanning the player's spellbook
// for totem summons and reading each one's required tool. Tools are fixed
// per element, so once a slot is resolved it never changes and the scan
// early-outs. Walks the flat learned-spell array `VAR_PLAYER_SPELLBOOK`
// (the same list `GetSpellName` reads) — the `IsPlayerSpell` bitmap
// doesn't reliably carry totems. Driven by SPELLS_CHANGED (when the
// spellbook is populated), so no polling.
void ScanTools() {
    bool anyUnknown = false;
    for (int s = 0; s < kSlots; ++s)
        if (g_toolItem[s] == 0) {
            anyUnknown = true;
            break;
        }
    if (!anyUnknown)
        return;

    auto *book = reinterpret_cast<const int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELLBOOK));
    for (int slot = 0; slot < Offsets::SPELLBOOK_MAX_SLOTS; ++slot) {
        const int spellID = book[slot];
        if (spellID <= 0)
            continue;
        const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
        if (rec == nullptr)
            continue;
        const auto *eff = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT);
        for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
            const int e = eff[i];
            if (e < kEffectSummonTotemSlot1 || e > kEffectSummonTotemSlot4)
                continue;
            const int s = e - kEffectSummonTotemSlot1;
            if (g_toolItem[s] == 0)
                g_toolItem[s] = TotemToolItem(rec);
            break;
        }
    }
}

// Re-resolve tool items whenever the player's spells (re)load.
// SPELLS_CHANGED is a no-arg event through the same dispatcher
// Event::SignalHook owns, so we observe it there (never suppress — return
// false). By the time it fires the spellbook is populated, so the scan
// sees the totems.
bool OnSignal(int eventID) {
    if (g_spellsChangedId < 0)
        g_spellsChangedId = Event::Custom::LookupByName("SPELLS_CHANGED");
    if (eventID == g_spellsChangedId)
        ScanTools();
    return false;
}

const Event::SignalHook::AutoSubscribe _spellsChanged{&OnSignal};

uint32_t SpellDurationMs(const uint8_t *rec) {
    using GetDuration_t = int(__fastcall *)(const uint8_t *rec, int pet,
                                            char skipMod);
    const int ms = reinterpret_cast<GetDuration_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_SPELL_DURATION))(rec, 0, 0);
    return ms > 0 ? static_cast<uint32_t>(ms) : 0;
}

// ---- the totem creature: find it, then follow it --------------------------

using EnumCallback_t = int(__fastcall *)(void *ctx, void *unusedEdx,
                                         uint64_t guid);
using EnumVisibleObjects_t = int(__fastcall *)(EnumCallback_t cb, void *ctx);
using GetUnitName_t = const char *(__thiscall *)(void *unit, int *outFlags);

const char *UnitName(void *unit) {
    return reinterpret_cast<GetUnitName_t>(
        static_cast<uintptr_t>(Offsets::FUN_OBJECT_GET_NAME))(unit, nullptr);
}

struct ScanCtx {
    uint64_t playerGuid;
    uint32_t entries[kSlots];   // 0 = slot not looking for its creature
    uint64_t stale[kSlots];     // the slot's previous totem, skipped
    uint64_t found[kSlots];
};

int __fastcall ScanCallback(ScanCtx *ctx, void * /*unusedEdx*/, uint64_t guid) {
    if (!Guid::IsCreature(guid))
        return 1;
    const uint32_t entry = static_cast<uint32_t>((guid >> 24) & 0xFFFFFFu);
    if (entry == 0)
        return 1;
    int match = -1;
    for (int s = 0; s < kSlots; ++s) {
        if (ctx->entries[s] == entry && ctx->found[s] == 0 &&
            ctx->stale[s] != guid) {
            match = s;
            break;
        }
    }
    if (match < 0)
        return 1;

    // Confirm it's OUR totem: resolve + check CreatedBy == player.
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (obj == nullptr)
        return 1;
    auto *desc = Game::Read<const uint8_t *>(
        obj, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 1;
    const uint64_t createdBy = Game::Read<uint64_t>(
        desc, Offsets::OFF_UNIT_FIELD_CREATEDBY);
    if (createdBy == ctx->playerGuid)
        ctx->found[match] = guid;
    return 1; // keep scanning — other slots may match other creatures
}

// PLAYER_TOTEM_UPDATE(totemSlot 1-4) — a TBC event absent from vanilla's
// table, reserved as a custom event and fired when a slot's state changes
// (totem dropped, its creature found or renamed, or the totem gone).
// Changes are recorded in a dirty mask by the cast path and the tick, then
// flushed from OnTick (WorldTick context — safe to run Lua handlers, unlike
// mid-packet).
constexpr const char *kPlayerTotemUpdate = "PLAYER_TOTEM_UPDATE";
const Event::Custom::AutoReserve _totemUpdate{kPlayerTotemUpdate};
int g_totemUpdateId = -1;
uint8_t g_dirtyMask = 0;

void MarkDirty(int slotIdx) {
    g_dirtyMask |= static_cast<uint8_t>(1u << slotIdx);
}

void FireDirty() {
    if (g_dirtyMask == 0)
        return;
    if (g_totemUpdateId < 0)
        g_totemUpdateId = Event::Custom::LookupByName(kPlayerTotemUpdate);
    if (g_totemUpdateId < 0)
        return; // slot not claimed yet — keep dirty, retry next tick
    for (int s = 0; s < kSlots; ++s)
        if (g_dirtyMask & (1u << s))
            Event::Custom::Fire(g_totemUpdateId, "%d", s + 1);
    g_dirtyMask = 0;
}

// 3.3.5 `CGGameUI::DestroyTotem` minus the CMSG_TOTEM_DESTROYED send.
void Destroy(int slotIdx) {
    g_slots[slotIdx] = Slot{};
    MarkDirty(slotIdx);
}

void OnTick() {
    // Out of the world: empty every slot without an event, as 3.3.5's
    // EnterWorld does. The object-manager reads below deref the manager
    // unconditionally, so nothing else runs either.
    if (Game::Read<void *volatile>(Offsets::VAR_LOCAL_PLAYER_PTR) == nullptr) {
        for (Slot &t : g_slots)
            t = Slot{};
        g_dirtyMask = 0;
        return;
    }

    const uint32_t now = NowMs();
    bool needScan = false;
    for (int s = 0; s < kSlots; ++s) {
        Slot &t = g_slots[s];
        if (!t.active)
            continue;
        if (t.guid != 0) {
            void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, t.guid, nullptr, 0);
            if (obj == nullptr) {
                Destroy(s);
                continue;
            }
            const char *name = UnitName(obj);
            if (name != t.name) {
                t.name = name;
                MarkDirty(s);
            }
        } else if (t.durationMs != 0 && now - t.startMs >= t.durationMs) {
            Destroy(s); // creature never seen — the duration is all we have
        } else {
            needScan = true;
        }
    }

    if (needScan && now - g_lastScanMs >= kScanIntervalMs) {
        const uint64_t player = Unit::Identity::PlayerGuid();
        if (player != 0) {
            g_lastScanMs = now;
            ScanCtx ctx{};
            ctx.playerGuid = player;
            for (int s = 0; s < kSlots; ++s) {
                const Slot &t = g_slots[s];
                if (t.active && t.guid == 0) {
                    ctx.entries[s] = t.entry;
                    ctx.stale[s] = t.staleGuid;
                }
            }

            auto Enum = reinterpret_cast<EnumVisibleObjects_t>(
                Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
            Enum(reinterpret_cast<EnumCallback_t>(&ScanCallback), &ctx);

            for (int s = 0; s < kSlots; ++s) {
                if (ctx.found[s] == 0)
                    continue;
                Slot &t = g_slots[s];
                t.guid = ctx.found[s];
                void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, t.guid, nullptr, 0);
                t.name = obj != nullptr ? UnitName(obj) : nullptr;
                MarkDirty(s);
            }
        }
    }

    FireDirty();
}

const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

// SpellIcon.dbc path for an icon ID (records indexed by ID; path at +4).
const char *IconPath(int iconID) {
    if (iconID <= 0)
        return nullptr;
    const int count = Game::Read<int>(Offsets::VAR_SPELL_ICON_COUNT);
    if (iconID > count)
        return nullptr;
    auto *const *records = Game::Read<const uint8_t *const *>(
        Offsets::VAR_SPELL_ICON_RECORDS);
    if (records == nullptr || records[iconID] == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(records[iconID] + 4);
}

// Pushes `s`, or nil for nullptr — Lua 5.1's `lua_pushstring(NULL)`, which
// 3.3.5 relies on for an unresolved name or icon.
void PushStringOrNil(void *L, const char *s) {
    if (s != nullptr)
        Game::Lua::PushString(L, s);
    else
        Game::Lua::PushNil(L);
}

// Reads the 1-based slot argument the way 3.3.5's totem scripts do: a usage
// error when it isn't a number, a range error outside 1-4. Returns the
// 0-based slot.
int CheckSlot(void *L, const char *usage) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, usage);
        return -1;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (slot < 0 || slot >= kSlots) {
        Game::Lua::Error(L, "Totem slot must be in range of 1 to %d", kSlots);
        return -1;
    }
    return slot;
}

// `GetTotemInfo(slot)` → `haveTotem, totemName, startTime, duration, icon,
// modRate, spellID`. The first five are 3.3.5's `Script_GetTotemInfo`
// (0x0051D330); retail appends `modRate` (always 1 — no per-totem haste in
// 1.12) and the summon `spellID` (0 when none). Slots: 1 Fire, 2 Earth,
// 3 Water, 4 Air. With a totem down: `true`, the creature's name (nil until
// it's seen), and start / duration in WHOLE seconds — 3.3.5 divides the
// millisecond fields as integers (`mul 0x10624DD3; shr 6`). With none:
// whether the player carries the slot's totem tool (3.3.5 searches the bags
// for the slot's TotemCategory; 1.12 has no TotemCategory, so the tool item
// comes from the totem spells' `Totem[0]`), then `"", 0, 0, "", 1, 0`.
int __fastcall Script_GetTotemInfo(void *L) {
    const int slot = CheckSlot(L, "Usage: GetTotemInfo(slot)");
    if (slot < 0)
        return 0;

    const Slot &t = g_slots[slot];
    if (t.active) {
        Game::Lua::PushBool(L, true);
        PushStringOrNil(L, t.name);
        Game::Lua::PushNumber(L, static_cast<double>(t.startMs / 1000));
        Game::Lua::PushNumber(L, static_cast<double>(t.durationMs / 1000));
        PushStringOrNil(L, t.icon);
        Game::Lua::PushNumber(L, 1.0);
        Game::Lua::PushNumber(L, static_cast<double>(t.spellID));
        return 7;
    }

    const uint32_t tool = g_toolItem[slot];
    Game::Lua::PushBool(
        L, tool != 0 && Item::Count::InInventory(static_cast<int>(tool)) > 0);
    Game::Lua::PushString(L, "");
    Game::Lua::PushNumber(L, 0.0);
    Game::Lua::PushNumber(L, 0.0);
    Game::Lua::PushString(L, "");
    Game::Lua::PushNumber(L, 1.0);
    Game::Lua::PushNumber(L, 0.0);
    return 7;
}

// Milliseconds until the slot's totem expires — 3.3.5's
// `TotemInfo::GetSpawnTimeLeft`.
uint32_t SpawnTimeLeft(const Slot &t) {
    return Time::Clock::Remaining(t.startMs + t.durationMs);
}

// `GetTotemTimeLeft(slot)` → whole seconds until the slot's totem expires,
// or `0` when none (3.3.5 0x00517C60, integer division like GetTotemInfo).
int __fastcall Script_GetTotemTimeLeft(void *L) {
    const int slot = CheckSlot(L, "Usage: GetTotemTimeLeft(slot)");
    if (slot < 0)
        return 0;
    const Slot &t = g_slots[slot];
    Game::Lua::PushNumber(
        L, t.active ? static_cast<double>(SpawnTimeLeft(t) / 1000) : 0.0);
    return 1;
}

// `GetTotemDuration(slot)` → the slot's active totem's full lifetime in
// whole seconds (GetTotemInfo's 4th value), or `0` when none. Retail returns
// a DurationObject here; seconds are the closest 1.12 shape.
int __fastcall Script_GetTotemDuration(void *L) {
    const int slot = CheckSlot(L, "Usage: GetTotemDuration(slot)");
    if (slot < 0)
        return 0;
    const Slot &t = g_slots[slot];
    Game::Lua::PushNumber(
        L, t.active ? static_cast<double>(t.durationMs / 1000) : 0.0);
    return 1;
}

// `TargetTotem(slot)` — 3.3.5's `CGGameUI::Target(m_guid)` on the slot's
// totem creature (1.12's CGGameUI::Target is `FUN_TARGET_BY_GUID`). No-op
// until the creature has been found.
int __fastcall Script_TargetTotem(void *L) {
    const int slot = CheckSlot(L, "Usage: TargetTotem(slot)");
    if (slot < 0)
        return 0;
    uint64_t guid = g_slots[slot].guid;
    if (guid == 0)
        return 0;
    using Target_t = void(__fastcall *)(const uint64_t *guid);
    reinterpret_cast<Target_t>(
        static_cast<uintptr_t>(Offsets::FUN_TARGET_BY_GUID))(&guid);
    return 0;
}

// ---- GameTooltip:SetTotem(slot) -------------------------------------------
//
// Built with the engine's own tooltip primitives — the per-tooltip clear
// and the raw add-line — exactly as retail's `Script_GameTooltip_SetTotem`
// does (no Lua method dispatch). Colors are the packed `{b,g,r,a}` values
// `FUN_GAMETOOLTIP_ADD_LINE` wants, pulled straight from `UI::ColorData`.

// Retail's two line colors, read verbatim from the 3.3.5 binary's SetTotem
// color data: name = NORMAL_FONT_COLOR (0xFFFFD200), time = white. Both are
// already in the 0xAARRGGBB packing the line-color setter consumes.
constexpr uint32_t kNameColor = UI::ColorData::ByTag("NORMAL_FONT_COLOR");
constexpr uint32_t kTimeColor = UI::ColorData::ByTag("HIGHLIGHT_FONT_COLOR");

// `GameTooltip:SetTotem(slot)` — 3.3.5's `Script_SetTotem` (0x006205c0).
// No-op (tooltip untouched) when the slot is out of range or empty;
// otherwise clears the tooltip and adds two lines, then shows it:
//
//   <totem name>          NORMAL_FONT_COLOR (yellow, 0xFFFFD200)
//   <time left>           white — GetDurationString(SPELL_TIME_REMAINING,
//                         roundUp): "%d Sec" floored under a minute,
//                         "%d Min" rounded up above, "0 Sec" once expired
//
// Totems never last an hour, so the _HOURS / _DAYS branches are left out.
int __fastcall Script_GameTooltipSetTotem(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE ||
        !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: GameTooltip:SetTotem(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (slot < 1 || slot > kSlots)
        return 0;
    const Slot &t = g_slots[slot - 1];
    if (!t.active)
        return 0;

    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;

    Tooltip::Lines::Clear(self);
    Tooltip::Lines::Add(self, t.name != nullptr ? t.name : "", kNameColor);

    const uint32_t msLeft = SpawnTimeLeft(t);
    const int savedTop = Game::Lua::GetTop(L);
    if (msLeft < 60000) {
        Game::Lua::PushLocalizedFormatInt(L, "SPELL_TIME_REMAINING_SEC",
                                          "%d Sec",
                                          static_cast<int>(msLeft / 1000));
    } else {
        const int minutes = static_cast<int>((msLeft - 1) / 60000 + 1); // ceil
        Game::Lua::PushLocalizedFormatInt(L, "SPELL_TIME_REMAINING_MIN",
                                          "%d Min", minutes);
    }
    const char *timeText = Game::Lua::ToString(L, -1);
    if (timeText != nullptr)
        Tooltip::Lines::Add(self, timeText, kTimeColor);
    Game::Lua::SetTop(L, savedTop);

    // Show ourselves — self is still at Lua stack index 1, which is what
    // Script_Show reads.
    Tooltip::Lines::Show(L);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTotemInfo", &Script_GetTotemInfo);
    Game::Lua::RegisterGlobalFunction("GetTotemTimeLeft",
                                      &Script_GetTotemTimeLeft);
    Game::Lua::RegisterGlobalFunction("GetTotemDuration",
                                      &Script_GetTotemDuration);
    Game::Lua::RegisterGlobalFunction("TargetTotem", &Script_TargetTotem);

    static const Game::Lua::FrameMethodEntry kTooltipMethods[] = {
        {"SetTotem", &Script_GameTooltipSetTotem},
    };
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_GAMETOOLTIP_METHOD_REGISTRY),
        kTooltipMethods,
        static_cast<int>(sizeof(kTooltipMethods) / sizeof(kTooltipMethods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void OnPlayerSpellGo(uint32_t spellID) {
    if (spellID == 0)
        return;
    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(spellID));
    if (rec == nullptr)
        return;

    const auto *effects = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT);
    const auto *misc = Game::Ptr<const int32_t>(rec, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        const int eff = effects[i];
        if (eff < kEffectSummonTotemSlot1 || eff > kEffectSummonTotemSlot4)
            continue;
        const int idx = eff - kEffectSummonTotemSlot1; // 0..3 (Fire..Air)
        Slot &t = g_slots[idx];
        // A recast replaces the slot's totem; the old creature may still be
        // visible for a moment, so the scan must not adopt it.
        const uint64_t previous = t.guid;
        t = Slot{};
        t.active = true;
        t.spellID = spellID;
        t.entry = static_cast<uint32_t>(misc[i]);
        t.staleGuid = previous;
        t.icon = IconPath(Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_ICON_ID));
        t.startMs = NowMs();
        t.durationMs = SpellDurationMs(rec);
        MarkDirty(idx); // totem dropped → PLAYER_TOTEM_UPDATE (fired next tick)
        // The cast spell IS a totem of this element, so it names the slot's
        // tool item directly — capture it (covers the slot even before the
        // spellbook scan runs).
        if (g_toolItem[idx] == 0)
            g_toolItem[idx] = TotemToolItem(rec);
        return; // one totem-slot effect per spell
    }
}

} // namespace Totem::Tracker
