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

// What each unit in view looks like, for replay tools: the movement file's
// UNIT_INFO, PLAYER_INFO and UNIT_GONE lines (Movement.cpp documents them). A
// ClassicAPI extension. Each frame walks the units in view and writes the
// lines of a unit new to view, or whose look changed since its last line, and
// UNIT_GONE for a unit no longer in view. Position and facing ride along on
// UNIT_INFO but are not a change: the move lines carry them. They are what
// places a unit that never moves.
//
// The fields are the ones the client builds a unit's model from
// (CGUnit_C::BuildCharacterComponent, 0x005FB200): the display, which a
// CreatureDisplayInfoExtra row dresses for a character-model NPC; and, for a
// player shown in its own display (DISPLAYID equal to NATIVEDISPLAYID), race
// and sex from UNIT_FIELD_BYTES_0, skin, face, hair and facial hair from
// PLAYER_BYTES and PLAYER_BYTES_2, and the 19 visible items it dresses with
// CGPlayer_C::ApplyEquipmentDisplay. The rest of UNIT_INFO is the look around
// the model: mount, scale, stand state, form, stealth, sheath state, and the
// hands a creature holds.
//
// UNIT_CAST is the unit's cast state as the client keeps it: the spell it
// casts (CGUnit +0xC8C, which SMSG_SPELL_START sets and ClearCastingSpell
// 0x0060D040 clears when the cast goes off, is interrupted, or the unit moves
// or dies) and the spell it channels (UNIT_FIELD_CHANNEL_SPELL). The cast
// time comes from the SMSG_SPELL_START that began the cast. A cast without a
// cast time is not written, as the combat log writes no SPELL_CAST_START for
// one. The combat log tells when a cast went off; this tells when one stopped
// without going off, and when a channel ended, which no combat log line does.

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "cache/Record.h"
#include "combatlog/File.h"
#include "combatlog/Movement.h"
#include "guid/Guid.h"
#include "item/ID.h"
#include "model/Outfit.h"
#include "object/Resolve.h"
#include "spell/CastPacket.h"
#include "tick/FrameTick.h"
#include "unit/Identity.h"
#include "unit/Position.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unordered_map>

