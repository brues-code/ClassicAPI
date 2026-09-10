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

// Owns the co-hook on the engine's single spell-name → spellbook-slot
// resolver (`FUN_RESOLVE_SPELL_NAME_TO_SLOT`) and normalizes the macro
// spell-name forms the engine never learned:
//
//   `!Shoot`  the "do not toggle this off" prefix — an ordinary name behind it
//   `5019`    a spellID written where a name goes
//
// Everything that resolves a spell name funnels through this one function —
// `Script_CastSpellByName`, `FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID` (so the
// engine's own macro parser and `Macro::ShowTooltip`), `Spell::Arg` — so one
// hook makes every path read the same forms, and a macro written with either
// still tags its action slot with the right spellID.
//
// Only the NAME is normalized here. Not toggling an active ability off is the
// caster's job: `Util/SlashCommandsRegistry.lua` routes a `/cast !Name` line
// through `CastSpellNoToggle`, which asks the engine whether the spell is
// already up before it casts.

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::NameResolve {


using NameToSlot_t = int(__fastcall *)(const char *name, void *out);
static NameToSlot_t NameToSlot_o = nullptr;

static const char *SkipBlanks(const char *s) {
    while (*s == ' ' || *s == '\t')
        ++s;
    return s;
}

// Parse leading digits of `name` into an int. `*end` is set to the first
// non-digit. Caps at 100_000_000 (well above any plausible spellID) to
// avoid signed-int overflow on garbage input.
static int ParseLeadingInt(const char *name, const char **end) {
    int value = 0;
    while (*name >= '0' && *name <= '9') {
        if (value > 100000000) {
            value = 0;
            break;
        }
        value = value * 10 + (*name - '0');
        ++name;
    }
    *end = name;
    return value;
}

static const char *LocaleNameForID(int spellID) {
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return nullptr;
    const int locale = Game::Read<int>(
        static_cast<uintptr_t>(Offsets::VAR_LOCALE_INDEX));
    return Game::Read<const char *>(record, Offsets::OFF_SPELL_NAMES + locale * 4);
}

// Hook on the single name → spellbook-slot resolver
// (`FUN_RESOLVE_SPELL_NAME_TO_SLOT`).
//
// A leading `!` is dropped, so `!Shoot` resolves as `Shoot` — for the runtime
// cast, for the engine's macro parser tagging the slot (which is what makes
// `IsAutoRepeatAction` light a `/cast !Shoot` button while it fires), and for
// the `#showtooltip` resolution.
//
// A pure-numeric string is looked up in `Spell.dbc` and the locale-resolved
// name given to the original instead. Same player-spellbook constraint as the
// original either way: spellIDs the player doesn't have in their spellbook
// still fail to resolve (the engine's downstream
// `[VAR_PLAYER_SPELLBOOK][slot]` deref returns 0 just like for unknown names).
//
// Anything else reaches the original byte-for-byte as it arrived.
static int __fastcall NameToSlot_h(const char *name, void *out) {
    if (name == nullptr)
        return NameToSlot_o(name, out);

    const char *p = SkipBlanks(name);
    if (*p == '!') {
        const char *afterPrefix = SkipBlanks(p + 1);
        // A bare `!` names nothing — leave it for the original to reject.
        if (*afterPrefix != '\0') {
            name = afterPrefix;
            p = afterPrefix;
        }
    }
    if (*p >= '0' && *p <= '9') {
        const char *afterDigits = nullptr;
        const int id = ParseLeadingInt(p, &afterDigits);
        const char *tail = SkipBlanks(afterDigits);
        // Only intercept if the whole name is digits (plus optional
        // surrounding whitespace). Inputs like "5019(Rank 1)" or "5019foo"
        // fall through to the original — the engine's name resolver handles
        // those as-is (and a numeric stem with a `(Rank N)` suffix has no
        // meaning anyway).
        if (id > 0 && *tail == '\0') {
            const char *resolvedName = LocaleNameForID(id);
            if (resolvedName != nullptr && *resolvedName != '\0') {
                return NameToSlot_o(resolvedName, out);
            }
        }
    }
    return NameToSlot_o(name, out);
}

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_RESOLVE_SPELL_NAME_TO_SLOT,
    reinterpret_cast<void *>(&NameToSlot_h),
    reinterpret_cast<void **>(&NameToSlot_o)};

} // namespace Spell::NameResolve
