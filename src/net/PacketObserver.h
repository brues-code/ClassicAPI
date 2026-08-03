// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

#include "net/PacketReader.h"

#include <cstdint>

// Shared plumbing for the two packet-observer funnels — the outgoing
// `Net::SendObserver` (CMSG, u32 opcode) and the incoming `Net::PacketDispatch`
// (SMSG, u16 opcode). Both co-hook a single engine chokepoint and fan each
// packet out to opcode subscribers; the only real differences are the opcode
// width and the hooked function's ABI. Everything else — the intrusive
// subscriber list, the subscribe helper, and the read-opcode / reset-cursor /
// fan-out / restore-cursor loop — lives here so a fix to the fan-out logic
// isn't made twice.
//
// Each observer keeps its own file-scope `PacketSubscriber *g_head` and defines
// a thin `AutoSubscribe` in its own namespace (so call sites stay
// `Net::PacketDispatch::AutoSubscribe{&cb}`); the shared pieces below do the
// work.

namespace Net {

using PacketCallback = void (*)(uint32_t opcode, CDataStore *packet);

// Intrusive node for an observer's subscriber list. Each observer's
// `AutoSubscribe` derives from this and chains itself on at static-init time.
struct PacketSubscriber {
    PacketCallback cb;
    PacketSubscriber *next;
};

// Chains `node` (already carrying its `cb`) onto `head`.
inline void Subscribe(PacketSubscriber *&head, PacketSubscriber *node,
                      PacketCallback cb) {
    node->cb = cb;
    node->next = head;
    head = node;
}

// Reads the leading opcode (width `OpcodeT`) at the packet's current cursor,
// then invokes every subscriber with the cursor reset to the body before each
// (so they read independently) and the original `m_read` restored afterward —
// so the engine (and any leaf-handler hooks) see an untouched packet.
template <typename OpcodeT>
inline void FanOutByOpcode(PacketSubscriber *head, CDataStore *packet) {
    const uint32_t saved = packet->m_read;
    const uint32_t opcode = static_cast<uint32_t>(Read<OpcodeT>(packet));
    const uint32_t afterOpcode = packet->m_read;
    for (auto *node = head; node != nullptr; node = node->next) {
        packet->m_read = afterOpcode; // each subscriber reads independently
        node->cb(opcode, packet);
    }
    packet->m_read = saved; // hand the engine an untouched cursor
}

} // namespace Net