namespace CombatLog::UnitInfo {

namespace {

using File::AppendFixed;
using File::AppendHex;
using File::AppendSigned;
using File::AppendUnsigned;

constexpr int kHands = Offsets::UNIT_VIRTUAL_ITEM_SLOTS;

// A unit's UNIT_INFO fields apart from its place. Dwords only, so two compare
// with memcmp.
struct Look {
    uint32_t display;
    uint32_t nativeDisplay;
    uint32_t mountDisplay;
    float scale;
    uint32_t bytes1;
    uint32_t bytes2;
    uint32_t heldDisplay[kHands];
    uint32_t heldInfo[kHands * 2];
};

// A player's PLAYER_INFO fields. Dwords only.
struct Dress {
    uint32_t race;
    uint32_t classId;
    uint32_t sex;
    uint32_t skin;
    uint32_t face;
    uint32_t hairStyle;
    uint32_t hairColor;
    uint32_t facialHair;
    uint32_t flags;
    Model::Outfit::Worn worn;
    int32_t emblem[5]; // its guild's tabard emblem, GetGuildEmblem's order; -1 unknown
};

// A unit's UNIT_CAST fields; all 0 while it neither casts nor channels.
struct Cast {
    uint32_t spell;   // the cast under way
    uint32_t castMs;  // its cast time
    uint32_t channel; // the channel under way
    bool operator==(const Cast &o) const {
        return spell == o.spell && castMs == o.castMs && channel == o.channel;
    }
};

// A unit's UNIT_STATE fields. Plain data, so two compare with memcmp.
struct State {
    uint64_t target;
    uint64_t owner;
    uint32_t flags;
    uint32_t level;
    uint32_t faction;
    uint32_t pad; // keeps the struct free of padding bytes for memcmp
};

// A unit's UNIT_POWER fields.
struct Vitals {
    uint32_t health;
    uint32_t maxHealth;
    uint32_t powerType;
    uint32_t power;
    uint32_t maxPower;
};

// A unit's UNIT_AURAS: each aura slot's spell (0 for an empty one), whether
// it is harmful, and its stacks.
struct Auras {
    uint32_t spell[Offsets::UNIT_AURA_TOTAL];
    uint8_t harmful[Offsets::UNIT_AURA_TOTAL];
    uint8_t stacks[Offsets::UNIT_AURA_TOTAL];
};

struct Seen {
    Look look;
    Dress dress;
    Cast cast;
    State state;
    Vitals vitals;
    Auras auras;
    std::string name;
    uint32_t frame; // the last walk that found it
};

std::unordered_map<uint64_t, Seen> g_seen;
// The cast time of each unit's last SMSG_SPELL_START: its spell and time.
std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> g_castTimes;
uint32_t g_frame = 0;
uint32_t g_generation = 0; // the movement file's, when g_seen was last emptied

Look ReadLook(void *unit, const uint8_t *fields) {
    Look look{};
    look.display = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_DISPLAYID);
    look.nativeDisplay = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_NATIVEDISPLAYID);
    look.mountDisplay = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MOUNTDISPLAYID);
    look.scale = Game::Read<float>(::Object::Data(unit), Offsets::OFF_OBJECT_FIELD_SCALE_X);
    look.bytes1 = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_BYTES_1);
    look.bytes2 = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_BYTES_2);
    for (int i = 0; i < kHands; ++i) {
        look.heldDisplay[i] =
            Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_VIRTUAL_ITEM_SLOT_DISPLAY + i * 4);
    }
    for (int i = 0; i < kHands * 2; ++i)
        look.heldInfo[i] = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_VIRTUAL_ITEM_INFO + i * 4);
    return look;
}

// The player's guild's emblem from the guild cache, as CGPlayer_C::ApplyTabard
// asks for it (0x005E0762..0x005E078F) but without a callback: the engine
// already asks for the guild of a player it draws in a tabard. All -1 while
// the cache has no record (or the player has no guild).
void ReadEmblem(uint64_t guid, uint32_t guild, int32_t (&emblem)[5]) {
    using GetEmblem_t = bool(__fastcall *)(uint32_t guild, const void *callback, uint32_t guidLow, uint32_t guidHigh,
                                           void *data, int32_t *style, int32_t *color, int32_t *border,
                                           int32_t *borderColor, int32_t *background);
    reinterpret_cast<GetEmblem_t>(Offsets::FUN_GET_GUILD_EMBLEM)(
        guild, nullptr, static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32), nullptr, &emblem[0],
        &emblem[1], &emblem[2], &emblem[3], &emblem[4]);
}

Dress ReadDress(uint64_t guid, void *player, const uint8_t *fields) {
    Dress dress{};
    for (int32_t &v : dress.emblem)
        v = -1;
    dress.race = fields[Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE];
    dress.classId = fields[Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE];
    dress.sex = fields[Offsets::OFF_UNIT_DESCRIPTOR_SEX_BYTE];
    const auto *info = Game::Read<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    if (info == nullptr)
        return dress;
    const uint8_t *bytes = info + Offsets::OFF_PLAYER_INFO_BYTES;
    dress.skin = bytes[0];
    dress.face = bytes[1];
    dress.hairStyle = bytes[2];
    dress.hairColor = bytes[3];
    dress.facialHair = info[Offsets::OFF_PLAYER_INFO_BYTES_2];
    dress.flags = Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_FLAGS);
    if (const uint32_t guild = Game::Read<uint32_t>(info, Offsets::OFF_PLAYER_INFO_GUILD_ID))
        ReadEmblem(guid, guild, dress.emblem);
    for (int slot = 0; slot < Model::Outfit::kInventorySlots; ++slot) {
        const uint8_t *item = Item::ID::VisibleEntry(player, slot);
        if (item == nullptr)
            continue;
        dress.worn[slot].item = Game::Read<uint32_t>(item, Offsets::OFF_VISIBLE_ITEM_ITEM_ID);
        if (dress.worn[slot].item != 0)
            dress.worn[slot].enchant = Model::Outfit::ShownEnchant(item);
    }
    return dress;
}

