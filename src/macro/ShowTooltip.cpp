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

// See ShowTooltip.h for the design. Mechanics — mirrored from 3.3.5's macro
// system (decomp: WoW/Source/Ui/UIMacros.cpp — `MACRONODE::Parse` 0x00565C40,
// `MACRONODE::FindSpellID` 0x005650C0, `CGUIMacros::GetFullIconPath`
// 0x00566AC0):
//
//   - Parse. Every macro body, directive or not, is read into option lines by
//     `ParseBody` — see it for 3.3.5's exact rules (`#show` anywhere, the seven
//     action families, `none`, the sticky dynamic flag). A body with a `#show`
//     line is Show / ShowTooltip; one with only action lines is Implicit.
//     Lines come from `FUN_STORM_STR_TOKENIZE`, like the engine's own parser
//     and runner tokenize them.
//   - Evaluate. Each option line with `[` or `;` goes through the Lua
//     `SecureCmdOptionParse` (pcall, stack restored — Timer.cpp is the
//     precedent for Lua from the world tick); a plain line is used as is. The
//     first line whose clause matches with a non-empty value is resolved, as
//     its type says (`/castsequence` asks for its step, `/castrandom` takes
//     its first entry, `/equip` resolves items only):
//     `bag slot` / inventory slot 1..23 → item; else an item the player
//     carries by that name (3.3.5 is item-first too, and so is our `/cast`);
//     else the engine's name → spellbook resolver (rank suffixes and numeric
//     spellIDs and a `!` prefix included, the latter two via the
//     `Spell::NameResolve` hook); else unresolved.
//   - Write. Spell → `+0x564` = spellID, `+0x568` = pet flag. Item or no
//     matching clause → `+0x564` = 0 (the engine's "no cast" value: usable,
//     macro's own icon). A value that matched but resolved to nothing →
//     `0xFFFFFFFF`, the engine's own "named an unknown spell" sentinel (the
//     usable helper greys negative spellIDs; 3.3.5 writes -1 here too). The
//     engine re-parses macros on create / edit / world-enter /
//     spellbook-update and overwrites the field; `Spell::MacroPrimarySpell`'s
//     post-parse observer marks us dirty and the next tick (or the next
//     reader, via `Lookup`'s catch-up) re-applies.
//   - Repaint. When the resolution changes, every action slot holding the
//     macro gets `FUN_ACTION_SLOT_CHANGED_NOTIFY(slot0, 0, 0)` — the engine's
//     own recompute-usable + ACTIONBAR_SLOT_CHANGED(slot+1), minus the server
//     packet.
//   - Cadence. 3.3.5 re-evaluates every dynamic macro each frame from its
//     world tick. We re-evaluate conditional directives the frame any of the
//     usual inputs change (modifier bitmap, mouseover GUID, target GUID,
//     combat flag) and every 200 ms otherwise (the cadence
//     `Util/SecureStateDriver.lua` already uses for the same evaluator);
//     static ones every second (a newly learned rank / newly looted item —
//     the engine never re-parses a resolved macro on its own).
//   - Unlearn. The engine's own sweep clears any action slot whose spell was
//     just unlearned, and it resolves macro slots through the cache above — so
//     a display resolution could get a working macro permanently deleted off
//     the bar (the clear is sent to the server). `PruneSpell_h` parks every
//     managed macro's cache across the sweep; see it for the full trail.
//   - One deliberate divergence from 3.3.5 (user decision), in the PARSER
//     rather than here: a group whose only piece is `@unit` passes only while
//     that unit exists (Util/MacroOptions.lua `GroupPasses`). 3.3.5 passes it
//     unconditionally and stores the (absent) unit's GUID separately. With
//     our rule `[@mouseover][] Spell` falls through to `[]` when nothing is
//     moused over — for the cast and for this icon alike — and a lone
//     `[@mouseover] Spell` shows `?` instead of a spell the click cannot cast.

#include "macro/ShowTooltip.h"

#include "Game.h"
#include "Offsets.h"
#include "action/Slot.h"
#include "input/Modifier.h"
#include "item/Arg.h"
#include "item/ID.h"
#include "item/Icon.h"
#include "item/Location.h"
#include "spell/Lookup.h"
#include "spell/MacroPrimarySpell.h"
#include "tick/WorldTick.h"
#include "time/Clock.h"
#include "unit/Identity.h"
#include "unit/TokenResolve.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Macro::ShowTooltip {

namespace {

constexpr int kMaxMacros = Offsets::MACRO_SLOT_MAP_COUNT;
constexpr int kMaxOptionLines = 8;
constexpr size_t kOptionsMax = 256;
constexpr size_t kLineBufferSize = Offsets::MACRO_LINE_BUFFER_SIZE;
constexpr uint32_t kUnresolvedSpell = 0xFFFFFFFFu;

// Same throttle as `STATE_DRIVER_UPDATE_THROTTLE` in Util/SecureStateDriver.lua.
constexpr uint32_t kConditionalIntervalMs = 200;
constexpr uint32_t kStaticIntervalMs = 1000;
constexpr uint32_t kYieldCheckIntervalMs = 1000;

// Seven families, each with as many `SLASH_<NAME>%d` aliases as the locale
// gives it. Sized well clear of that: the families load in order, so a table
// that fills up drops the LAST ones — and `/castsequence` is third, which
// would take its icon down with no error to show for it.
constexpr int kMaxSlashNames = 32;
constexpr size_t kSlashNameMax = 32;

constexpr const char *kQuestionMarkIcon = "INV_Misc_QuestionMark";

// What an option line came from — 3.3.5's MacroCommand::m_type, which decides
// how its value resolves (`FindSpellID`).
enum class LineType : uint8_t {
    Directive, // `#show` / `#showtooltip <value>` (type 1)
    Action,    // `/cast`, `/use` (type 2)
    Random,    // `/castrandom`, `/userandom` (type 3): the first entry shows
    Sequence,  // `/castsequence` (type 4): the current step shows
    Equip,     // `/equip` (type 5): items only
    EquipSlot, // `/equipslot` (type 6): items only, after the slot number
};

struct Entry {
    uint32_t macroID = 0;
    Kind kind = Kind::None;
    bool conditional = false;
    // The body stopped collecting at a `none` argument: the macro shows its own
    // icon and holds no spell, whatever lines precede it.
    bool none = false;
    bool dirty = false;
    // The macro's buttons must be repainted on the next tick: its body was
    // re-parsed, or a reader's catch-up changed its resolution while a button
    // handler was running. See `FlushRepaints`.
    bool needRepaint = false;
    // The option lines use a condition our parser does not own, so another
    // macro addon owns this macro. Set once per parse of the body.
    bool foreign = false;
    // An addon published this resolution through `Publish`, so it owns the
    // macro: we neither parse the body nor re-evaluate it, and the lookups
    // answer from it even while yielding. See the header.
    bool external = false;
    // A `SetMacroSpell` / `SetMacroItem` claim: it ends when the body changes,
    // which `Rescan` tells by this hash of the body taken at the claim.
    bool untilEdit = false;
    uint32_t bodyHash = 0;
    // The last resolution had a value to resolve, even if it resolved to
    // nothing. The engine's cache tells those two apart — a value that named
    // something unresolvable greys the button (`0xFFFFFFFF`), nothing matched
    // at all leaves it usable (`0`) — so it has to be remembered. It cannot be
    // re-derived from `kind`, which `Publish` sets whether or not the publisher
    // had a value for us.
    bool matched = false;
    int optionCount = 0;
    char options[kMaxOptionLines][kOptionsMax] = {};
    LineType lineType[kMaxOptionLines] = {};
    Target target = Target::None;
    uint32_t spellID = 0;
    uint32_t isPet = 0;
    int itemID = 0;
    uint64_t unit = 0; // Info::unit
    // A published `PublishOptions::liveUnit` token, resolved afresh by every
    // lookup in place of `unit`. Empty for none.
    char unitToken[kOptionsMax] = {};
    uint32_t lastEvalMs = 0;
};

// The inputs conditionals commonly depend on; a change re-evaluates every
// conditional directive at once instead of waiting for the periodic pass.
struct WorldState {
    uint32_t modifiers = 0;
    uint64_t mouseover = 0;
    uint64_t target = 0;
    bool combat = false;

