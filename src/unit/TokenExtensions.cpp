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
// - `mark1`..`mark8` — raid-target markers (star … skull), resolved
//   from the engine's marker table via `Unit::RaidTarget`.
//
// - `totem1`..`totem4` — the player's totem creature in each totem slot
//   (Fire, Earth, Water, Air), backed by `Totem::Tracker`.
//
// - `softinteract` / `anyinteract` / `anyleftinteract` — the interact
//   key's object, backed by `Target::SoftInteract`. Any can be a game object
//   or a corpse, so only GUID readers (UnitGUID) see those; unit readers
//   resolve nil. `anyleftinteract` is retail's alias of `anyinteract`.
//
// - `softenemy` / `softfriend` / `anyenemy` / `anyfriend` — the soft
//   enemy and soft friend, backed by `Target::SoftUnit`.
//
// - `0x<hex>` — a raw 64-bit GUID literal (SuperWoW's format:
//   `"0x"` + up to 16 hex digits). Lets every `Unit*` accept a GUID
//   in place of a token, which is SuperWoW's headline "hard
//   dependency" feature. Only enabled when SuperWoW is NOT loaded —
//   when it is, its own resolver hook owns GUID input and we defer to
//   avoid double-handling. See `GuidTokenEnabled` below.
//
// - A name: yours, a group member's, or one of your pets' (`Feral`,
//   `Feral-target`, `Feral-pet`), as 4.3.4's resolver takes them. See
//   "Names" below and `unit/TokenNames.cpp`.
//
// Every family composes with the engine's own `target`-suffix walker
// (`focustarget`, `nameplate1targettarget`) via the shared
// `WalkSuffix` helper, which mirrors the engine's
// `LAB_005159d3..0x00515A2C` walker instruction-for-instruction
// (SStrCmpI "target" + ObjectByGUID + read UNIT_FIELD_TARGET).
//
// Resolution order in the hook: raw GUID, focus, the soft targets,
// nameplateN (requires a digit), markN (one digit 1..8), totemN (one
// digit 1..4), then a name. Each branch short-circuits; on no match
// the original engine resolver runs unchanged.

#include "Game.h"
#include "Offsets.h"
#include "nameplate/Walk.h"
#include "object/Resolve.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "totem/Tracker.h"
#include "unit/Focus.h"
#include "unit/RaidTarget.h"
#include "unit/Identity.h"
#include "unit/TokenExtensions.h"
#include "unit/TokenNames.h"

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

constexpr const char kNamePlatePrefix[] = "nameplate";
constexpr int kNamePlatePrefixLen = static_cast<int>(sizeof(kNamePlatePrefix) - 1);
constexpr const char kFocusPrefix[] = "focus";
constexpr int kFocusPrefixLen = static_cast<int>(sizeof(kFocusPrefix) - 1);
constexpr const char kSoftInteractPrefix[] = "softinteract";
constexpr int kSoftInteractPrefixLen = static_cast<int>(sizeof(kSoftInteractPrefix) - 1);
constexpr const char kAnyInteractPrefix[] = "anyinteract";
constexpr int kAnyInteractPrefixLen = static_cast<int>(sizeof(kAnyInteractPrefix) - 1);
constexpr const char kAnyLeftInteractPrefix[] = "anyleftinteract";
constexpr int kAnyLeftInteractPrefixLen = static_cast<int>(sizeof(kAnyLeftInteractPrefix) - 1);
constexpr const char kSoftEnemyPrefix[] = "softenemy";
constexpr int kSoftEnemyPrefixLen = static_cast<int>(sizeof(kSoftEnemyPrefix) - 1);
constexpr const char kSoftFriendPrefix[] = "softfriend";
constexpr int kSoftFriendPrefixLen = static_cast<int>(sizeof(kSoftFriendPrefix) - 1);
constexpr const char kAnyEnemyPrefix[] = "anyenemy";
constexpr int kAnyEnemyPrefixLen = static_cast<int>(sizeof(kAnyEnemyPrefix) - 1);
constexpr const char kAnyFriendPrefix[] = "anyfriend";
constexpr int kAnyFriendPrefixLen = static_cast<int>(sizeof(kAnyFriendPrefix) - 1);
constexpr const char kMarkPrefix[] = "mark";
constexpr int kMarkPrefixLen = static_cast<int>(sizeof(kMarkPrefix) - 1);
constexpr const char kTotemPrefix[] = "totem";
constexpr int kTotemPrefixLen = static_cast<int>(sizeof(kTotemPrefix) - 1);
constexpr const char kSuffixTarget[] = "target";
constexpr int kSuffixTargetLen = static_cast<int>(sizeof(kSuffixTarget) - 1);