Cast ReadCast(uint64_t guid, void *unit, const uint8_t *fields) {
    Cast cast{};
    cast.channel = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_CHANNEL_SPELL);
    const uint32_t spell = Game::Read<uint32_t>(unit, Offsets::OFF_UNIT_CAST_SPELL);
    const auto it = g_castTimes.find(guid);
    if (spell != 0 && it != g_castTimes.end() && it->second.first == spell && it->second.second != 0) {
        cast.spell = spell;
        cast.castMs = it->second.second;
    }
    return cast;
}

void WriteCast(uint64_t guid, const Cast &cast) {
    std::string line = "UNIT_CAST";
    File::AppendGuid(line, guid);
    AppendUnsigned(line, cast.spell);
    AppendUnsigned(line, cast.castMs);
    AppendUnsigned(line, cast.channel);
    Movement::Emit(line);
}

State ReadState(void *unit, const uint8_t *fields) {
    State state{};
    state.target = Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_TARGET);
    state.owner = ::Unit::Identity::OwnerGuid(unit);
    state.flags = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);
    state.level = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_LEVEL);
    state.faction = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FACTIONTEMPLATE);
    return state;
}

void WriteState(uint64_t guid, const State &state) {
    std::string line = "UNIT_STATE";
    File::AppendGuid(line, guid);
    File::AppendGuid(line, state.target);
    File::AppendGuid(line, state.owner);
    AppendHex(line, state.flags);
    AppendUnsigned(line, state.level);
    AppendUnsigned(line, state.faction);
    Movement::Emit(line);
}

// Health, and the power of the unit's own type (UnitPowerType's byte), in
// stored units as the descriptor has them (rage in tenths).
Vitals ReadVitals(const uint8_t *fields) {
    Vitals vitals{};
    vitals.health = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH);
    vitals.maxHealth = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MAXHEALTH);
    vitals.powerType = fields[Offsets::OFF_UNIT_DESCRIPTOR_POWER_TYPE_BYTE];
    if (vitals.powerType < Offsets::UNIT_POWER_TYPE_COUNT) {
        vitals.power = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_POWER1 + vitals.powerType * 4);
        vitals.maxPower = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_MAXPOWER1 + vitals.powerType * 4);
    }
    return vitals;
}

void WriteVitals(uint64_t guid, const Vitals &vitals) {
    std::string line = "UNIT_POWER";
    File::AppendGuid(line, guid);
    for (const uint32_t v : {vitals.health, vitals.maxHealth, vitals.powerType, vitals.power, vitals.maxPower})
        AppendUnsigned(line, v);
    Movement::Emit(line);
}

// Each slot's aura, its polarity as this server encodes it
// (Aura::Data::IsSlotHarmful: the slot range, or the flag nibble on Turtle,
// which also puts debuffs in buff slots) and its stacks.
Auras ReadAuras(const uint8_t *unit) {
    Auras auras{};
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        auras.spell[slot] = ::Aura::Data::ReadSpellID(unit, slot);
        if (auras.spell[slot] == 0)
            continue;
        auras.harmful[slot] = ::Aura::Data::IsSlotHarmful(unit, slot) ? 1 : 0;
        auras.stacks[slot] = static_cast<uint8_t>(::Aura::Data::ReadStacks(unit, slot));
    }
    return auras;
}

bool Empty(const Auras &auras) {
    for (const uint32_t spell : auras.spell)
        if (spell != 0)
            return false;
    return true;
}

