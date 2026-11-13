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

// `GetSpellBonusDamage(school)` / `GetSpellBonusHealing()` — the local
// player's flat spell-damage (per school) and healing bonus.
//
// **Spell damage is a direct field read.** The value lives in the CGPlayer
// sub-struct at `[player + OFF_CGPLAYER_INFO]` (the +0xE68 blob), NOT the
// broadcast UpdateField descriptor at `+0x110` — the descriptor copy of
// this field is never sent to the client and reads zero (which sent an
// earlier version of this module down a "derive it from gear" path). The
// +0xE68 copy is the client's fully-computed value: `MOD_DAMAGE_DONE_POS[7]`
// at `+0xFD4`, `MOD_DAMAGE_DONE_NEG[7]` at `+0xFF0`, one int32 per school;
// net = POS − NEG. Because it's the finished value, gear + enchants + buffs
// + talents + set bonuses are all already baked in.
//
// This mirrors nampower's `Script_GetSpellPower` (same base + offsets),
// cross-verified in-game: a geared mage reads 613 generic across the magic
// schools with Fire at 647 (a +34 fire-specific piece on top of the 613).
//
// **Healing has no such field** — vanilla 1.12 never had `MOD_HEALING_DONE`
// (TBC addition; `PLAYER_FIELD_BYTES` follows `_PCT` directly), and nothing
// client-side computes a healing total (verified in-game: toggling a pure
// +44-healing item moved NO field in the +0xE68 struct). So
// `GetSpellBonusHealing` is DERIVED in two parts, both read from `Spell.dbc`:
//
//   1. Flat healing — Σ `SPELL_AURA_MOD_HEALING_DONE` (aura 135) off gear
//      equip-effects, item enchants, random suffixes, and active buffs.
//   2. Talent/buff stat→healing — the server's own `SpellBaseHealingBonusDone`
//      sums `SPELL_AURA_MOD_SPELL_HEALING_OF_STAT_PERCENT` (aura 175) ×
//      total Spirit / 100 over the unit's aura-175 auras. We mirror it: read
//      total Spirit from `UNIT_FIELD_STAT4` (descriptor +0x250), then sum
//      aura-175 percents over PASSIVE known spells (talents — Spiritual
//      Guidance, `Spell.dbc` ranks 14901/15028..15031 = 5/10/15/20/25%) plus
//      active buffs. Generic (any aura-175 talent/buff, incl. Turtle customs)
//      and robust — it reads the percent directly, no client-vs-server value
//      delta.
//
// An earlier attempt derived (2) as (spell_damage_field − derived_gear_damage)
// — the part of the damage field gear doesn't explain. That was FRAGILE: the
// field reflects values AFTER SpellMods, but our gear sum used raw DBC values.
// Inner Fire (10951) is +60 in the DBC but +78 live (Improved Inner Fire, a
// +15%/rank SpellMod), so an 18-point gap leaked into healing when that pure
// +damage buff was gained. Reading aura 175 directly (above) sidesteps it.
//
//   3. Set bonuses — the bonus spells of every equipped set whose member
//      count is met (ItemSet.dbc, the same rule the tooltip's set-bonus
//      lines use), decoded through the same aura walk.
//
// Residual: assumes talent ranks supersede (only the learned rank is known —
// otherwise summed) and ignores a set's profession-skill requirement.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Lookup.h"
#include "item/CGItem.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "item/StatAccum.h"
#include "player/StatSignal.h"
#include "spell/HealingAura.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Spell::BonusDamage {

namespace {

// ---------------------------------------------------------------------------
// Spell damage — direct read from the CGPlayer +0xE68 sub-struct.
// ---------------------------------------------------------------------------

const uint8_t *PlayerFields() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return nullptr;
    return Game::Read<const uint8_t *>(
        player, Offsets::OFF_CGPLAYER_INFO);
}

// Net spell damage for a 0-based school (0=physical .. 6=arcane). POS is the
// full per-school value (generic "+spell damage" is already folded into every
// magic school by the server), NEG the signed-magnitude negative accumulator.
long SpellDamageForSchool(int school0) {
    const uint8_t *pf = PlayerFields();
    if (pf == nullptr)
        return 0;
    const int32_t pos = Game::Read<int32_t>(
        pf, Offsets::OFF_PLAYER_FIELD_MOD_DAMAGE_DONE_POS + school0 * 4);
    const int32_t neg = Game::Read<int32_t>(
        pf, Offsets::OFF_PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + school0 * 4);
    return static_cast<long>(pos) - static_cast<long>(neg);
}

