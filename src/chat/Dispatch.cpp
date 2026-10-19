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

// Single owner of the chat-dispatch hook (FUN_0049A870).
//
// The engine routes every server-delivered chat message — say/yell/whisper/
// channel/emote and synthetic system notifications — through this one function.
// (Addon `print`/`AddMessage` does NOT pass through it.) Several independent
// concerns need to observe or transform a dispatched message, but MinHook allows
// only ONE hook per target, so this module owns the hook and orchestrates them,
// mirroring Net::PacketDispatch (one hook, fanned out to independent concerns):
//
//   - Chat::IconFilter::Sanitize — strip player-injected `|T` icon spoofs from
//     the message before the engine builds the chat line AND the speech bubble
//     (both come from the same buffer, so one sanitize covers both).
//   - Chat::RaidMarkers::Substitute — expand `{rt1}`/`{skull}`/… raid-target
//     tokens into inline marker icons. Runs AFTER Sanitize so a player gets the
//     fixed marker icons but still can't inject an arbitrary `|T` path.
//   - Chat::EventArgs::SenderScope — name the sender for the CHAT_MSG_* event
//     the original fires synchronously: its arg12 and `GetCurrentChatGUID()`.
//
// Signature — `__fastcall`, `RET 0x28` (stack args at entry):
//   ECX  = the MESSAGE text (`MOV ECX,[EBP-0x10]` in FUN_0049D560; copied into
//          one buffer that feeds both the chat frame and the bubble spawn
//          FUN_00608AC0)
//   EDX  = chat type
//   [esp+04] senderName   [esp+08] languageID   [esp+0c] channel
//   [esp+10] targetName   [esp+14] flags ("AFK"/"DND"/"GM")
//   [esp+18] / [esp+1c]  speaker GUID lo/hi — the unit the bubble goes on
//   [esp+20] rank
//   [esp+24] / [esp+28]  GUID lo/hi read right before the message text
//
// The `RET 0x28` cleanup (40 bytes = 10 stack args) MUST match the declared
// signature exactly; an earlier 10-arg / `ret 0x20` version left 8 bytes
// uncleaned per call and crashed the engine on a later indirect call.
//
// WHICH GUID IS THE SENDER. Neither pair is the sender for every caller, so
// Sender() picks per caller and chat type (packet layouts in Offsets.h at
// FUN_CHAT_DISPATCH):
//   - Monster say/yell/emote/whisper and raid boss emote: the last pair is the
//     monster's TARGET — often the local player — and the speaker pair, where
//     the packet has one, is a creature. 4.3.4 reports no GUID for a creature
//     sender, so these report none.
//   - Raid boss whisper (wire type 0x59): FUN_0049D560 reads name + TARGET and
//     dispatches it as a plain whisper (type 6), so the chat type no longer
//     shows it. The SMSG_MESSAGECHAT subscriber below keeps the wire type for
//     the dispatch the handler is about to make.
//   - The two pending-chat callers pass zeros for both pairs: the name-query
//     callback that sends a sender's queued messages once the name arrives
//     (every message from a sender the name cache did not know yet), and the
//     enter-world drain. The entry keeps the GUID it was waiting on, and
//     arg 5 / arg 7 point into that entry.
//   - Everything else: the last pair.

#include "EventArgs.h"
#include "IconFilter.h"
#include "RaidMarkers.h"

#include "Game.h"
#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"

#include <cstdint>
#include <intrin.h>

