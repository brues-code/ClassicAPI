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
//   - Chat::CurrentGUID::DispatchScope — publish the sender GUID for the
//     synchronous CHAT_MSG_* event so `GetCurrentChatGUID()` can read it.
//
// Signature — `__fastcall`, `RET 0x28`:
//   ECX  = arg1  — the MESSAGE text (verified from the call site in FUN_0049D560:
//                  `MOV ECX,[EBP-0x10]` = the raw/processed message. An older
//                  note mislabeled this "sender name".) Copied into one buffer
//                  fed to both the chat frame and the bubble spawn FUN_00608AC0.
//   EDX  = arg2  — chat type byte
//   [esp+04] = arg3      [esp+08] = arg4      [esp+0c] = arg5
//   [esp+10] = arg6      [esp+14] = arg7
//   [esp+18] = arg8  — target GUID lo (whisper/channel target)
//   [esp+1c] = arg9  — target GUID hi
//   [esp+20] = arg10
//   [esp+24] = arg11 — sender GUID lo   ← published
//   [esp+28] = arg12 — sender GUID hi   ← published
//
// The `RET 0x28` cleanup (40 bytes = 10 stack args) MUST match the declared
// signature exactly; an earlier 10-arg / `ret 0x20` version left 8 bytes
// uncleaned per call and crashed the engine on a later indirect call.

#include "CurrentGUID.h"
#include "IconFilter.h"
#include "RaidMarkers.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Chat::Dispatch {

namespace {

using ChatDispatch_t = void(__fastcall *)(
    const char *message, int chatType,
    int arg3, int arg4, int arg5, int arg6, int arg7,
    int targetGuidLo, int targetGuidHi, int arg10,
    int senderGuidLo, int senderGuidHi);

ChatDispatch_t g_original = nullptr;

void __fastcall ChatDispatch_h(
    const char *message, int chatType,
    int arg3, int arg4, int arg5, int arg6, int arg7,
    int targetGuidLo, int targetGuidHi, int arg10,
    int senderGuidLo, int senderGuidHi)
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

    // Publish the sender GUID for the CHAT_MSG_* event the original fires
    // synchronously; restored when this scope ends (after the original returns).
    Chat::CurrentGUID::DispatchScope guid(static_cast<uint32_t>(senderGuidLo),
                                          static_cast<uint32_t>(senderGuidHi));

    g_original(msg, chatType, arg3, arg4, arg5, arg6, arg7,
               targetGuidLo, targetGuidHi, arg10,
               senderGuidLo, senderGuidHi);
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_CHAT_DISPATCH,
    reinterpret_cast<void *>(&ChatDispatch_h),
    reinterpret_cast<void **>(&g_original)};

} // namespace

} // namespace Chat::Dispatch
