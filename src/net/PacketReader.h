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

#include <cstdint>

// Minimal reader for the engine's `CDataStore` packet buffer, shared by the
// packet-observer subscribers (`Aura::Source`'s SpellGo, `Spell::Cast`'s
// SpellStart, …) that read incoming SMSGs via `Net::PacketDispatch`.
//
// `CDataStore` is POLYMORPHIC in the engine (virtual Internal*/Reset/etc.),
// so a vtable pointer sits at offset 0 and the data members start at +0x04.
// Omitting `vtable` shifts every field by 4 and makes `m_buffer` read the
// vptr — a garbage read address (this caused an early ACCESS_VIOLATION in
// the SpellGo hook before the slot was added).
//
// Incoming SMSG packets are a single contiguous buffer (`m_base == 0`,
// `m_alloc == size`), so the simple `m_buffer - m_base + m_read` read with
// an `m_size` bound is sufficient — the same critical guard as the engine's
// own `CDataStore::Get`. Callers should save `m_read` before reading and
// restore it before handing the packet to the original handler.

namespace Net {

struct CDataStore {
    void *vtable;       // +0x00
    uint8_t *m_buffer;  // +0x04
    uint32_t m_base;    // +0x08
    uint32_t m_alloc;   // +0x0C
    uint32_t m_size;    // +0x10
    uint32_t m_read;    // +0x14
};

// Reads a fixed-width field and advances the cursor. On overrun it poisons
// `m_read` (so subsequent reads short-circuit) and returns a zero value.
template <typename T>
T Read(CDataStore *p) {
    T val{};
    const uint32_t end = p->m_read + sizeof(T);
    if (end > p->m_size) {
        p->m_read = p->m_size + 1;
        return val;
    }
    val = *reinterpret_cast<const T *>(p->m_buffer - p->m_base + p->m_read);
    p->m_read += sizeof(T);
    return val;
}

// Reads a WoW packed GUID: a 1-byte mask followed by one byte per set bit,
// each contributing the corresponding byte of the 64-bit value.
inline uint64_t ReadPackedGuid(CDataStore *p) {
    uint64_t guid = 0;
    const uint8_t mask = Read<uint8_t>(p);
    for (int i = 0; i < 8; ++i) {
        if (mask & (1u << i))
            guid |= static_cast<uint64_t>(Read<uint8_t>(p)) << (i * 8);
    }
    return guid;
}

} // namespace Net
