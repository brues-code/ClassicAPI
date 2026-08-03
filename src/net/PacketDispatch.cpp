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

#include "PacketDispatch.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Net::PacketDispatch {

namespace {

PacketSubscriber *g_head = nullptr;

// `FUN_NET_MESSAGE_DISPATCH` — `__thiscall(netClient, param_1, CDataStore*)`,
// rendered here as `__fastcall(self /*ecx*/, edx, param_1, packet)`. The
// engine reads the leading u16 opcode from the packet, then dispatches to
// `netClient->handlers[opcode]`. We peek the same u16, fan out to subscribers
// with the cursor at the body, restore the cursor, and let the original run —
// so the engine (and any leaf-handler hooks) see an untouched packet.
using Dispatch_t = void(__fastcall *)(void *self, void *edx, uint32_t param_1,
                                      CDataStore *packet);
Dispatch_t g_orig = nullptr;

void __fastcall Dispatch_h(void *self, void *edx, uint32_t param_1,
                           CDataStore *packet) {
    if (packet != nullptr && g_head != nullptr)
        Net::FanOutByOpcode<uint16_t>(g_head, packet); // opcode is a u16 SMSG id
    g_orig(self, edx, param_1, packet);
}

} // namespace

AutoSubscribe::AutoSubscribe(Net::PacketCallback cb) {
    Net::Subscribe(g_head, this, cb);
}

static const Game::HookAutoRegister _hook{
    Offsets::FUN_NET_MESSAGE_DISPATCH,
    reinterpret_cast<void *>(&Dispatch_h),
    reinterpret_cast<void **>(&g_orig)};

} // namespace Net::PacketDispatch
