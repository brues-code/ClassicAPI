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

// `GetTotemInfo(slot)` — the four-element totem bar, backported. Vanilla
// 1.12 tracks nothing about the shaman's totems client-side (the totem
// bar, PLAYER_TOTEM_UPDATE, and GetTotemInfo are all TBC additions), so
// this is a self-tracking subsystem — but a fully DATA-DRIVEN one:
//
//   * SLOT is read straight from the totem summon spell's `Spell.dbc`
//     effect: 87/88/89/90 = SUMMON_TOTEM_SLOT1..4 = Fire/Earth/Water/Air,
//     so `slot = effect - 86`. No hardcoded spell table; Turtle custom
//     totems self-classify.
//   * CREATURE ENTRY is the effect's `EffectMiscValue` (the summoned
//     totem's creature-template ID) — used to detect the totem's death.
//   * DURATION is the summon spell's `SpellDuration.dbc` value (via the
//     engine's own duration helper, so player duration mods apply).
//
// Detection: the player's totem cast fires SMSG_SPELL_GO, which
// `Aura::Source` already co-hooks; it calls `OnPlayerSpellGo` here (before
// its aura gate, since a summon applies no aura). We stamp the slot with
// start time + duration.
//
// Death / removal: on WorldTick we scan the object manager for a
// player-owned creature of the slot's entry. A "seen once, then gone"
// latch clears the slot early (totem killed, Totemic Recall, or manually
// destroyed) — the owner check (CreatedBy == player) means another
// shaman's same-entry totem can't keep ours alive, and the seen-latch
// tolerates the ~1-tick spawn lag before the totem appears. Duration is
// the backstop for the normal case. A recast into the same slot simply
// re-stamps it.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "guid/Guid.h"
#include "item/Count.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
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
    bool active;
    bool seen;          // the totem creature has been observed in the world
    uint32_t spellID;
    uint32_t entry;     // summoned creature-template ID (for death detection)
    uint32_t startMs;   // FUN_OS_TICKCOUNT_MS (shares GetTime's epoch × 1000)
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
    using GetDuration_t = int(__fastcall *)(const uint8_t *rec, int unit,
                                            char skipMod);
    const int ms = reinterpret_cast<GetDuration_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_SPELL_DURATION))(rec, 0, 0);
    return ms > 0 ? static_cast<uint32_t>(ms) : 0;
}

// ---- object-manager existence scan (player-owned creature by entry) ------

using EnumCallback_t = int(__fastcall *)(void *ctx, void *unusedEdx,
                                         uint64_t guid);
using EnumVisibleObjects_t = int(__fastcall *)(EnumCallback_t cb, void *ctx);

struct ScanCtx {
    uint64_t playerGuid;
    uint32_t entries[kSlots]; // 0 = slot not being checked
    bool present[kSlots];
};

