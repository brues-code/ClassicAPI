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

// Extend `FUN_TOKEN_TO_GUID` (`0x00515970`) with modern WoW's
// `nameplateN` unit-token family. Hooking the central token→GUID
// resolver means every `Script_Unit*` (UnitName, UnitGUID, UnitHealth,
// UnitClass, UnitExists, …) gains nameplate-token support for free,
// since they all funnel here.
//
// Resolution:
//
// 1. SStrCmpI the input against `"nameplate"` (9 chars). Non-match →
//    fall through to the original resolver and behave exactly as
//    before (engine handles `player` / `target` / `partyN` / etc.,
//    or raises "Unknown unit name").
// 2. Parse trailing digits — `nameplate1`, `nameplate12`, ... — into
//    a 1-based index `N`.
// 3. Look up `N` in `NamePlate::Events`' ordered GUID list. Out of
//    range returns 0 quietly, which the engine treats as "no unit"
//    and Lua callers see as `nil` — matches modern behavior, no
//    "Unknown unit name" error.
// 4. If the token continues past the digits (`nameplate1target`,
//    `nameplate1targettarget`, …) mirror the engine's own suffix
//    walker at `LAB_005159d3`: each `"target"` chunk advances the
//    GUID by reading `m_objectFields + OFF_UNIT_FIELD_TARGET`
//    (UNIT_FIELD_TARGET) via `ObjectByGUID`.
//
// The engine's walker is **inside** the function we hook, so a hook
// that returns early at the prefix-parse step would skip it
// entirely. Replicating the walker here keeps `nameplate1target`
// working without rewriting the resolver. The walker's logic is
// short (one `SStrCmpI` + one `ObjectByGUID` + one field-read per
// iteration) and matches the engine instruction-for-instruction —
// verified against the disassembly of `0x005159D3..0x00515A2C`.

#include "Game.h"
#include "Offsets.h"
#include "nameplate/Walk.h"

#include <cstdint>

namespace NamePlate::TokenResolver {

namespace {

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);
// `__stdcall`, not `__cdecl` — `FUN_00064A4C0` ends with `ret 0xc`,
// callee cleans 3-arg stack frame. Calling it through a cdecl typedef
// makes MSVC emit `add esp, 12` after the call on top of the callee's
// own cleanup, drifting ESP +12 per call and corrupting the caller's
// frame.
using SStrCmpI_t = int(__stdcall *)(const char *a, const char *b, int n);
using ResolveByGUID_t = void *(__fastcall *)(int type, const char *debugName,
                                              uint32_t guidLo, uint32_t guidHi,
                                              int priority);

constexpr const char kPrefix[] = "nameplate";
constexpr int kPrefixLen = static_cast<int>(sizeof(kPrefix) - 1);
constexpr const char kSuffixTarget[] = "target";
constexpr int kSuffixTargetLen = static_cast<int>(sizeof(kSuffixTarget) - 1);

// Engine's own priority value for the suffix walker's ObjectByGUID
// calls (verified at `0x005159FE: PUSH 0x6E`). Different from the
// `0x172` we use elsewhere — matching the engine here so the
// resolver's lookup path is identical to the native `targettarget`
// walker.
constexpr int kResolvePriority = 0x6e;

TokenToGUID_t Original_o = nullptr;

uint64_t WalkSuffix(uint64_t guid, const char *suffix) {
    auto resolve = reinterpret_cast<ResolveByGUID_t>(
        Offsets::FUN_OBJECT_RESOLVE_BY_GUID);
    auto sstrcmpi = reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I);
    while (guid != 0 && *suffix != '\0') {
        if (sstrcmpi(suffix, kSuffixTarget, kSuffixTargetLen) != 0)
            return 0; // unknown suffix component — modern returns nil
        suffix += kSuffixTargetLen;
        auto *obj = static_cast<uint8_t *>(
            resolve(Offsets::OBJ_TYPE_UNIT, "nameplate",
                    static_cast<uint32_t>(guid),
                    static_cast<uint32_t>(guid >> 32),
                    kResolvePriority));
        if (obj == nullptr)
            return 0;
        // `m_objectFields` is a *pointer* stored at `obj + 0x110`,
        // not an inline array. Engine's walker does
        // `mov eax, [obj+0x110]; mov edi, [eax+0x28]` — two
        // dereferences. Earlier version of this code missed the
        // first one and read instruction bytes interpreted as GUIDs.
        auto *fields = *reinterpret_cast<const uint8_t *const *>(
            obj + Offsets::OFF_CGUNIT_OBJECT_FIELDS);
        if (fields == nullptr)
            return 0;
        guid = *reinterpret_cast<const uint64_t *>(
            fields + Offsets::OFF_UNIT_FIELD_TARGET);
    }
    return guid;
}

uint64_t __fastcall Hook_h(const char *token) {
    if (token == nullptr || *token == '\0')
        return Original_o(token);

    auto sstrcmpi = reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I);
    if (sstrcmpi(token, kPrefix, kPrefixLen) != 0)
        return Original_o(token);

    // Must have at least one digit after `"nameplate"`; otherwise
    // it's `"nameplate"` (no number) or `"nameplateX"` — fall
    // through to the original so the engine raises its
    // "Unknown unit name" error consistently.
    const char *p = token + kPrefixLen;
    if (*p < '0' || *p > '9')
        return Original_o(token);

    int index = 0;
    while (*p >= '0' && *p <= '9') {
        index = index * 10 + (*p - '0');
        ++p;
    }

    const uint64_t guid = NamePlate::Events::GetGUIDByIndex(index);
    if (guid == 0)
        return 0; // OOB / no plate at index — modern returns nil
    return WalkSuffix(guid, p);
}

} // namespace

static const Game::HookAutoRegister _hook{
    Offsets::FUN_TOKEN_TO_GUID,
    reinterpret_cast<void *>(&Hook_h),
    reinterpret_cast<void **>(&Original_o)};

} // namespace NamePlate::TokenResolver
