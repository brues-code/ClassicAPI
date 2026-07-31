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

#include "SendObserver.h"

#include "Game.h"
#include "Offsets.h"

namespace Net::SendObserver {

namespace {

AutoSubscribe *g_head = nullptr;

// `FUN_NET_SEND` — `__thiscall(conn, CDataStore*)`, rendered here as
// `__fastcall(conn /*ecx*/, edx, packet)`. The leading u32 of the buffer is
// the opcode.
using NetSend_t = void(__fastcall *)(void *conn, void *edx, CDataStore *packet);
NetSend_t g_origNetSend = nullptr;

void __fastcall NetSend_h(void *conn, void *edx, CDataStore *packet) {
    if (packet != nullptr && g_head != nullptr) {
        const uint32_t saved = packet->m_read;
        const uint32_t opcode = Net::Read<uint32_t>(packet);
        const uint32_t afterOpcode = packet->m_read;
        for (auto *node = g_head; node != nullptr; node = node->next) {
            packet->m_read = afterOpcode; // each subscriber reads independently
            node->cb(opcode, packet);
        }
        packet->m_read = saved; // hand the engine an untouched cursor
    }
    g_origNetSend(conn, edx, packet);
}

} // namespace

AutoSubscribe::AutoSubscribe(Callback cb) : cb(cb), next(g_head) {
    g_head = this;
}

static const Game::HookAutoRegister _hook{
    Offsets::FUN_NET_SEND,
    reinterpret_cast<void *>(&NetSend_h),
    reinterpret_cast<void **>(&g_origNetSend)};

} // namespace Net::SendObserver