// ---------------------------------------------------------------------------
// Healing — derived (no client field exists in vanilla).
//
// Item flat +healing goes through the shared Item::StatAccum engine (base
// record + on-equip spells + random suffix + applied enchants) — exactly the
// ITEM_MOD_SPELL_HEALING_DONE_SHORT (aura 135) value GetItemStats reports, so
// the item spell/suffix/enchant walk isn't re-implemented here. Buff / talent
// healing (flat + Spirit/Armor conversions) is decoded from Spell.dbc below.
// ---------------------------------------------------------------------------

// Flat +healing contributed by one equipped item, via Item::StatAccum.
long ItemFlatHealing(const uint8_t *cgItem) {
    if (cgItem == nullptr)
        return 0;
    Item::StatAccum::Accum acc[Item::StatAccum::kCount];
    Item::StatAccum::Init(acc);

    const int itemID = Item::ID::FromCGItem(cgItem);
    if (itemID > 0)
        Item::StatAccum::AccumulateRecord(
            acc, Item::PeekRecord(static_cast<uint32_t>(itemID)), +1);

    auto *desc = Item::ObjectFields(cgItem);
    if (desc != nullptr) {
        // Random suffix ("… of Restoration") + applied permanent (slot 0) and
        // temporary (slot 1) enchants (Healing Power, Mana Oil, …).
        Item::StatAccum::ApplyRandomSuffix(
            acc, Game::Read<int32_t>(
                     desc, Offsets::OFF_DESCRIPTOR_RANDOM_PROPERTY), +1);
        for (int slot = 0; slot <= 1; ++slot)
            Item::StatAccum::ApplyEnchant(
                acc, Game::Read<uint32_t>(
                         desc, Offsets::OFF_DESCRIPTOR_ENCHANTMENT_ID +
                         slot * Offsets::DESCRIPTOR_ENCHANTMENT_SLOT_STRIDE), +1);
    }
    return Item::StatAccum::Value(acc, "ITEM_MOD_SPELL_HEALING_DONE_SHORT");
}

// --- Buff / talent healing auras (flat + Spirit/Armor conversions) ---
// One decoder for every healing aura a buff or passive talent can carry,
// mirroring the server's SpellBaseHealingBonusDone. Stock vanilla auras are
// handled inline:
//   135  MOD_HEALING_DONE                   → flat  += amount
//   175  MOD_SPELL_HEALING_OF_STAT_PERCENT  → += Spirit × amount / 100  (Spiritual Guidance)
// Any other aura index is deferred to `Spell::HealingAura::Resolve`, the
// extension point where server-custom healing auras live (e.g. Turtle's
// Ironclad, aura 199 = × Armor — see src/turtle/HealingAura.cpp). Amounts/
// percents are read straight from Spell.dbc — generic and robust (no
// client-vs-server value delta). `requirePassive` gates the bitmap walk to
// always-on talents, so a *known but not-currently-cast* buff (Divine
// Spirit) isn't counted as on; active buffs pass `requirePassive == false`.
constexpr int kAuraModHealingDone = 135;
constexpr int kAuraModHealingOfStatPercent = 175;  // × Spirit

void AddSpellHealingAuras(long &total, int spellID, int32_t spirit,
                          int32_t armor, bool requirePassive) {
    if (spellID <= 0)
        return;
    const uint8_t *sp = DBC::Record(Offsets::VAR_SPELL_RECORDS,
                                    Offsets::VAR_SPELL_RECORD_COUNT,
                                    static_cast<uint32_t>(spellID));
    if (sp == nullptr)
        return;
    if (requirePassive &&
        !(Game::Read<uint32_t>(sp, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) &
          Offsets::SPELL_ATTR_PASSIVE))
        return;
    auto aura = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_APPLY_AURA_NAME);
    auto base = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_BASE_POINTS);
    auto dice = Game::Ptr<const int32_t>(
        sp, Offsets::OFF_SPELL_RECORD_EFFECT_BASE_DICE);
    for (int i = 0; i < Offsets::SPELL_RECORD_EFFECT_COUNT; ++i) {
        const long amount = static_cast<long>(base[i]) + static_cast<long>(dice[i]);
        if (aura[i] == kAuraModHealingDone)
            total += amount;
        else if (aura[i] == kAuraModHealingOfStatPercent)
            total += static_cast<long>(spirit) * amount / 100;
        else
            total += Spell::HealingAura::Resolve(aura[i], amount, spirit, armor);
    }
}