namespace Chat::Dispatch {

namespace {

constexpr int CHAT_MSG_MONSTER_SAY = 0x0B;
constexpr int CHAT_MSG_MONSTER_YELL = 0x0C;
constexpr int CHAT_MSG_MONSTER_EMOTE = 0x0D;
constexpr int CHAT_MSG_MONSTER_WHISPER = 0x1A;
constexpr int CHAT_MSG_RAID_BOSS_WHISPER = 0x59;
constexpr int CHAT_MSG_RAID_BOSS_EMOTE = 0x5A;

using ChatDispatch_t = void(__fastcall *)(
    const char *message, int chatType,
    const char *senderName, int languageID, const char *channel, const char *targetName,
    const char *flags, int speakerGuidLo, int speakerGuidHi, int rank,
    int guidLo, int guidHi);

ChatDispatch_t g_original = nullptr;

// Wire chat type of the SMSG_MESSAGECHAT being handled. Set before the engine's
// handler runs, which is the only caller that returns to
// CHAT_DISPATCH_RET_PACKET, so it is always that packet's type there.
int g_packetChatType = -1;

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode == Offsets::SMSG_MESSAGECHAT && packet != nullptr)
        g_packetChatType = Net::Read<uint8_t>(packet);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

bool IsMonsterChat(int chatType) {
    switch (chatType) {
    case CHAT_MSG_MONSTER_SAY:
    case CHAT_MSG_MONSTER_YELL:
    case CHAT_MSG_MONSTER_EMOTE:
    case CHAT_MSG_MONSTER_WHISPER:
    case CHAT_MSG_RAID_BOSS_EMOTE:
        return true;
    default:
        return false;
    }
}

uint64_t Sender(const void *caller, int chatType, const char *channel, const char *flags,
                int guidLo, int guidHi) {
    if (IsMonsterChat(chatType))
        return 0;
    const auto at = reinterpret_cast<uintptr_t>(caller);
    if (at == Offsets::CHAT_DISPATCH_RET_PENDING_NAME ||
        at == Offsets::CHAT_DISPATCH_RET_PENDING_DRAIN) {
        const auto *entry =
            reinterpret_cast<const uint8_t *>(channel) - Offsets::OFF_PENDING_CHAT_CHANNEL;
        // Both pointers must land in one entry, or this is not the layout the
        // drain is known to pass.
        if (reinterpret_cast<const uint8_t *>(flags) != entry + Offsets::OFF_PENDING_CHAT_FLAGS)
            return 0;
        return *reinterpret_cast<const uint64_t *>(entry + Offsets::OFF_PENDING_CHAT_GUID);
    }
    if (at == Offsets::CHAT_DISPATCH_RET_PACKET && g_packetChatType == CHAT_MSG_RAID_BOSS_WHISPER)
        return 0;
    return (static_cast<uint64_t>(static_cast<uint32_t>(guidHi)) << 32) |
           static_cast<uint32_t>(guidLo);
}

void __fastcall ChatDispatch_h(
    const char *message, int chatType,
    const char *senderName, int languageID, const char *channel, const char *targetName,
    const char *flags, int speakerGuidLo, int speakerGuidHi, int rank,
    int guidLo, int guidHi)
{
    // Strip player-injected `|T` icon escapes (covers chat line + speech bubble;
    // both build from this message). The 0x800 buffer matches the engine's own
    // message-buffer cap.
    char iconBuf[0x800];
    const char *msg = Chat::IconFilter::Sanitize(message, iconBuf, sizeof iconBuf);

    // Expand raid-target tokens ({rt1}/{skull}/…) into inline marker icons —
    // after the sanitize, so only these fixed, safe escapes are added.
    char markerBuf[0x800];
    msg = Chat::RaidMarkers::Substitute(msg, markerBuf, sizeof markerBuf);

    // The sender for the CHAT_MSG_* event the original fires synchronously;
    // restored when this scope ends (after the original returns).
    Chat::EventArgs::SenderScope sender(
        Sender(_ReturnAddress(), chatType, channel, flags, guidLo, guidHi), flags, true);

    g_original(msg, chatType, senderName, languageID, channel, targetName, flags,
               speakerGuidLo, speakerGuidHi, rank, guidLo, guidHi);
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_CHAT_DISPATCH,
    reinterpret_cast<void *>(&ChatDispatch_h),
    reinterpret_cast<void **>(&g_original)};

} // namespace

} // namespace Chat::Dispatch