    bool operator!=(const WorldState &o) const {
        return modifiers != o.modifiers || mouseover != o.mouseover ||
               target != o.target || combat != o.combat;
    }
};

Entry g_entries[kMaxMacros];
bool g_rescanPending = true;
bool g_yielding = false;
bool g_yieldChecked = false;
uint32_t g_lastYieldCheckMs = 0;
WorldState g_lastState;
// Set while an evaluation runs. Its repaint fires ACTIONBAR_SLOT_CHANGED,
// whose Lua handlers call our overrides → `Lookup`; the lazy catch-up in
// `Lookup` must not re-enter the evaluator from there.
bool g_busy = false;
// Set while a READER's catch-up runs (`CatchUpIfPending`). Readers are button
// handlers in the middle of their own update, often inside the engine's own
// ACTIONBAR_SLOT_CHANGED dispatch, so a repaint from there would fire that
// event again from inside its handler. Such repaints are marked and done by
// the next tick instead.
bool g_inReaderCatchUp = false;

char g_slashNames[kMaxSlashNames][kSlashNameMax];
LineType g_slashNameType[kMaxSlashNames] = {};
int g_slashNameCount = 0;
bool g_slashNamesLoaded = false;

using MacroIDToEntry_t = uint8_t *(__fastcall *)(uint32_t macroID);
using MacroParse_t = void(__fastcall *)(int macroEntry);
using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);
using ResolveSpellName_t = int(__fastcall *)(const char *name, int *outIsPet);
using SlotChangedNotify_t = void(__fastcall *)(uint32_t slot0, int sendToServer, int quiet);

uint8_t *EntryForID(uint32_t macroID) {
    if (macroID == 0)
        return nullptr;
    return reinterpret_cast<MacroIDToEntry_t>(Offsets::FUN_MACRO_ID_TO_ENTRY)(macroID);
}

void NextLine(const char **cursor, char *out, unsigned outSize) {
    reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE)(
        cursor, out, outSize,
        reinterpret_cast<const char *>(Offsets::VAR_MACRO_LINE_DELIMS), nullptr);
}

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

const char *SkipBlanks(const char *p) {
    while (IsBlank(*p))
        ++p;
    return p;
}

void CopyTrimmed(const char *src, char *dst, size_t dstSize) {
    src = SkipBlanks(src);
    size_t len = std::strlen(src);
    while (len > 0 && IsBlank(src[len - 1]))
        --len;
    if (len >= dstSize)
        len = dstSize - 1;
    std::memcpy(dst, src, len);
    dst[len] = '\0';
}

// `#showtooltip` / `#show` at the start of `line`, followed by end-of-line or
// whitespace. Case-insensitive. Sets `*args` to the (blank-skipped) rest.
//
// Leading blanks are skipped first: the tokenizer splits on `\r\n` only, so a
// directive typed with an indent arrives with it, and testing byte 0 would
// both miss the directive and (through the `#` filters in `Macro::RunBody` /
// `Macro::Execute`) let the line reach chat.
Kind ParseDirective(const char *line, const char **args) {
    struct Word {
        const char *text;
        size_t len;
        Kind kind;
    };
    static const Word kWords[] = {
        {"#showtooltip", 12, Kind::ShowTooltip},
        {"#show", 5, Kind::Show},
    };
    line = SkipBlanks(line);
    for (const Word &w : kWords) {
        if (_strnicmp(line, w.text, w.len) != 0)
            continue;
        const char after = line[w.len];
        if (after != '\0' && !IsBlank(after))
            continue;
        *args = SkipBlanks(line + w.len);
        return w.kind;
    }
    return Kind::None;
}

// `_G[name]` when it is a non-empty string. Raw read — no `__index` may run
// on the globals table from inside the tick.
bool ReadGlobalString(void *L, const char *name, char *out, size_t outSize) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, name);
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    bool ok = false;
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING) {
        const char *s = Game::Lua::ToString(L, -1);
        if (s != nullptr && s[0] != '\0') {
            std::snprintf(out, outSize, "%s", s);
            ok = true;
        }
    }
    Game::Lua::SetTop(L, top);
    return ok;
}

// The live action-command names, read the way the engine parser reads them
// (`SLASH_CAST%d` for %d = 1.. until the first gap).
void LoadSlashNames(void *L) {
    g_slashNameCount = 0;
    // The families 3.3.5's macro parser collects, in its order and with its
    // types (`MACRONODE::Parse`'s s_slashCommands table).
    struct Family {
        const char *fmt;
        LineType type;
    };
    static const Family kFamilies[] = {{"SLASH_CAST%d", LineType::Action},
                                       {"SLASH_CASTRANDOM%d", LineType::Random},
                                       {"SLASH_CASTSEQUENCE%d", LineType::Sequence},
                                       {"SLASH_USE%d", LineType::Action},
                                       {"SLASH_USERANDOM%d", LineType::Random},
                                       {"SLASH_EQUIP%d", LineType::Equip},
                                       {"SLASH_EQUIP_TO_SLOT%d", LineType::EquipSlot}};
    for (const Family &f : kFamilies) {
        for (int i = 1; g_slashNameCount < kMaxSlashNames; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), f.fmt, i);
            if (!ReadGlobalString(L, key, g_slashNames[g_slashNameCount], kSlashNameMax))
                break;
            g_slashNameType[g_slashNameCount] = f.type;
            ++g_slashNameCount;
        }
    }
    g_slashNamesLoaded = true;
}

// The args after an action command at the start of `line` — the engine's
// rule: the command text followed by a space. Null when the line is none of
// them. `*outType` reports which family matched; `/castsequence x` cannot
// match `/cast` because the space has to follow the whole name.
const char *CastCommandArgs(const char *line, LineType *outType) {
    line = SkipBlanks(line); // an indented `/cast` line is still a cast line
    for (int i = 0; i < g_slashNameCount; ++i) {
        const size_t len = std::strlen(g_slashNames[i]);
        if (_strnicmp(line, g_slashNames[i], len) == 0 && line[len] == ' ') {
            *outType = g_slashNameType[i];
            return SkipBlanks(line + len);
        }
    }
    return nullptr;
}

