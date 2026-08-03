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

#include "net/PacketObserver.h"
#include "net/PacketReader.h"

#include <cstdint>

// Shared observer for outgoing packets. The engine's NetClient send funnel
// (`FUN_NET_SEND`) is a general chokepoint every dispatched CMSG passes
// through — not something a single feature should own. MinHook permits one
// hook per target, so any module that wants to watch sent packets subscribes
// here instead of hooking the funnel itself.
//
// Mirrors `Tick::WorldTick`: declare a file-scope
// `static const Net::SendObserver::AutoSubscribe _sub{&callback};`. The
// constructor chains onto the internal list at static-init time.
//
// Each subscriber's callback receives the leading `opcode` (u32) already
// read, and the `CDataStore` positioned **right after the opcode** so it can
// read the message body directly. The observer resets that cursor before
// every subscriber (so they read independently) and restores the original
// `m_read` before handing the packet to the engine. Callbacks must only
// READ — never mutate the packet.

namespace Net::SendObserver {

// Chains onto the outgoing-packet subscriber list at static-init time. The
// list plumbing + fan-out live in net/PacketObserver.h (shared with
// Net::PacketDispatch).
struct AutoSubscribe : Net::PacketSubscriber {
    explicit AutoSubscribe(Net::PacketCallback cb);
};

} // namespace Net::SendObserver
