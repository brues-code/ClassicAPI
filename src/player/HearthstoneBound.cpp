// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

// `HEARTHSTONE_BOUND` (no payload) — fires each time the player binds their
// hearthstone, including a rebind at the same inn. Addons re-read
// `GetBindLocation()` on it.
//
// The trigger is SMSG_PLAYERBOUND, not SMSG_BINDPOINTUPDATE: the server
// re-sends the bind point at every login and map change, but sends
// SMSG_PLAYERBOUND only from the bind spell effect, right after the
// bind-point update that carries the new location (tortoise-wow and vmangos
// `EffectBind`). So nothing needs filtering, and because the 0x155 packet
// has already been processed, GetBindLocation() answers with the new inn
// even though PacketDispatch subscribers run before the engine's handler.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/PacketDispatch.h"

#include <cstdint>

namespace Player::HearthstoneBound {

namespace {

const Event::Custom::AutoReserve _reserve{"HEARTHSTONE_BOUND"};

void OnPacket(uint32_t opcode, Net::CDataStore *) {
    if (opcode == Offsets::SMSG_PLAYERBOUND)
        Event::Custom::Fire(_reserve.Slot(), "");
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

} // namespace Player::HearthstoneBound
