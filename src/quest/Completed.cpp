// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// Completed quests on Turtle-lineage servers: `QueryQuestsCompleted`,
// `GetQuestsCompleted` and `QUEST_QUERY_COMPLETE` (3.3.5), and
// `C_QuestLog.IsQuestFlaggedCompleted` / `C_QuestLog.GetAllCompletedQuestIDs`.
//
// The 1.12 protocol has no completed-quests query and the client keeps no
// quest history. Turtle WoW's server and its forks answer the player command
// `.queststatuses` (tortoise-wow ChatHandler::HandleQuestStatusesCommand,
// SEC_PLAYER) with the character's rewarded quests, repeatable ones left out,
// as addon messages: chat type GUILD, language ADDON, sent by the player, text
// "TWQUEST\t<id> <id> ... ". It sends a message each time its text reaches
// MESSAGE_SPLIT characters and always sends what is left last, so every
// message but the last is at least MESSAGE_SPLIT long and the last is
// shorter - empty when the split fell on the end. An ID never spans two
// messages. Probed on a live Turtle-lineage server for one character: 2003,
// 2004, 0.
//
// The list:
//   - is replaced by every complete answer, whoever asked (pfQuest, Questie, a
//     typed command), and QUEST_QUERY_COMPLETE fires - as 3.3.5's
//     SMSG_QUERY_QUESTS_COMPLETED_RESPONSE handler (0x005B5190) empties its
//     list, refills it and signals the event on every response;
//   - is asked for once per login, on SMSG_LOGIN_VERIFY_WORLD. The server reads
//     chat only while the player is in the world (WorldSession::Update,
//     STATUS_LOGGEDIN) and puts the player there in the call that sends this
//     packet, so this is the earliest request it answers. The answer still
//     lands after PLAYER_LOGIN and PLAYER_ENTERING_WORLD: that call also sends
//     the player's own object, which fires both, before the server reads
//     anything we send;
//   - gets a quest turned in (SMSG_QUESTGIVER_QUEST_COMPLETE) at once, then
//     the server is asked again. Only the server knows whether a quest is
//     repeatable (quest_template SpecialFlags), and it leaves repeatable
//     quests out (Player::RewardQuest sets their status back to NONE), so its
//     answer takes them out again;
//   - lives through /reload, when nothing is sent, and is emptied at the next
//     login.
//
// The request is the packet `SendChatMessage(".queststatuses", "GUILD")`
// sends, without that function's AFK clear, and in language 0 like
// CGPlayer_C::SetAFK's packet: on SMSG_LOGIN_VERIFY_WORLD there is no player
// object to read the race's language from. The server parses commands before
// it looks up the guild (WorldSession::HandleMessagechatOpcode), so the
// request works without one, as pfQuest-turtle and Questie-Octo rely on.
//
// Only on a Turtle-lineage client (Turtle::Detected): elsewhere the text would
// reach guild chat, so nothing is sent and the functions stay absent.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "net/PacketWriter.h"
#include "turtle/Detect.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Quest::Completed {

