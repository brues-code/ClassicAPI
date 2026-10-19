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

// CHAT_MSG_* arg11 (lineID) and arg12 (sender GUID), as 4.3.4 fires them, and
// `GetCurrentChatGUID()`, which reads the same sender from inside the handler.
//
// 4.3.4's SendChatEvent (ChatFrame.cpp, 0x008E82E0) fires every chat event
// with "%s%s%s%s%s%s%d%d%s%d%d%s%u%b": the ten arguments 1.12 already passes,
// then lineID, the sender GUID string, and two Battle.net fields. 1.12 fires
// the first ten from one place, FUN_CHAT_EVENT_FIRE, so its FUN_FIRE_EVENT
// call is redirected here and the two are appended:
//
//   arg11 lineID  — ++counter for each message, before its event fires
//                   (4.3.4: `uint32_t lineID = ++s_chatLineID`). Text emotes
//                   get 0, as 4.3.4's AddTextEmoteMessage passes.
//   arg12 guid    — the sender's GUID string, only for a PLAYER GUID and not
//                   for a GM-tagged message; "" otherwise, never nil (4.3.4
//                   leaves its guidString buffer empty).
//
// 4.3.4's arg13 (Battle.net presence ID) and arg14 (mobile flag) have no
// 1.12 source and are left off.
//
// `GetCurrentChatGUID()` returns that same sender, as nil where arg12 is "",
// so code written for the function and code reading arg12 always agree.
//
// The sender comes from a SenderScope. Chat::Dispatch opens one around the
// chat-message dispatch with the GUID it picks for that chat type; the text
// emote builder is co-hooked below because it fires without passing its GUID
// on. A message fired outside any scope (FUN_0049B1B0, client-made notices)
// has no sender and still takes a lineID, as every 4.3.4 AddChatMessage does.
//
// The call site is redirected rather than FUN_FIRE_EVENT hooked: every event
// goes through FUN_FIRE_EVENT, chat goes through this one call. It also works
// under Turtle's patch, whose .tdata detour returns to the PUSH just before
// the redirected CALL.

#include "EventArgs.h"

#include "Common.h"
#include "RaidMarkers.h"
#include "Game.h"
#include "Offsets.h"
#include "debug/Log.h"
#include "guid/Guid.h"

#include <cstdint>
#include <cstring>

namespace Chat::EventArgs {

namespace {

uint64_t g_sender = 0; // 0 unless a player sender, already filtered
bool g_takesLineID = true;
uint32_t g_lineID = 0;

using Fire_t = void(__cdecl *)(int eventId, const char *fmt, ...);
using TextEmote_t = void(__fastcall *)(const uint64_t *senderGuid, int emote, const char *target,
                                       int emoteNum);

TextEmote_t g_textEmoteOriginal = nullptr;

// 1.12's chat tags are "AFK", "DND" and "GM"; 4.3.4's IsStaffChatFlag also
// matches "DEV", which this client never sends.
bool IsStaffFlag(const char *flags) { return flags != nullptr && std::strcmp(flags, "GM") == 0; }

// Same order as the engine's own ten; see Offsets.h at FUN_CHAT_EVENT_FIRE.
// cdecl with exactly the pushed argument count, so the caller's ADD ESP,0x30
// stays balanced.
void __cdecl FireSite(int eventId, const char *fmt, const char *text, const char *sender,
                      const char *language, const char *channelString, const char *target,
                      const char *flags, int zoneChannelID, int channelNumber,
                      const char *channelName, int channelUnk) {
    const auto fire = reinterpret_cast<Fire_t>(static_cast<uintptr_t>(Offsets::FUN_FIRE_EVENT));
    // `{rt1}` → marker icon in the event text only; the engine's buffer keeps
    // the tag for the bubble. 0x800 matches the engine's message-buffer cap.
    char markerBuf[0x800];
    text = Chat::RaidMarkers::ForChatEvent(text, markerBuf, sizeof markerBuf);
    // Another loader passing its own format: the arguments may not be ours to
    // extend, so fire exactly what was asked.
    if (fmt != reinterpret_cast<const char *>(Offsets::VAR_STR_CHAT_EVENT_FORMAT)) {
        fire(eventId, fmt, text, sender, language, channelString, target, flags, zoneChannelID,
             channelNumber, channelName, channelUnk);
        return;
    }
    const uint32_t lineID = g_takesLineID ? ++g_lineID : 0;
    char guid[Guid::STRING_SIZE] = "";
    if (g_sender != 0)
        Guid::FormatAsString(g_sender, guid, sizeof guid);
    fire(eventId, "%s%s%s%s%s%s%d%d%s%d%d%s", text, sender, language, channelString, target, flags,
         zoneChannelID, channelNumber, channelName, channelUnk, static_cast<int>(lineID), guid);
}

void __fastcall TextEmote_h(const uint64_t *senderGuid, int emote, const char *target,
                            int emoteNum) {
    SenderScope scope(*senderGuid, nullptr, false); // the engine dereferences it unchecked too
    g_textEmoteOriginal(senderGuid, emote, target, emoteNum);
}

int __fastcall Script_GetCurrentChatGUID(void *L) {
    if (g_sender == 0)
        return 0;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::FormatAsString(g_sender, buf, sizeof buf));
    return 1;
}

// Once per process; a patched site no longer reaches FUN_FIRE_EVENT, so a
// second pass would refuse anyway.
bool g_applied = false;

void Apply() {
    if (g_applied)
        return;
    g_applied = true;
    auto *site = reinterpret_cast<uint8_t *>(Offsets::PATCH_CHAT_EVENT_FIRE_CALL);
    int32_t rel;
    std::memcpy(&rel, site + 1, sizeof rel);
    if (site[0] != 0xE8 ||
        reinterpret_cast<uintptr_t>(site) + 5 + rel != Offsets::FUN_FIRE_EVENT) {
        Debug::Log::Printf("Chat::EventArgs: chat event fire call is not the stock CALL; "
                           "CHAT_MSG_* events keep 10 arguments");
        return;
    }
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FireSite) -
                               (reinterpret_cast<uintptr_t>(site) + 5));
    Common::PatchBytes(site + 1, &rel, sizeof rel);
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetCurrentChatGUID", &Script_GetCurrentChatGUID);
    Apply();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::HookAutoRegister _textEmoteHook{Offsets::FUN_CHAT_TEXT_EMOTE,
                                            reinterpret_cast<void *>(&TextEmote_h),
                                            reinterpret_cast<void **>(&g_textEmoteOriginal)};

} // namespace

SenderScope::SenderScope(uint64_t senderGuid, const char *flags, bool takesLineID)
    : prevGuid_(g_sender), prevTakesLineID_(g_takesLineID) {
    g_sender = (Guid::IsPlayer(senderGuid) && !IsStaffFlag(flags)) ? senderGuid : 0;
    g_takesLineID = takesLineID;
}

SenderScope::~SenderScope() {
    g_sender = prevGuid_;
    g_takesLineID = prevTakesLineID_;
}

} // namespace Chat::EventArgs
