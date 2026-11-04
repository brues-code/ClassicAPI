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

// With `expandedCursors` at 1 (Cursor::Extended::Enabled), the cursor over an
// NPC shows what it offers, as in later clients: an anvil for a repairer, a
// hearthstone for an innkeeper, a horseshoe for a stable master, and a quest
// giver's ! or ? (Cursor::Extended). At 0, the default, it stays 1.12's.
//
// 1.12 picks the cursor for a hovered unit in ClassifyHoveredUnit
// (FUN_WORLD_FRAME_CLASSIFY_UNIT). For a unit the player can interact with,
// its NPC flag chain takes the lowest flag bit first: gossip, quest giver,
// vendor, flight master and so on. It never tests the repair flag, and shows
// Interact for an innkeeper. 4.3.4's CursorTrackUnit first asks the quest
// giver's status (GetQuestGiverCursor), then tests the services from repair
// down, with vendor and gossip near the end.
//
// The thunk stands in for the head of 1.12's chain
// (SITE_CLASSIFY_UNIT_NPC_FLAGS). Everything the engine decides before it
// stays the engine's: mouselook, spell targeting, charm, whether the unit can
// be interacted with now, and whether it is beyond the interact reach (EBX,
// the Unable form). With the cvar at 0 the thunk does the instruction it
// replaced and the stock chain runs. The pick goes to the engine's
// SetCursorMode, whose hook (cursor/Hover) shows an extended mode and records
// every mode while the soft-interact code asks for the hover verdict.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cursor/Extended.h"

#include <cstdint>

