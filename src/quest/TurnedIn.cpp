// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `QUEST_TURNED_IN(questID, xpReward, moneyReward)` event — polyfills
// modern WoW's event of the same name. Fires when the server confirms
// a quest turn-in (SMSG_QUESTGIVER_QUEST_COMPLETE, opcode 0x191).
//
// Hook target is `FUN_QUEST_GIVER_QUEST_COMPLETE_HANDLER`
// (`CGPlayer_C::ReadQuestComplete`), the per-opcode handler for 0x191. It
// prints "<quest> completed." and the experience, money and item lines; we
// fire after the original so the event trails those lines, the same order
// QUEST_ACCEPTED keeps with its announce. That ordering is why this is a
// hook and not a `Net::PacketDispatch` subscriber, which runs before the
// engine handler. No other DLL in the Octo load list hooks this function.
//
// The payload is the packet's first four uint32s: questID, a constant 3,
// experience, money (tortoise-wow `Player::SendQuestReward`). We read them
// with the shared packet reader and restore the read cursor, so the engine
// handler sees an untouched packet.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/PacketReader.h"

#include <cstdint>

namespace Quest::TurnedIn {

namespace {

const Game::Doc::Field kPayload[] = {
    Game::Doc::Req("questID", "number"),
    Game::Doc::Req("xpReward", "number", "Experience the turn-in gave; 0 at the level cap."),
    Game::Doc::Req("moneyReward", "number", "Money the turn-in gave, in copper."),
};
const Game::Doc::Event kTurnedInDoc{
    "QuestLog", "Fires when the server confirms a quest turn-in.", kPayload};
const Event::Custom::AutoReserve _reserve{"QUEST_TURNED_IN", &kTurnedInDoc};

// `__thiscall(player, CDataStore *packet)`, wired as `__fastcall` with a
// dummy EDX.
using Handler_t = int(__fastcall *)(void *thisPlayer, void *edx_unused, Net::CDataStore *packet);
Handler_t Handler_o = nullptr;

int __fastcall Handler_h(void *thisPlayer, void *edx, Net::CDataStore *packet) {
    const uint32_t saved = packet->m_read;
    const uint32_t questID = Net::Read<uint32_t>(packet);
    Net::Read<uint32_t>(packet); // always 3
    const uint32_t xpReward = Net::Read<uint32_t>(packet);
    const uint32_t moneyReward = Net::Read<uint32_t>(packet);
    const bool complete = packet->m_read <= packet->m_size;
    packet->m_read = saved;

    const int result = Handler_o(thisPlayer, edx, packet);

    if (complete && questID > 0)
        Event::Custom::Fire(_reserve.Slot(), "%u%u%u", questID, xpReward, moneyReward);
    return result;
}

} // namespace

static const Game::HookAutoRegister _hook{
    Offsets::FUN_QUEST_GIVER_QUEST_COMPLETE_HANDLER,
    reinterpret_cast<void *>(&Handler_h),
    reinterpret_cast<void **>(&Handler_o)};

} // namespace Quest::TurnedIn