struct Parsed {
    Kind kind = Kind::None;
    // Some line's value can change on its own — conditions, a sequence step, a
    // slot number (3.3.5's m_dynamic) — so it needs the conditional cadence.
    bool conditional = false;
    bool none = false;
    int optionCount = 0;
    char options[kMaxOptionLines][kOptionsMax] = {};
    LineType lineType[kMaxOptionLines] = {};
};

// The value an EquipSlot line names, past its slot number (3.3.5 skips the
// digits and the spaces after them). Returns `args` itself for other lines.
const char *SkipEquipSlotNumber(const char *args, LineType type) {
    if (type != LineType::EquipSlot)
        return args;
    while (*args >= '0' && *args <= '9')
        ++args;
    return SkipBlanks(args);
}

// 3.3.5's `MACRONODE::Parse` (0x00565C40), line by line:
//   - A `/` line counts when it is one of the seven action families; a `#`
//     line when it is `#show` / `#showtooltip` (anywhere in the body, not only
//     line 1). Anything else is skipped.
//   - A line with no args is skipped, so a bare `#show` keeps reading the
//     action lines below it. An arg of `none` stops the whole parse.
//   - A line is DYNAMIC when it is a `/castsequence`, its args start with `[`,
//     or (past an `/equipslot` slot number) they start with a digit. Dynamic
//     is sticky for the rest of the body.
//   - Every counted line joins the list; the list ends after a `#show` line
//     with args, or after any line while nothing has been dynamic yet.
// Every macro gets this list, directive or not — 3.3.5 shows the first
// action's icon on any question-mark macro, and `#show` only picks the value
// while `#showtooltip` also hands over the tooltip.
void ParseBody(const char *body, Parsed *out) {
    *out = Parsed{};
    bool sawShow = false, sawShowTooltip = false, dynamic = false;
    const char *cursor = body;
    char line[kLineBufferSize];
    while (cursor != nullptr && *cursor != '\0' && out->optionCount < kMaxOptionLines) {
        NextLine(&cursor, line, kLineBufferSize);
        if (line[0] == '\0')
            continue;
        LineType type = LineType::Action;
        const char *args = nullptr;
        const Kind directive = ParseDirective(line, &args);
        if (directive != Kind::None) {
            type = LineType::Directive;
            sawShow = true;
            if (directive == Kind::ShowTooltip)
                sawShowTooltip = true;
        } else {
            args = CastCommandArgs(line, &type);
            if (args == nullptr)
                continue;
        }
        char value[kOptionsMax];
        CopyTrimmed(args, value, sizeof(value));
        if (value[0] == '\0')
            continue;
        if (_stricmp(value, "none") == 0) {
            out->none = true;
            break;
        }
        const char *v = SkipEquipSlotNumber(value, type);
        if (type == LineType::Sequence || value[0] == '[' || (*v >= '0' && *v <= '9'))
            dynamic = true;
        std::memcpy(out->options[out->optionCount], value, sizeof(value));
        out->lineType[out->optionCount] = type;
        ++out->optionCount;
        if (type == LineType::Directive || !dynamic)
            break;
    }
    out->conditional = dynamic;
    if (sawShowTooltip)
        out->kind = Kind::ShowTooltip;
    else if (sawShow)
        out->kind = Kind::Show;
    else if (out->optionCount > 0)
        out->kind = Kind::Implicit;
}

bool SameOptions(const Entry &e, const Parsed &p) {
    if (e.optionCount != p.optionCount || e.none != p.none)
        return false;
    for (int i = 0; i < p.optionCount; ++i) {
        if (std::strcmp(e.options[i], p.options[i]) != 0)
            return false;
        if (e.lineType[i] != p.lineType[i])
            return false; // same text, different command — re-resolve it
    }
    return true;
}

// The entry has a resolution of ours to keep in the engine's cache: a body we
// parsed into option lines (or a `none` that says "nothing"), in our own
// dialect. Published entries are handled separately.
bool Drives(const Entry &e) {
    return e.kind != Kind::None && !e.foreign && (e.optionCount > 0 || e.none);
}

// FNV-1a over a macro's body: enough to tell that an edit changed it.
uint32_t BodyHash(const uint8_t *macroEntry) {
    uint32_t h = 2166136261u;
    for (const char *p = reinterpret_cast<const char *>(macroEntry + Offsets::OFF_MACRO_BODY);
         *p != '\0'; ++p)
        h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
    return h;
}

void Rescan(void *L) {
    if (!g_slashNamesLoaded)
        LoadSlashNames(L);

    auto *slotMap = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_MACRO_SLOT_MAP));
    for (int i = 0; i < kMaxMacros; ++i) {
        Entry &e = g_entries[i];
        const uint32_t macroID = slotMap[i];
        const uint8_t *entry = EntryForID(macroID);
        if (entry == nullptr) {
            e = Entry{};
            continue;
        }
        // A published macro is the publisher's to describe — don't re-read
        // its body. Ownership follows the macro, so it only drops when the
        // slot comes to hold a different one.
        if (e.external) {
            if (e.macroID == macroID &&
                !(e.untilEdit && BodyHash(entry) != e.bodyHash))
                continue;
            e = Entry{};
        }
        Parsed parsed;
        ParseBody(reinterpret_cast<const char *>(entry + Offsets::OFF_MACRO_BODY), &parsed);
        if (e.macroID != macroID || e.kind != parsed.kind || !SameOptions(e, parsed)) {
            // The engine repainted this macro's buttons straight after its
            // parse (the edit worker FUN_MACRO_EDIT ends in the slot refresh
            // FUN_004E5CC0, 3.3.5's CGActionBar::UpdateMacro) — before this
            // rescan, so they read the resolution being replaced. Repaint
            // again once ours is current, whatever it turns out to be. 3.3.5
            // repaints after every edit the same way.
            e = Entry{};
            e.macroID = macroID;
            e.needRepaint = true;
            e.kind = parsed.kind;
            e.conditional = parsed.conditional;
            e.none = parsed.none;
            e.optionCount = parsed.optionCount;
            std::memcpy(e.options, parsed.options, sizeof(e.options));
            std::memcpy(e.lineType, parsed.lineType, sizeof(e.lineType));
        }
        // The engine parse that triggered this rescan rewrote `+0x564` —
        // every directive gets re-applied, changed or not. A macro in
        // another addon's dialect is not ours to re-apply, and leaving it
        // dirty would keep `HasPendingWork` true and re-run the catch-up on
        // every reader.
        e.dirty = Drives(e);
    }
}

