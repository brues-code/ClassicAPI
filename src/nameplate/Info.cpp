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

// `C_NamePlate.GetNamePlates()` — returns nameplate Frame objects,
// matching modern WoW's signature. Two paths because vanilla has two
// kinds of nameplates:
//
// 1. **Addon-created** (pfUI, TidyPlates, etc.) — already registered
//    with Lua via `CreateFrame`. Their Lua-registry ref-key sits at
//    `+0x08`; we push `registry[refKey]` to return the cached
//    wrapper. Identity stable across calls.
//
// 2. **Default vanilla nameplates** — created internally without ever
//    calling `CreateFrame`, so `+0x08` is 0. We build a fresh wrapper
//    table per call: `{[0] = lightuserdata(frame)}` with the global
//    `__framescript_meta` metatable. Methods work
//    (`:GetWidth()` / `:GetAlpha()` / etc.) but the wrapper isn't
//    cached engine-side — calling `GetNamePlates()` again returns a
//    different table for the same frame. Don't compare wrappers by
//    identity, and don't cache them across the unit going out of
//    range (the underlying frame may be freed).
//
// We deliberately don't call the engine's frame-registration helper
// (`FUN_00701BD0`) for the unregistered case — it increments a Lua
// refcount on the frame that's never decremented, pinning the frame
// in memory.
//
// `C_NamePlate.GetNamePlateGUIDs()` — companion returning the GUID
// strings of the same set of units, regardless of registration
// state. Cheapest enumeration when an addon only needs the GUIDs.
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

using LuaRawGetI_t = void(__fastcall *)(void *L, int idx, int n);
using LuaPushLightUserdata_t = void(__fastcall *)(void *L, void *p);
using LuaSetMetatable_t = int(__fastcall *)(void *L, int idx);

constexpr uintptr_t kFunLuaPushLightUserdata = 0x006F3A20;
constexpr uintptr_t kFunLuaSetMetatable = 0x006F4020;
constexpr int kLuaGlobalsIndex = -10001;
constexpr const char *kFrameMetatableGlobal = "__framescript_meta";

// Build a fresh frame wrapper on the Lua stack: `{[0] = frame}` with
// `_G["__framescript_meta"]` as metatable. Same shape the engine's
// frame-registration helper builds, minus the registry-cache step
// (which we skip to avoid pinning the frame's refcount).
void PushFreshFrameWrapper(void *L, void *frame) {
    auto pushLight = reinterpret_cast<LuaPushLightUserdata_t>(
        kFunLuaPushLightUserdata);
    auto setMetatable = reinterpret_cast<LuaSetMetatable_t>(
        kFunLuaSetMetatable);

    Game::Lua::NewTable(L);
    Game::Lua::PushNumber(L, 0);
    pushLight(L, frame);
    Game::Lua::RawSet(L, -3);

    Game::Lua::PushString(L, kFrameMetatableGlobal);
    Game::Lua::GetTable(L, kLuaGlobalsIndex);
    setMetatable(L, -2);
}

// Walk visible units with allocated nameplates, invoking `emit(unit,
// nameplate, instance)` for each. Returns the number of emissions.
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

} // namespace

static int __fastcall Script_GetNamePlates(void *L) {
    Game::Lua::NewTable(L);
    auto rawgeti = reinterpret_cast<LuaRawGetI_t>(
        Offsets::FUN_FRAMESCRIPT_PUSH_OBJECT);
    int nextIndex = 1;
    ForEachNamePlatedUnit(
        [L, rawgeti, &nextIndex](const uint8_t *, const uint8_t *nameplate,
                                  const uint8_t *) {
            Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
            // The engine initializes `OFF_COBJECT_LUA_REGISTRY_REF` to
            // `LUA_NOREF` (`-2`) for internally-created frames; a real
            // refkey is always a positive integer from `luaL_ref`.
            // Treat anything `<= 0` as unregistered.
            const int refKey = *reinterpret_cast<const int *>(
                nameplate + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
            if (refKey > 0) {
                rawgeti(L, Game::Lua::REGISTRY_INDEX, refKey);
            } else {
                PushFreshFrameWrapper(L, const_cast<uint8_t *>(nameplate));
            }
            Game::Lua::SetTable(L, -3);
        });
    return 1;
}

static int __fastcall Script_GetNamePlateGUIDs(void *L) {
    Game::Lua::NewTable(L);
    int nextIndex = 1;
    ForEachNamePlatedUnit(
        [L, &nextIndex](const uint8_t *, const uint8_t *,
                         const uint8_t *instance) {
            const uint64_t guid = *reinterpret_cast<const uint64_t *>(instance);
            if (guid == 0)
                return;
            char buf[Guid::STRING_SIZE];
            Game::Lua::PushNumber(L, static_cast<double>(nextIndex++));
            Game::Lua::PushString(L,
                Guid::FormatAsString(guid, buf, sizeof buf));
            Game::Lua::SetTable(L, -3);
        });
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlates",
                                     &Script_GetNamePlates);
    Game::Lua::RegisterTableFunction("C_NamePlate", "GetNamePlateGUIDs",
                                     &Script_GetNamePlateGUIDs);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace NamePlate::Info
