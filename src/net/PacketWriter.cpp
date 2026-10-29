// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#include "net/PacketWriter.h"

#include "Offsets.h"

namespace Net {

namespace {

using PutU8_t = void(__thiscall *)(CDataStore *self, uint8_t value);
using PutU32_t = void(__thiscall *)(CDataStore *self, uint32_t value);
using PutGuid_t = void(__thiscall *)(CDataStore *self, uint32_t lo, uint32_t hi);
using PutString_t = void(__thiscall *)(CDataStore *self, const char *text);
using Send_t = void(__thiscall *)(CDataStore *self);
using Free_t = void(__thiscall *)(CDataStore *self, uint8_t **buffer, uint32_t *base,
                                  uint32_t *alloc);

static_assert(sizeof(CDataStore) == 0x18, "CDataStore layout");

} // namespace

OutPacket::OutPacket(uint32_t opcode)
    : m_store{reinterpret_cast<void *>(Offsets::VTABLE_CDATASTORE), nullptr, 0, 0, 0,
              0xFFFFFFFF} {
    PutU32(opcode);
}

OutPacket::~OutPacket() {
    if (m_store.m_alloc != 0xFFFFFFFF) {
        const auto release =
            *reinterpret_cast<const Free_t *>(Offsets::VTABLE_CDATASTORE + sizeof(uintptr_t));
        release(&m_store, &m_store.m_buffer, &m_store.m_base, &m_store.m_alloc);
    }
}

void OutPacket::PutU8(uint8_t value) {
    reinterpret_cast<PutU8_t>(Offsets::FUN_DATASTORE_PUT_U8)(&m_store, value);
}

void OutPacket::PutU32(uint32_t value) {
    reinterpret_cast<PutU32_t>(Offsets::FUN_DATASTORE_PUT_U32)(&m_store, value);
}

void OutPacket::PutGuid(uint64_t guid) {
    reinterpret_cast<PutGuid_t>(Offsets::FUN_DATASTORE_PUT_GUID)(
        &m_store, static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32));
}

void OutPacket::PutString(const char *text) {
    reinterpret_cast<PutString_t>(Offsets::FUN_DATASTORE_PUT_CSTRING)(&m_store, text);
}

void OutPacket::Send() {
    m_store.m_read = 0;
    reinterpret_cast<Send_t>(Offsets::FUN_CLIENT_SERVICES_SEND)(&m_store);
}

} // namespace Net
