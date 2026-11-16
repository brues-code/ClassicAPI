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

// The bodies, as the server writes them (vmangos WorldPackets::Spell):
//
//   SMSG_SPELL_START  packed item-or-caster, packed caster, u32 spell,
//                     u16 castFlags, u32 castTime, SpellCastTargets
//   SMSG_SPELL_GO     packed item-or-caster, packed caster, u32 spell,
//                     u16 castFlags, u8 hits + u64 each, u8 misses + {u64
//                     guid, u8 reason, u8 reflect result only for
//                     SPELL_MISS_REFLECT} each, SpellCastTargets
//
// SpellCastTargets::write: u16 mask; one packed GUID when the mask has an
// object bit (unit, game object or corpse); one packed GUID for an item
// target; 3 floats for the source and 3 for the destination; a C string for a
// string target. Ammo info (CAST_FLAG_AMMO) follows and nobody reads it.
//
//   CMSG_CAST_SPELL   u32 spell, SpellCastTargets (the client writes what the
//                     server's SpellCastTargets::read takes)

#include "spell/CastPacket.h"

#include "Offsets.h"
#include "net/PacketDispatch.h"
#include "net/SendObserver.h"

#include <cstdint>

namespace Spell::CastPacket {

namespace {

using Net::CDataStore;
using Net::Read;
using Net::ReadPackedGuid;

constexpr uint8_t SPELL_MISS_REFLECT = 11; // vmangos SpellMissInfo
// The string target's bound: CDataStore::GetCString's 1.12 callers read target
// strings into 256-byte buffers.
constexpr uint32_t kTargetStringCapacity = 256;

AutoStart *g_start = nullptr;
AutoGo *g_go = nullptr;
AutoSent *g_sent = nullptr;

// Every hit a u8 count can name.
uint64_t g_hits[255];

bool Overrun(const CDataStore *p) { return p->m_read > p->m_size; }

Header ReadHeader(CDataStore *p) {
    Header h{};
    h.item = ReadPackedGuid(p);
    h.caster = ReadPackedGuid(p);
    h.spellId = Read<uint32_t>(p);
    h.castFlags = Read<uint16_t>(p);
    return h;
}

Targets ReadTargets(CDataStore *p) {
    Targets t{};
    t.mask = Read<uint16_t>(p);
    if (t.mask & Offsets::SPELL_TARGET_FLAGS_OBJECT)
        t.object = ReadPackedGuid(p);
    if (t.mask & Offsets::SPELL_TARGET_FLAGS_ITEM)
        t.item = ReadPackedGuid(p);
    if (t.mask & Offsets::SPELL_TARGET_FLAG_SOURCE_LOCATION)
        for (float &v : t.src)
            v = Read<float>(p);
    if (t.mask & Offsets::SPELL_TARGET_FLAG_DEST_LOCATION)
        for (float &v : t.dst)
            v = Read<float>(p);
    if (t.mask & Offsets::SPELL_TARGET_FLAG_STRING)
        Net::ReadCString(p, kTargetStringCapacity);
    return t;
}

void DecodeStart(CDataStore *p) {
    if (g_start == nullptr)
        return;
    Start s{};
    s.header = ReadHeader(p);
    s.castTimeMs = Read<uint32_t>(p);
    s.targets = ReadTargets(p);
    if (Overrun(p))
        return;
    for (const AutoStart *a = g_start; a != nullptr; a = a->next)
        a->fn(s);
}

void DecodeGo(CDataStore *p) {
    if (g_go == nullptr)
        return;
    Go g{};
    g.header = ReadHeader(p);
    g.hitCount = Read<uint8_t>(p);
    for (int i = 0; i < g.hitCount; ++i)
        g_hits[i] = Read<uint64_t>(p);
    g.hits = g_hits;
    const int misses = Read<uint8_t>(p);
    for (int i = 0; i < misses && !Overrun(p); ++i) {
        Read<uint64_t>(p);
        if (Read<uint8_t>(p) == SPELL_MISS_REFLECT)
            Read<uint8_t>(p);
    }
    g.targets = ReadTargets(p);
    if (Overrun(p))
        return;
    for (const AutoGo *a = g_go; a != nullptr; a = a->next)
        a->fn(g);
}

void OnPacket(uint32_t opcode, CDataStore *p) {
    if (p == nullptr)
        return;
    if (opcode == Offsets::SMSG_SPELL_START)
        DecodeStart(p);
    else if (opcode == Offsets::SMSG_SPELL_GO)
        DecodeGo(p);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

void OnSend(uint32_t opcode, CDataStore *p) {
    if (opcode != Offsets::OP_CMSG_CAST_SPELL || p == nullptr || g_sent == nullptr)
        return;
    Sent s{};
    s.spellId = Read<uint32_t>(p);
    s.targets = ReadTargets(p);
    if (Overrun(p) || s.spellId == 0)
        return;
    for (const AutoSent *a = g_sent; a != nullptr; a = a->next)
        a->fn(s);
}

const Net::SendObserver::AutoSubscribe _sendSub{&OnSend};

} // namespace

AutoStart::AutoStart(StartCallback f) : fn(f), next(g_start) { g_start = this; }
AutoGo::AutoGo(GoCallback f) : fn(f), next(g_go) { g_go = this; }
AutoSent::AutoSent(SentCallback f) : fn(f), next(g_sent) { g_sent = this; }

} // namespace Spell::CastPacket
