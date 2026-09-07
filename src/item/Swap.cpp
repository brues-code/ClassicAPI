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

#include "item/Swap.h"

#include "Game.h"
#include "Offsets.h"
#include "item/CGItem.h"
#include "item/Location.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Item::Swap {

namespace {

using SwapFn_t = void(__thiscall *)(
    void *player,
    uint32_t srcItemLo, uint32_t srcItemHi,
    uint32_t srcContainerLo, uint32_t srcContainerHi,
    uint32_t srcLinearSlot,
    uint32_t dstContainerLo, uint32_t dstContainerHi,
    uint32_t dstLinearSlot,
    int flag);

// Reads the 64-bit GUID stored at instance-block offset 0 of any
// CGObject-derived pointer (CGItem, CGPlayer, CGContainer all use
// the same +0x8 → instance block → +0x00 GUID layout).
bool ReadGuid(const void *cgObject, uint32_t *lo, uint32_t *hi) {
    if (cgObject == nullptr)
        return false;
    auto *base = static_cast<const uint8_t *>(cgObject);
    auto *inst = Item::InstanceBlock(base);
    if (inst == nullptr)
        return false;
    *lo = Game::Read<uint32_t>(inst, Offsets::OFF_INSTANCE_BLOCK_GUID);
    *hi = Game::Read<uint32_t>(inst, Offsets::OFF_INSTANCE_BLOCK_GUID + 4);
    return true;
}

// Bag-container linear-slot range — the four equipped-bag slots
// the engine treats as "directly in the player's invMgr" for the
// purposes of opcode 0x10D's same-container check. Just here so
// the magic +19 in FromBag is explained at the call site.
constexpr int FIRST_BAG_CONTAINER_LINEAR = 19;

// Player-container linear-slot bases, DERIVED from the named
// first-slot constants in Offsets.h rather than restated. The whole map
// (paperdoll / backpack / bank / bank bags / keyring) is documented at
// `Offsets::OFF_INVMGR_GUID_ARRAY`; a 1-based slot S in one of these
// ranges maps to `base + S`, so each base is that range's first linear
// slot minus one.
constexpr int BACKPACK_LINEAR_OFFSET = Offsets::INVMGR_BACKPACK_FIRST_SLOT - 1;
constexpr int BANK_MAIN_LINEAR_OFFSET = Offsets::INVMGR_BANK_MAIN_FIRST_SLOT - 1;
constexpr int KEYRING_LINEAR_OFFSET = Offsets::INVMGR_KEYRING_FIRST_SLOT - 1;

// Bank bag containers are addressed by bagID 5..10 — bagID 5 = first
// bank bag. `ResolveEquipmentSlot` takes a 1-based equipment slot, so
// the base is the bank-bag range's first linear slot plus one.
// Requires bank window open; otherwise the engine's CGContainer for
// the bag is unsynced and `ResolveEquipmentSlot` returns null.
constexpr int FIRST_BANK_BAG_EQ_SLOT = Offsets::INVMGR_BANK_BAG_FIRST_SLOT + 1;
constexpr int FIRST_BANK_BAG_ID = 5;
constexpr int LAST_BANK_BAG_ID = 10;

// Bounds a player-container linear slot the way `FUN_PACK_BAG_SLOT`
// does — against the invMgr's own live slot count, not a hardcoded
// size. That is what makes the keyring addressable without knowing its
// capacity, which grows as the character unlocks slots.
bool LinearSlotInRange(const void *player, uint32_t linearSlot) {
    const uint32_t count = Game::Read<uint32_t>(
        player,
        Offsets::OFF_PLAYER_INVENTORY_MANAGER + Offsets::OFF_INVMGR_SLOT_COUNT);
    return linearSlot < count;
}

// (bagID, 1-based slot) → (containerGUID, engine linear slot). Returns
// false for unequipped bags or out-of-range slots; does NOT validate
// that the slot is occupied (caller checks separately).
//
//   bagID  0     → container = player, linear = 22 + slot (backpack)
//   bagID  1..4  → container = equipped bag's CGContainer, linear = slot - 1
//   bagID -1     → container = player, linear = 38 + slot (bank main)
//   bagID -2     → container = player, linear = 80 + slot (keyring)
//   bagID  5..10 → container = bank bag's CGContainer, linear = slot - 1
//
// This mirrors the engine's own bagID→(container, linear slot) map in
// `FUN_PACK_BAG_SLOT`, minus that function's Lua coupling: it reads its
// bagID and slot off the Lua stack and stomps it, which is why this is
// a separate encoder rather than a call into it. Keep the two in step —
// the keyring row above exists because this encoder was missing it
// while the engine's map had it all along.
//
// Bank-side encodings only resolve once the bank window has been
// opened in the current session (the engine doesn't sync the bank
// CGContainers or bank-slot inventory entries before then).
bool EncodeBagSlot(int bagID, int slotInBag,
                   uint32_t *containerLo, uint32_t *containerHi,
                   uint32_t *linearSlot) {
    if (slotInBag < 1)
        return false;

    // Backpack, main bank and keyring all live directly in the player's
    // invMgr and differ only in their base — and in whether the range
    // has a fixed end. `lastLinear == 0` means "no fixed end", which is
    // the keyring: its capacity grows, so the invMgr count is the only
    // correct bound.
    int base = 0;
    int lastLinear = 0;
    bool inPlayer = true;
    if (bagID == 0) {
        base = BACKPACK_LINEAR_OFFSET;
        lastLinear = Offsets::INVMGR_BACKPACK_LAST_SLOT;
    } else if (bagID == -1) {
        base = BANK_MAIN_LINEAR_OFFSET;
        lastLinear = Offsets::INVMGR_BANK_MAIN_LAST_SLOT;
    } else if (bagID == -2) {
        base = KEYRING_LINEAR_OFFSET;
    } else {
        inPlayer = false;
    }
    if (inPlayer) {
        const void *player = Unit::Identity::PlayerObject();
        if (player == nullptr)
            return false;
        const uint32_t linear = static_cast<uint32_t>(base + slotInBag);
        if (lastLinear != 0 && linear > static_cast<uint32_t>(lastLinear))
            return false; // would alias into the next range
        if (!LinearSlotInRange(player, linear))
            return false;
        if (!ReadGuid(player, containerLo, containerHi))
            return false;
        *linearSlot = linear;
        return true;
    }

    if (bagID >= 1 && bagID <= 4) {
        const uint8_t *bag = Item::Location::ResolveEquipmentSlot(
            FIRST_BAG_CONTAINER_LINEAR + bagID);
        if (bag == nullptr)
            return false;
        if (!ReadGuid(bag, containerLo, containerHi))
            return false;
        *linearSlot = static_cast<uint32_t>(slotInBag - 1);
        return true;
    }
    if (bagID >= FIRST_BANK_BAG_ID && bagID <= LAST_BANK_BAG_ID) {
        const uint8_t *bag = Item::Location::ResolveEquipmentSlot(
            FIRST_BANK_BAG_EQ_SLOT + (bagID - FIRST_BANK_BAG_ID));
        if (bag == nullptr)
            return false;
        if (!ReadGuid(bag, containerLo, containerHi))
            return false;
        *linearSlot = static_cast<uint32_t>(slotInBag - 1);
        return true;
    }
    return false;
}

// Container GUID for a bagID, with no slot involved — what the
// autostore opcode needs, since it names a destination container and
// lets the server choose the slot inside it. Bags 0 and -1 resolve to
// the player, which the engine's converter reports as `0xFF`
// (INVENTORY_SLOT_BAG_0); the two are indistinguishable on the wire.
bool EncodeContainerGuid(int bagID, uint32_t *containerLo, uint32_t *containerHi) {
    if (bagID == 0 || bagID == -1) {
        void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
        if (player == nullptr)
            return false;
        return ReadGuid(player, containerLo, containerHi);
    }
    if (bagID >= 1 && bagID <= 4) {
        const uint8_t *bag = Item::Location::ResolveEquipmentSlot(
            FIRST_BAG_CONTAINER_LINEAR + bagID);
        if (bag == nullptr)
            return false;
        return ReadGuid(bag, containerLo, containerHi);
    }
    if (bagID >= FIRST_BANK_BAG_ID && bagID <= LAST_BANK_BAG_ID) {
        const uint8_t *bag = Item::Location::ResolveEquipmentSlot(
            FIRST_BANK_BAG_EQ_SLOT + (bagID - FIRST_BANK_BAG_ID));
        if (bag == nullptr)
            return false;
        return ReadGuid(bag, containerLo, containerHi);
    }
    return false;
}

// Common send path. All paperdoll-side state (player ptr, player
// GUID) is resolved here; caller supplies the source side
// (already-encoded linear slot + the container GUID it lives in).
bool SendSwap(const void *srcItem,
              uint32_t srcContainerLo, uint32_t srcContainerHi,
              uint32_t srcLinearSlot,
              int dstPaperdollSlot) {
    if (dstPaperdollSlot < 1 || dstPaperdollSlot > 19)
        return false;
    if (srcItem == nullptr)
        return false;

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;

    uint32_t playerLo = 0, playerHi = 0;
    if (!ReadGuid(player, &playerLo, &playerHi))
        return false;

    uint32_t itemLo = 0, itemHi = 0;
    if (!ReadGuid(srcItem, &itemLo, &itemHi))
        return false;

    auto fn = reinterpret_cast<SwapFn_t>(Offsets::FUN_INVENTORY_SWAP);
    fn(player,
       itemLo, itemHi,
       srcContainerLo, srcContainerHi, srcLinearSlot,
       playerLo, playerHi,
       static_cast<uint32_t>(dstPaperdollSlot - 1),
       0);
    return true;
}

} // namespace