// Engine's own priority value for the suffix walker's ObjectByGUID
// calls (verified at `0x005159FE: PUSH 0x6E`). Different from the
// `0x172` we use elsewhere — matching the engine here so the
// resolver's lookup path is identical to the native `targettarget`
// walker.
constexpr int kResolvePriority = 0x6e;

TokenToGUID_t Original_o = nullptr;

// One `target` hop: the unit's UNIT_FIELD_TARGET, 0 for no unit.
uint64_t TargetOf(uint64_t guid) {
    if (guid == 0)
        return 0;
    auto *obj = static_cast<uint8_t *>(
        Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "ClassicAPI", kResolvePriority));
    if (obj == nullptr)
        return 0;
    // `m_objectFields` is a *pointer* stored at `obj + 0x110`,
    // not an inline array. Engine's walker does
    // `mov eax, [obj+0x110]; mov edi, [eax+0x28]` — two
    // dereferences. Earlier version of this code missed the
    // first one and read instruction bytes interpreted as GUIDs.
    auto *fields = Object::Fields(obj);
    if (fields == nullptr)
        return 0;
    return *reinterpret_cast<const uint64_t *>(fields + Offsets::OFF_UNIT_FIELD_TARGET);
}

// Whether `suffix` is nothing but `target` hops, so a prefix match is a whole
// token. A player named "Focusfire" is not `focus` + a bad suffix but a name,
// as 4.3.4 reads it: there a prefix whose rest does not parse falls through to
// the name compare.
bool IsTargetChain(const char *suffix) {
    for (; *suffix != '\0'; suffix += kSuffixTargetLen)
        if (Game::StrCmpI(suffix, kSuffixTarget, kSuffixTargetLen) != 0)
            return false;
    return true;
}

uint64_t WalkSuffix(uint64_t guid, const char *suffix) {
    while (guid != 0 && *suffix != '\0') {
        if (Game::StrCmpI(suffix, kSuffixTarget, kSuffixTargetLen) != 0)
            return 0; // unknown suffix component — modern returns nil
        suffix += kSuffixTargetLen;
        guid = TargetOf(guid);
    }
    return guid;
}

// --- Names ------------------------------------------------------------------
//
// 4.3.4's resolver takes the name of you, a group member, or one of your pets
// wherever it takes a token (Script_GetGUIDFromToken, 0x0044F430, falling back
// to Script_GetGUIDFromString, 0x0044EE40), so `UnitHealth("Feral")` and
// `CastSpellByName(spell, "Feral")` work, and a macro's `[@Feral]` needs no
// translation. Unit::TokenNames keeps the names; this is the parse around
// them.

constexpr const char kHopPet[] = "pet";
constexpr int kHopPetLen = static_cast<int>(sizeof(kHopPet) - 1);

// The name part of `text`, as Script_GetGUIDFromString copies it: up to the
// end, or to a '-' that starts a `-target` or `-pet` hop. Any other '-' is a
// realm separator and stays in the name. -1 for text with a digit, which no
// name and no hop has: `party1`, `raid12` and `0x…` skip the list at once.
int NameLength(const char *text) {
    int n = 0;
    for (; text[n] != '\0'; ++n) {
        const char c = text[n];
        if (c >= '0' && c <= '9')
            return -1;
        if (c == '-' && (Game::StrCmpI(text + n, "-target") == 0 ||
                         Game::StrCmpI(text + n, "-pet") == 0 ||
                         Game::StrCmpI(text + n, "-target-", 8) == 0 ||
                         Game::StrCmpI(text + n, "-pet-", 5) == 0))
            break;
    }
    return n;
}

// The hops after a name, as 4.3.4's ParseTrailingTokens (0x0044F2E0) walks
// them: each an optional '-' and then `target`, or `pet` off a unit that has a
// pet token (you or a group member). 0 for a hop that does not parse or a
// unit that is not there.
uint64_t WalkHops(uint64_t guid, const char *hops) {
    while (*hops != '\0') {
        if (*hops == '-')
            ++hops;
        if (Game::StrCmpI(hops, kSuffixTarget, kSuffixTargetLen) == 0) {
            hops += kSuffixTargetLen;
            guid = TargetOf(guid);
        } else if (Game::StrCmpI(hops, kHopPet, kHopPetLen) == 0) {
            hops += kHopPetLen;
            const char *petToken = Unit::TokenNames::PetTokenFor(guid, Original_o);
            if (petToken == nullptr)
                return 0;
            guid = Original_o(petToken);
        } else {
            return 0;
        }
    }
    return guid;
}

// --- Unknown strings --------------------------------------------------------
//
// The 1.12 resolver raises "Unknown unit name: %s" for a string it does not
// know. 4.3.4's does not (Script_GetGUIDFromToken returns false and the unit
// functions answer nil), and neither does retail: `UnitName("Notbruestarget")`
// is an empty result there. So the engine is asked under protection, and a
// raise becomes "no unit". Other DLLs depend on this too: nampower's resolver
// detour (unit_scripts.cpp GetGUIDFromNameHook) answers a 0 for a string
// ending in `target` / `pet` / `owner` by resolving what is left without it,
// so `Feral-target` with no target comes back as a lookup of "Feral-".

