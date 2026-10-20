// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `GetAverageItemLevel()` → `(avgItemLevel, avgItemLevelEquipped)` and the
// `PLAYER_AVG_ITEM_LEVEL_READY` event. A line-for-line port of 4.3.4's
// `CGCharacterInfo::CalculateAverageItemLevel` (0x0097E0F0) and
// `Script_GetAverageItemLevel` (0x0097EE60) — wowdecomp15595
// Ui/PaperDollInfoFrame.cpp, PaperDollInfoFrame_Scripts.cpp.
//
// The calculation:
//   - Walks player inventory slots 0..18 (equipped only) or 0..last bank bag
//     (everything). An item that is a container contributes its CONTENTS,
//     not itself, so equipped bags, bank bags and quivers are looked inside
//     and an empty bag in the backpack adds nothing.
//   - Skips any item the player cannot use (`CGPlayer_C::CanUseItem`) and
//     any with inventory type 0.
//   - Keeps the best item level per INVENTORY TYPE, plus the second best for
//     rings, trinkets and one-hand weapons (the types that fill two slots).
//   - Merges chest + robe, and ranged + thrown + ranged-right + relic.
//   - Weapons: a player who can dual wield takes the two best of {main-hand
//     only, best of shield / off-hand / held-in-off-hand, best one-hand,
//     second one-hand}; anyone else takes max(main-hand only, one-hand) and
//     max(held-in-off-hand, shield). The answer is the better of
//     (rest + mainHand + offHand) / 17 and (rest + twoHand) / 16, in float.
//
// 1.12 differences, each the vanilla form of the same rule:
//   - Dual wield: 4.3.4 hard-codes the classes that learn Dual Wield (and
//     adds two two-handers for a Titan's Grip warrior). We ask whether this
//     character has learned Dual Wield (Spell::Known::CanDualWield). No
//     Titan's Grip, so the second-two-hander bucket is gone.
//   - The everything walk ends at INVMGR_BANK_BAG_LAST_SLOT (68), 1.12's last
//     bank-bag slot (4.3.4: 73). Reading the GUID array directly reaches the
//     bank without the bank window open, as 4.3.4's walk does.
//   - A container whose own record is missing is still looked inside, so
//     one pass requests every missing record (see Calculate).
//   - Quiver (type 27) is zeroed with the other non-slot types. 1.12's
//     quivers are containers, so the walk already looks inside them; 4.3.4
//     had no quivers to zero.
//
// Missing records. 4.3.4 requests each uncached record with a callback,
// counts the outstanding ones, returns 0 from the calculation while any
// are out, and when the last arrives fires PLAYER_AVG_ITEM_LEVEL_READY. The
// script keeps the last complete answer and pushes that meanwhile. We do
// the same through `Item::Data::WarmCache` (the module that owns every
// item query and its world gate / rate limit), waiting on the set of
// itemIDs and observing completions with a `LoadSubscriber`. One change: a
// FAILED load also leaves the set. 4.3.4 only counts successes down, which
// strands the answer until logout; here a query lost to a loading screen
// fails after its resends, and the next call asks again.
//
// 4.3.4 also caches the answer behind a dirty flag set on container
// updates; we recompute on every call (same answer, always current, no
// container hook).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"
#include "object/Resolve.h"
#include "player/CanUseItem.h"
#include "spell/Known.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace Item::AverageLevel {

