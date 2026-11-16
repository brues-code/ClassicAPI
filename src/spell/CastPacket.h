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

#pragma once

#include <cstdint>

// SMSG_SPELL_START and SMSG_SPELL_GO, and the player's own CMSG_CAST_SPELL,
// decoded once for every module that reads them. One Net::PacketDispatch subscriber parses each packet (so, like every
// dispatch subscriber, it runs before the engine's own handler) and calls the
// subscribers below with the result. The layout is the server's writer
// (vmangos WorldPackets::Spell::SpellStart / SpellGo::AppendBodyTo and
// SpellCastTargets::write).
namespace Spell::CastPacket {

// The SpellCastTargets block both packets end with.
struct Targets {
    uint16_t mask;
    // The one object GUID a SPELL_TARGET_FLAGS_OBJECT bit carries (a unit,
    // game object or corpse), 0 when none. Test `mask` for which kind.
    uint64_t object;
    uint64_t item; // the item a SPELL_TARGET_FLAGS_ITEM bit carries, 0 when none
    float src[3];  // valid when mask has SPELL_TARGET_FLAG_SOURCE_LOCATION
    float dst[3];  // valid when mask has SPELL_TARGET_FLAG_DEST_LOCATION
};

// The fields both packets open with. `item` is the item cast from (0 for a
// spell); `caster` the unit that casts.
struct Header {
    uint64_t item;
    uint64_t caster;
    uint32_t spellId;
    uint16_t castFlags;
};

struct Start {
    Header header;
    uint32_t castTimeMs;
    Targets targets;
};

// `hits` lists every unit the spell landed on, in packet order; it points into
// the decoder's buffer and is valid only during the callback.
struct Go {
    Header header;
    const uint64_t *hits;
    int hitCount;
    Targets targets;
};

// CMSG_CAST_SPELL leaving the client (Net::SendObserver), as the server's
// SpellCastTargets::read takes it: u32 spell, then the targets block.
struct Sent {
    uint32_t spellId;
    Targets targets;
};

// Called only for a packet that decoded to its end without overrunning.
using StartCallback = void (*)(const Start &);
using GoCallback = void (*)(const Go &);
using SentCallback = void (*)(const Sent &);

// File-scope `static const` instances chain onto the decoder at static-init
// time. Order across subscribers is unspecified.
struct AutoStart {
    explicit AutoStart(StartCallback fn);
    StartCallback fn;
    AutoStart *next;
};

struct AutoGo {
    explicit AutoGo(GoCallback fn);
    GoCallback fn;
    AutoGo *next;
};

struct AutoSent {
    explicit AutoSent(SentCallback fn);
    SentCallback fn;
    AutoSent *next;
};

} // namespace Spell::CastPacket