// Set bonuses the equipped gear has earned. The engine's own set-bonus lines
// (the item tooltip, 0x52DFB9..) count how many of the set's members the
// player has equipped and light a bonus whose member count (ItemSet.dbc
// `+0x8C`) is met; the server applies the same bonus spells as auras. Those
// auras are neither buffs nor known spells, so this is the only pass that
// sees them — Lawbringer (3) is "Increase Healing 31", Judgement (3) is 57.
// A set member's record names its set (`OFF_ITEMSTATS_ITEM_SET`), so the
// equipped count per set is the equipped members' count.
constexpr int kEquipSlots = 19;

void AddSetBonusHealing(long &total, int32_t spirit, int32_t armor) {
    struct Worn { uint32_t setID; int count; };
    Worn worn[kEquipSlots];
    int sets = 0;
    for (int slot = 1; slot <= kEquipSlots; ++slot) {
        const int itemID = Item::ID::FromCGItem(Item::Location::ResolveEquipmentSlot(slot));
        const uint8_t *record = itemID > 0 ? Item::PeekRecord(static_cast<uint32_t>(itemID)) : nullptr;
        const uint32_t setID = record ? Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_ITEM_SET) : 0;
        if (setID == 0)
            continue;
        int s = 0;
        while (s < sets && worn[s].setID != setID)
            ++s;
        if (s == sets)
            worn[sets++] = {setID, 0};
        ++worn[s].count;
    }
    for (int s = 0; s < sets; ++s) {
        const uint8_t *set = DBC::Record(Offsets::VAR_ITEMSET_RECORDS, Offsets::VAR_ITEMSET_COUNT,
                                         worn[s].setID);
        if (set == nullptr)
            continue;
        auto spells = Game::Ptr<const uint32_t>(set, Offsets::OFF_ITEMSET_SPELL_IDS);
        auto needed = Game::Ptr<const uint32_t>(set, Offsets::OFF_ITEMSET_THRESHOLDS);
        for (int i = 0; i < Offsets::ITEMSET_MAX_BONUSES; ++i)
            if (spells[i] != 0 && needed[i] <= static_cast<uint32_t>(worn[s].count))
                AddSpellHealingAuras(total, static_cast<int>(spells[i]), spirit, armor,
                                     /*requirePassive=*/false);
    }
}

long ComputeHealing() {
    long total = 0;

    // Items — flat +healing via the shared StatAccum engine.
    for (int slot = 1; slot <= kEquipSlots; ++slot)
        total += ItemFlatHealing(Item::Location::ResolveEquipmentSlot(slot));

    // Player Spirit / Armor for the aura-175 / aura-199 conversions.
    const uint8_t *desc = Unit::Identity::PlayerDescriptor();
    const int32_t spirit = desc ? Game::Read<int32_t>(
        desc, Offsets::OFF_UNIT_FIELD_STAT_SPIRIT) : 0;
    const int32_t armor = desc ? Game::Read<int32_t>(
        desc, Offsets::OFF_UNIT_FIELD_RESISTANCE_ARMOR) : 0;

    AddSetBonusHealing(total, spirit, armor);

    // Active buffs — flat healing + Spirit/Armor conversions, one pass.
    auto *table = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_BUFF_TABLE));
    for (int i = 0; i < Offsets::PLAYER_BUFF_TABLE_COUNT; ++i) {
        const uint8_t *entry = table + i * Offsets::PLAYER_BUFF_ENTRY_STRIDE;
        if (Game::Read<int32_t>(
                entry, Offsets::OFF_PLAYER_BUFF_SLOT_CODE) < 0)
            continue;
        AddSpellHealingAuras(total, Game::Read<int32_t>(
                                        entry, Offsets::OFF_PLAYER_BUFF_SPELL_ID),
                             spirit, armor, /*requirePassive=*/false);
    }

    // Passive talents — Spirit/Armor conversions (Spiritual Guidance, Ironclad)
    // and any passive flat healing, via the known-spell bitmap (bounded by
    // spellCount). The passive gate assumes ranks supersede (only the learned
    // rank is known), so no per-rank dedup is needed.
    auto *bitmap = Game::Read<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_SPELL_BITMAP));
    const int spellCount = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_RECORD_COUNT));
    if (bitmap != nullptr && spellCount > 0) {
        for (int id = 1; id <= spellCount; ++id)
            if (bitmap[id >> 5] & (1u << (id & 31)))
                AddSpellHealingAuras(total, id, spirit, armor, /*requirePassive=*/true);
    }

    return total;
}