// Whether the 1.12 resolver answers `token` without raising, so the call needs
// no protection. It raises only for a string whose start matches none of its
// prefixes (benilla-re ui/unittokens: player, pet, target, partypet, party,
// raidpet, raid, mouseover, and npc compared whole); a GUID literal is taken by
// Turtle's stub at 0xD06670, by SuperWoW, or by the branch in Hook_h. This
// only decides the cost of the call: what any string means is still the
// engine's answer, and a string not listed here is just asked under
// protection.
bool EngineNeverRaises(const char *token) {
    struct Prefix {
        const char *text;
        int len;
    };
    static const Prefix kPrefixes[] = {
        {"player", 6}, {"pet", 3}, {"target", 6}, {"party", 5}, {"raid", 4}, {"mouseover", 9},
    };
    for (const Prefix &prefix : kPrefixes)
        if (Game::StrCmpI(token, prefix.text, prefix.len) == 0)
            return true;
    return Game::StrCmpI(token, "npc") == 0 ||
           (token[0] == '0' && (token[1] == 'x' || token[1] == 'X'));
}

uint64_t g_probeGuid = 0;

int __fastcall ProbeThunk(void *L) {
    const char *token = Game::Lua::ToString(L, 1);
    g_probeGuid = token != nullptr ? Original_o(token) : 0;
    return 0;
}

// The engine's answer for `token`, 0 with `*raised` set when it raised.
// Without a Lua state there is nothing to catch with, so the call is made as
// the engine would make it.
uint64_t ProtectedEngine(const char *token, bool *raised) {
    *raised = false;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return Original_o(token);
    const int base = Game::Lua::GetTop(L);
    Game::Lua::PushCClosure(L, &ProbeThunk, 0);
    Game::Lua::PushString(L, token);
    g_probeGuid = 0;
    const int status = Game::Lua::PCall(L, /*nargs*/ 1, /*nresults*/ 0, /*errfunc*/ 0);
    Game::Lua::SetTop(L, base);
    *raised = status != 0;
    return *raised ? 0 : g_probeGuid;
}

// The string IsKnown is asking about, and whether the engine raised for it.
// Keyed by pointer: a DLL outside this hook that retries with a shortened copy
// (nampower) asks about a buffer of its own, and that answer is not this one.
const char *g_watched = nullptr;
bool g_watchedUnknown = false;

uint64_t EngineAnswer(const char *token) {
    if (EngineNeverRaises(token))
        return Original_o(token);
    bool raised;
    const uint64_t guid = ProtectedEngine(token, &raised);
    if (raised && token == g_watched)
        g_watchedUnknown = true;
    return guid;
}

