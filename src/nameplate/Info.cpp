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

// `C_NamePlate.GetNamePlateGUIDs()` — enumerates GUIDs of currently-
// visible units that have an allocated nameplate frame.
//
// Named differently from modern `C_NamePlate.GetNamePlates` because
// the modern call returns nameplate `Frame` objects, not GUIDs. This
// backport ships only the GUID primitive; surfacing the frames would
// require additional engine hooks.
//
// Vanilla 1.12 stores each unit's nameplate pointer at `CGUnit + 0xE60`
// (verified via `FUN_006086E0`'s "ensure nameplate exists" path). The
// nameplate also caches the unit's GUID at `+0x4E8` for back-lookup.
// There's no central "active nameplates" list — the engine maintains
// per-unit pointers updated by per-unit state-change handlers.
//
// To enumerate, we walk the local-player-anchored object hash table
// (`player + 0x1C` = bucket array, `player + 0x24` = mask). Each
// bucket header stores the link-field offset at byte 0 and the
// chain-head pointer at byte 8 — Storm's intrusive-hash pattern.
// Filter by `TYPEMASK_UNIT` (`flags & 0x08` at `*(entry+8) + 8`) and
// check `+0xE60` for a non-null nameplate pointer.

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"

#include <cstdint>

namespace NamePlate::Info {

namespace {

constexpr uintptr_t kLocalPlayerGlobal = 0x00B41414;
constexpr int kOffPlayerBucketArray = 0x1C;
constexpr int kOffPlayerBucketMask = 0x24;
constexpr int kBucketStride = 12;
constexpr int kBucketLinkOffsetField = 0; // byte 0: link-field offset within entry
constexpr int kBucketChainHeadField = 8;  // byte 8: chain head pointer
constexpr int kOffEntryInstanceBlock = 0x08;
constexpr int kOffInstanceTypeMask = 0x08;
constexpr uint32_t kTypeMaskUnit = 0x08;
constexpr int kOffUnitNamePlate = 0xE60;

} // namespace

static int __fastcall Script_GetNamePlateGUIDs(void *L) {
    Game::Lua::NewTable(L);

    auto *player = *reinterpret_cast<uint8_t *const *>(kLocalPlayerGlobal);
    if (player == nullptr)
        return 1;

    auto *buckets = *reinterpret_cast<uint8_t *const *>(
        player + kOffPlayerBucketArray);
    const uint32_t mask = *reinterpret_cast<const uint32_t *>(
        player + kOffPlayerBucketMask);
    if (buckets == nullptr || mask == 0xFFFFFFFFu)
        return 1;

    int nextIndex = 1;
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
                    const auto *nameplate = *reinterpret_cast<const void *const *>(
                        obj + kOffUnitNamePlate);
                    if (nameplate != nullptr) {
                        const uint64_t guid = *reinterpret_cast<const uint64_t *>(
                            instance);
                        if (guid != 0) {
                            char buf[Guid::STRING_SIZE];
                            Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
                            Game::Lua::PushString(L,
                                Guid::FormatAsString(guid, buf, sizeof buf));
                            Game::Lua::SetTable(L, -3);
                        }
                    }
                }
            }

            entry = *reinterpret_cast<const uintptr_t *>(
                obj + linkOffset + 4);
        }
    }
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateGUIDs",
                                     &Script_GetNamePlateGUIDs);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace NamePlate::Info
