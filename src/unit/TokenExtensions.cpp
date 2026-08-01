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

// Extend `FUN_TOKEN_TO_GUID` (`0x00515970`) with post-vanilla unit
// tokens. Hooking the central token→GUID resolver means every
// `Script_Unit*` (UnitName, UnitGUID, UnitHealth, UnitClass,
// UnitExists, …) gains support for the new token families for free,
// since they all funnel here.
//
// Currently extended families:
//
// - `nameplate1`..`nameplateN` — index into the ordered list of
//   visible nameplates maintained in `nameplate/Events.cpp`. See
//   `Unit tokens (nameplateN)` in docs/API.md.
//
// - `focus` — single-slot sticky target backed by `Unit::Focus`.
//   See `unit/Focus.cpp` for the storage + `PLAYER_FOCUS_CHANGED`
//   event firing.
//
// - `0x<hex>` — a raw 64-bit GUID literal (SuperWoW's format:
//   `"0x"` + up to 16 hex digits). Lets every `Unit*` accept a GUID
//   in place of a token, which is SuperWoW's headline "hard
//   dependency" feature. Only enabled when SuperWoW is NOT loaded —
//   when it is, its own resolver hook owns GUID input and we defer to
//   avoid double-handling. See `GuidTokenEnabled` below.
//
// Both families compose with the engine's own `target`-suffix walker
// (`focustarget`, `nameplate1targettarget`) via the shared
// `WalkSuffix` helper, which mirrors the engine's
// `LAB_005159d3..0x00515A2C` walker instruction-for-instruction
// (SStrCmpI "target" + ObjectByGUID + read UNIT_FIELD_TARGET).
//
// Resolution order in the hook: focus check first (literal-equal,
// short token), then nameplate prefix (longer, requires digit).
// Both branches short-circuit; on no-match the original engine
// resolver runs unchanged.

#include "Game.h"
#include "Offsets.h"
#include "nameplate/Walk.h"
#include "unit/Focus.h"

#include <cstdint>

#include <windows.h>

namespace Unit::TokenExtensions {

namespace {

// SuperWoW (loaded by VanillaFixes from `dlls.txt` as `SuperWoWhook.dll`)
// already makes the token resolver accept `"0x..."` GUID literals, so we
// only add it ourselves when SuperWoW is absent — otherwise both would
// parse the same input. VanillaFixes `LoadLibrary`s every listed DLL during
// its injection pass, long before this hook installs at game boot, so the
// module handle is present iff SuperWoW is active. Cached — module presence
// is fixed for the process lifetime.
bool GuidTokenEnabled() {
    static const bool enabled =
        GetModuleHandleA("SuperWoWhook.dll") == nullptr;
    return enabled;
}

// Hex nibble, or -1 for a non-hex character. Case-insensitive.
int HexNibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

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

constexpr const char kNamePlatePrefix[] = "nameplate";
constexpr int kNamePlatePrefixLen = static_cast<int>(sizeof(kNamePlatePrefix) - 1);
constexpr const char kFocusPrefix[] = "focus";
constexpr int kFocusPrefixLen = static_cast<int>(sizeof(kFocusPrefix) - 1);
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
            resolve(Offsets::OBJ_TYPE_UNIT, "ClassicAPI",
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

    // Raw GUID literal `0x<hex>` (SuperWoW input format). The parsed GUID
    // is returned straight to the engine's own back-half of the resolver
    // (`FUN_00468460(unit-mask, guid)`), the identical path `"player"`
    // takes — so an out-of-range / unloaded GUID resolves to nil exactly
    // like any absent unit, no special handling. Feeds the same suffix
    // walker as the other families, so `"0x…target"` composes too.
    if (GuidTokenEnabled() && token[0] == '0' &&
        (token[1] == 'x' || token[1] == 'X')) {
        const char *p = token + 2;
        uint64_t guid = 0;
        int digits = 0;
        for (int nibble; (nibble = HexNibble(*p)) >= 0; ++p) {
            guid = (guid << 4) | static_cast<uint32_t>(nibble);
            ++digits;
        }
        if (digits > 0)
            return WalkSuffix(guid, p);
    }

    auto sstrcmpi = reinterpret_cast<SStrCmpI_t>(Offsets::FUN_SSTR_CMP_I);

    // `focus` / `focustarget*`. The 5-char prefix is unique among
    // unit tokens (no `focuscast` / `focused*` etc. live in vanilla
    // or modern resolvers), so any match continues as our token.
    if (sstrcmpi(token, kFocusPrefix, kFocusPrefixLen) == 0) {
        const uint64_t guid = Unit::Focus::Get();
        if (guid == 0)
            return 0;
        return WalkSuffix(guid, token + kFocusPrefixLen);
    }

    // `nameplateN` / `nameplate1target*`. Requires a digit after the
    // prefix; anything else (`nameplate`, `nameplateX`) falls through
    // to the engine for its standard "Unknown unit name" error.
    if (sstrcmpi(token, kNamePlatePrefix, kNamePlatePrefixLen) == 0) {
        const char *p = token + kNamePlatePrefixLen;
        if (*p >= '0' && *p <= '9') {
            int index = 0;
            while (*p >= '0' && *p <= '9') {
                index = index * 10 + (*p - '0');
                ++p;
            }
            const uint64_t guid = NamePlate::Events::GetGUIDByIndex(index);
            if (guid == 0)
                return 0;
            return WalkSuffix(guid, p);
        }
    }

    return Original_o(token);
}

} // namespace

static const Game::HookAutoRegister _hook{
    Offsets::FUN_TOKEN_TO_GUID,
    reinterpret_cast<void *>(&Hook_h),
    reinterpret_cast<void **>(&Original_o)};

} // namespace Unit::TokenExtensions