// The first-clause value of one option line: a plain line as is, anything
// with conditions or alternatives through `SecureCmdOptionParse`. False when
// no clause matched (nil) or the function isn't available. Unit existence
// for a bare `[@unit]` group is the parser's rule (Util/MacroOptions.lua),
// shared with `/cast`, so a `[@mouseover][]` clause falls through to its
// `[]` group here exactly as it does for the cast.
//
// `unit` gets the matched clause's `@unit` (the parser's second return), or
// an empty string when it named none.
//
// `*outForeign` is set when the line uses a condition our parser does not
// own. We ask quietly, because a macro body is not ours: another macro addon
// has its own conditions, and naming one at the player would be noise about a
// macro that works.
bool ResolveOptions(void *L, const char *options, char *out, size_t outSize, char *unit,
                    size_t unitSize, bool *outForeign) {
    out[0] = '\0';
    unit[0] = '\0';
    *outForeign = false;
    if (std::strchr(options, '[') == nullptr && std::strchr(options, ';') == nullptr) {
        CopyTrimmed(options, out, outSize);
        return true;
    }
    const int top = Game::Lua::GetTop(L);
    if (!Game::Lua::PushGlobalFunction(L, "SecureCmdOptionParse")) {
        Game::Lua::SetTop(L, top);
        return false;
    }
    Game::Lua::PushString(L, options);
    Game::Lua::PushBoolean(L, 1); // quiet
    bool matched = false;
    if (Game::Lua::PCall(L, 2, 3, 0) == 0) {
        // Third return: the first condition we do not own, or nil.
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING)
            *outForeign = true;
        if (Game::Lua::Type(L, -3) == Game::Lua::TYPE_STRING) {
            const char *value = Game::Lua::ToString(L, -3);
            if (value != nullptr) {
                CopyTrimmed(value, out, outSize); // copy before the SetTop below
                matched = true;
            }
            if (Game::Lua::Type(L, -2) == Game::Lua::TYPE_STRING) {
                if (const char *token = Game::Lua::ToString(L, -2))
                    CopyTrimmed(token, unit, unitSize);
            }
        }
    }
    Game::Lua::SetTop(L, top);
    return matched;
}

bool ParseUInt(const char *s, const char **end, int *out) {
    if (*s < '0' || *s > '9')
        return false;
    int v = 0;
    for (; *s >= '0' && *s <= '9'; ++s) {
        if (v > 100000000)
            return false;
        v = v * 10 + (*s - '0');
    }
    *end = s;
    *out = v;
    return true;
}

// `^(%d+)%s+(%d+)$`
bool ParseBagSlot(const char *s, int *bag, int *slot) {
    const char *p = nullptr;
    if (!ParseUInt(s, &p, bag) || !IsBlank(*p))
        return false;
    p = SkipBlanks(p);
    return ParseUInt(p, &p, slot) && *p == '\0';
}

// `^(%d+)$`
bool ParseInt(const char *s, int *out) {
    const char *p = nullptr;
    return ParseUInt(s, &p, out) && *p == '\0';
}

struct Resolution {
    Target target = Target::None;
    uint32_t spellID = 0;
    uint32_t isPet = 0;
    int itemID = 0;
    uint64_t unit = 0;
};

// The GUID of the unit a clause or a publisher names: a unit token through the
// engine's resolver, else a unit with that exact name. 0 for none, and for a
// token that points at nothing, which leaves the current target in charge.
uint64_t UnitGuid(const char *unit) {
    if (unit == nullptr || unit[0] == '\0')
        return 0;
    if (::Unit::TokenResolve::IsUnitToken(unit))
        return ::Unit::Identity::GuidForToken(unit);
    return ::Unit::Identity::FindGuidByName(unit, Offsets::TYPEMASK_UNIT, true);
}

void SetItem(const uint8_t *cgItem, Resolution *r) {
    const int itemID = Item::ID::FromCGItem(cgItem);
    if (itemID > 0) {
        r->target = Target::Item;
        r->itemID = itemID;
    }
}

// A spell named by ID for DISPLAY, straight out of `Spell.dbc` — no spellbook
// involved, so it resolves for a spell the player has not learned.
//
// The name path cannot do this. It resolves a name (or an ID through its name)
// to a spellbook SLOT, and an unlearned spell has none — the same constraint
// `/cast` has, and rightly, since a cast needs that slot. An icon and a tooltip
// do not: both come from the spell record either way.
bool SetSpellByID(int spellID, Resolution *r) {
    if (spellID <= 0 || ::Spell::Lookup::RecordForID(spellID) == nullptr)
        return false;
    r->target = Target::Spell;
    r->spellID = static_cast<uint32_t>(spellID);
    r->isPet = 0;
    return true;
}

// `spell:N`, the display counterpart of `item:N`. Returns 0 when `value` is
// not that form.
int ParseSpellRef(const char *value) {
    static const char kPrefix[] = "spell:";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (_strnicmp(value, kPrefix, kPrefixLen) != 0)
        return 0;
    int id = 0;
    const char *end = nullptr;
    if (!ParseUInt(value + kPrefixLen, &end, &id) || *SkipBlanks(end) != '\0')
        return 0;
    return id;
}

// Item forms first, then a carried item by name, then a spell — the 3.3.5
// evaluator's order, and the order our `/cast` handler uses.
//
// `explicitValue` is set when the directive carried the value itself
// (`#showtooltip <value>`) or a publisher handed it over, as opposed to a
// value read off the macro's action lines. Only then does a bare spellID skip
// the spellbook: an explicit value is the author naming what to display, while
// a value lifted off a cast line should resolve exactly as far as that cast
// would. `spell:N` skips it either way — nobody writes that form except to name
// a spell for display.
//
// `itemOnly` is an `/equip` / `/equipslot` value, which 3.3.5 never resolves
// to a spell.
void ResolveValue(const char *value, Resolution *r, bool explicitValue, bool itemOnly) {
    if (const int spellRef = ParseSpellRef(value)) {
        if (!itemOnly)
            SetSpellByID(spellRef, r);
        return;
    }
    int bag = 0, slot = 0;
    if (ParseBagSlot(value, &bag, &slot)) {
        SetItem(Item::Location::ResolveBagSlotNoLua(bag, slot), r);
        return;
    }
    int n = 0;
    const bool numeric = ParseInt(value, &n);
    // An inventory slot: the 19 equipment slots and the four equipped bags
    // (INVSLOT_BAG1..+3) — 3.3.5's player-bag range, 1..23.
    if (numeric && n >= Offsets::EQUIPMENT_SLOT_FIRST && n <= Offsets::INVSLOT_BAG1 + 3) {
        SetItem(Item::Location::ResolveEquipmentSlot(n), r);
        return;
    }
    // A bare number past the slots stays a spellID, which is the documented
    // `/cast 5019` rule, so it must not reach the item lookup —
    // `Item::Arg::ResolveString` would read it as an itemID.
    // Everything else goes through the shared parser, so an `item:N` value
    // or a pasted item link resolves by ID and anything else by name.
    if (!numeric) {
        const Item::Arg::Resolved arg = Item::Arg::ResolveString(value);
        Item::Location::ByGUIDResult found;
        if (Item::Location::FindByArgNoLua(arg, &found)) {
            SetItem(found.item, r);
            if (r->target == Target::Item)
                return;
        }
        // An explicit ID needs no instance — the icon and tooltip come from
        // the item record, so it resolves even with none carried (in the
        // bank, or a macro written ahead of looting it). This is the one
        // item form not limited to what you hold; a NAME still is, since
        // 1.12 has no name-keyed item cache to search.
        if (arg.itemID > 0) {
            r->target = Target::Item;
            r->itemID = arg.itemID;
            return;
        }
    }
    if (itemOnly)
        return;
    int isPet = 0;
    const int spellID = reinterpret_cast<ResolveSpellName_t>(
        Offsets::FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID)(value, &isPet);
    if (spellID > 0) {
        r->target = Target::Spell;
        r->spellID = static_cast<uint32_t>(spellID);
        r->isPet = (isPet != 0) ? 1u : 0u;
        return;
    }
    // The spellbook had no slot for it. An explicitly named ID is still a
    // spell we can describe, so fall back to the record; the pet flag stays 0,
    // since a pet-book slot is exactly what we just failed to find.
    if (explicitValue && numeric)
        SetSpellByID(n, r);
}