namespace {

constexpr const char *kReadyEvent = "PLAYER_AVG_ITEM_LEVEL_READY";
const Game::Doc::Event kReadyDoc{
    "PaperDollInfo",
    "Fires when the item data that GetAverageItemLevel was waiting for has loaded.",
    {}};
const Event::Custom::AutoReserve _reserveReady{kReadyEvent, &kReadyDoc};

// Inventory types (ItemRecord InventoryType).
constexpr uint32_t kTypeBody = 4;
constexpr uint32_t kTypeChest = 5;
constexpr uint32_t kTypeFinger = 11;
constexpr uint32_t kTypeTrinket = 12;
constexpr uint32_t kTypeWeapon = 13;
constexpr uint32_t kTypeShield = 14;
constexpr uint32_t kTypeRanged = 15;
constexpr uint32_t kType2HWeapon = 17;
constexpr uint32_t kTypeBag = 18;
constexpr uint32_t kTypeTabard = 19;
constexpr uint32_t kTypeRobe = 20;
constexpr uint32_t kTypeWeaponMainHand = 21;
constexpr uint32_t kTypeWeaponOffHand = 22;
constexpr uint32_t kTypeHoldable = 23;
constexpr uint32_t kTypeAmmo = 24;
constexpr uint32_t kTypeThrown = 25;
constexpr uint32_t kTypeRangedRight = 26;
constexpr uint32_t kTypeQuiver = 27;
constexpr uint32_t kTypeRelic = 28;
constexpr int kTypeCount = 29;

constexpr int kLastEquippedSlot = 18; // 0-based: INVSLOT_TABARD

// Item IDs whose records the last calculation asked for and that have not
// loaded yet (4.3.4's m_avgItemLevelPending, as a set so each completion
// is matched to its item).
std::vector<uint32_t> g_waiting;
// The last complete answer (m_avgItemLevel / m_avgItemLevelEquipped).
float g_avgItemLevel = 0.0f;
float g_avgItemLevelEquipped = 0.0f;

struct Levels {
    int32_t byType[kTypeCount] = {};
    int32_t secondFinger = 0;
    int32_t secondTrinket = 0;
    int32_t secondWeapon = 0;
};

// UpdateBestItemLevel (0x0097B360).
void UpdateBest(int32_t level, int32_t *best, int32_t *second) {
    if (second == nullptr) {
        *best = std::max(*best, level);
    } else if (level > *best) {
        *second = *best;
        *best = level;
    } else if (level > *second) {
        *second = level;
    }
}

struct Walk {
    const uint8_t *player;
    Levels levels;
    bool incomplete = false; // a record is missing
};

// The record for `item`, or nullptr after queuing its load and noting the
// wait.
const uint8_t *RecordOrWait(Walk &walk, const uint8_t *item) {
    const int itemID = Item::ID::FromCGItem(item);
    if (itemID <= 0)
        return nullptr;
    if (const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID)))
        return record;
    walk.incomplete = true;
    if (Item::Data::WarmCache(static_cast<uint32_t>(itemID)) &&
        std::find(g_waiting.begin(), g_waiting.end(), static_cast<uint32_t>(itemID)) ==
            g_waiting.end())
        g_waiting.push_back(static_cast<uint32_t>(itemID));
    return nullptr;
}

// The player can use the item and it has an inventory type.
bool IsUsable(const Walk &walk, const uint8_t *record) {
    return Player::CanUseItem::Check(walk.player, record) &&
           Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE) != 0;
}

// The record of a usable, equippable item, else nullptr.
const uint8_t *UsableRecord(Walk &walk, const uint8_t *item) {
    const uint8_t *record = RecordOrWait(walk, item);
    return record != nullptr && IsUsable(walk, record) ? record : nullptr;
}

void Bucket(Levels &levels, const uint8_t *record) {
    const uint32_t type = Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    if (type >= static_cast<uint32_t>(kTypeCount))
        return;
    int32_t *second = nullptr;
    switch (type) {
    case kTypeFinger:
        second = &levels.secondFinger;
        break;
    case kTypeTrinket:
        second = &levels.secondTrinket;
        break;
    case kTypeWeapon:
        second = &levels.secondWeapon;
        break;
    default:
        break;
    }
    UpdateBest(static_cast<int32_t>(
                   Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_ITEM_LEVEL)),
               &levels.byType[type], second);
}

const uint64_t *GuidArray(const uint8_t *invMgr, int &count) {
    count = static_cast<int>(Game::Read<uint32_t>(invMgr, Offsets::OFF_INVMGR_SLOT_COUNT));
    return Game::Read<const uint64_t *>(invMgr, Offsets::OFF_INVMGR_GUID_ARRAY);
}

void WalkContainer(Walk &walk, const uint8_t *container) {
    auto *invMgr = static_cast<const uint8_t *>(Item::Location::ContainerInventory(container));
    if (invMgr == nullptr)
        return;
    int count = 0;
    const uint64_t *guids = GuidArray(invMgr, count);
    if (guids == nullptr)
        return;
    for (int i = 0; i < count; ++i) {
        const uint8_t *item = Item::Location::ResolveByGUID(guids[i]);
        if (item == nullptr)
            continue;
        if (const uint8_t *record = UsableRecord(walk, item))
            Bucket(walk.levels, record);
    }
}

