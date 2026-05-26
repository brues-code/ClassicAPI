// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>

// Shared nameplate enumeration — walks the local-player-anchored
// object hash table for visible `TYPEMASK_UNIT` entries and invokes
// `emit(unit, nameplate, instance)` for each unit that currently has
// an allocated nameplate (`unit + 0xE60` non-null).
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
// the modern Lua token form). `0` means no nameplate at that index.
// Backed by the per-tick UNIT_ADDED / UNIT_REMOVED diff in
// `Events.cpp` — order is creation-order (append on ADDED, erase on
// REMOVED), stable within a frame.
uint64_t GetGUIDByIndex(int oneBased);

// Clears the per-tick diff state (seen pointers, last tick's
// snapshot, ordered GUID list) so the post-`/reload` tick refires
// CREATED + UNIT_ADDED for every currently-visible plate. Called
// from `FrameScript_Initialize_h` alongside the wrapper-cache reset
// in `NamePlate::Info::PrepareForReload`.
void PrepareForReload();

} // namespace NamePlate::Events

namespace NamePlate::Walk {

// `0x00B41414` is a player-related global that overlays multiple
// sub-objects at different offsets. We read the object-hash-table
// fields at `+0x1C` (bucket array) and `+0x24` (bucket mask) — that
// sub-struct is safe to walk for any unit the engine knows about.
// **Do not feed this pointer to inventory routines** (`GetItemBySlot`
// etc.); the CGPlayer_C inventory manager lives at a different
// offset (`+0x1D38`) and is sourced via `ResolveUnitToken("player")`,
// not via this global. See CLAUDE.md "Resolving an item-location"
// for the inventory-crash story.
constexpr uintptr_t kLocalPlayerGlobal = 0x00B41414;
constexpr int kOffPlayerBucketArray = 0x1C;
constexpr int kOffPlayerBucketMask = 0x24;
constexpr int kBucketStride = 12;
constexpr int kBucketLinkOffsetField = 0;
constexpr int kBucketChainHeadField = 8;
constexpr int kOffEntryInstanceBlock = 0x08;
constexpr int kOffInstanceTypeMask = 0x08;
constexpr uint32_t kTypeMaskUnit = 0x08;
constexpr int kOffUnitNamePlate = 0xE60;

template <typename F>
int ForEachNamePlatedUnit(F &&emit) {
    auto *player = *reinterpret_cast<uint8_t *const *>(kLocalPlayerGlobal);
    if (player == nullptr)
        return 0;

    auto *buckets = *reinterpret_cast<uint8_t *const *>(
        player + kOffPlayerBucketArray);
    const uint32_t mask = *reinterpret_cast<const uint32_t *>(
        player + kOffPlayerBucketMask);
    if (buckets == nullptr || mask == 0xFFFFFFFFu)
        return 0;

    int count = 0;
    for (uint32_t b = 0; b <= mask; ++b) {
        const uint8_t *bucket = buckets + b * kBucketStride;
        const uint32_t linkOffset = *reinterpret_cast<const uint32_t *>(
            bucket + kBucketLinkOffsetField);
        uintptr_t entry = *reinterpret_cast<const uintptr_t *>(
            bucket + kBucketChainHeadField);

        while (entry != 0 && (entry & 1) == 0) {
            auto *obj = reinterpret_cast<const uint8_t *>(entry);
            auto *instance = *reinterpret_cast<const uint8_t *const *>(
                obj + kOffEntryInstanceBlock);
            if (instance != nullptr) {
                const uint32_t typeMask = *reinterpret_cast<const uint32_t *>(
                    instance + kOffInstanceTypeMask);
                if ((typeMask & kTypeMaskUnit) != 0) {
                    const auto *nameplate = *reinterpret_cast<const uint8_t *const *>(
                        obj + kOffUnitNamePlate);
                    if (nameplate != nullptr) {
                        emit(obj, nameplate, instance);
                        ++count;
                    }
                }
            }
            entry = *reinterpret_cast<const uintptr_t *>(
                obj + linkOffset + 4);
        }
    }
    return count;
}

} // namespace NamePlate::Walk