// A spell only, as 4.3.4's SetMacroSpell finds it: by name in the spellbook
// (the player's, then the pet's), with `spell:N` and a bare ID as the display
// forms `ResolveValue` gives a published value.
void ResolveSpell(const char *value, Resolution *r) {
    if (const int spellRef = ParseSpellRef(value)) {
        SetSpellByID(spellRef, r);
        return;
    }
    int isPet = 0;
    const int spellID = reinterpret_cast<ResolveSpellName_t>(
        Offsets::FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID)(value, &isPet);
    if (spellID > 0) {
        r->target = Target::Spell;
        r->spellID = static_cast<uint32_t>(spellID);
        r->isPet = (isPet != 0) ? 1u : 0u;
        return;
    }
    int n = 0;
    if (ParseInt(value, &n))
        SetSpellByID(n, r);
}

// Repaint every action slot holding `macroID` through the engine's own
// slot-changed notifier (recompute usable + ACTIONBAR_SLOT_CHANGED, no server
// packet). Lua handlers run synchronously inside — keep the stack balanced.
void Repaint(void *L, uint32_t macroID) {
    auto notify = reinterpret_cast<SlotChangedNotify_t>(Offsets::FUN_ACTION_SLOT_CHANGED_NOTIFY);
    const int top = Game::Lua::GetTop(L);
    for (int slot0 = 0; slot0 < Offsets::ACTION_TABLE_MAX_SLOTS; ++slot0) {
        if (Action::Slot::MacroIDForSlot(slot0) == macroID)
            notify(static_cast<uint32_t>(slot0), 0, 0);
    }
    Game::Lua::SetTop(L, top);
}

struct BusyScope {
    BusyScope() { g_busy = true; }
    ~BusyScope() { g_busy = false; }
};

// What the engine's per-macro spell cache should hold for `r`: the spell
// itself, the "named but unknown" sentinel that greys the button, or 0 for an
// item / nothing.
void DesiredCache(const Resolution &r, bool matched, uint32_t *spell, uint32_t *pet) {
    *spell = 0;
    *pet = 0;
    if (r.target == Target::Spell) {
        *spell = r.spellID;
        *pet = r.isPet;
    } else if (r.target == Target::None && matched) {
        *spell = kUnresolvedSpell;
    }
}

// Store `r` on the entry, push it into the engine's cache, and repaint the
// slots when anything a button reads has moved. Shared by our own evaluation
// and by `Publish`.
void ApplyResolution(void *L, Entry &e, const Resolution &r, bool matched) {
    uint8_t *entry = EntryForID(e.macroID);
    if (entry == nullptr) {
        e = Entry{}; // macro deleted underneath us
        return;
    }
    uint32_t cacheSpell = 0, cachePet = 0;
    DesiredCache(r, matched, &cacheSpell, &cachePet);

    auto &cache = Game::Ref<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL);
    auto &pet = Game::Ref<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL_IS_PET);
    const bool changed = r.target != e.target || r.spellID != e.spellID ||
                         r.isPet != e.isPet || r.itemID != e.itemID ||
                         cache != cacheSpell || pet != cachePet;
    e.target = r.target;
    e.spellID = r.spellID;
    e.isPet = r.isPet;
    e.itemID = r.itemID;
    // The unit changes no button state the engine caches (the range answer is
    // asked for fresh), so it never forces a repaint.
    e.unit = r.unit;
    e.matched = matched;
    cache = cacheSpell;
    pet = cachePet;

    if (!changed && !e.needRepaint)
        return;
    if (g_inReaderCatchUp) {
        e.needRepaint = true;
        return;
    }
    e.needRepaint = false;
    Repaint(L, e.macroID);
}

// Repaint every macro marked for it — re-parsed bodies, including ones that are
// no longer ours to drive, and changes a reader's catch-up deferred.
void FlushRepaints(void *L) {
    for (Entry &e : g_entries) {
        if (!e.needRepaint)
            continue;
        e.needRepaint = false;
        if (e.macroID == 0)
            continue;
        BusyScope busy;
        Repaint(L, e.macroID);
    }
}

// The step a `/castsequence` is on, replacing the sequence text in `value`
// with the action that step names. Mirrors what 3.3.5's macro code does at
// `FUN_00564900`: call the Lua global `QueryCastSequence` with the sequence,
// take three returns, and prefer the item over the spell. The index lives in
// Lua because that is where the sequence advances, so asking is the only way
// the icon and the cast can agree on which step is current.
//
// False when the global is missing or errored (the addon did not load, or an
// addon replaced it with something that throws) — the caller then leaves the
// line unresolved rather than showing a sequence string as a spell name.
bool QuerySequenceStep(void *L, char *value, size_t valueSize) {
    const int top = Game::Lua::GetTop(L);
    if (!Game::Lua::PushGlobalFunction(L, "QueryCastSequence")) {
        Game::Lua::SetTop(L, top);
        return false;
    }
    Game::Lua::PushString(L, value);
    bool ok = false;
    if (Game::Lua::PCall(L, 1, 3, 0) == 0) {
        // Returns are (index, item, spell); the item wins when the step names
        // one, exactly as the engine's own reader picks.
        const char *item = Game::Lua::Type(L, -2) == Game::Lua::TYPE_STRING
                               ? Game::Lua::ToString(L, -2)
                               : nullptr;
        const char *spell = Game::Lua::Type(L, -1) == Game::Lua::TYPE_STRING
                                ? Game::Lua::ToString(L, -1)
                                : nullptr;
        const char *pick = (item != nullptr && item[0] != '\0') ? item : spell;
        if (pick != nullptr) {
            CopyTrimmed(pick, value, valueSize);
            ok = true;
        }
    }
    Game::Lua::SetTop(L, top);
    return ok;
}