// `complete` is cleared when a record was missing (its load queued, or the
// queue full); the 0 returned then is not an answer.
float Calculate(bool equippedOnly, bool &complete) {
    if (!g_waiting.empty()) {
        complete = false;
        return 0.0f;
    }
    const uint8_t *player = Unit::Identity::PlayerObject();
    const uint8_t *invMgr = Unit::Identity::PlayerInventoryManager();
    if (player == nullptr || invMgr == nullptr)
        return 0.0f;

    Walk walk{player};
    int count = 0;
    const uint64_t *guids = GuidArray(invMgr, count);
    if (guids == nullptr)
        return 0.0f;
    const int lastSlot =
        std::min(equippedOnly ? kLastEquippedSlot : Offsets::INVMGR_BANK_BAG_LAST_SLOT, count - 1);
    for (int slot = 0; slot <= lastSlot; ++slot) {
        const uint8_t *item = Item::Location::ResolveByGUID(guids[slot]);
        if (item == nullptr)
            continue;
        const uint8_t *record = RecordOrWait(walk, item);
        const auto *container = static_cast<const uint8_t *>(
            Object::ByGuid(Offsets::TYPEMASK_CONTAINER, guids[slot], "AverageLevel"));
        // A container whose own record is still loading is walked anyway,
        // only to queue its contents' loads in this same pass; the walk is
        // already incomplete, so nothing it buckets is used. 4.3.4 skips
        // it, which costs a second round (and a READY fired before the
        // answer is) once the bag's record arrives.
        if (record == nullptr) {
            if (container != nullptr)
                WalkContainer(walk, container);
            continue;
        }
        if (!IsUsable(walk, record))
            continue;
        if (container != nullptr)
            WalkContainer(walk, container);
        else
            Bucket(walk.levels, record);
    }
    if (walk.incomplete) {
        complete = false;
        return 0.0f;
    }

    int32_t *lv = walk.levels.byType;
    const int32_t ranged =
        std::max({lv[kTypeRangedRight], lv[kTypeRelic], lv[kTypeThrown], lv[kTypeRanged]});
    const int32_t twoHand = lv[kType2HWeapon];
    int32_t mainHand;
    int32_t offHand;
    if (Spell::Known::CanDualWield()) {
        int32_t weapons[4] = {
            lv[kTypeWeaponMainHand],
            std::max({lv[kTypeShield], lv[kTypeWeaponOffHand], lv[kTypeHoldable]}),
            lv[kTypeWeapon],
            walk.levels.secondWeapon,
        };
        std::sort(std::begin(weapons), std::end(weapons), std::greater<int32_t>());
        mainHand = weapons[0];
        offHand = weapons[1];
    } else {
        mainHand = std::max(lv[kTypeWeaponMainHand], lv[kTypeWeapon]);
        offHand = std::max(lv[kTypeHoldable], lv[kTypeShield]);
    }

    for (uint32_t type : {0u, kTypeBody, kTypeRelic, kType2HWeapon, kTypeBag, kTypeTabard,
                          kTypeWeapon, kTypeShield, kTypeRanged, kTypeWeaponMainHand,
                          kTypeWeaponOffHand, kTypeHoldable, kTypeAmmo, kTypeThrown,
                          kTypeRangedRight, kTypeQuiver})
        lv[type] = 0;
    lv[kTypeChest] = std::max(lv[kTypeChest], lv[kTypeRobe]);
    lv[kTypeRobe] = 0;

    int32_t total = 0;
    for (int type = 1; type < kTypeCount; ++type)
        total += lv[type];
    total += walk.levels.secondFinger + walk.levels.secondTrinket + ranged;
    const float oneHanded = static_cast<float>(total + mainHand + offHand) * (1.0f / 17.0f);
    const float twoHanded = static_cast<float>(total + twoHand) * (1.0f / 16.0f);
    return oneHanded > twoHanded ? oneHanded : twoHanded;
}

void OnItemLoaded(uint32_t itemID, bool /*success*/) {
    const auto it = std::find(g_waiting.begin(), g_waiting.end(), itemID);
    if (it == g_waiting.end())
        return;
    g_waiting.erase(it);
    if (g_waiting.empty())
        Event::Custom::Fire(_reserveReady.Slot(), "");
}

const Item::Data::LoadSubscriber _loadSub{&OnItemLoaded};

int __fastcall Script_GetAverageItemLevel(void *L) {
    bool complete = true;
    const float total = Calculate(false, complete);
    const float equipped = Calculate(true, complete);
    if (complete) {
        g_avgItemLevel = total;
        g_avgItemLevelEquipped = equipped;
    }
    Game::Lua::PushNumber(L, g_avgItemLevel);
    Game::Lua::PushNumber(L, g_avgItemLevelEquipped);
    return 2;
}

const Game::Doc::Field kRets[] = {
    Game::Doc::Req("avgItemLevel", "number"),
    Game::Doc::Req("avgItemLevelEquipped", "number"),
};
const Game::Doc::Function kGetAverageItemLevel{
    "Returns the average item level of the best gear the player owns and can use, and of the "
    "gear the player wears.",
    {},
    kRets,
    "PaperDollInfo"};

void RegisterLuaFunctions() {
    // A new world session (login, /reload): 4.3.4 clears the wait when the
    // character UI is destroyed.
    g_waiting.clear();
    g_avgItemLevel = 0.0f;
    g_avgItemLevelEquipped = 0.0f;
    Game::Lua::RegisterGlobalFunction("GetAverageItemLevel", &Script_GetAverageItemLevel,
                                      &kGetAverageItemLevel);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::AverageLevel
