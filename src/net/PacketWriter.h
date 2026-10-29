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

// Outgoing packets, built the way every client send builds one: a stack
// `CDataStore` with the engine's vtable, filled through the engine's own Put
// functions, sent through ClientServices::Send, then freed through the
// vtable's slot 1 (the send of FUN_DBCACHE_ITEMSTATS_GET_RECORD,
// 0x0055BB18..0x0055BB89, step for step). The first write is the opcode as a
// u32. ClientServices::Send drops the packet while there is no world
// connection (benilla-re net/server).

namespace Net {

class OutPacket {
public:
    explicit OutPacket(uint32_t opcode);
    ~OutPacket();
    OutPacket(const OutPacket &) = delete;
    OutPacket &operator=(const OutPacket &) = delete;

    void PutU8(uint8_t value);
    void PutU32(uint32_t value);
    void PutGuid(uint64_t guid);
    // A C string with its terminator.
    void PutString(const char *text);
    void Send();

private:
    CDataStore m_store;
};

} // namespace Net