void Evaluate(void *L, Entry &e, uint32_t nowMs) {
    BusyScope busy;
    e.dirty = false;
    e.lastEvalMs = nowMs;

    // First option line whose clause matches with a non-empty value.
    bool matched = false;
    int matchedLine = -1;
    char value[kOptionsMax] = {};
    char unit[kOptionsMax] = {};
    for (int i = 0; i < e.optionCount; ++i) {
        bool foreign = false;
        bool got = ResolveOptions(L, e.options[i], value, sizeof(value), unit, sizeof(unit),
                                  &foreign);
        if (got && value[0] != '\0') {
            switch (e.lineType[i]) {
            case LineType::Sequence:
                // The value is the sequence, not an action. Swap in the step
                // it is on before anything tries to resolve it. An empty step
                // (nothing castable there) falls through to the next line.
                got = QuerySequenceStep(L, value, sizeof(value));
                break;
            case LineType::Random: {
                // 3.3.5 shows the FIRST entry of the list (`FindSpellID` cuts
                // the value at its first comma).
                if (char *comma = std::strchr(value, ','))
                    *comma = '\0';
                char first[kOptionsMax];
                CopyTrimmed(value, first, sizeof(first));
                std::memcpy(value, first, sizeof(first));
                break;
            }
            case LineType::EquipSlot: {
                const char *named = SkipEquipSlotNumber(value, LineType::EquipSlot);
                std::memmove(value, named, std::strlen(named) + 1);
                break;
            }
            default:
                break;
            }
        }
        if (foreign) {
            // Another macro addon's dialect. Its conditions decide what this
            // macro does, and we cannot evaluate them, so the macro is not
            // ours to describe: leave the engine's own parse of the body in
            // its cache and stop re-evaluating. A publisher can still claim
            // it through `Publish`, and an edit re-tests it (`Rescan` clears
            // the entry whenever the option lines change).
            e.foreign = true;
            return;
        }
        if (got && value[0] != '\0') {
            matched = true;
            matchedLine = i;
            break;
        }
    }
    Resolution r;
    if (matched) {
        const LineType type = e.lineType[matchedLine];
        ResolveValue(value, &r, type == LineType::Directive,
                     type == LineType::Equip || type == LineType::EquipSlot);
        r.unit = UnitGuid(unit);
    }
    ApplyResolution(L, e, r, matched);
}

// Re-push every published resolution whose engine cache has drifted. The
// engine rewrites that field whenever it re-parses a macro (create, edit,
// world enter, spellbook update), which would otherwise silently replace a
// publisher's answer with the engine's own first `/cast` line. Runs even
// while yielding, since a publisher owns its macro either way.
void MaintainExternal(void *L) {
    for (Entry &e : g_entries) {
        if (!e.external || e.macroID == 0)
            continue;
        const uint8_t *entry = EntryForID(e.macroID);
        if (entry == nullptr) {
            e = Entry{};
            continue;
        }
        Resolution r;
        r.target = e.target;
        r.spellID = e.spellID;
        r.isPet = e.isPet;
        r.itemID = e.itemID;
        r.unit = e.unit;
        uint32_t wantSpell = 0, wantPet = 0;
        // `e.matched`, never `kind != None`: `Publish` marks a claimed macro
        // `ShowTooltip` even when the publisher had nothing for us, and
        // re-deriving it from that would turn the publisher's "nothing matched"
        // (cache `0`, button usable) into "named an unknown spell" (the
        // grey-out sentinel) on the first tick after the publish.
        DesiredCache(r, e.matched, &wantSpell, &wantPet);
        const uint32_t haveSpell = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_MACRO_PRIMARY_SPELL);
        const uint32_t havePet = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_MACRO_PRIMARY_SPELL_IS_PET);
        if (haveSpell == wantSpell && havePet == wantPet)
            continue;
        BusyScope busy;
        // Force the repaint: the stored resolution already matches, so only
        // the engine's copy is stale.
        e.target = Target::None;
        ApplyResolution(L, e, r, e.matched);
    }
}

// Keep our resolution out of the engine's unlearn sweep.
//
// `FUN_ACTION_BAR_PRUNE_SPELL` runs straight after the unlearn writer and
// walks every action slot, resolving each through the MACRO-AWARE
// `FUN_ACTION_SLOT_TO_SPELL` — for a macro slot, the primary-spell cache this
// module writes — and clears every slot whose spell is the one just unlearned.
// The clear is `FUN_ACTION_SLOT_CLEAR`, which notifies with `sendToServer = 1`,
// so a CMSG_SET_ACTION_BUTTON goes out and the removal is persisted: the macro
// is gone for good, not until the next reload.
//
// That rule is sound for a cache the engine filled from a `/cast` line. It is
// wrong for one we filled from a DISPLAY directive, which names an icon rather
// than what the button does — `#showtooltip [known:18223] 18223` over a
// `/cast` the player can still perform would lose the whole macro on a respec.
// A conditional directive makes it arbitrary besides: the cached value is
// whichever clause happened to match on the last evaluation.
//
// So for the length of the sweep every macro we describe shows a cache of `0`,
// which no real spellID matches, and gets its value back afterwards. That
// includes Implicit macros: their value comes from an action line, but which
// line depends on whichever condition matched last, so the sweep would delete
// a macro over a clause it isn't even on. The restore is
// a plain field write with no repaint — it puts back exactly what was there a
// moment earlier, and the slots the engine really did clear were announced by
// the engine's own notify.
//
// The directive itself catches up on its own: the spellbook just changed, so
// the next evaluation re-runs `[known:…]` against it and repaints if the
// answer moved.
using PruneSpell_t = void(__fastcall *)(uint32_t spellID);
PruneSpell_t g_origPruneSpell = nullptr;

void __fastcall PruneSpell_h(uint32_t spellID) {
    // The sweep's ACTIONBAR_SLOT_CHANGED handlers run our action overrides,
    // which reach `Lookup` — and its catch-up would re-apply the very values
    // we just parked, mid-sweep, for every slot the loop has yet to reach.
    BusyScope busy;

    uint8_t *entries[kMaxMacros];
    uint32_t saved[kMaxMacros];
    int savedCount = 0;
    for (Entry &e : g_entries) {
        if (e.macroID == 0)
            continue;
        // Exactly the macros this module drives — the same test `CatchUp` uses
        // to decide what it may evaluate, plus published ones.
        const bool managed = e.external || Drives(e);
        if (!managed)
            continue;
        uint8_t *entry = EntryForID(e.macroID);
        if (entry == nullptr)
            continue;
        auto &cache = Game::Ref<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL);
        entries[savedCount] = entry;
        saved[savedCount] = cache; // whatever is there, ours or the engine's
        ++savedCount;
        cache = 0;
    }

    g_origPruneSpell(spellID);

    for (int i = 0; i < savedCount; ++i)
        Game::Ref<uint32_t>(entries[i], Offsets::OFF_MACRO_PRIMARY_SPELL) = saved[i];
}

// Read `CleveRoids.<field>` as a boolean. The table is already at the top of
// the stack; leaves the stack as it found it apart from what the caller pops.
bool ReadCleveRoidsFlag(void *L, const char *field) {
    Game::Lua::PushString(L, field);
    Game::Lua::RawGet(L, -2);
    const bool set = Game::Lua::ToBoolean(L, -1) != 0;
    Game::Lua::SetTop(L, Game::Lua::GetTop(L) - 1);
    return set;
}

// SuperCleveRoidMacros owns macro display when it is loaded and hasn't
// disabled itself: `CleveRoids` is its namespace table, `CleveRoids.disabled`
// its self-disable flag. A build that drives our display instead of replacing
// the action globals announces itself with `ClassicAPIMacroDisplay`, and then
// we do NOT stand down wholesale — it publishes per macro through `Publish`,
// and macros it does not claim stay ours. Forks without the flag keep the
// original all-or-nothing yield, so an older one can't end up fighting us.
bool DetectYield(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "CleveRoids");
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    bool yield = false;
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        yield = !ReadCleveRoidsFlag(L, "disabled") &&
                !ReadCleveRoidsFlag(L, "ClassicAPIMacroDisplay");
    }
    Game::Lua::SetTop(L, top);
    return yield;
}