// The occupied slots in slot order: spell, 1 for harmful else 0, stacks.
void WriteAuras(uint64_t guid, const Auras &auras) {
    std::string line = "UNIT_AURAS";
    File::AppendGuid(line, guid);
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        if (auras.spell[slot] == 0)
            continue;
        AppendUnsigned(line, auras.spell[slot]);
        AppendUnsigned(line, auras.harmful[slot]);
        AppendUnsigned(line, auras.stacks[slot]);
    }
    Movement::Emit(line);
}

// The name the client has for the unit, by the branches of its own namer
// (CGUnit_C::GetUnitName, FUN_OBJECT_GET_NAME): a player's from the name
// cache; a unit with a pet number from the pet name cache, while the record's
// stamp matches the unit's; else the first name of the unit's creature
// record. Empty while it has none, where the namer gives a placeholder. These
// are passive peeks: the namer itself asks the server on every miss, without
// dedupe, so calling it each frame would pile up pending callbacks.
std::string ReadName(uint64_t guid, const void *unit, const uint8_t *fields) {
    const char *name = nullptr;
    if ((::Object::Type(unit) & Offsets::TYPEMASK_PLAYER) != 0) {
        name = ::Unit::Identity::NameForGuid(guid);
    } else if (const uint32_t pet = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_PETNUMBER)) {
        const uint8_t *rec = ::Cache::Record::Peek(Offsets::FUN_PET_NAME_GET_RECORD, Offsets::VAR_PET_NAME_CACHE, pet);
        if (rec != nullptr && Game::Read<uint32_t>(rec, Offsets::OFF_PET_NAME_RECORD_TIMESTAMP) ==
                                  Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_PET_NAME_TIMESTAMP))
            name = reinterpret_cast<const char *>(rec);
    } else if (const auto *rec = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_CREATURE_CACHE_ROW)) {
        name = Game::Read<const char *>(rec, Offsets::OFF_CREATURE_NAME);
    }
    return name != nullptr ? name : "";
}

void WriteName(uint64_t guid, const std::string &name) {
    std::string line = "UNIT_NAME";
    File::AppendGuid(line, guid);
    File::AppendQuoted(line, name.c_str());
    Movement::Emit(line);
}

void WriteLook(uint64_t guid, void *unit, const Look &look) {
    std::string line = "UNIT_INFO";
    File::AppendGuid(line, guid);
    float position[3] = {};
    ::Unit::Position::Read(unit, position);
    for (const float v : position)
        AppendFixed(line, v, 2);
    AppendFixed(line, ::Unit::Position::Facing(unit), 4);
    // The speeds the client moves the unit at (CMovement::GetCurrentSpeed
    // picks one by its movement flags): walk, run, run back, swim, swim back.
    // Changes after this come as UNIT_SPEED lines.
    const auto *mover = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_MOVEMENT_INFO_PTR);
    for (const uintptr_t offset :
         {Offsets::OFF_MOVEMENT_WALK_SPEED, Offsets::OFF_MOVEMENT_RUN_SPEED,
          Offsets::OFF_MOVEMENT_RUN_BACK_SPEED, Offsets::OFF_MOVEMENT_SWIM_SPEED,
          Offsets::OFF_MOVEMENT_SWIM_BACK_SPEED})
        AppendFixed(line, mover != nullptr ? Game::Read<float>(mover, offset) : 0.0f, 4);
    AppendUnsigned(line, look.display);
    AppendUnsigned(line, look.nativeDisplay);
    AppendUnsigned(line, look.mountDisplay);
    AppendFixed(line, look.scale, 4);
    AppendHex(line, look.bytes1);
    AppendHex(line, look.bytes2);
    for (const uint32_t v : look.heldDisplay)
        AppendUnsigned(line, v);
    for (const uint32_t v : look.heldInfo)
        AppendHex(line, v);
    Movement::Emit(line);
}

