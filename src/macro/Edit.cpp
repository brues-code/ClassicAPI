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

// `C_Macro.CreateMacro` / `C_Macro.EditMacro` — create/edit macros whose
// icon is given as a texture STRING (a full `Interface\Icons\<name>` path
// or a bare `<name>` basename), not a `GetMacroIconInfo` index.
//
//   C_Macro.CreateMacro(name, iconTexture, body, isCharacterMacro) -> index
//   C_Macro.EditMacro(index, name, iconTexture, body)             -> index
//
// Why this exists: vanilla `CreateMacro`/`EditMacro` take an icon INDEX
// into the `GetMacroIconInfo` list, and that list omits `INV_*` item
// icons entirely — so item icons are unreachable through the stock API
// even though the engine stores and renders them fine. The macro struct
// keeps the icon as a bare basename at `entry + OFF_MACRO_ICON` and
// `GetMacroInfo` reads it back as `Interface\Icons\%s`, so ANY texture
// living under `Interface\Icons\` round-trips once you can write the
// basename directly. These two functions do exactly that, unblocking a
// pfUI macro-UI remodel that wants arbitrary icons.
//
// The legacy globals `CreateMacro`/`EditMacro` are left untouched
// (index-only) — no overloading; string-icon callers opt in via the
// `C_Macro` namespace.
//
// Implementation: both go straight to the engine's own workers
// (`FUN_MACRO_CREATE` / `FUN_MACRO_EDIT`, see [[src/Offsets.h]]), which do
// the bounded SStrCopy into the struct, re-run the primary-spell parser,
// set the persist-on-logout dirty flag, and refresh the action bars.
// We only translate the icon string to a basename and, for edits,
// preserve the entry's `local` flag (the edit worker rewrites it
// unconditionally).
//
// Icon input is STRING-only by design (that's the whole point — item
// icons have no index). A numeric icon arg is treated as "no icon" on
// create and "leave unchanged" on edit; callers who want the numeric
// index path already have the legacy globals.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Macro::Edit {

namespace {

using MacroCreate_t = uint32_t(__fastcall *)(const char *name,
                                             const char *iconBasename,
                                             const char *body, uint32_t local,
                                             int perCharacter);
using MacroEdit_t = void(__fastcall *)(uint32_t macroID, const char *name,
                                       const char *iconBasename,
                                       const char *body, uint32_t flag);
using MacroIdToSlot_t = uint32_t(__fastcall *)(uint32_t macroID);
using MacroNameToSlot_t = uint32_t(__fastcall *)(const char *name);
using MacroSlotToEntry_t = uint8_t *(__fastcall *)(uint32_t slot0Based);

constexpr uint32_t kSlotMiss = 0xFFFFFFFFu;

// Last path component of `s` (after the final `\` or `/`). Turns
// "Interface\\Icons\\INV_Sword_25" and a bare "INV_Sword_25" alike into
// the basename the macro struct stores; the engine re-prefixes it with
// `Interface\Icons\` on read. Aliases into `s` — no copy.
const char *IconBasename(const char *s) {
    const char *bn = s;
    for (const char *p = s; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/')
            bn = p + 1;
    }
    return bn;
}

// The icon to store, or nullptr (create: no icon; edit: leave unchanged).
// String-only — see the file header.
const char *IconArg(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return IconBasename(Game::Lua::ToString(L, idx));
}

// A body/name string arg, or nullptr when absent (edit: leave unchanged).
const char *OptString(void *L, int idx) {
    return Game::Lua::IsString(L, idx) ? Game::Lua::ToString(L, idx) : nullptr;
}

uint8_t *EntryForSlot(uint32_t slot0) {
    auto fn = reinterpret_cast<MacroSlotToEntry_t>(
        Offsets::FUN_MACRO_SLOT_TO_ENTRY);
    return fn(slot0);
}

// C_Macro.CreateMacro(name, iconTexture, body, isCharacterMacro) -> index
// Returns the new macro's 1-based slot, or nil when creation fails (empty
// name, or the per-scope 18-macro list is full).
int __fastcall Script_CreateMacro(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(
            L, "Usage: C_Macro.CreateMacro(name, iconTexture, body, "
               "isCharacterMacro)");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    const char *icon = IconArg(L, 2);
    const char *body = OptString(L, 3);
    const int perCharacter = Game::Lua::ToBoolean(L, 4);

    auto create = reinterpret_cast<MacroCreate_t>(Offsets::FUN_MACRO_CREATE);
    const uint32_t macroID = create(name, icon, body, /*local*/ 0, perCharacter);
    if (macroID == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }

    auto idToSlot =
        reinterpret_cast<MacroIdToSlot_t>(Offsets::FUN_MACRO_ID_TO_SLOT);
    const uint32_t slot0 = idToSlot(macroID);
    if (slot0 == kSlotMiss) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(slot0 + 1));
    return 1;
}

