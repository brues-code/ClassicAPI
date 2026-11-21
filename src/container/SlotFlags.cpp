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

// Bag slot flags: `C_Container.GetBagSlotFlag` / `SetBagSlotFlag` /
// `IsBagSlotFlagEnabledOnOtherBankBags`, `Enum.BagSlotFlags`, the
// `LE_BAG_FILTER_FLAG_*` globals and the two *_SLOT_FLAGS_UPDATED events.
//
// The 3.80.2 client keeps one BagSlotFlags word per bag slot in ActivePlayer
// update fields (+0x16468, four equipped bags; +0x16478, seven bank bags).
// SetBagSlotFlag (0x140BEA290) changes nothing locally: it sends
// PlayerCliChangeBagSlotFlag / PlayerCliChangeBankBagSlotFlag (opcodes
// 0x3E0191 / 0x3E0192: uint8 bag, uint32 mask, bit isSet) and the server's
// field update fires the event. Both Get and Set are gated on a build
// constant (0x142C14CA9) that is 0 there, so in that client Get is always
// false and Set never sends. The argument readers, ranges and errors below
// are that client's; the gate is not copied, since the point here is a
// working flag.
//
// 1.12's server has no such field, so each word is a character cvar: the
// flag belongs to the slot, not to the bag in it, and to the character. A
// Set writes it, then fires the event the server's update would have.
// Not copied: 3.80.2's limit of five Sets per second, which protects
// messages this client never sends.

#include "container/SlotFlags.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Character.h"
#include "event/Custom.h"

#include <cmath>
#include <cstdio>