// `Name`, `Name-target`, `Name-pet-target`, … False when the name part names
// nobody in the group, and the string goes on to the engine.
bool ResolveName(const char *token, uint64_t *guid) {
    const int len = NameLength(token);
    Unit::TokenNames::Match match;
    if (len <= 0 || !Unit::TokenNames::Find(token, len, Original_o, &match))
        return false;
    // A token comes first, as in 4.3.4, so a member named "Target" does not
    // shadow your target. The engine's grammar is not re-listed here (other
    // DLLs extend it), so "is a token" is read as "resolves to a unit": a
    // prefix with a bad remainder (`Petra` = `pet` + "ra") quietly gives 0
    // there and stays a name. The one difference from 4.3.4: a member whose
    // whole name is also a token naming nobody right now ("Pet" with no pet
    // out) resolves to the member.
    bool raised;
    if (const uint64_t engine = ProtectedEngine(token, &raised)) {
        *guid = engine;
        return true;
    }
    *guid = WalkHops(match.guid, token + len);
    return true;
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

    // A letter-only prefix of ours whose rest is no `target` chain: a name
    // gets the first look, else it stays the quiet nil it always was.
    bool prefixOnly = false;

    // `focus` / `focustarget*`. The 5-char prefix is unique among
    // unit tokens (no `focuscast` / `focused*` etc. live in vanilla
    // or modern resolvers).
    if (Game::StrCmpI(token, kFocusPrefix, kFocusPrefixLen) == 0) {
        if (IsTargetChain(token + kFocusPrefixLen)) {
            const uint64_t guid = Unit::Focus::Get();
            if (guid == 0)
                return 0;
            return WalkSuffix(guid, token + kFocusPrefixLen);
        }
        prefixOnly = true;
    }

    // The soft targets: `softinteract*` / `anyinteract*` /
    // `anyleftinteract*`, `softenemy*` / `anyenemy*`, `softfriend*` /
    // `anyfriend*`. None of these prefixes starts any engine token, so a
    // match is ours.
    using Target::SoftUnit::Side;
    struct Soft {
        const char *prefix;
        int len;
        uint64_t (*guid)();
    };
    static const Soft kSoft[] = {
        {kSoftInteractPrefix, kSoftInteractPrefixLen, &Target::SoftInteract::Get},
        {kAnyInteractPrefix, kAnyInteractPrefixLen, &Target::SoftInteract::GetAny},
        {kAnyLeftInteractPrefix, kAnyLeftInteractPrefixLen, &Target::SoftInteract::GetAny},
        {kSoftEnemyPrefix, kSoftEnemyPrefixLen, [] { return Target::SoftUnit::Get(Side::Enemy); }},
        {kAnyEnemyPrefix, kAnyEnemyPrefixLen, [] { return Target::SoftUnit::GetAny(Side::Enemy); }},
        {kSoftFriendPrefix, kSoftFriendPrefixLen,
         [] { return Target::SoftUnit::Get(Side::Friend); }},
        {kAnyFriendPrefix, kAnyFriendPrefixLen,
         [] { return Target::SoftUnit::GetAny(Side::Friend); }},
    };
    for (const Soft &soft : kSoft) {
        if (Game::StrCmpI(token, soft.prefix, soft.len) == 0) {
            if (!IsTargetChain(token + soft.len)) {
                prefixOnly = true;
                break;
            }
            const uint64_t guid = soft.guid();
            return guid == 0 ? 0 : WalkSuffix(guid, token + soft.len);
        }
    }

    // `nameplateN` / `nameplate1target*`. Requires a digit after the
    // prefix; anything else (`nameplate`, `nameplateX`) falls through
    // to the engine, which does not know it either.
    if (Game::StrCmpI(token, kNamePlatePrefix, kNamePlatePrefixLen) == 0) {
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

    // `markN` / `mark1target*`. Raid-target markers, one digit `1`..`8`
    // after the prefix (`mark1` = star … `mark8` = skull). Anything else
    // (`mark`, `mark0`, `mark9`) falls through to the engine. Single
    // digit only, so `mark1target` leaves `p` at "target" for the walker.
    if (Game::StrCmpI(token, kMarkPrefix, kMarkPrefixLen) == 0) {
        const char *p = token + kMarkPrefixLen;
        if (*p >= '1' && *p <= '8') {
            const int mark = *p - '0';
            ++p;
            const uint64_t guid = Unit::RaidTarget::GetGUIDByMark(mark);
            if (guid == 0)
                return 0;
            return WalkSuffix(guid, p);
        }
    }

    // `totemN` / `totem1target*`. One digit `1`..`4` after the prefix
    // (Fire, Earth, Water, Air — GetTotemInfo's slots). Anything else
    // (`totem`, `totem0`, `totem5`) falls through to the engine.
    if (Game::StrCmpI(token, kTotemPrefix, kTotemPrefixLen) == 0) {
        const char *p = token + kTotemPrefixLen;
        if (*p >= '1' && *p < '1' + Totem::Tracker::kSlotCount) {
            const int slot = *p - '0';
            ++p;
            const uint64_t guid = Totem::Tracker::GetGUIDBySlot(slot);
            if (guid == 0)
                return 0;
            return WalkSuffix(guid, p);
        }
    }

    // A name of you, your group, or a pet of yours (see ResolveName).
    if (uint64_t guid; ResolveName(token, &guid))
        return guid;
    if (prefixOnly)
        return 0;

    return EngineAnswer(token);
}

} // namespace

bool IsKnown(const char *token) {
    if (token == nullptr || *token == '\0' || Game::Lua::State() == nullptr)
        return false;
    // Asked on a copy: SuperWoW's detour cuts an `owner` suffix off by writing
    // a NUL into the string it is given. No token or name runs this long.
    char copy[64];
    int len = 0;
    for (; token[len] != '\0'; ++len) {
        if (len == sizeof copy - 1)
            return false;
        copy[len] = token[len];
    }
    copy[len] = '\0';
    // Nested asks (a resolve inside a resolve) keep the outer one's watch.
    const char *outer = g_watched;
    const bool outerUnknown = g_watchedUnknown;
    g_watched = copy;
    g_watchedUnknown = false;
    Unit::Identity::GuidForToken(copy); // the whole chain, as every caller meets it
    const bool known = !g_watchedUnknown;
    g_watched = outer;
    g_watchedUnknown = outerUnknown;
    return known;
}

static const Game::HookAutoRegister _hook{
    Offsets::FUN_TOKEN_TO_GUID,
    reinterpret_cast<void *>(&Hook_h),
    reinterpret_cast<void **>(&Original_o)};

} // namespace Unit::TokenExtensions