namespace Cursor::Npc {

namespace {

// UNIT_NPC_FLAGS bits, as 1.12's chain tests them (tortoise-wow
// UnitDefines.h names them).
constexpr uint32_t kGossip = 0x1;
constexpr uint32_t kVendor = 0x4;
constexpr uint32_t kFlightMaster = 0x8;
constexpr uint32_t kTrainer = 0x10;
constexpr uint32_t kSpiritHealer = 0x20;
constexpr uint32_t kSpiritGuide = 0x40;
constexpr uint32_t kInnkeeper = 0x80;
constexpr uint32_t kBanker = 0x100;
constexpr uint32_t kPetitioner = 0x200;
constexpr uint32_t kTabardDesigner = 0x400;
constexpr uint32_t kBattleMaster = 0x800;
constexpr uint32_t kAuctioneer = 0x1000;
constexpr uint32_t kStableMaster = 0x2000;
constexpr uint32_t kRepair = 0x4000;

// Quest giver statuses (OFF_CGUNIT_QUEST_STATUS). 1.12 numbers them its own
// way (tortoise-wow QuestDef.h); its status-to-marker table (0x80C454) shows
// each one's meaning: 3 a grey ?, 4 a blue ?, 5 a yellow !, 6 and 7 a yellow
// ?. 0 none, 1 unavailable (grey !) and 2 chat (no marker) have no cursor,
// as 4.3.4's none and unavailable have none.
constexpr uint32_t kStatusIncomplete = 3;
constexpr uint32_t kStatusRewardRepeatable = 4;
constexpr uint32_t kStatusAvailable = 5;
constexpr uint32_t kStatusRewardOld = 6;
constexpr uint32_t kStatusReward = 7;

using IsQuestGiverToTalk_t = uint32_t(__stdcall *)(void *unit);
using SetCursorMode_t = void(__fastcall *)(uint32_t mode);

uintptr_t g_classifyReturn = Offsets::CLASSIFY_UNIT_RETURN;

// 4.3.4's quest cursor for a status (GetQuestGiverCursor), matched by the
// status's meaning: 4.3.4 gives Quest to an available and to an incomplete
// quest, QuestRepeatable to a repeatable reward (its blue ?), and
// QuestTurnIn to a reward (its yellow ?).
uint32_t QuestMode(uint32_t status) {
    switch (status) {
    case kStatusIncomplete:
    case kStatusAvailable:
        return Cursor::Extended::kQuest;
    case kStatusRewardRepeatable:
        return Cursor::Extended::kQuestRepeatable;
    case kStatusRewardOld:
    case kStatusReward:
        return Cursor::Extended::kQuestTurnIn;
    default:
        return 0;
    }
}

// 4.3.4's order (CursorTrackUnit) in 1.12's flag bits, or 0 for none.
// 4.3.4's guild master, guild banker and spell click tests have no 1.12
// flag, and its flight master test also reads an icon type; for that flag
// 1.12 shows Taxi, as here. 4.3.4 also takes a cursor that the creature's
// cache record names (IconName), which 1.12's creature query does not carry.
// A stable master gets retail's StableMaster in 4.3.4's place for it, where
// 4.3.4 shows Speak. Where 4.3.4 tests gossip, this also takes the quest
// giver 1.12 would speak to (IsQuestGiverToTalk), so a status without a
// quest cursor keeps 1.12's Speak.
uint32_t ServiceMode(void *unit) {
    if (const uint32_t quest = QuestMode(Game::Read<uint32_t>(unit, Offsets::OFF_CGUNIT_QUEST_STATUS)))
        return quest;
    const uint32_t flags =
        Game::Read<uint32_t>(Game::Read<const void *>(unit, Offsets::OFF_UNIT_DESCRIPTOR),
                             Offsets::OFF_UNIT_DESCRIPTOR_NPC_FLAGS);
    if (flags & kRepair)
        return Cursor::Extended::kRepairNpc;
    if (flags & kInnkeeper)
        return Cursor::Extended::kInnkeeper;
    if (flags & kFlightMaster)
        return Offsets::CURSOR_MODE_TAXI;
    if (flags & kTrainer)
        return Offsets::CURSOR_MODE_TRAINER;
    if (flags & (kSpiritHealer | kSpiritGuide))
        return Offsets::CURSOR_MODE_SPEAK;
    if (flags & kBanker)
        return Offsets::CURSOR_MODE_BUY;
    if (flags & (kPetitioner | kTabardDesigner | kBattleMaster))
        return Offsets::CURSOR_MODE_SPEAK;
    if (flags & kAuctioneer)
        return Offsets::CURSOR_MODE_BUY;
    if (flags & kStableMaster)
        return Cursor::Extended::kStableMaster;
    if (flags & kVendor)
        return Offsets::CURSOR_MODE_PICKUP;
    if ((flags & kGossip) ||
        reinterpret_cast<IsQuestGiverToTalk_t>(Offsets::FUN_IS_QUEST_GIVER_TO_TALK)(unit) != 0)
        return Offsets::CURSOR_MODE_SPEAK;
    return 0;
}

// True when it set the mode. `unable` is the engine's EBX: 1 beyond the
// interact reach, else 0. A unit with no service gets the base mode, where
// the stock chain ends too (ShowBaseCursor).
bool __stdcall Pick(void *unit, uint32_t unable) {
    if (!Cursor::Extended::Enabled())
        return false;
    uint32_t mode = ServiceMode(unit);
    if (mode == 0)
        mode = Game::Read<uint32_t>(Offsets::VAR_CURSOR_BASE_MODE);
    else if (unable != 0)
        mode += Cursor::Extended::Is(mode) ? Cursor::Extended::kUnableOffset
                                           : Offsets::CURSOR_MODE_UNABLE_OFFSET;
    reinterpret_cast<SetCursorMode_t>(Offsets::FUN_SET_CURSOR_MODE)(mode);
    return true;
}

static_assert(Offsets::OFF_UNIT_DESCRIPTOR == 0x110, "NpcFlagsThunk replays [esi + 0x110]");

// Stands in for `mov ecx, [esi + 0x110]`, the head of the chain: the unit in
// ESI, the Unable flag in EBX. When Pick set the mode, it leaves the
// classifier through the chain's epilogue; otherwise it does the instruction
// it replaced and the chain runs.
__declspec(naked) void NpcFlagsThunk() {
    __asm {
        push ebx
        push esi
        call Pick
        test al, al
        jnz picked
        mov ecx, dword ptr [esi + 110h]
        ret
    picked:
        add esp, 4
        jmp dword ptr [g_classifyReturn]
    }
}

void Patch() {
    static const uint8_t kFlagsRead[6] = {0x8B, 0x8E, 0x10, 0x01, 0x00, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_CLASSIFY_UNIT_NPC_FLAGS, kFlagsRead, sizeof kFlagsRead,
                            reinterpret_cast<const void *>(&NpcFlagsThunk));
}

const Game::ModuleAutoRegister _autoreg{&Patch};

} // namespace

} // namespace Cursor::Npc
