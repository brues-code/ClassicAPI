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

// Shared observer for INCOMING packets — the receive-side mirror of
// `Net::SendObserver`. The engine's SMSG dispatch funnel
// (`FUN_NET_MESSAGE_DISPATCH`) is the single chokepoint every dispatched SMSG
// passes through *before* the engine's per-opcode handler runs.
//
// Why this exists: co-existing DLLs (notably nampower via hadesmem, plus
// SuperWoW) MinHook/detour the individual per-opcode SMSG handlers
// (SpellGo/SpellStart/SpellDelayed/…). Two inline hookers on the same prologue
// is the documented collision risk. nampower does NOT hook this funnel — it
// hooks the leaf handlers — so any feature that only needs to *read* an
// incoming packet subscribes here instead, and we stop sharing those
// prologues. One hook on a function nobody else patches replaces N hooks on
// functions everybody patches.
//
// Declare a file-scope
//   static const Net::PacketDispatch::AutoSubscribe _sub{&callback};
// Each callback receives the u16 `opcode` (already read) and the `CDataStore`
// positioned **right after the opcode** (body start), exactly as the engine's
// per-opcode handler would see it. The dispatcher resets that cursor before
// every subscriber (so they read independently) and restores the original
// `m_read` before handing the packet to the engine. Callbacks must only
// READ — never mutate the packet — and must filter on `opcode` themselves.

namespace Net::PacketDispatch {

// Chains onto the incoming-packet subscriber list at static-init time. The
// list plumbing + fan-out live in net/PacketObserver.h (shared with
// Net::SendObserver).
struct AutoSubscribe : Net::PacketSubscriber {
    explicit AutoSubscribe(Net::PacketCallback cb);
};

} // namespace Net::PacketDispatch