// Event-driven cache for the derived healing sum. The walk (StatAccum over 19
// items + the buff table + the known-spell bitmap) is expensive, so we
// recompute it ONLY when the player's stat inputs change — Player::StatSignal
// is bumped from the aura and equipment hooks, so a per-frame
// GetSpellBonusHealing poll is O(1) between real gear/buff changes.
//
// Every input to the sum is now event-driven: the aura, equipment, and
// spell-learn/unlearn hooks all bump Player::StatSignal (buffs, gear, and
// talent respecs respectively). This coarse world-tick recompute (at most once
// per kFallbackTicks frames, ~4s) is a pure backstop against a missed signal —
// e.g. a player aura the object-pointer guard in aura/Source didn't match —
// bounding staleness rather than being the primary mechanism.
constexpr int kFallbackTicks = 256;
uint32_t g_cachedEpoch = 0;     // 0 = never computed (epoch starts at 1)
int g_ticksSinceCompute = 0;
long g_cachedHealing = 0;

void OnTick() { ++g_ticksSinceCompute; }
const Tick::WorldTick::AutoSubscribe _tick{&OnTick};

long CurrentHealing() {
    const uint32_t epoch = Player::StatSignal::Epoch();
    if (epoch != g_cachedEpoch || g_ticksSinceCompute >= kFallbackTicks) {
        g_cachedHealing = ComputeHealing();
        g_cachedEpoch = epoch;
        g_ticksSinceCompute = 0;
    }
    return g_cachedHealing;
}

// ---------------------------------------------------------------------------
// Lua bindings.
// ---------------------------------------------------------------------------

// `GetSpellBonusDamage(school)` → number. `school` is 1-based (1=Physical
// through 7=Arcane). Raises the usage error for a missing / out-of-range
// school, matching 3.3.5's Script_GetSpellBonusDamage (0x0060E310).
int __fastcall Script_GetSpellBonusDamage(void *L) {
    const int school =
        Game::Lua::IsNumber(L, 1) ? static_cast<int>(Game::Lua::ToNumber(L, 1)) : 0;
    const unsigned idx = static_cast<unsigned>(school - 1);
    if (idx >= static_cast<unsigned>(Offsets::SPELL_SCHOOL_COUNT)) {
        Game::Lua::Error(L, "Usage: GetSpellBonusDamage(school)");
        return 0;
    }
    Game::Lua::PushNumber(L, static_cast<double>(SpellDamageForSchool(static_cast<int>(idx))));
    return 1;
}

// `GetSpellBonusHealing()` → number. The player's flat healing bonus (derived).
int __fastcall Script_GetSpellBonusHealing(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CurrentHealing()));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kBonusDamageArgs[] = {
    Game::Doc::Req("school", "luaIndex", "1 Physical, 2 Holy, 3 Fire, 4 Nature, 5 Frost, "
                                         "6 Shadow, 7 Arcane."),
};
const Game::Doc::Field kBonusRets[] = {
    Game::Doc::Req("result", "number"),
};
const Game::Doc::Function kGetSpellBonusDamage{
    "Returns your bonus spell damage for one magic school, with gear, buffs and talents "
    "included.",
    kBonusDamageArgs, kBonusRets, "PlayerGlobals"};
const Game::Doc::Function kGetSpellBonusHealing{
    "Returns your bonus healing from gear, enchants, set bonuses, buffs and Spirit-to-healing "
    "talents.",
    {}, kBonusRets, "PlayerGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSpellBonusDamage", &Script_GetSpellBonusDamage,
                                      &kGetSpellBonusDamage);
    Game::Lua::RegisterGlobalFunction("GetSpellBonusHealing", &Script_GetSpellBonusHealing,
                                      &kGetSpellBonusHealing);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::BonusDamage