bool FromBag(const void *cgItem, int bagID, int slotInBag, int dstPaperdollSlot) {
    uint32_t containerLo = 0, containerHi = 0, linearSlot = 0;
    if (!EncodeBagSlot(bagID, slotInBag, &containerLo, &containerHi, &linearSlot))
        return false;
    return SendSwap(cgItem, containerLo, containerHi, linearSlot, dstPaperdollSlot);
}

bool FromPaperdoll(const void *cgItem, int srcPaperdollSlot, int dstPaperdollSlot) {
    if (srcPaperdollSlot < 1 || srcPaperdollSlot > 19)
        return false;

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;
    uint32_t playerLo = 0, playerHi = 0;
    if (!ReadGuid(player, &playerLo, &playerHi))
        return false;
    return SendSwap(cgItem,
                    playerLo, playerHi,
                    static_cast<uint32_t>(srcPaperdollSlot - 1),
                    dstPaperdollSlot);
}

bool ToBag(const void *cgItem, int srcPaperdollSlot, int dstBagID, int dstSlotInBag) {
    if (srcPaperdollSlot < 1 || srcPaperdollSlot > 19)
        return false;
    if (cgItem == nullptr)
        return false;

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;
    uint32_t playerLo = 0, playerHi = 0;
    if (!ReadGuid(player, &playerLo, &playerHi))
        return false;

    uint32_t dstContainerLo = 0, dstContainerHi = 0, dstLinear = 0;
    if (!EncodeBagSlot(dstBagID, dstSlotInBag,
                       &dstContainerLo, &dstContainerHi, &dstLinear))
        return false;

    uint32_t itemLo = 0, itemHi = 0;
    if (!ReadGuid(cgItem, &itemLo, &itemHi))
        return false;

    auto fn = reinterpret_cast<SwapFn_t>(Offsets::FUN_INVENTORY_SWAP);
    fn(player,
       itemLo, itemHi,
       playerLo, playerHi,
       static_cast<uint32_t>(srcPaperdollSlot - 1),
       dstContainerLo, dstContainerHi, dstLinear,
       0);
    return true;
}