bool HasGlobalFunction(void *L, const char *name) {
    const int top = Game::Lua::GetTop(L);
    const bool ok = Game::Lua::PushGlobalFunction(L, name);
    Game::Lua::SetTop(L, top);
    return ok;
}

// The Lua state, once the player is in the world (the embedded addon and the
// macro registry are both up by then). Null otherwise.
void *ReadyState() {
    void *L = Game::Lua::State();
    if (L == nullptr || Unit::Identity::PlayerObject() == nullptr)
        return nullptr;
    return L;
}

WorldState ReadWorldState() {
    WorldState s;
    s.modifiers = Input::Modifier::CurrentMask();
    s.mouseover = (static_cast<uint64_t>(Game::Read<uint32_t>(
                       static_cast<uintptr_t>(Offsets::VAR_MOUSEOVER_GUID_HI))) << 32) |
                  Game::Read<uint32_t>(static_cast<uintptr_t>(Offsets::VAR_MOUSEOVER_GUID_LO));
    if (const uint8_t *desc = Unit::Identity::PlayerDescriptor()) {
        s.target = Game::Read<uint64_t>(desc, Offsets::OFF_UNIT_FIELD_TARGET);
        s.combat = (Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_FLAGS) &
                    Offsets::UNIT_FLAG_IN_COMBAT) != 0;
    }
    return s;
}

// Re-check the SuperCleveRoidMacros latch at most once a second.
void RefreshYield(void *L, uint32_t now) {
    if (!g_yieldChecked || Time::Clock::Elapsed(g_lastYieldCheckMs, now) >= kYieldCheckIntervalMs) {
        g_yielding = DetectYield(L);
        g_yieldChecked = true;
        g_lastYieldCheckMs = now;
    }
}

bool HasPendingWork() {
    if (g_rescanPending)
        return true;
    for (const Entry &e : g_entries) {
        if (e.dirty)
            return true;
    }
    return false;
}

// Process a pending rescan and every dirty entry now. Returns false when the
// evaluator (the embedded addon's `SecureCmdOptionParse`) isn't loaded yet —
// the login load pass, or the re-load after /reload.
bool CatchUp(void *L, uint32_t now) {
    if (g_rescanPending) {
        if (!HasGlobalFunction(L, "SecureCmdOptionParse"))
            return false;
        Rescan(L);
        g_rescanPending = false;
    }
    for (Entry &e : g_entries) {
        if (e.dirty && Drives(e))
            Evaluate(L, e, now);
    }
    return true;
}

void Tick() {
    void *L = ReadyState();
    if (L == nullptr)
        return;

    const uint32_t now = Time::Clock::NowMs();
    RefreshYield(L, now);
    // Published macros are maintained either way — their owner is driving us
    // directly, which the wholesale yield has no say over.
    MaintainExternal(L);
    if (g_yielding)
        return;
    if (!CatchUp(L, now))
        return;

    const WorldState state = ReadWorldState();
    const bool stateChanged = state != g_lastState;
    g_lastState = state;

    for (Entry &e : g_entries) {
        if (!Drives(e))
            continue;
        const uint32_t interval = e.conditional ? kConditionalIntervalMs : kStaticIntervalMs;
        if ((e.conditional && stateChanged) ||
            Time::Clock::Elapsed(e.lastEvalMs, now) >= interval) {
            Evaluate(L, e, now);
        }
    }
    FlushRepaints(L);
}

// Lazy half of the cadence: a reader (button repaint, Macro UI update)
// arriving in the same frame the engine re-parsed a macro — before the
// next tick — gets the fresh resolution instead of the engine's interim
// value. No-op while an evaluation is already on the stack.
void CatchUpIfPending() {
    if (g_busy || !HasPendingWork())
        return;
    void *L = ReadyState();
    if (L == nullptr)
        return;
    const uint32_t now = Time::Clock::NowMs();
    RefreshYield(L, now);
    if (g_yielding)
        return;
    g_inReaderCatchUp = true;
    CatchUp(L, now);
    g_inReaderCatchUp = false;
}

void OnMacroParsed(int /*macroEntry*/) {
    g_rescanPending = true;
}

// Lua-derived state (slash-command names, the SCRM latch) is reload-fragile;
// the engine also re-parses every macro after a reload.
//
// Published claims are dropped here, because a claim must not outlive its
// publisher. Nothing re-evaluates a published macro for us, so its owner
// republishes on every load — and an addon that is disabled or removed between
// reloads would otherwise leave every macro it ever claimed frozen on its last
// answer forever, since `Rescan` deliberately never re-reads a claimed body.
// Each one gets the engine's own parse of its body first, the same handoff
// `Release` performs, so a macro we stop describing isn't left holding a value
// of ours in the engine's field.
void PrepareForReload() {
    for (Entry &e : g_entries) {
        if (e.external && e.macroID != 0) {
            if (uint8_t *entry = EntryForID(e.macroID))
                reinterpret_cast<MacroParse_t>(Offsets::FUN_MACRO_PARSE_PRIMARY_SPELL)(
                    static_cast<int>(reinterpret_cast<uintptr_t>(entry)));
        }
        e = Entry{};
    }
    g_rescanPending = true;
    g_slashNamesLoaded = false;
    g_yieldChecked = false;
}

// The lookup itself, over what the entries already hold. `Lookup` runs the
// catch-up first; `LookupPassive` does not.
// The entry describing `macroID`, or null when no live macro slot holds that
// id or the entry there hasn't caught up to it yet.
//
// Entries are keyed by macro-slot index, and the slot map is the only thing
// that says which index holds an id right now — so resolve through it rather
// than scanning for a matching `macroID`. A scan answers from whichever entry
// happens to carry the id, and that is not sound: `FUN_MACRO_CREATE` bumps one
// of two per-scope counters, so a general and a per-character macro can share
// an id, and an entry whose index shifted keeps its old id until the next
// rescan. Either way another macro's resolution could answer for this one. A
// mismatch here means "not ours yet" and the caller falls back to the engine
// until the rescan re-keys the entry.
const Entry *EntryForMacro(uint32_t macroID) {
    const int slot = Action::Slot::MacroSlotForID(macroID);
    if (slot <= 0 || slot > kMaxMacros)
        return nullptr;
    const Entry &e = g_entries[slot - 1];
    return e.macroID == macroID ? &e : nullptr;
}