// C_Macro.EditMacro(index, name, iconTexture, body) -> index
// `index` is a 1-based slot number or a macro name. Any of name /
// iconTexture / body left nil is unchanged. Returns the macro's 1-based
// slot, or nil when `index` doesn't resolve to an existing macro.
int __fastcall Script_EditMacro(void *L) {
    uint32_t slot0;
    if (Game::Lua::IsNumber(L, 1)) {
        slot0 = static_cast<uint32_t>(static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1);
    } else if (Game::Lua::IsString(L, 1)) {
        auto nameToSlot = reinterpret_cast<MacroNameToSlot_t>(
            Offsets::FUN_MACRO_NAME_TO_SLOT);
        slot0 = nameToSlot(Game::Lua::ToString(L, 1));
    } else {
        Game::Lua::Error(
            L, "Usage: C_Macro.EditMacro(index, name, iconTexture, body)");
        return 0;
    }

    // One validation covers both a bad slot number and a name miss:
    // FUN_MACRO_NAME_TO_SLOT returns 0xFFFFFFFF on miss, which the slot
    // resolver bounds-rejects to NULL just like an out-of-range index.
    uint8_t *entry = EntryForSlot(slot0);
    if (entry == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    const uint32_t macroID = *reinterpret_cast<const uint32_t *>(entry);
    // The edit worker rewrites entry+OFF_MACRO_LOCAL_FLAG unconditionally;
    // read the current value and hand it back so an edit that only touches
    // name/icon/body leaves the flag genuinely unchanged.
    const uint32_t flag = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_MACRO_LOCAL_FLAG);

    const char *name = OptString(L, 2);
    const char *icon = IconArg(L, 3);
    const char *body = OptString(L, 4);

    auto edit = reinterpret_cast<MacroEdit_t>(Offsets::FUN_MACRO_EDIT);
    edit(macroID, name, icon, body, flag);

    Game::Lua::PushNumber(L, static_cast<double>(slot0 + 1));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kCreateArgs[] = {
    Game::Doc::Req("name", "string"),
    Game::Doc::Opt("iconTexture", "string", nullptr,
                   "An icon path or a bare icon name; item icons work too."),
    Game::Doc::Opt("body", "string", nullptr),
    Game::Doc::Opt("isCharacterMacro", "bool", "false"),
};
const Game::Doc::Field kIndexRets[] = {
    Game::Doc::Opt("index", "luaIndex", nullptr, "Nil when the macro could not be made."),
};
const Game::Doc::Function kCreateMacro{
    "Creates a macro whose icon is given as a texture name instead of an icon index.",
    kCreateArgs, kIndexRets, nullptr, true};

const Game::Doc::Field kEditArgs[] = {
    Game::Doc::Req("index", "luaIndex", "A macro index, or a macro name."),
    Game::Doc::Opt("name", "string", nullptr, "Nil keeps the name."),
    Game::Doc::Opt("iconTexture", "string", nullptr,
                   "An icon path or a bare icon name; nil keeps the icon."),
    Game::Doc::Opt("body", "string", nullptr, "Nil keeps the body."),
};
const Game::Doc::Field kEditRets[] = {
    Game::Doc::Opt("index", "luaIndex", nullptr, "Nil when no such macro exists."),
};
const Game::Doc::Function kEditMacro{
    "Changes a macro's name, icon or body; the icon is given as a texture name.",
    kEditArgs, kEditRets, nullptr, true};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Macro", "CreateMacro",
                                     &Script_CreateMacro, &kCreateMacro);
    Game::Lua::RegisterTableFunction("C_Macro", "EditMacro", &Script_EditMacro, &kEditMacro);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Edit