int __fastcall ScanCallback(ScanCtx *ctx, void * /*unusedEdx*/, uint64_t guid) {
    if (!Guid::IsCreature(guid))
        return 1;
    const uint32_t entry = static_cast<uint32_t>((guid >> 24) & 0xFFFFFFu);
    if (entry == 0)
        return 1;
    int match = -1;
    for (int s = 0; s < kSlots; ++s) {
        if (ctx->entries[s] == entry) {
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
        ctx->present[match] = true;
    return 1; // keep scanning — other slots may match other creatures
}

// On-demand lookup of the live GUID of the player's totem creature of a
// given entry (for TargetTotem). Returns 0 when none is visible.
struct FindGuidCtx {
    uint64_t playerGuid;
    uint32_t entry;
    uint64_t found;
};

int __fastcall FindGuidCallback(FindGuidCtx *ctx, void * /*unusedEdx*/,
                                uint64_t guid) {
    if (!Guid::IsCreature(guid))
        return 1;
    if (static_cast<uint32_t>((guid >> 24) & 0xFFFFFFu) != ctx->entry)
        return 1;
    void *obj = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    if (obj == nullptr)
        return 1;
    auto *desc = Game::Read<const uint8_t *>(
        obj, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 1;
    if (Game::Read<uint64_t>(
            desc, Offsets::OFF_UNIT_FIELD_CREATEDBY) == ctx->playerGuid) {
        ctx->found = guid;
        return 0; // stop
    }
    return 1;
}

uint64_t FindTotemGuid(uint32_t entry) {
    if (entry == 0)
        return 0;
    if (Game::Read<void *volatile>(Offsets::VAR_LOCAL_PLAYER_PTR) ==
        nullptr)
        return 0;
    const uint64_t player = Unit::Identity::PlayerGuid();
    if (player == 0)
        return 0;
    FindGuidCtx ctx{player, entry, 0};
    auto Enum = reinterpret_cast<EnumVisibleObjects_t>(
        Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
    Enum(reinterpret_cast<EnumCallback_t>(&FindGuidCallback), &ctx);
    return ctx.found;
}

// PLAYER_TOTEM_UPDATE(totemSlot 1-4) — a TBC event absent from vanilla's
// table, reserved as a custom event and fired when a slot's state changes
// (totem dropped, expired, killed, or recalled). Changes are recorded in a
// dirty mask by the cast path and the clear paths, then flushed from OnTick
// (WorldTick context — safe to run Lua handlers, unlike mid-packet).
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

void OnTick() {
    const uint32_t now = NowMs();

    // Duration backstop — always cheap, runs every tick.
    bool anyActive = false;
    for (int s = 0; s < kSlots; ++s) {
        Slot &t = g_slots[s];
        if (!t.active)
            continue;
        if (t.durationMs != 0 && now - t.startMs >= t.durationMs) {
            t.active = false;
            MarkDirty(s);
            continue;
        }
        anyActive = true;
    }

    // Death / removal scan — throttled, and only while a slot is up. The
    // object-manager enumerator derefs the manager unconditionally, so it's
    // gated on the local player existing.
    if (anyActive && now - g_lastScanMs >= kScanIntervalMs &&
        Game::Read<void *volatile>(Offsets::VAR_LOCAL_PLAYER_PTR) !=
            nullptr) {
        const uint64_t player = Unit::Identity::PlayerGuid();
        if (player != 0) {
            g_lastScanMs = now;
            ScanCtx ctx{};
            ctx.playerGuid = player;
            for (int s = 0; s < kSlots; ++s)
                ctx.entries[s] = g_slots[s].active ? g_slots[s].entry : 0;

            auto Enum = reinterpret_cast<EnumVisibleObjects_t>(
                Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS);
            Enum(reinterpret_cast<EnumCallback_t>(&ScanCallback), &ctx);

            for (int s = 0; s < kSlots; ++s) {
                Slot &t = g_slots[s];
                if (!t.active)
                    continue;
                if (ctx.present[s])
                    t.seen = true;
                else if (t.seen) {
                    t.active = false; // was up, now gone → died / recalled
                    MarkDirty(s);
                }
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

// Pushes the empty tail (no active totem) after `haveTotem` has been
// pushed by the caller. Returns the total count including that first value.
int PushEmptyTail(void *L) {
    Game::Lua::PushString(L, "");    // totemName
    Game::Lua::PushNumber(L, 0.0);   // startTime
    Game::Lua::PushNumber(L, 0.0);   // duration
    Game::Lua::PushNil(L);           // icon
    Game::Lua::PushNumber(L, 1.0);   // modRate
    Game::Lua::PushNumber(L, 0.0);   // spellID
    return 7;
}

// `GetTotemInfo(slot)` → `haveTotem, totemName, startTime, duration, icon,
// modRate, spellID`. Slots: 1 Fire, 2 Earth, 3 Water, 4 Air. `haveTotem`
// is TOOL presence (the documented meaning) — whether the player carries
// the slot's totem tool item (Earth/Fire/Water/Air Totem) — NOT whether a
// totem is summoned; the active totem is signaled by a non-empty
// `totemName` / non-zero `startTime`.
int __fastcall Script_GetTotemInfo(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTotemInfo(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot < 1 || slot > kSlots) {
        Game::Lua::PushBool(L, false);
        return PushEmptyTail(L);
    }

    // Tool presence — independent of the summoned state. The tool item
    // (resolved from the totem spell's Totem[0] on SPELLS_CHANGED, not
    // hardcoded) is checked against the bag. The bag walk reads the Lua
    // stack, so it runs before we push any return values (`slot` captured).
    const uint32_t tool = g_toolItem[slot - 1];
    const bool haveTotem =
        tool != 0 && Item::Count::InInventory(L, static_cast<int>(tool)) > 0;

    const Slot &t = g_slots[slot - 1];
    if (!t.active) {
        Game::Lua::PushBool(L, haveTotem);
        return PushEmptyTail(L);
    }

    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(t.spellID));
    const char *name = "";
    const char *icon = nullptr;
    if (rec != nullptr) {
        const int locale =
            Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
        const char *n = Game::Read<const char *>(
            rec, Offsets::OFF_SPELL_NAMES + locale * 4);
        if (n != nullptr)
            name = n;
        icon = IconPath(Game::Read<int>(rec, Offsets::OFF_SPELL_RECORD_ICON_ID));
    }

    Game::Lua::PushBool(L, haveTotem);
    Game::Lua::PushString(L, name);
    Game::Lua::PushNumber(L, static_cast<double>(t.startMs) * 0.001);
    Game::Lua::PushNumber(L, static_cast<double>(t.durationMs) / 1000.0);
    if (icon != nullptr)
        Game::Lua::PushString(L, icon);
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushNumber(L, 1.0); // modRate — no per-totem haste in 1.12
    Game::Lua::PushNumber(L, static_cast<double>(t.spellID));
    return 7;
}

// `GetTotemTimeLeft(slot)` → seconds until the slot's totem is auto-
// destroyed, or `0` when no totem is active in that slot. Slots: 1 Fire,
// 2 Earth, 3 Water, 4 Air.
int __fastcall Script_GetTotemTimeLeft(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTotemTimeLeft(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    double seconds = 0.0;
    if (slot >= 1 && slot <= kSlots) {
        const Slot &t = g_slots[slot - 1];
        if (t.active && t.durationMs != 0) {
            const uint32_t left = Time::Clock::Remaining(t.startMs + t.durationMs);
            if (left != 0)
                seconds = static_cast<double>(left) / 1000.0;
        }
    }
    Game::Lua::PushNumber(L, seconds);
    return 1;
}

// `GetTotemDuration(slot)` → the total duration (seconds) of the slot's
// active totem, or `0` when none. This is the totem's full lifetime from
// cast (the 4th value of GetTotemInfo); see GetTotemTimeLeft for the
// remaining time.
int __fastcall Script_GetTotemDuration(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetTotemDuration(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    double seconds = 0.0;
    if (slot >= 1 && slot <= kSlots) {
        const Slot &t = g_slots[slot - 1];
        if (t.active)
            seconds = static_cast<double>(t.durationMs) / 1000.0;
    }
    Game::Lua::PushNumber(L, seconds);
    return 1;
}

// `TargetTotem(slot)` — target the player's totem in the given slot
// (1 Fire, 2 Earth, 3 Water, 4 Air). No-op when the slot has no active
// totem or the totem creature isn't currently visible. Finds the totem
// creature's live GUID and sets the target via the same engine path
// `TargetUnit` uses.
int __fastcall Script_TargetTotem(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: TargetTotem(slot)");
        return 0;
    }
    const int slot = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (slot < 1 || slot > kSlots)
        return 0;
    const Slot &t = g_slots[slot - 1];
    if (!t.active || t.entry == 0)
        return 0;
    uint64_t guid = FindTotemGuid(t.entry);
    if (guid == 0)
        return 0;
    using TargetByGuid_t = void(__fastcall *)(const uint64_t *guid);
    reinterpret_cast<TargetByGuid_t>(
        static_cast<uintptr_t>(Offsets::FUN_TARGET_BY_GUID))(&guid);
    return 0;
}

// ---- GameTooltip:SetTotem(slot) -------------------------------------------
//
// Built with the engine's own tooltip primitives — the per-tooltip clear
// and the raw add-line — exactly as retail's `Script_GameTooltip_SetTotem`
// does (no Lua method dispatch). Colors are the packed `{b,g,r,a}` values
// `FUN_GAMETOOLTIP_ADD_LINE` wants, pulled straight from `UI::ColorData`.

using AddLine_t = void(__thiscall *)(void *self, const char *left, const char *right,
                                     const void *leftColor, const void *rightColor, int wrap);
using ClearTooltip_t = void(__fastcall *)(void *self);
using ShowScript_t = int(__fastcall *)(void *L); // engine's Script_Show

// Retail's two line colors, read verbatim from the 3.3.5 binary's SetTotem
// color data: name = NORMAL_FONT_COLOR (0xFFFFD200), time = white. Both are
// already in the 0xAARRGGBB packing the line-color setter consumes.
constexpr uint32_t kNameColor = UI::ColorData::ByTag("NORMAL_FONT_COLOR");
constexpr uint32_t kTimeColor = UI::ColorData::ByTag("HIGHLIGHT_FONT_COLOR");

// Left-aligned colored line via the engine's raw add-line. `color` must
// outlive the call (its address is passed through).
void AddColoredLine(void *self, const char *text, const uint32_t &color) {
    reinterpret_cast<AddLine_t>(Offsets::FUN_GAMETOOLTIP_ADD_LINE)(
        self, text, nullptr, &color, nullptr, 0);
}

// `GameTooltip:SetTotem(slot)` — a TBC (2.4.0) tooltip method backported to
// 1.12. Fills the tooltip with the slot's active totem, mirroring retail's
// two-line shape (3.3.5 `Script_GameTooltip_SetTotem` at 0x006205c0):
//
//   <Totem Name>          NORMAL_FONT_COLOR (yellow, 0xFFFFD200)
//   <Time Left>           white — SecondsToTimeAbbrev(SPELL_TIME_REMAINING)
//
// The time line mirrors retail's `SecondsToTimeAbbrev` with its round-up
// flag: raw seconds under a minute, minutes rounded up above. The unit
// wording is localized through the project's GlobalString helper (retail's
// `SPELL_TIME_REMAINING_SEC`/`_MIN` keys, English fallback). No-op (tooltip
// left untouched) when the slot has no active totem — same as retail's
// `+8/+0xc` timing guard. Shows itself at the end (via the engine's
// Script_Show) — retail's TotemFrame.xml OnEnter calls SetTotem with no
// trailing :Show(), so the method is expected to show.
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
    if (!t.active || t.spellID == 0)
        return 0;

    void *self = Game::Lua::ResolveTooltip(L);
    if (self == nullptr)
        return 0;

    const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(t.spellID));
    if (rec == nullptr)
        return 0;
    const int locale =
        Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    const char *name =
        Game::Read<const char *>(rec, Offsets::OFF_SPELL_NAMES + locale * 4);
    if (name == nullptr || name[0] == '\0')
        return 0;

    // Milliseconds remaining (0 = unknown / expired duration).
    uint32_t msLeft = 0;
    if (t.durationMs != 0)
        msLeft = Time::Clock::Remaining(t.startMs + t.durationMs);

    reinterpret_cast<ClearTooltip_t>(Offsets::FUN_GAMETOOLTIP_CLEAR)(self);
    AddColoredLine(self, name, kNameColor);

    if (msLeft > 0) {
        const int savedTop = Game::Lua::GetTop(L);
        if (msLeft < 60000) {
            Game::Lua::PushLocalizedFormatInt(L, "SPELL_TIME_REMAINING_SEC",
                                              "%d Sec",
                                              static_cast<int>(msLeft / 1000));
        } else {
            const int minutes = static_cast<int>((msLeft + 59999) / 60000); // ceil
            Game::Lua::PushLocalizedFormatInt(L, "SPELL_TIME_REMAINING_MIN",
                                              "%d Min", minutes);
        }
        const char *timeText = Game::Lua::ToString(L, -1);
        if (timeText != nullptr)
            AddColoredLine(self, timeText, kTimeColor);
        Game::Lua::SetTop(L, savedTop);
    }

    // Show ourselves — self is still at Lua stack index 1, which is what
    // Script_Show reads.
    reinterpret_cast<ShowScript_t>(Offsets::FUN_SCRIPT_FRAME_SHOW)(L);
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
        t.active = true;
        t.seen = false;
        t.spellID = spellID;
        t.entry = static_cast<uint32_t>(misc[i]);
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