bool LookupEntries(uint32_t macroID, Info *out) {
    const Entry *found = EntryForMacro(macroID);
    if (found == nullptr)
        return false;
    const Entry &e = *found;

    // A published resolution answers even while yielding: its owner asked us
    // to display it, so the wholesale stand-down doesn't apply to it.
    if (e.external) {
        if (e.target == Target::None)
            return false;
        out->kind = e.kind;
        out->target = e.target;
        out->spellID = e.spellID;
        out->isPet = e.isPet;
        out->itemID = e.itemID;
        out->unit = e.unitToken[0] != '\0' ? ::Unit::Identity::GuidForToken(e.unitToken)
                                           : e.unit;
        // A publisher re-publishes the moment its answer changes, so a claimed
        // macro never needs the polling refresh a condition of ours does.
        out->conditional = false;
        return true;
    }
    if (g_yielding || e.kind == Kind::None)
        return false;
    if (e.target != Target::None) {
        out->kind = e.kind;
        out->target = e.target;
        out->spellID = e.spellID;
        out->isPet = e.isPet;
        out->itemID = e.itemID;
        out->unit = e.unit;
        out->conditional = e.conditional;
        return true;
    }
    if (e.optionCount == 0 && !e.none) {
        // Bare directive with no `/cast` or `/use` line to draw from: show
        // whatever the engine's own parse resolved (a `CastSpellByName("...")`
        // line, for instance).
        const uint8_t *entry = EntryForID(macroID);
        if (entry == nullptr)
            return false;
        const uint32_t spellID = Game::Read<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL);
        if (spellID == 0 || spellID == kUnresolvedSpell)
            return false;
        out->kind = e.kind;
        out->target = Target::Spell;
        out->spellID = spellID;
        out->isPet = Game::Read<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL_IS_PET);
        out->itemID = 0;
        out->unit = 0;
        // A bare directive has no option lines, so nothing to re-evaluate.
        out->conditional = false;
        return true;
    }
    return false;
}

const Tick::WorldTick::AutoSubscribe _tick{&Tick};
const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Spell::MacroPrimarySpell::PostParseAutoRegister _parsed{&OnMacroParsed};
const Game::HookAutoRegister _hookPruneSpell{
    Offsets::FUN_ACTION_BAR_PRUNE_SPELL, reinterpret_cast<void *>(&PruneSpell_h),
    reinterpret_cast<void **>(&g_origPruneSpell)};

} // namespace

bool Publish(int macroSlot, const char *value, const PublishOptions &options) {
    if (macroSlot < 1 || macroSlot > kMaxMacros)
        return false;
    void *L = ReadyState();
    if (L == nullptr)
        return false;

    auto *slotMap = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_MACRO_SLOT_MAP));
    const uint32_t macroID = slotMap[macroSlot - 1];
    const uint8_t *macroEntry = EntryForID(macroID);
    if (macroEntry == nullptr)
        return false;

    Entry &e = g_entries[macroSlot - 1];
    if (!e.external || e.macroID != macroID) {
        e = Entry{};
        e.macroID = macroID;
        e.external = true;
    }
    e.untilEdit = options.untilEdit;
    e.bodyHash = options.untilEdit ? BodyHash(macroEntry) : 0;
    // Kind marks the macro as claimed even when nothing resolved, so the button
    // falls back to the question mark rather than to our own parse. WHICH kind
    // is the body's call, not the publisher's: `#show` is the macro's author
    // asking for the icon alone, and a body with no directive keeps the
    // engine's macro-name tooltip the way retail does. Only `#showtooltip`
    // hands the tooltip over. The claim stands either way, so the icon,
    // cooldown, count and usable state still follow the published value.
    Parsed parsed;
    ParseBody(reinterpret_cast<const char *>(macroEntry + Offsets::OFF_MACRO_BODY), &parsed);
    e.kind = (parsed.kind == Kind::ShowTooltip) ? Kind::ShowTooltip : Kind::Show;

    Resolution r;
    const bool matched = value != nullptr && value[0] != '\0';
    // A publisher naming a value is naming it explicitly, the same as a
    // directive that carries one.
    if (matched) {
        if (options.accept == Accept::Spell)
            ResolveSpell(value, &r);
        else
            ResolveValue(value, &r, /*explicitValue=*/true,
                         /*itemOnly=*/options.accept == Accept::Item);
    }
    // Set on every publish, so one that changes only the unit still lands:
    // `ApplyResolution` stores the unit before its unchanged-value early out.
    if (options.liveUnit && options.unit != nullptr)
        std::snprintf(e.unitToken, sizeof(e.unitToken), "%s", options.unit);
    else
        e.unitToken[0] = '\0';
    r.unit = options.liveUnit ? 0 : UnitGuid(options.unit);

    BusyScope busy;
    ApplyResolution(L, e, r, matched);
    return e.target != Target::None;
}

void Release(int macroSlot) {
    if (macroSlot < 1 || macroSlot > kMaxMacros)
        return;
    Entry &e = g_entries[macroSlot - 1];
    if (!e.external)
        return;
    const uint32_t macroID = e.macroID;
    e = Entry{};
    // Re-read the body and re-apply our own resolution for it.
    g_rescanPending = true;

    // Give the engine's field back to the engine. Its parser is what fills
    // the primary-spell cache from the body, and a macro with no directive of
    // its own gets no resolution from us — so nothing else would ever
    // overwrite the publisher's spell, and the button, its tooltip, cooldown
    // and auto-repeat state would stay on it. The parse also marks a rescan
    // through the post-parse observer, so our own resolution resumes for a
    // macro that does carry a directive.
    uint8_t *entry = EntryForID(macroID);
    if (entry == nullptr)
        return;
    reinterpret_cast<MacroParse_t>(Offsets::FUN_MACRO_PARSE_PRIMARY_SPELL)(
        static_cast<int>(reinterpret_cast<uintptr_t>(entry)));
    if (void *L = ReadyState()) {
        BusyScope busy; // the repaint's Lua handlers read back through `Lookup`
        Repaint(L, macroID);
    }
}

bool Lookup(uint32_t macroID, Info *out) {
    if (macroID == 0)
        return false;
    CatchUpIfPending();
    return LookupEntries(macroID, out);
}

bool LookupPassive(uint32_t macroID, Info *out) {
    return macroID != 0 && LookupEntries(macroID, out);
}

bool ForSlot(int slot0, Info *out) {
    const uint32_t macroID = Action::Slot::MacroIDForSlot(slot0);
    return macroID != 0 && Lookup(macroID, out);
}

const char *MacroNameForSlot(int slot0) {
    const uint32_t macroID = Action::Slot::MacroIDForSlot(slot0);
    if (macroID == 0)
        return nullptr;
    const uint8_t *entry = EntryForID(macroID);
    if (entry == nullptr)
        return nullptr;
    return reinterpret_cast<const char *>(entry + Offsets::OFF_MACRO_NAME);
}

bool HasQuestionMarkIcon(uint32_t macroID) {
    const uint8_t *entry = EntryForID(macroID);
    if (entry == nullptr)
        return false;
    return _stricmp(reinterpret_cast<const char *>(entry + Offsets::OFF_MACRO_ICON),
                    kQuestionMarkIcon) == 0;
}

bool ResolvedIconPath(const Info &info, bool activeIcon, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    if (info.target == Target::Spell) {
        const char *path = ::Spell::Lookup::IconPath(
            ::Spell::Lookup::RecordForID(static_cast<int>(info.spellID)), activeIcon);
        if (path == nullptr || path[0] == '\0')
            return false;
        std::snprintf(out, outSize, "%s", path);
        return true;
    }
    if (info.target == Target::Item)
        return ::Item::Icon::PathForItemID(static_cast<uint32_t>(info.itemID), out, outSize);
    return false;
}

} // namespace Macro::ShowTooltip