bool MoveCount(void *L, int srcBag, int srcSlot, int dstBag, int dstSlot, int count) {
    return MoveCountFrom(Item::Location::ResolveBag(L, srcBag, srcSlot),
                         srcBag, srcSlot, dstBag, dstSlot, count);
}

bool MoveCountFrom(const void *srcItem, int srcBag, int srcSlot, int dstBag,
                   int dstSlot, int count) {
    if (srcItem == nullptr)
        return false; // empty source slot

    // Vanilla protocol writes count as a single byte. Clamp at 255;
    // anything larger would either truncate silently (255 sent) or
    // wrap (0 = no-op). Refuse zero outright too.
    if (count < 1 || count > 255)
        return false;

    uint32_t srcContainerLo = 0, srcContainerHi = 0, srcLinear = 0;
    uint32_t dstContainerLo = 0, dstContainerHi = 0, dstLinear = 0;
    if (!EncodeBagSlot(srcBag, srcSlot,
                       &srcContainerLo, &srcContainerHi, &srcLinear))
        return false;
    if (!EncodeBagSlot(dstBag, dstSlot,
                       &dstContainerLo, &dstContainerHi, &dstLinear))
        return false;

    // Vanilla server rejects `CMSG_SPLIT_ITEM` when count == srcStack
    // (`EQUIP_ERR_COULDNT_SPLIT_ITEMS`) — conceptually "split N off,
    // leave residual", and splitting the entire stack would leave
    // source empty. The drag-and-drop UI's "drop whole stack on
    // matching stack" goes through CMSG_SWAP_ITEM instead, which the
    // server treats as a merge for same-item destinations (up to
    // maxStack). Route there for count == srcStack so callers don't
    // have to special-case "move everything".
    auto *srcDescriptor =
        Item::ObjectFields(static_cast<const uint8_t *>(srcItem));
    const int srcStack = srcDescriptor == nullptr ? 0 :
        static_cast<int>(Game::Read<uint32_t>(
            srcDescriptor, Offsets::OFF_DESCRIPTOR_STACK_COUNT));
    if (count > srcStack)
        return false; // server would reject anyway; fail fast and locally
    if (count == srcStack)
        return ContainersFrom(srcItem, srcBag, srcSlot, dstBag, dstSlot);

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;

    using SplitFn_t = void(__thiscall *)(
        void *thisPlayer,
        uint32_t unused1, uint32_t unused2,
        uint32_t srcContainerLo, uint32_t srcContainerHi, uint32_t srcSlot,
        uint32_t dstContainerLo, uint32_t dstContainerHi, uint32_t dstSlot,
        uint32_t count);
    auto fn = reinterpret_cast<SplitFn_t>(Offsets::FUN_INVENTORY_SPLIT);
    fn(player,
       0, 0,
       srcContainerLo, srcContainerHi, srcLinear,
       dstContainerLo, dstContainerHi, dstLinear,
       static_cast<uint32_t>(count));
    return true;
}