namespace {

constexpr uint8_t CHAT_MSG_GUILD = 3;
constexpr uint32_t LANG_UNIVERSAL = 0;
constexpr uint32_t LANG_ADDON = 0xFFFFFFFF;
constexpr char COMMAND[] = ".queststatuses";
constexpr char PREFIX[] = "TWQUEST\t";
constexpr uint32_t PREFIX_LENGTH = sizeof(PREFIX) - 1;
// HandleQuestStatusesCommand's MaxMessageSize.
constexpr uint32_t MESSAGE_SPLIT = 2000;

// Sorted and unique: the last complete answer, plus quests turned in since.
std::vector<uint32_t> g_completed;
// The IDs of the answer being received; they replace g_completed at its last
// message.
std::vector<uint32_t> g_incoming;

const Game::Doc::Event kQueryCompleteDoc{
    "QuestLog", "Fires when the list of completed quests arrives from the server.", {}};
const Event::Custom::AutoReserve _reserve{"QUEST_QUERY_COMPLETE", &kQueryCompleteDoc};

void Request() {
    Net::OutPacket packet(Offsets::CMSG_MESSAGECHAT);
    packet.PutU32(CHAT_MSG_GUILD);
    packet.PutU32(LANG_UNIVERSAL);
    packet.PutString(COMMAND);
    packet.Send();
}

bool IsCompleted(uint32_t questID) {
    return std::binary_search(g_completed.begin(), g_completed.end(), questID);
}

void OnLoginVerifyWorld() {
    g_completed.clear();
    g_incoming.clear();
    if (Turtle::Detected())
        Request();
}

void OnQuestComplete(Net::CDataStore *packet) {
    const uint32_t questID = Net::Read<uint32_t>(packet);
    if (packet->m_read > packet->m_size || questID == 0 || !Turtle::Detected())
        return;
    const auto at = std::lower_bound(g_completed.begin(), g_completed.end(), questID);
    if (at == g_completed.end() || *at != questID)
        g_completed.insert(at, questID);
    Request();
}

// SMSG_MESSAGECHAT for chat type GUILD: u8 type, u32 language, the sender's
// GUID, u32 length (the terminator counted), the text, u8 tag (benilla-re
// ui/chatevents "The chat message"; tortoise-wow ChatHandler::BuildChatPacket).
void OnChat(Net::CDataStore *packet) {
    if (Net::Read<uint8_t>(packet) != CHAT_MSG_GUILD || Net::Read<uint32_t>(packet) != LANG_ADDON)
        return;
    const uint64_t sender = Net::Read<uint64_t>(packet);
    const uint32_t length = Net::Read<uint32_t>(packet);
    const auto *text = reinterpret_cast<const char *>(Net::ReadBytes(packet, length));
    if (text == nullptr || length <= PREFIX_LENGTH ||
        std::memcmp(text, PREFIX, PREFIX_LENGTH) != 0)
        return;
    // Anyone in the guild can send a TWQUEST addon message; the server's
    // answer comes from the player.
    if (sender == 0 || sender != Unit::Identity::PlayerGuid())
        return;

    const char *ids = text + PREFIX_LENGTH;
    const uint32_t idsLength = length - 1 - PREFIX_LENGTH;
    uint32_t id = 0;
    for (uint32_t i = 0; i < idsLength; ++i) {
        const char c = ids[i];
        if (c >= '0' && c <= '9') {
            id = id * 10 + static_cast<uint32_t>(c - '0');
        } else {
            if (id != 0)
                g_incoming.push_back(id);
            id = 0;
        }
    }
    if (id != 0)
        g_incoming.push_back(id);

    if (idsLength >= MESSAGE_SPLIT)
        return;
    std::sort(g_incoming.begin(), g_incoming.end());
    g_incoming.erase(std::unique(g_incoming.begin(), g_incoming.end()), g_incoming.end());
    g_completed.swap(g_incoming);
    g_incoming.clear();
    Event::Custom::Fire(_reserve.Slot(), "");
}

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    switch (opcode) {
    case Offsets::SMSG_MESSAGECHAT:
        OnChat(packet);
        break;
    case Offsets::SMSG_LOGIN_VERIFY_WORLD:
        OnLoginVerifyWorld();
        break;
    case Offsets::SMSG_QUESTGIVER_QUEST_COMPLETE:
        OnQuestComplete(packet);
        break;
    default:
        break;
    }
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

// 3.3.5's Script_QueryQuestsCompleted (0x005B1BB0) sends its request every
// time, with no answer awaited.
int __fastcall Script_QueryQuestsCompleted(void *L) {
    (void)L;
    Request();
    return 0;
}

// 3.3.5's Script_GetQuestsCompleted (0x005B5290): fills the table argument,
// or a new table, with [questID] = true.
int __fastcall Script_GetQuestsCompleted(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE)
        Game::Lua::PushValue(L, 1);
    else
        Game::Lua::NewTable(L);
    for (const uint32_t questID : g_completed) {
        Game::Lua::PushNumber(L, static_cast<double>(questID));
        Game::Lua::PushBoolean(L, 1);
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

int __fastcall Script_IsQuestFlaggedCompleted(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_QuestLog.IsQuestFlaggedCompleted(questID)");
        return 0;
    }
    const double questID = Game::Lua::ToNumber(L, 1);
    Game::Lua::PushBool(L, questID >= 1.0 && questID <= 4294967295.0 &&
                               IsCompleted(static_cast<uint32_t>(questID)));
    return 1;
}

int __fastcall Script_GetAllCompletedQuestIDs(void *L) {
    Game::Lua::NewTable(L);
    int index = 0;
    for (const uint32_t questID : g_completed) {
        Game::Lua::PushNumber(L, static_cast<double>(++index));
        Game::Lua::PushNumber(L, static_cast<double>(questID));
        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Function kQueryQuestsCompleted{
    "Asks the server for the completed quests; QUEST_QUERY_COMPLETE fires when they arrive.",
    {}, {}, "QuestLogGlobals"};

const Game::Doc::Field kGetQuestsCompletedArgs[] = {
    Game::Doc::Opt("questTable", "table", nullptr,
                   "The table to add the quests to. Without it, a new table."),
};
const Game::Doc::Field kGetQuestsCompletedRets[] = {
    Game::Doc::Req("completedQuests", "table", "Each completed quest ID, set to true."),
};
const Game::Doc::Function kGetQuestsCompleted{
    "Returns the completed quests.", kGetQuestsCompletedArgs, kGetQuestsCompletedRets,
    "QuestLogGlobals"};

const Game::Doc::Field kIsQuestFlaggedCompletedArgs[] = {
    Game::Doc::Req("questID", "number"),
};
const Game::Doc::Field kIsQuestFlaggedCompletedRets[] = {
    Game::Doc::Req("isCompleted", "bool"),
};
const Game::Doc::Function kIsQuestFlaggedCompleted{
    "Returns whether the character has completed a quest.", kIsQuestFlaggedCompletedArgs,
    kIsQuestFlaggedCompletedRets};

const Game::Doc::Field kGetAllCompletedQuestIDsRets[] = {
    Game::Doc::Req("quests", "table", "The completed quest IDs, from lowest to highest."),
};
const Game::Doc::Function kGetAllCompletedQuestIDs{
    "Returns the IDs of the completed quests.", {}, kGetAllCompletedQuestIDsRets};

} // namespace

static void RegisterLuaFunctions() {
    if (!Turtle::Detected())
        return;
    Game::Lua::RegisterGlobalFunction("QueryQuestsCompleted", &Script_QueryQuestsCompleted,
                                      &kQueryQuestsCompleted);
    Game::Lua::RegisterGlobalFunction("GetQuestsCompleted", &Script_GetQuestsCompleted,
                                      &kGetQuestsCompleted);
    Game::Lua::RegisterTableFunction("C_QuestLog", "IsQuestFlaggedCompleted",
                                     &Script_IsQuestFlaggedCompleted, &kIsQuestFlaggedCompleted);
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetAllCompletedQuestIDs",
                                     &Script_GetAllCompletedQuestIDs, &kGetAllCompletedQuestIDs);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Quest::Completed
