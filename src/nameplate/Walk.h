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

#include "Offsets.h"
#include "object/Resolve.h"

#include <cstdint>

// Shared nameplate enumeration — walks the engine's list of shown
// nameplates and invokes `emit(unit, nameplate, guid)` for each.
//
// Header-only template — used by both the Lua-facing accessors in
// `nameplate/Info.cpp` and the per-tick differ in `nameplate/Events.cpp`.

namespace NamePlate::Info {

// Pushes the nameplate Frame at `nameplate` onto the Lua stack.
// Returns the engine's canonical wrapper table — the same one
// `rawgeti(REGISTRY, nameplate + 0x08)` resolves to anywhere else
// in the engine — so addon-set fields (pfUI: `plate.nameplate`)
// survive every roundtrip through our API. Defined in `Info.cpp`.
void PushNamePlateFrame(void *L, void *nameplate);

} // namespace NamePlate::Info

namespace NamePlate::Events {

// Returns the GUID currently bound to `nameplateN` (1-based, matching
// the modern Lua token form). `0` means the slot is out of range OR
// currently free. Backed by the retail-exact slot assignment in
// `Events.cpp`: a plate keeps its slot for its lifetime and freed slots
// are reused, so the slot array is SPARSE (gaps read back as 0).
uint64_t GetGUIDByIndex(int oneBased);

// Highest slot index in use (1-based). Callers that iterate every plate
// must loop `1..GetSlotCount()` and skip indices where `GetGUIDByIndex`
// returns 0 — they must NOT stop at the first gap, since the array is
// sparse.
int GetSlotCount();

// The nameplate Frame `guid` is losing, valid ONLY while that GUID's
// `NAME_PLATE_UNIT_REMOVED` handler is running; `nullptr` at any other time
// or for any other GUID.
//
// A plate is reported removed precisely because the unit→plate binding
// (`unit + 0xE60`) is already gone, so `GetNamePlateForUnit` /
// `GetNamePlateForGUID` cannot derive it from live state during the event.
// Retail resolves it there — `NamePlateDriverMixin:OnNamePlateRemoved` calls
// `C_NamePlate.GetNamePlateForUnit(unitToken, issecure())` — so the getters in
// `Info.cpp` consult this as their last fallback. Returns `nullptr` when the
// engine has already recycled the frame onto another unit, since serving it
// then would hand the handler somebody else's plate.
const void *PlateBeingRemoved(uint64_t guid);

// Clears the per-tick diff state (seen pointers, last tick's
// snapshot, ordered GUID list) so the post-`/reload` tick refires
// CREATED + UNIT_ADDED for every currently-visible plate. Called
// from `FrameScript_Initialize_h` alongside the wrapper-cache reset
// in `NamePlate::Info::PrepareForReload`.
void PrepareForReload();

} // namespace NamePlate::Events

namespace NamePlate::Walk {

// Calls `emit(unit, nameplate, guid)` for every shown nameplate, in the
// engine's own list order (farthest from the screen centre first). Returns
// the count.
//
// Walks the engine's shown-plate list (VAR_NAMEPLATE_LIST_HEAD) — a dozen
// or so entries — and resolves each plate's unit by the GUID the plate
// carries, exactly as the engine's NamePlateClearAll (FUN_0060F890) does. A
// plate counts only while that unit still points back at it
// (unit+0xE60 == plate), the binding the engine itself reads. The list is
// re-sorted as plates move, but only by plain engine code on the main
// thread, so it is consistent whenever a WorldTick handler or a Lua call
// reads it.
template <typename F>
int ForEachNamePlatedUnit(F &&emit) {
    // A corrupt link must not spin forever; the engine shows a few dozen
    // plates at most.
    constexpr int kMaxPlates = 1024;
    uintptr_t entry = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_NAMEPLATE_LIST_HEAD));
    int count = 0;
    for (int guard = 0; entry != 0 && (entry & 1) == 0 && guard < kMaxPlates; ++guard) {
        const auto *plate = reinterpret_cast<const uint8_t *>(entry);
        entry = *reinterpret_cast<const uintptr_t *>(plate + Offsets::OFF_NAMEPLATE_LINK_NEXT);
        const uint64_t guid =
            *reinterpret_cast<const uint64_t *>(plate + Offsets::OFF_NAMEPLATE_GUID);
        if (guid == 0)
            continue;
        const auto *unit = static_cast<const uint8_t *>(
            Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0));
        if (unit == nullptr ||
            *reinterpret_cast<const uint8_t *const *>(unit + Offsets::OFF_UNIT_NAMEPLATE) !=
                plate)
            continue;
        emit(unit, plate, guid);
        ++count;
    }
    return count;
}

} // namespace NamePlate::Walk
