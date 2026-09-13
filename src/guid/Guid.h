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

#include <cstddef>
#include <cstdint>

namespace Guid {

// 1.12 GUID "high guid" prefix bits — the top 16 bits of the 64-bit
// GUID. Modern WoW switched to a string-prefix scheme
// (`"Player-Realm-..."`); vanilla packs the type directly into the
// upper bits. Values match the well-known WoW 1.12.1 / CMaNGOS
// constants.
//
// Bit layout: `(guid >> 48) & 0xFFFF` is the type prefix. The lower
// 48 bits encode the per-entity identity (with the lower bits of
// the high dword carrying creature-template-entry IDs for the
// world-object types).
enum class Type {
    Unknown = 0,    // unrecognized prefix, or GUID = 0 sentinel
    Player,         // 0x0000
    Item,           // 0x4000 (containers share this bucket)
    Creature,       // 0xF130
    Pet,            // 0xF140
    GameObject,     // 0xF110
    DynamicObject,  // 0xF100
    Corpse,         // 0xF101
};

// `(guid >> 48) & 0xFFFF` prefix constants. Exposed mainly for tests
// and tracing — runtime code should prefer `Classify` / `IsPlayer`
// etc. so the bit layout stays inside this module.
constexpr uint16_t PREFIX_PLAYER         = 0x0000;
constexpr uint16_t PREFIX_ITEM           = 0x4000;
constexpr uint16_t PREFIX_CREATURE       = 0xF130;
constexpr uint16_t PREFIX_PET            = 0xF140;
constexpr uint16_t PREFIX_GAMEOBJECT     = 0xF110;
constexpr uint16_t PREFIX_DYNAMICOBJECT  = 0xF100;
constexpr uint16_t PREFIX_CORPSE         = 0xF101;

// Parses a `"0xHHHHHHHHLLLLLLLL"` GUID string (16 hex digits after
// `0x`) — the canonical format `UnitGUID(unit)` and
// `C_Item.GetItemGUID(...)` return. Also accepts the 8-digit
// `"0xLLLLLLLL"` form (high dword implicitly 0) because some
// vanilla addons drop the high half when it's zero (i.e., for
// player GUIDs).
//
// Returns false on malformed input; leaves `*out` untouched in
// that case.
bool Parse(const char *str, uint64_t *out);

// Classifies a parsed GUID by inspecting its type prefix. Returns
// `Type::Unknown` for the `0` sentinel or any unrecognized prefix.
Type Classify(uint64_t guid);

// Convenience predicates — one per known prefix. Each handles the
// `guid == 0` sentinel correctly (returns false even though
// `0 >> 48 == 0` would otherwise match `Player`).
inline bool IsPlayer(uint64_t guid)        { return Classify(guid) == Type::Player; }
inline bool IsItem(uint64_t guid)          { return Classify(guid) == Type::Item; }
inline bool IsCreature(uint64_t guid)      { return Classify(guid) == Type::Creature; }
inline bool IsPet(uint64_t guid)           { return Classify(guid) == Type::Pet; }
inline bool IsGameObject(uint64_t guid)    { return Classify(guid) == Type::GameObject; }
inline bool IsDynamicObject(uint64_t guid) { return Classify(guid) == Type::DynamicObject; }
inline bool IsCorpse(uint64_t guid)        { return Classify(guid) == Type::Corpse; }

// The creature-template entry packed into a CREATURE GUID (bits 24-47; the
// per-spawn counter is the low 24 bits). Returns 0 for every other GUID type
// and when the field itself is 0 — the engine never assigns entry 0, so 0
// uniformly means "no creature template". Pet GUIDs (0xF140) deliberately
// return 0: on the (v)mangos family the pet's entry bits are the PET NUMBER
// (`Pet::Create` → `_Create(guidlow, pet_number, HIGHGUID_PET)`), not a
// template. This is only the OUT-OF-VIEW fallback — the server builds a
// creature GUID from its spawn row's first id and re-rolls the live template
// without touching the GUID, so prefer `Unit::CreatureID::ForGuid`, which
// reads the unit's live instance-block entry whenever the object is resolvable.
uint32_t CreatureEntry(uint64_t guid);

// Minimum buffer size for `FormatAsString`'s output — 16 hex digits +
// the `"0x"` prefix + a NUL terminator.
constexpr std::size_t STRING_SIZE = 19;

// Formats `guid` as `"0xHHHHHHHHLLLLLLLL"` (high dword then low,
// matching what `UnitGUID` and `C_Item.GetItemGUID` return). Writes
// into `buf`; `cap` must be at least `STRING_SIZE` bytes. Returns the
// buffer pointer for chaining (e.g.
// `PushString(L, Guid::FormatAsString(g, buf, sizeof buf))`).
const char *FormatAsString(uint64_t guid, char *buf, std::size_t cap);

} // namespace Guid