bool AutoStoreFrom(const void *srcItem, int srcBag, int srcSlot, int dstBag) {
    // The opcode puts no item GUID on the wire — the server locates the
    // item from (bag, slot) — so the pointer is here purely to prove the
    // source is occupied, which keeps an empty slot a clean local false
    // instead of a server reject.
    if (srcItem == nullptr)
        return false;

    const bool srcIsBank =
        srcBag == -1 || (srcBag >= FIRST_BANK_BAG_ID && srcBag <= LAST_BANK_BAG_ID);

    // Which of the two autostore opcodes serves this pair — and whether
    // either does. The bank opcode takes its direction from the source,
    // so it can only cross the boundary; see the header note.
    bool crossToBank = false; // use FUN_INVENTORY_AUTOSTORE_BANK
    if (srcIsBank) {
        if (dstBag != 0)
            return false;     // bank→bank, or a bank source aimed at one bag
        crossToBank = true;   // 0x282 sends a bank source to the inventory
    } else if (dstBag == -1) {
        crossToBank = true;   // 0x282 sends an inventory source to the bank
    } else if (dstBag < 0 || dstBag > 4) {
        return false;         // individual bank bags are not addressable
    }

    uint32_t srcContainerLo = 0, srcContainerHi = 0, srcLinear = 0;
    if (!EncodeBagSlot(srcBag, srcSlot,
                       &srcContainerLo, &srcContainerHi, &srcLinear))
        return false;

    uint32_t dstContainerLo = 0, dstContainerHi = 0;
    if (!crossToBank &&
        !EncodeContainerGuid(dstBag, &dstContainerLo, &dstContainerHi))
        return false;

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;

    if (crossToBank) {
        // Three stack args, `RET 0xC`. No destination on the wire — the
        // server reads the direction off the source position.
        using AutoStoreBankFn_t = void(__thiscall *)(
            void *thisPlayer,
            uint32_t srcContainerLo, uint32_t srcContainerHi,
            uint32_t srcSlot);
        auto fnBank = reinterpret_cast<AutoStoreBankFn_t>(
            Offsets::FUN_INVENTORY_AUTOSTORE_BANK);
        fnBank(player, srcContainerLo, srcContainerHi, srcLinear);
        return true;
    }

    // No item GUID on the wire for this opcode — the server locates the
    // item from (srcBag, srcLinearSlot). The first two args are the
    // family's ABI padding, as with the split builder.
    //
    // EIGHT stack args, matching the callee's `RET 0x20`. The trailing
    // one is never read by the body, but declaring seven makes it pop
    // four bytes we never pushed, and the caller's stack shifts under
    // it — which shows up much later as a garbage pointer in whoever
    // runs next, not as a fault here.
    using AutoStoreFn_t = void(__thiscall *)(
        void *thisPlayer,
        uint32_t unused1, uint32_t unused2,
        uint32_t srcContainerLo, uint32_t srcContainerHi, uint32_t srcSlot,
        uint32_t dstContainerLo, uint32_t dstContainerHi,
        uint32_t unused3);
    auto fn = reinterpret_cast<AutoStoreFn_t>(Offsets::FUN_INVENTORY_AUTOSTORE);
    fn(player,
       0, 0,
       srcContainerLo, srcContainerHi, srcLinear,
       dstContainerLo, dstContainerHi,
       0);
    return true;
}