void WriteDress(uint64_t guid, const Dress &dress) {
    std::string line = "PLAYER_INFO";
    File::AppendGuid(line, guid);
    for (const uint32_t v : {dress.race, dress.classId, dress.sex, dress.skin, dress.face,
                             dress.hairStyle, dress.hairColor, dress.facialHair})
        AppendUnsigned(line, v);
    AppendHex(line, dress.flags);
    for (const Model::Outfit::Appearance &slot : dress.worn) {
        AppendUnsigned(line, slot.item);
        AppendUnsigned(line, slot.enchant);
    }
    for (const int32_t v : dress.emblem)
        AppendSigned(line, v);
    Movement::Emit(line);
}

// One unit the walk found.
void Visit(uint64_t guid) {
    void *unit = ::Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
    const uint8_t *fields = ::Object::Fields(unit);
    if (fields == nullptr || ::Object::Data(unit) == nullptr)
        return;
    const Look look = ReadLook(unit, fields);
    // Only a player has the player block (OFF_CGPLAYER_INFO's hazard).
    const bool player = (::Object::Type(unit) & Offsets::TYPEMASK_PLAYER) != 0;
    const Dress dress = player ? ReadDress(guid, unit, fields) : Dress{};
    const Cast cast = ReadCast(guid, unit, fields);
    const State state = ReadState(unit, fields);
    const Vitals vitals = ReadVitals(fields);
    const Auras auras = ReadAuras(static_cast<const uint8_t *>(unit));
    const std::string name = ReadName(guid, unit, fields);

    const auto [it, added] = g_seen.try_emplace(guid);
    Seen &seen = it->second;
    seen.frame = g_frame;
    if (added || std::memcmp(&seen.look, &look, sizeof look) != 0) {
        seen.look = look;
        WriteLook(guid, unit, look);
    }
    if (player && (added || std::memcmp(&seen.dress, &dress, sizeof dress) != 0)) {
        seen.dress = dress;
        WriteDress(guid, dress);
    }
    if (!name.empty() && name != seen.name) {
        seen.name = name;
        WriteName(guid, name);
    }
    if (added || std::memcmp(&seen.state, &state, sizeof state) != 0) {
        seen.state = state;
        WriteState(guid, state);
    }
    if (added || std::memcmp(&seen.vitals, &vitals, sizeof vitals) != 0) {
        seen.vitals = vitals;
        WriteVitals(guid, vitals);
    }
    if (added ? !Empty(auras) : std::memcmp(&seen.auras, &auras, sizeof auras) != 0) {
        seen.auras = auras;
        WriteAuras(guid, auras);
    }
    if (added ? !(cast == Cast{}) : !(seen.cast == cast)) {
        seen.cast = cast;
        WriteCast(guid, cast);
    }
}

void OnFrame() {
    if (!Movement::IsOpen()) {
        g_seen.clear();
        g_castTimes.clear();
        return;
    }
    // Out of world the walk finds nothing, which is not every unit leaving.
    if (!::Object::RegistryReady())
        return;
    if (Movement::Generation() != g_generation) {
        g_generation = Movement::Generation();
        g_seen.clear();
    }
    ++g_frame;
    ::Object::ForEachVisible([](uint64_t guid) {
        // Most objects are items: the GUID's type skips them without a resolve.
        if (Guid::IsPlayer(guid) || Guid::IsCreature(guid) || Guid::IsPet(guid))
            Visit(guid);
        return true;
    });
    for (auto it = g_seen.begin(); it != g_seen.end();) {
        if (it->second.frame == g_frame) {
            ++it;
            continue;
        }
        std::string line = "UNIT_GONE";
        File::AppendGuid(line, it->first);
        Movement::Emit(line);
        g_castTimes.erase(it->first);
        it = g_seen.erase(it);
    }
}

void OnSpellStart(const Spell::CastPacket::Start &s) {
    if (Movement::IsOpen())
        g_castTimes[s.header.caster] = {s.header.spellId, s.castTimeMs};
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};
const Spell::CastPacket::AutoStart _start{&OnSpellStart};

} // namespace

} // namespace CombatLog::UnitInfo
