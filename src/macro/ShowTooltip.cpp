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
// system (parser `FUN_00565c40`, evaluator `FUN_005650c0`, icon rule
// `FUN_00566ac0`, decompiled on the 3.3.5 binary; see CLAUDE.md):
//
//   - Parse. Line 1 of the body must be `#showtooltip [args]` or `#show
//     [args]`. With args, that single line is the option list. Bare form: the
//     args of every later line that starts with a live `SLASH_CAST%d` /
//     `SLASH_USE%d` command followed by a space, in order, up to and
//     including the first line whose args do NOT start with `[` — a static
//     line ends the list and acts as the default (3.3.5 collects exactly
//     this way). Lines come from `FUN_STORM_STR_TOKENIZE`, like the engine's
//     own parser and runner tokenize them.
//   - Evaluate. Each option line with `[` or `;` goes through the Lua
//     `SecureCmdOptionParse` (pcall, stack restored — Timer.cpp is the
//     precedent for Lua from the world tick); a plain line is used as is. The
//     first line whose clause matches with a non-empty value is resolved:
//     `bag slot` / equipment slot 1..19 → item; else an item the player
//     carries by that name (3.3.5 is item-first too, and so is our `/cast`);
//     else the engine's name → spellbook resolver (rank suffixes and numeric
//     spellIDs included, the latter via the CastByID hook); else unresolved.
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

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Macro::ShowTooltip {

namespace {

constexpr int kMaxMacros = Offsets::MACRO_SLOT_MAP_COUNT;
constexpr int kMaxOptionLines = 8;
constexpr size_t kOptionsMax = 256;
constexpr size_t kLineBufferSize = 0x400; // the engine runner's line buffer
constexpr uint32_t kUnresolvedSpell = 0xFFFFFFFFu;

// Same throttle as `STATE_DRIVER_UPDATE_THROTTLE` in Util/SecureStateDriver.lua.
constexpr uint32_t kConditionalIntervalMs = 200;
constexpr uint32_t kStaticIntervalMs = 1000;
constexpr uint32_t kYieldCheckIntervalMs = 1000;

constexpr int kMaxSlashNames = 16;
constexpr size_t kSlashNameMax = 32;

constexpr const char *kQuestionMarkIcon = "INV_Misc_QuestionMark";

struct Entry {
    uint32_t macroID = 0;
    Kind kind = Kind::None;
    bool conditional = false;
    bool dirty = false;
    int optionCount = 0;
    char options[kMaxOptionLines][kOptionsMax] = {};
    Target target = Target::None;
    uint32_t spellID = 0;
    uint32_t isPet = 0;
    int itemID = 0;
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

char g_slashNames[kMaxSlashNames][kSlashNameMax];
int g_slashNameCount = 0;
bool g_slashNamesLoaded = false;

using MacroIDToEntry_t = uint8_t *(__fastcall *)(uint32_t macroID);
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

// The live `/cast`-family and `/use` command names, read the way the engine
// parser reads them (`SLASH_CAST%d` for %d = 1.. until the first gap).
void LoadSlashNames(void *L) {
    g_slashNameCount = 0;
    const char *const kFamilies[] = {"SLASH_CAST%d", "SLASH_USE%d"};
    for (const char *fmt : kFamilies) {
        for (int i = 1; g_slashNameCount < kMaxSlashNames; ++i) {
            char key[32];
            std::snprintf(key, sizeof(key), fmt, i);
            if (!ReadGlobalString(L, key, g_slashNames[g_slashNameCount], kSlashNameMax))
                break;
            ++g_slashNameCount;
        }
    }
    g_slashNamesLoaded = true;
}

// The args after a cast/use command at the start of `line` — the engine's
// rule: the command text followed by a space. `/castsequence x` does not
// match `/cast`. Null when the line is not a cast/use command.
const char *CastCommandArgs(const char *line) {
    for (int i = 0; i < g_slashNameCount; ++i) {
        const size_t len = std::strlen(g_slashNames[i]);
        if (_strnicmp(line, g_slashNames[i], len) == 0 && line[len] == ' ')
            return SkipBlanks(line + len);
    }
    return nullptr;
}

struct Parsed {
    Kind kind = Kind::None;
    bool conditional = false;
    int optionCount = 0;
    char options[kMaxOptionLines][kOptionsMax] = {};
};

void AppendOptionLine(Parsed *out, const char *args) {
    if (out->optionCount >= kMaxOptionLines)
        return;
    CopyTrimmed(args, out->options[out->optionCount], kOptionsMax);
    if (out->options[out->optionCount][0] == '\0')
        return; // an empty argument list contributes nothing (3.3.5 skips it too)
    if (std::strchr(out->options[out->optionCount], '[') != nullptr)
        out->conditional = true;
    ++out->optionCount;
}

void ParseBody(const char *body, Parsed *out) {
    *out = Parsed{};
    const char *cursor = body;
    char line[kLineBufferSize];
    NextLine(&cursor, line, kLineBufferSize);
    if (line[0] == '\0')
        return;

    const char *args = nullptr;
    const Kind kind = ParseDirective(line, &args);
    if (kind == Kind::None)
        return;
    out->kind = kind;

    if (*args != '\0') {
        AppendOptionLine(out, args);
        return;
    }
    // Bare form: collect cast/use lines until the first static one (its args
    // don't start with `[`), which is the default and ends the list.
    while (cursor != nullptr && *cursor != '\0' && out->optionCount < kMaxOptionLines) {
        NextLine(&cursor, line, kLineBufferSize);
        if (line[0] == '\0')
            continue;
        const char *rest = CastCommandArgs(line);
        if (rest == nullptr)
            continue;
        const int before = out->optionCount;
        AppendOptionLine(out, rest);
        if (out->optionCount > before && out->options[before][0] != '[')
            break;
    }
}

bool SameOptions(const Entry &e, const Parsed &p) {
    if (e.optionCount != p.optionCount)
        return false;
    for (int i = 0; i < p.optionCount; ++i) {
        if (std::strcmp(e.options[i], p.options[i]) != 0)
            return false;
    }
    return true;
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
        Parsed parsed;
        ParseBody(reinterpret_cast<const char *>(entry + Offsets::OFF_MACRO_BODY), &parsed);
        if (e.macroID != macroID || e.kind != parsed.kind || !SameOptions(e, parsed)) {
            e = Entry{};
            e.macroID = macroID;
            e.kind = parsed.kind;
            e.conditional = parsed.conditional;
            e.optionCount = parsed.optionCount;
            std::memcpy(e.options, parsed.options, sizeof(e.options));
        }
        // The engine parse that triggered this rescan rewrote `+0x564` —
        // every directive gets re-applied, changed or not.
        e.dirty = (e.kind != Kind::None && e.optionCount > 0);
    }
}

// The first-clause value of one option line: a plain line as is, anything
// with conditions or alternatives through `SecureCmdOptionParse`. False when
// no clause matched (nil) or the function isn't available. Unit existence
// for a bare `[@unit]` group is the parser's rule (Util/MacroOptions.lua),
// shared with `/cast`, so a `[@mouseover][]` clause falls through to its
// `[]` group here exactly as it does for the cast.
bool ResolveOptions(void *L, const char *options, char *out, size_t outSize) {
    out[0] = '\0';
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
    bool matched = false;
    if (Game::Lua::PCall(L, 1, 2, 0) == 0 && Game::Lua::Type(L, -2) == Game::Lua::TYPE_STRING) {
        const char *value = Game::Lua::ToString(L, -2);
        if (value != nullptr) {
            CopyTrimmed(value, out, outSize); // copy before the SetTop below
            matched = true;
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
};

void SetItem(const uint8_t *cgItem, Resolution *r) {
    const int itemID = Item::ID::FromCGItem(cgItem);
    if (itemID > 0) {
        r->target = Target::Item;
        r->itemID = itemID;
    }
}

// Item forms first, then a carried item by name, then a spell — the 3.3.5
// evaluator's order, and the order our `/cast` handler uses.
void ResolveValue(const char *value, Resolution *r) {
    int bag = 0, slot = 0;
    if (ParseBagSlot(value, &bag, &slot)) {
        SetItem(Item::Location::ResolveBagSlotNoLua(bag, slot), r);
        return;
    }
    int n = 0;
    const bool numeric = ParseInt(value, &n);
    if (numeric && n >= Offsets::EQUIPMENT_SLOT_FIRST &&
        n <= Offsets::EQUIPMENT_SLOT_LAST) {
        SetItem(Item::Location::ResolveEquipmentSlot(n), r);
        return;
    }
    // A bare number that is not an equipment slot stays a spellID, which is
    // the documented `/cast 5019` rule, so it must not reach the item
    // lookup — `Item::Arg::ResolveString` would read it as an itemID.
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
    int isPet = 0;
    const int spellID = reinterpret_cast<ResolveSpellName_t>(
        Offsets::FUN_RESOLVE_SPELL_NAME_TO_BOOK_ID)(value, &isPet);
    if (spellID > 0) {
        r->target = Target::Spell;
        r->spellID = static_cast<uint32_t>(spellID);
        r->isPet = (isPet != 0) ? 1u : 0u;
    }
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

void Evaluate(void *L, Entry &e, uint32_t nowMs) {
    BusyScope busy;
    e.dirty = false;
    e.lastEvalMs = nowMs;
    uint8_t *entry = EntryForID(e.macroID);
    if (entry == nullptr) {
        e = Entry{}; // macro deleted underneath us
        return;
    }

    // First option line whose clause matches with a non-empty value.
    bool matched = false;
    char value[kOptionsMax] = {};
    for (int i = 0; i < e.optionCount; ++i) {
        if (ResolveOptions(L, e.options[i], value, sizeof(value)) && value[0] != '\0') {
            matched = true;
            break;
        }
    }
    Resolution r;
    if (matched)
        ResolveValue(value, &r);

    uint32_t cacheSpell = 0, cachePet = 0;
    if (r.target == Target::Spell) {
        cacheSpell = r.spellID;
        cachePet = r.isPet;
    } else if (r.target == Target::None && matched) {
        cacheSpell = kUnresolvedSpell;
    }

    auto &cache = Game::Ref<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL);
    auto &pet = Game::Ref<uint32_t>(entry, Offsets::OFF_MACRO_PRIMARY_SPELL_IS_PET);
    const bool changed = r.target != e.target || r.spellID != e.spellID ||
                         r.isPet != e.isPet || r.itemID != e.itemID ||
                         cache != cacheSpell || pet != cachePet;
    e.target = r.target;
    e.spellID = r.spellID;
    e.isPet = r.isPet;
    e.itemID = r.itemID;
    cache = cacheSpell;
    pet = cachePet;

    if (changed)
        Repaint(L, e.macroID);
}

// SuperCleveRoidMacros owns macro display when it is loaded and hasn't
// disabled itself: `CleveRoids` is its namespace table, `CleveRoids.disabled`
// its self-disable flag.
bool DetectYield(void *L) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "CleveRoids");
    Game::Lua::RawGet(L, Game::Lua::GLOBALS_INDEX);
    bool yield = false;
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE) {
        Game::Lua::PushString(L, "disabled");
        Game::Lua::RawGet(L, -2);
        const bool disabled = Game::Lua::Type(L, -1) == Game::Lua::TYPE_BOOLEAN &&
                              Game::Lua::ToBoolean(L, -1) != 0;
        yield = !disabled;
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
        if (e.dirty && e.kind != Kind::None && e.optionCount > 0)
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
    if (g_yielding)
        return;
    if (!CatchUp(L, now))
        return;

    const WorldState state = ReadWorldState();
    const bool stateChanged = state != g_lastState;
    g_lastState = state;

    for (Entry &e : g_entries) {
        if (e.kind == Kind::None || e.optionCount == 0)
            continue;
        const uint32_t interval = e.conditional ? kConditionalIntervalMs : kStaticIntervalMs;
        if ((e.conditional && stateChanged) ||
            Time::Clock::Elapsed(e.lastEvalMs, now) >= interval) {
            Evaluate(L, e, now);
        }
    }
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
    if (!g_yielding)
        CatchUp(L, now);
}

void OnMacroParsed(int /*macroEntry*/) {
    g_rescanPending = true;
}

// Lua-derived state (slash-command names, the SCRM latch) is reload-fragile;
// the engine also re-parses every macro after a reload.
void PrepareForReload() {
    g_rescanPending = true;
    g_slashNamesLoaded = false;
    g_yieldChecked = false;
}

const Tick::WorldTick::AutoSubscribe _tick{&Tick};
const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Spell::MacroPrimarySpell::PostParseAutoRegister _parsed{&OnMacroParsed};

} // namespace

bool Active() {
    return !g_yielding;
}

bool Lookup(uint32_t macroID, Info *out) {
    if (macroID == 0)
        return false;
    CatchUpIfPending();
    if (g_yielding)
        return false;
    for (const Entry &e : g_entries) {
        if (e.macroID != macroID || e.kind == Kind::None)
            continue;
        if (e.target != Target::None) {
            out->kind = e.kind;
            out->target = e.target;
            out->spellID = e.spellID;
            out->isPet = e.isPet;
            out->itemID = e.itemID;
            return true;
        }
        if (e.optionCount == 0) {
            // Bare directive with no `/cast` or `/use` line to draw from:
            // show whatever the engine's own parse resolved (a
            // `CastSpellByName("...")` line, for instance).
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
            return true;
        }
        return false;
    }
    return false;
}

bool ForSlot(int slot0, Info *out) {
    const uint32_t macroID = Action::Slot::MacroIDForSlot(slot0);
    return macroID != 0 && Lookup(macroID, out);
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