namespace Container::SlotFlags {

namespace {

constexpr int kBagCount = Offsets::BAG_INDEX_BAG_LAST - Offsets::BAG_INDEX_BAG_FIRST + 1;
constexpr int kBankBagCount =
    Offsets::BAG_INDEX_BANK_BAG_LAST - Offsets::BAG_INDEX_BANK_BAG_FIRST + 1;

// Every Enum.BagSlotFlags bit (0x1 .. 0x200).
constexpr uint32_t kAllFlags = 0x3FF;

const char *const kBagCVarNames[kBagCount] = {
    "bagSlotFlags1", "bagSlotFlags2", "bagSlotFlags3", "bagSlotFlags4"};
const char *const kBankBagCVarNames[kBankBagCount] = {
    "bankBagSlotFlags1", "bankBagSlotFlags2", "bankBagSlotFlags3",
    "bankBagSlotFlags4", "bankBagSlotFlags5", "bankBagSlotFlags6"};

CVar::Factory::Handle g_bagWords[kBagCount] = {};
CVar::Factory::Handle g_bankBagWords[kBankBagCount] = {};

bool IsBag(int bagID) {
    return bagID >= Offsets::BAG_INDEX_BAG_FIRST && bagID <= Offsets::BAG_INDEX_BAG_LAST;
}

bool IsBankBag(int bagID) {
    return bagID >= Offsets::BAG_INDEX_BANK_BAG_FIRST &&
           bagID <= Offsets::BAG_INDEX_BANK_BAG_LAST;
}

// The cvar that stores `bagID`'s word, or null for an ID without one.
CVar::Factory::Handle WordOf(int bagID) {
    if (IsBag(bagID))
        return g_bagWords[bagID - Offsets::BAG_INDEX_BAG_FIRST];
    if (IsBankBag(bagID))
        return g_bankBagWords[bagID - Offsets::BAG_INDEX_BANK_BAG_FIRST];
    return nullptr;
}

uint32_t Read(CVar::Factory::Handle word) {
    return static_cast<uint32_t>(CVar::Factory::GetInt(word, 0)) & kAllFlags;
}

const Game::Doc::Field kEventPayload[] = {
    Game::Doc::Req("slot", "number"),
};
const Game::Doc::Event kBagEventDoc{
    "Container", "Fires when the flags of an equipped bag's slot change.", kEventPayload};
const Game::Doc::Event kBankBagEventDoc{
    "Bank", "Fires when the flags of a bank bag's slot change.", kEventPayload};
const Event::Custom::AutoReserve kBagEvent{"BAG_SLOT_FLAGS_UPDATED", &kBagEventDoc};
const Event::Custom::AutoReserve kBankBagEvent{"BANK_BAG_SLOT_FLAGS_UPDATED",
                                               &kBankBagEventDoc};

// --- argument readers (3.80.2's) -------------------------------------------
//
// Each raises luaL_argerror with the usage string, "<detail> - <usage>"
// when it names what is wrong, and returns false after raising.

bool Missing(void *L, int idx) {
    return Game::Lua::Type(L, idx) <= Game::Lua::TYPE_NIL;
}

bool Fail(void *L, int idx, const char *usage, const char *detail = nullptr) {
    if (detail == nullptr) {
        Game::Lua::ArgError(L, idx, usage);
    } else {
        char message[512];
        std::snprintf(message, sizeof message, "%s - %s", detail, usage);
        Game::Lua::ArgError(L, idx, message);
    }
    return false;
}

// bagIndex (FUN_140BEC680): a finite number in the int32 range, truncated.
// Nil is the bare usage error; anything else wrong names the range.
bool BagArg(void *L, int idx, const char *usage, int &out) {
    if (Missing(L, idx))
        return Fail(L, idx, usage);
    const double v = Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : NAN;
    if (!(v >= -2147483648.0 && v <= 2147483647.0)) {
        char detail[96];
        std::snprintf(detail, sizeof detail, "outside of expected range %i to %i",
                      static_cast<int>(-2147483647 - 1), 2147483647);
        return Fail(L, idx, usage, detail);
    }
    out = static_cast<int>(v);
    return true;
}

// flag (FUN_140BEC9A0): a BagSlotFlags bit value, or several or none of them
// together. Anything else, a negative number included, is the bare usage
// error.
bool FlagArg(void *L, int idx, const char *usage, uint32_t &out) {
    if (!Game::Lua::IsNumber(L, idx))
        return Fail(L, idx, usage);
    const double v = Game::Lua::ToNumber(L, idx);
    if (!(v > -1.0 && v < static_cast<double>(kAllFlags + 1)))
        return Fail(L, idx, usage);
    out = static_cast<uint32_t>(v);
    return true;
}

// flagIndex (FUN_140A6BF10): a finite number in the uint32 range, truncated.
bool FlagIndexArg(void *L, int idx, const char *usage, uint32_t &out) {
    if (Missing(L, idx))
        return Fail(L, idx, usage);
    const double v = Game::Lua::IsNumber(L, idx) ? Game::Lua::ToNumber(L, idx) : NAN;
    if (!(v >= 0.0 && v <= 4294967295.0)) {
        char detail[96];
        std::snprintf(detail, sizeof detail, "outside of expected range %u to %u", 0u,
                      4294967295u);
        return Fail(L, idx, usage, detail);
    }
    out = static_cast<uint32_t>(v);
    return true;
}

// --- Lua -------------------------------------------------------------------

// `C_Container.GetBagSlotFlag(bagIndex, flag)` -> isSet. True when any of
// the bits in `flag` is set on that slot; false for an ID without a slot.
int __fastcall Script_GetBagSlotFlag(void *L) {
    constexpr char kUsage[] = "Usage: local isSet = C_Container.GetBagSlotFlag(bagIndex, flag)";
    int bagID;
    uint32_t flag;
    if (!BagArg(L, 1, kUsage, bagID) || !FlagArg(L, 2, kUsage, flag))
        return 0;
    Game::Lua::PushBool(L, (Of(bagID) & flag) != 0);
    return 1;
}

// `C_Container.SetBagSlotFlag(bagIndex, flag, isSet)`. Sets or clears every
// bit in `flag` on that slot. An ID without a slot does nothing.
int __fastcall Script_SetBagSlotFlag(void *L) {
    constexpr char kUsage[] = "Usage: C_Container.SetBagSlotFlag(bagIndex, flag, isSet)";
    int bagID;
    uint32_t flag;
    if (!BagArg(L, 1, kUsage, bagID) || !FlagArg(L, 2, kUsage, flag))
        return 0;
    if (Missing(L, 3)) {
        Fail(L, 3, kUsage);
        return 0;
    }
    const bool isSet = Game::Lua::ToBoolean(L, 3) != 0;

    CVar::Factory::Handle word = WordOf(bagID);
    if (word == nullptr)
        return 0;
    const uint32_t before = Read(word);
    const uint32_t after = isSet ? (before | flag) : (before & ~flag);
    if (after == before)
        return 0;
    CVar::Factory::SetInt(word, static_cast<int>(after));
    Event::Custom::Fire(IsBag(bagID) ? kBagEvent.Slot() : kBankBagEvent.Slot(), "%d", bagID);
    return 0;
}

// `C_Container.IsBagSlotFlagEnabledOnOtherBankBags(bagIndex, flagIndex)` ->
// isSet. `flagIndex` counts the BagSlotFlags bits from 1. True when a bank
// bag slot has that bit.
//
// Copied as 0x140BE9E70 has it: the slot it skips is the one whose
// bank-bag position (0-based) equals bagIndex - 1, so a bank container ID
// skips a later bank bag, or none, rather than itself.
int __fastcall Script_IsBagSlotFlagEnabledOnOtherBankBags(void *L) {
    constexpr char kUsage[] = "Usage: local isSet = "
                              "C_Container.IsBagSlotFlagEnabledOnOtherBankBags(bagIndex, flagIndex)";
    int bagID;
    uint32_t flagIndex;
    if (!BagArg(L, 1, kUsage, bagID) || !FlagIndexArg(L, 2, kUsage, flagIndex))
        return 0;
    const uint32_t mask = 1u << ((flagIndex - 1u) & 31u);
    const int skipped = bagID - 1;
    bool found = false;
    for (int i = 0; i < kBankBagCount && !found; ++i)
        found = i != skipped && (Read(g_bankBagWords[i]) & mask) == mask;
    Game::Lua::PushBool(L, found);
    return 1;
}

// --- registration ----------------------------------------------------------

const Game::Lua::EnumIntegerEntry kFlags[] = {
    {"DisableAutoSort", 0x1},     {"ClassEquipment", 0x2},   {"ClassConsumables", 0x4},
    {"ClassProfessionGoods", 0x8}, {"ClassJunk", 0x10},      {"ClassQuestItems", 0x20},
    {"ExcludeJunkSell", 0x40},    {"ClassReagents", 0x80},   {"ExpansionCurrent", 0x100},
    {"ExpansionLegacy", 0x200},
};

// 3.80.2's FUN_141686D80: the older bag filter numbering, 1..5, not bits.
struct Constant {
    const char *name;
    int value;
};
constexpr Constant kFilterConstants[] = {
    {"LE_BAG_FILTER_FLAG_IGNORE_CLEANUP", 1},
    {"LE_BAG_FILTER_FLAG_EQUIPMENT", 2},
    {"LE_BAG_FILTER_FLAG_CONSUMABLES", 3},
    {"LE_BAG_FILTER_FLAG_TRADE_GOODS", 4},
    {"LE_BAG_FILTER_FLAG_JUNK", 5},
    {"NUM_LE_BAG_FILTER_FLAGS", 5},
};

const Game::Doc::Field kGetArgs[] = {
    Game::Doc::Req("bagIndex", "BagIndex"),
    Game::Doc::Req("flag", "BagSlotFlags"),
};
const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("bagIndex", "BagIndex"),
    Game::Doc::Req("flag", "BagSlotFlags"),
    Game::Doc::Req("isSet", "bool"),
};
const Game::Doc::Field kOtherArgs[] = {
    Game::Doc::Req("bagIndex", "BagIndex"),
    Game::Doc::Req("flagIndex", "number"),
};
const Game::Doc::Field kIsSetRets[] = {
    Game::Doc::Req("isSet", "bool"),
};
const Game::Doc::Function kGetDoc{
    "Returns whether any of the given flags is set on the slot of an equipped bag or bank bag.",
    kGetArgs, kIsSetRets};
const Game::Doc::Function kSetDoc{
    "Sets or clears the given flags on the slot of an equipped bag or bank bag.", kSetArgs, {}};
const Game::Doc::Function kOtherDoc{
    "Returns whether a bank bag slot has the flag with the given number, counted from 1.",
    kOtherArgs, kIsSetRets};

void RegisterLuaFunctions() {
    constexpr int kCategory = Offsets::CVAR_CATEGORY_GAME;
    for (int i = 0; i < kBagCount; ++i)
        g_bagWords[i] = CVar::Character::Register(kBagCVarNames[i], nullptr, kCategory, "0");
    for (int i = 0; i < kBankBagCount; ++i)
        g_bankBagWords[i] =
            CVar::Character::Register(kBankBagCVarNames[i], nullptr, kCategory, "0");

    Game::Lua::RegisterIntegerEnum("Enum", "BagSlotFlags", kFlags, "BagConstants");
    if (void *L = Game::Lua::State()) {
        for (const auto &c : kFilterConstants)
            Game::Lua::SetGlobalNumber(L, c.name, c.value);
    }

    Game::Lua::RegisterTableFunction("C_Container", "GetBagSlotFlag", &Script_GetBagSlotFlag,
                                     &kGetDoc);
    Game::Lua::RegisterTableFunction("C_Container", "SetBagSlotFlag", &Script_SetBagSlotFlag,
                                     &kSetDoc);
    Game::Lua::RegisterTableFunction("C_Container", "IsBagSlotFlagEnabledOnOtherBankBags",
                                     &Script_IsBagSlotFlagEnabledOnOtherBankBags, &kOtherDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

uint32_t Of(int bagID) {
    CVar::Factory::Handle word = WordOf(bagID);
    return word != nullptr ? Read(word) : 0;
}

} // namespace Container::SlotFlags