bool AutoStore(void *L, int srcBag, int srcSlot, int dstBag) {
    // Resolving stomps the Lua stack, so callers must have read every
    // argument they need before getting here.
    return AutoStoreFrom(Item::Location::ResolveBag(L, srcBag, srcSlot),
                         srcBag, srcSlot, dstBag);
}

bool Containers(void *L, int srcBag, int srcSlot, int dstBag, int dstSlot) {
    return ContainersFrom(Item::Location::ResolveBag(L, srcBag, srcSlot),
                          srcBag, srcSlot, dstBag, dstSlot);
}

bool ContainersFrom(const void *srcItem, int srcBag, int srcSlot,
                    int dstBag, int dstSlot) {
    if (srcItem == nullptr)
        return false; // empty source slot

    uint32_t srcContainerLo = 0, srcContainerHi = 0, srcLinear = 0;
    uint32_t dstContainerLo = 0, dstContainerHi = 0, dstLinear = 0;
    if (!EncodeBagSlot(srcBag, srcSlot,
                       &srcContainerLo, &srcContainerHi, &srcLinear))
        return false;
    if (!EncodeBagSlot(dstBag, dstSlot,
                       &dstContainerLo, &dstContainerHi, &dstLinear))
        return false;

    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;

    uint32_t itemLo = 0, itemHi = 0;
    if (!ReadGuid(srcItem, &itemLo, &itemHi))
        return false;

    // flag = 1 SUPPRESSES the engine's pre-send confirmation gate, and
    // that is load-bearing rather than an optimization. With flag = 0 the
    // builder inspects the item being moved and, if it is Bind-on-Equip
    // and this character could equip it, stashes the parameters and
    // raises the bind-confirmation dialog INSTEAD OF SENDING ANYTHING —
    // silently, since the builder returns void. It does the same when the
    // item is not in the item cache yet. Either way the move just does
    // not happen.
    //
    // Suppressing it is correct here because this function addresses bag
    // CONTENT slots on both sides and never a paperdoll slot, so nothing
    // it sends can bind an item; the gate exists for the paths that move
    // an item into equipment, which are `FromBag` / `ToBag` /
    // `FromPaperdoll` and which pass 0. The engine sets this same flag
    // itself when it re-issues a swap after the player accepts the
    // dialog.
    auto fn = reinterpret_cast<SwapFn_t>(Offsets::FUN_INVENTORY_SWAP);
    fn(player,
       itemLo, itemHi,
       srcContainerLo, srcContainerHi, srcLinear,
       dstContainerLo, dstContainerHi, dstLinear,
       1);
    return true;
}

} // namespace Item::Swap
