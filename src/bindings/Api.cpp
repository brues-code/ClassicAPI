// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// ClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// The binding API 4.3.4 has and 1.12 lacks (decomp Ui/UIBindings.cpp,
// UIBindings_Scripts.cpp):
//
//   SetBindingSpell / Item / Macro / Click     — "SPELL x" etc. into the
//       current set through the engine's own SetBinding.
//   SetOverrideBinding[Spell / Item / Macro / Click], ClearOverrideBindings
//       — owner-scoped bindings in front of the saved ones, never saved.
//   GetBindingAction(key [, checkOverride])   — sees overrides when asked.
//   GetBindingByKey(key)                      — what a press of the key runs.
//
// Overrides — 4.3.4's OVERRIDEKEYBINDING: per key (case-insensitive), two
// lists (normal, priority) of {owner, command}; setting an owner's binding
// unlinks its old one from both and links the new one at the head of its
// priority's list, so the newest priority binding wins, then the newest
// normal one. 4.3.4 consults them inside its key lookup, before the saved
// binding, for every key it tries.
//
// Reduced keys — 4.3.4's GetReducedKeyBinding. 1.12's key dispatch
// (FUN_BINDING_KEY_DISPATCH) tries only two keys: the pressed one, then the
// pressed one minus its first modifier. 4.3.4 tries every subset of the
// modifiers (ALT-SHIFT-Q: ALT-SHIFT-Q, SHIFT-Q, ALT-Q, Q). The co-hook walks
// 4.3.4's list, tries an override, then the saved binding
// (FUN_BINDINGS_GET_COMMAND_ACTION), for each key in turn — skipping one whose
// command is unknown, as 4.3.4 does — and lets the engine run a saved binding
// it finds.
//
// Around a direct action, the key-state override (Input::Modifier) hides the
// matched key's own modifiers, as 4.3.4's ExecKey does; a Lua command sees
// the live state, as in 1.12 (see KeyDispatch_h). Direct actions
// (Bindings::Command) run only for a key: the command co-hook acts while the
// manager's executing flag — set by the key dispatch, not by RunBinding — is
// up, which is 4.3.4's ExecKey / ExecCommand split.

#include "Game.h"
#include "Offsets.h"
#include "bindings/Command.h"
#include "bindings/Context.h"
#include "input/Modifier.h"
#include "storm/HashTable.h"
#include "storm/String.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace Bindings::Api {

namespace {

using KeyDispatch_t = int(__fastcall *)(void *mgr, void *edx, const char *key, int isDown);
using CommandExecute_t = int(__fastcall *)(void *mgr, void *edx, const char *cmd, int isDown);
using GetCommandAction_t = const char *(__thiscall *)(void *mgr, const char *key);

KeyDispatch_t g_keyDispatchOriginal = nullptr;
CommandExecute_t g_commandExecuteOriginal = nullptr;

// --- override table (OVERRIDEKEYBINDING) ------------------------------------

struct Entry {
    const void *owner;
    std::string command;
};

struct OverrideKey {
    std::string key;
    std::list<Entry> lists[2]; // [0] normal, [1] priority; head = newest
};

std::vector<OverrideKey> g_overrides;

OverrideKey *FindKey(const char *key) {
    for (OverrideKey &entry : g_overrides)
        if (_stricmp(entry.key.c_str(), key) == 0)
            return &entry;
    return nullptr;
}

// OVERRIDEKEYBINDING::SetOverrideBinding.
void SetOverride(OverrideKey &binding, const void *owner, bool priority, const char *command) {
    for (auto &list : binding.lists)
        list.remove_if([owner](const Entry &entry) { return entry.owner == owner; });
    if (command != nullptr && *command != '\0')
        binding.lists[priority ? 1 : 0].push_front(Entry{owner, command});
}

// OVERRIDEKEYBINDING::GetActiveBinding.
const char *ActiveOverride(const char *key) {
    const OverrideKey *binding = FindKey(key);
    if (binding == nullptr)
        return nullptr;
    for (int i = 1; i >= 0; --i)
        if (!binding->lists[i].empty())
            return binding->lists[i].front().command.c_str();
    return nullptr;
}

// CGUIBindings::SetOverrideBinding.
void SetOverrideBinding(const void *owner, bool priority, const char *key, const char *command) {
    OverrideKey *binding = FindKey(key);
    if (binding == nullptr) {
        g_overrides.push_back(OverrideKey{key, {}});
        binding = &g_overrides.back();
    }
    SetOverride(*binding, owner, priority, command);
}

// --- key dispatch -------------------------------------------------------------

void *Manager() { return Game::Read<void *>(Offsets::VAR_BINDINGS_MANAGER); }

const char *SavedCommand(void *mgr, const char *key) {
    return reinterpret_cast<GetCommandAction_t>(Offsets::FUN_BINDINGS_GET_COMMAND_ACTION)(mgr,
                                                                                          key);
}

// Whether the command table has `command` — the walk the engine inlines in
// FUN_BINDING_COMMAND_EXECUTE (see OFF_BINDINGS_COMMAND_TABLE).
bool IsKnownCommand(void *mgr, const char *command) {
    return Storm::HashTable::FindStri(reinterpret_cast<uintptr_t>(mgr) + Offsets::OFF_BINDINGS_COMMAND_TABLE +
                                          Offsets::OFF_HASHTABLE_SLOTS,
                                      command) != nullptr;
}

// CGUIBindings::GetKeyBinding: the key's active override, else its saved
// binding — the slot of the first active binding context that holds one
// (Bindings::Context, 3.80.2's resolver), else the engine's table — and none
// when that command is neither a direct action nor a known command (a saved
// one is never checked when set; 3.80.2 does not retry a lower context
// either). 4.3.4's retry under the generic modifier names (LSHIFT- → SHIFT-)
// is left out: 1.12 builds chords from ALT-, CTRL- and SHIFT- only, so a
// pressed key is already generic.
struct KeyBinding {
    const char *command = nullptr;
    bool runsHere = false; // not in the engine's table, so its dispatch cannot run it
};

KeyBinding FindKeyBinding(void *mgr, const char *key) {
    KeyBinding binding{ActiveOverride(key), true};
    if (binding.command == nullptr)
        binding = {Context::Resolve(key), true};
    if (binding.command == nullptr)
        binding = {SavedCommand(mgr, key), false};
    if (binding.command == nullptr ||
        !(Command::IsDirectAction(binding.command) || IsKnownCommand(mgr, binding.command)))
        return {};
    return binding;
}

// CGUIBindings::GetReducedKeyBinding: up to three leading modifiers split off
// at '-', then the key with each subset of them in 4.3.4's fixed order (bit j
// keeps modifier j) — ALT-SHIFT-Q tries ALT-SHIFT-Q, SHIFT-Q, ALT-Q, Q. The
// first reduced key with a binding wins and is left in `reduced`.
KeyBinding FindReducedKeyBinding(void *mgr, const char *key, std::string &reduced) {
    std::string modifiers[3];
    size_t count = 0;
    const char *rest = key;
    while (count < 3) {
        const char *separator = std::strchr(rest, '-');
        if (separator == nullptr || separator == rest)
            break;
        modifiers[count++].assign(rest, separator - rest + 1);
        rest = separator + 1;
    }

    static constexpr uint8_t kCombinations[4][9] = {
        {1, 0},
        {2, 1, 0},
        {4, 3, 2, 1, 0},
        {8, 7, 6, 5, 3, 4, 2, 1, 0},
    };
    for (uint8_t i = 0; i < kCombinations[count][0]; ++i) {
        reduced.clear();
        for (size_t j = 0; j < count; ++j)
            if ((1u << j) & kCombinations[count][i + 1])
                reduced += modifiers[j];
        reduced += rest;
        const KeyBinding binding = FindKeyBinding(mgr, reduced.c_str());
        if (binding.command != nullptr)
            return binding;
    }
    return {};
}

int ExecuteCommand(void *mgr, const char *command, int isDown) {
    if (Command::IsDirectAction(command))
        return Command::RunKey(command, isDown);
    return g_commandExecuteOriginal(mgr, nullptr, command, isDown);
}

// A command the engine's table does not hold (an override, a context slot's),
// run in the frame the engine dispatch gives a saved one: exec context saved
// and cleared, depth bumped, the manager's executing flag up
// (FUN_BINDING_KEY_DISPATCH, see Offsets.h).
int RunHere(void *mgr, const char *command, int isDown) {
    const std::string copy = command; // the command may clear its own override
    auto &context = Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    auto &depth = Game::Ref<int32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH);
    auto &executing = Game::Ref<uint32_t>(mgr, Offsets::OFF_BINDING_MANAGER_EXECUTING);
    const uint32_t saved = context;
    if (++depth != 0)
        context = 0;
    executing = 1;
    const int result = ExecuteCommand(mgr, copy.c_str(), isDown);
    executing = 0;
    if (depth != 0)
        context = saved;
    depth = depth - 1 > 0 ? depth - 1 : 0;
    return result;
}

// CGUIBindings::ExecKey. A saved binding is run by the engine, handed the
// reduced key: it uses the key only for its table lookup, and finds it as the
// first of its own two. No binding runs nothing, as in 4.3.4.
int __fastcall KeyDispatch_h(void *mgr, void *edx, const char *key, int isDown) {
    if (key == nullptr || *key == '\0')
        return g_keyDispatchOriginal(mgr, edx, key, isDown);

    std::string reduced;
    const KeyBinding binding = FindReducedKeyBinding(mgr, key, reduced);
    if (binding.command == nullptr)
        return 0;
    // 4.3.4 hides the key's modifiers around every command. 1.12 Lua bindings
    // were written against the live state: an addon that binds SHIFT-E and
    // reads IsShiftKeyDown() inside its handler (Puppeteer) then sees nothing
    // held. Only the direct actions, which 1.12 never had, get the override.
    std::optional<Input::Modifier::KeyStateOverride> keyState;
    if (Command::IsDirectAction(binding.command))
        keyState.emplace(reduced.c_str());
    if (binding.runsHere)
        return RunHere(mgr, binding.command, isDown);
    return g_keyDispatchOriginal(mgr, edx, reduced.c_str(), isDown);
}

// Direct actions only for a key (executing flag up); RunBinding reaches the
// engine's command table, which has none of them — as in 4.3.4.
int __fastcall CommandExecute_h(void *mgr, void *edx, const char *command, int isDown) {
    if (Game::Read<uint32_t>(mgr, Offsets::OFF_BINDING_MANAGER_EXECUTING) != 0 &&
        Command::IsDirectAction(command))
        return Command::RunKey(command, isDown);
    return g_commandExecuteOriginal(mgr, edx, command, isDown);
}

// --- Lua ----------------------------------------------------------------------


const char *OptString(void *L, int idx) {
    return Game::Lua::IsString(L, idx) ? Game::Lua::ToString(L, idx) : nullptr;
}

// SetBinding(key, command) through the engine's own Script_SetBinding: its
// key normalization, current-set write, UPDATE_BINDINGS and 1/nil result.
int SetBinding(void *L, const std::string &key, const std::string &command) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, key.c_str());
    Game::Lua::PushString(L, command.c_str());
    return Game::Lua::CallScript(Offsets::FUN_SCRIPT_SET_BINDING, L);
}

int SetBindingCommand(void *L, const char *prefix, const char *usage) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, usage);
        return 0;
    }
    return SetBinding(L, Game::Lua::ToString(L, 1),
                      std::string(prefix) + Game::Lua::ToString(L, 2));
}

int __fastcall Script_SetBindingSpell(void *L) {
    return SetBindingCommand(L, "SPELL ", "Usage: SetBindingSpell(\"KEY\", \"spellname\")");
}
int __fastcall Script_SetBindingItem(void *L) {
    return SetBindingCommand(L, "ITEM ", "Usage: SetBindingItem(\"KEY\", \"itemname\")");
}
int __fastcall Script_SetBindingMacro(void *L) {
    return SetBindingCommand(L, "MACRO ",
                             "Usage: SetBindingMacro(\"KEY\", \"macroname\"|macroid)");
}

std::string ClickCommand(const char *button, const char *mouseButton) {
    return std::string("CLICK ") + button + ":" +
           (mouseButton != nullptr ? mouseButton : "LeftButton");
}

int __fastcall Script_SetBindingClick(void *L) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(
            L, "Usage: SetBindingClick(\"KEY\", \"buttonName\"[, \"mouseButton\"])");
        return 0;
    }
    return SetBinding(L, Game::Lua::ToString(L, 1),
                      ClickCommand(Game::Lua::ToString(L, 2), OptString(L, 3)));
}

// The owner argument: a frame, as 4.3.4 requires.
const void *Owner(void *L) { return Game::Lua::ResolveFrame(L, 1, /*raiseError*/ false); }

int __fastcall Script_SetOverrideBinding(void *L) {
    const void *owner = Owner(L);
    if (owner == nullptr || !Game::Lua::IsString(L, 3)) {
        Game::Lua::Error(L, "Usage: SetOverrideBinding(owner, isPriority, \"KEY\"[, \"COMMAND\"])");
        return 0;
    }
    SetOverrideBinding(owner, Game::Lua::ToFlag(L, 2), Game::Lua::ToString(L, 3), OptString(L, 4));
    return 0;
}

int SetOverrideCommand(void *L, const char *prefix, const char *usage) {
    const void *owner = Owner(L);
    if (owner == nullptr || !Game::Lua::IsString(L, 3) || !Game::Lua::IsString(L, 4)) {
        Game::Lua::Error(L, usage);
        return 0;
    }
    const std::string command = std::string(prefix) + Game::Lua::ToString(L, 4);
    SetOverrideBinding(owner, Game::Lua::ToFlag(L, 2), Game::Lua::ToString(L, 3), command.c_str());
    return 0;
}

int __fastcall Script_SetOverrideBindingSpell(void *L) {
    return SetOverrideCommand(
        L, "SPELL ", "Usage: SetOverrideBindingSpell(owner, isPriority, \"KEY\", \"spellname\")");
}
int __fastcall Script_SetOverrideBindingItem(void *L) {
    return SetOverrideCommand(
        L, "ITEM ", "Usage: SetOverrideBindingItem(owner, isPriority, \"KEY\", \"itemname\")");
}
int __fastcall Script_SetOverrideBindingMacro(void *L) {
    return SetOverrideCommand(
        L, "MACRO ",
        "Usage: SetOverrideBindingMacro(owner, isPriority, \"KEY\", \"macroname\"|macroid)");
}

int __fastcall Script_SetOverrideBindingClick(void *L) {
    const void *owner = Owner(L);
    if (owner == nullptr || !Game::Lua::IsString(L, 3) || !Game::Lua::IsString(L, 4)) {
        Game::Lua::Error(L, "Usage: SetOverrideBindingClick(owner, isPriority, \"KEY\", "
                            "\"buttonName\"[, \"mouseButton\"])");
        return 0;
    }
    const std::string command = ClickCommand(Game::Lua::ToString(L, 4), OptString(L, 5));
    SetOverrideBinding(owner, Game::Lua::ToFlag(L, 2), Game::Lua::ToString(L, 3), command.c_str());
    return 0;
}

int __fastcall Script_ClearOverrideBindings(void *L) {
    const void *owner = Owner(L);
    if (owner == nullptr) {
        Game::Lua::Error(L, "Usage: ClearOverrideBindings(owner)");
        return 0;
    }
    for (OverrideKey &binding : g_overrides)
        SetOverride(binding, owner, false, nullptr);
    return 0;
}

// GetBindingAction(key [, checkOverride, context]): 4.3.4's GetCommandAction
// checks the override first when asked. Then 3.80.2's reads the slot of the
// given context (FUN_14124EE30): no active-context walk, None by default,
// which is the engine's own answer.
int __fastcall Script_GetBindingAction(void *L) {
    if (Game::Lua::IsString(L, 1)) {
        const char *key = Game::Lua::ToString(L, 1);
        if (Game::Lua::ToFlag(L, 2)) {
            if (const char *command = ActiveOverride(key)) {
                Game::Lua::PushString(L, command);
                return 1;
            }
        }
        if (const int context = Context::OptionalArg(L, 3); context > 0) {
            const char *command = Context::Slot(key, context);
            Game::Lua::PushString(L, command != nullptr ? command : "");
            return 1;
        }
    }
    return Game::Lua::CallScript(Offsets::FUN_SCRIPT_GET_BINDING_ACTION, L);
}

// GetBindingByKey(key): the command a press of `key` runs — the reduced-key
// lookup ExecKey makes, overrides included.
int __fastcall Script_GetBindingByKey(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: GetBindingByKey(\"action\"[, mode])");
        return 0;
    }
    std::string reduced;
    const KeyBinding binding = FindReducedKeyBinding(Manager(), Game::Lua::ToString(L, 1), reduced);
    if (binding.command == nullptr)
        return 0;
    Game::Lua::PushString(L, binding.command);
    return 1;
}

const Game::Doc::Field kGetBindingByKeyArgs[] = {
    Game::Doc::Req("key", "string", "A key with its modifiers, such as \"ALT-SHIFT-Q\"."),
};
const Game::Doc::Field kGetBindingByKeyRets[] = {
    Game::Doc::Opt("command", "string", nullptr,
                   "The command a press of the key runs, else nil. When the key itself has no "
                   "binding, the key with fewer modifiers is tried."),
};
const Game::Doc::Function kGetBindingByKey{"Returns the command that a key press runs.",
                                           kGetBindingByKeyArgs, kGetBindingByKeyRets,
                                           "BindingsGlobals"};

const Game::Doc::Field kSetBindingRets[] = {
    Game::Doc::Opt("ok", "number", nullptr, "1 when the binding changed, otherwise nil."),
};
const Game::Doc::Field kSetBindingSpellArgs[] = {
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("spell", "string", "A spell name."),
};
const Game::Doc::Function kSetBindingSpell{
    "Binds a key in the current binding set to cast a spell.", kSetBindingSpellArgs,
    kSetBindingRets, "BindingsGlobals"};
const Game::Doc::Field kSetBindingItemArgs[] = {
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("item", "string", "An item name."),
};
const Game::Doc::Function kSetBindingItem{
    "Binds a key in the current binding set to use an item.", kSetBindingItemArgs,
    kSetBindingRets, "BindingsGlobals"};
const Game::Doc::Field kSetBindingMacroArgs[] = {
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("macro", "string", "A macro name, or a macro index as a number."),
};
const Game::Doc::Function kSetBindingMacro{
    "Binds a key in the current binding set to run a macro.", kSetBindingMacroArgs,
    kSetBindingRets, "BindingsGlobals"};
const Game::Doc::Field kSetBindingClickArgs[] = {
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("buttonName", "string", "The global name of the button."),
    Game::Doc::Opt("mouseButton", "string", "\"LeftButton\""),
};
const Game::Doc::Function kSetBindingClick{
    "Binds a key in the current binding set to click a button.", kSetBindingClickArgs,
    kSetBindingRets, "BindingsGlobals"};

const Game::Doc::Field kSetOverrideBindingArgs[] = {
    Game::Doc::Req("owner", "SimpleFrame"),
    Game::Doc::Req("isPriority", "bool"),
    Game::Doc::Req("key", "string",
                   "Not case sensitive; write the modifiers in the order ALT-CTRL-SHIFT."),
    Game::Doc::Opt("command", "string", nullptr,
                   "Nil or an empty string removes the owner's override for the key."),
};
const Game::Doc::Function kSetOverrideBinding{
    "Sets a temporary binding for a key that replaces its saved binding until it is removed.",
    kSetOverrideBindingArgs, {}, "BindingsGlobals"};
const Game::Doc::Field kSetOverrideBindingSpellArgs[] = {
    Game::Doc::Req("owner", "SimpleFrame"),
    Game::Doc::Req("isPriority", "bool"),
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("spell", "string", "A spell name."),
};
const Game::Doc::Function kSetOverrideBindingSpell{
    "Sets a temporary binding for a key that casts a spell.", kSetOverrideBindingSpellArgs, {},
    "BindingsGlobals"};
const Game::Doc::Field kSetOverrideBindingItemArgs[] = {
    Game::Doc::Req("owner", "SimpleFrame"),
    Game::Doc::Req("isPriority", "bool"),
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("item", "string", "An item name."),
};
const Game::Doc::Function kSetOverrideBindingItem{
    "Sets a temporary binding for a key that uses an item.", kSetOverrideBindingItemArgs, {},
    "BindingsGlobals"};
const Game::Doc::Field kSetOverrideBindingMacroArgs[] = {
    Game::Doc::Req("owner", "SimpleFrame"),
    Game::Doc::Req("isPriority", "bool"),
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("macro", "string", "A macro name, or a macro index as a number."),
};
const Game::Doc::Function kSetOverrideBindingMacro{
    "Sets a temporary binding for a key that runs a macro.", kSetOverrideBindingMacroArgs, {},
    "BindingsGlobals"};
const Game::Doc::Field kSetOverrideBindingClickArgs[] = {
    Game::Doc::Req("owner", "SimpleFrame"),
    Game::Doc::Req("isPriority", "bool"),
    Game::Doc::Req("key", "string"),
    Game::Doc::Req("buttonName", "string", "The global name of the button."),
    Game::Doc::Opt("mouseButton", "string", "\"LeftButton\""),
};
const Game::Doc::Function kSetOverrideBindingClick{
    "Sets a temporary binding for a key that clicks a button.", kSetOverrideBindingClickArgs,
    {}, "BindingsGlobals"};
const Game::Doc::Field kClearOverrideBindingsArgs[] = {
    Game::Doc::Req("owner", "SimpleFrame"),
};
const Game::Doc::Function kClearOverrideBindings{"Removes all override bindings of an owner.",
                                                 kClearOverrideBindingsArgs, {},
                                                 "BindingsGlobals"};

const Game::Doc::Field kGetBindingActionArgs[] = {
    Game::Doc::Req("key", "string"),
    Game::Doc::Opt("checkOverride", "bool", "false",
                   "True returns an active override binding first."),
    Game::Doc::Opt("context", "BindingContext", "None",
                   "The binding context whose binding to return, active or not."),
};
const Game::Doc::Field kGetBindingActionRets[] = {
    Game::Doc::Req("action", "string", "An empty string when nothing is bound."),
};
const Game::Doc::Function kGetBindingAction{"Returns the command bound to a key.",
                                            kGetBindingActionArgs, kGetBindingActionRets,
                                            "BindingsGlobals"};

// The engine's binding sets (Offsets::BINDING_SET_*), as retail names them.
const Game::Lua::EnumIntegerEntry kBindingSet[] = {
    {"Default", Offsets::BINDING_SET_DEFAULT},
    {"Account", Offsets::BINDING_SET_ACCOUNT},
    {"Character", Offsets::BINDING_SET_CHARACTER},
    {"Current", Offsets::BINDING_SET_CURRENT},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "BindingSet", kBindingSet, "KeyBindings");
    Game::Lua::RegisterGlobalFunction("GetBindingByKey", &Script_GetBindingByKey,
                                      &kGetBindingByKey);
    Game::Lua::RegisterGlobalFunction("SetBindingSpell", &Script_SetBindingSpell,
                                      &kSetBindingSpell);
    Game::Lua::RegisterGlobalFunction("SetBindingItem", &Script_SetBindingItem,
                                      &kSetBindingItem);
    Game::Lua::RegisterGlobalFunction("SetBindingMacro", &Script_SetBindingMacro,
                                      &kSetBindingMacro);
    Game::Lua::RegisterGlobalFunction("SetBindingClick", &Script_SetBindingClick,
                                      &kSetBindingClick);
    Game::Lua::RegisterGlobalFunction("SetOverrideBinding", &Script_SetOverrideBinding,
                                      &kSetOverrideBinding);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingSpell", &Script_SetOverrideBindingSpell,
                                      &kSetOverrideBindingSpell);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingItem", &Script_SetOverrideBindingItem,
                                      &kSetOverrideBindingItem);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingMacro", &Script_SetOverrideBindingMacro,
                                      &kSetOverrideBindingMacro);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingClick", &Script_SetOverrideBindingClick,
                                      &kSetOverrideBindingClick);
    Game::Lua::RegisterGlobalFunction("ClearOverrideBindings", &Script_ClearOverrideBindings,
                                      &kClearOverrideBindings);
    Game::Lua::RegisterGlobalFunction("GetBindingAction", &Script_GetBindingAction,
                                      &kGetBindingAction);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// Owners are frames of the Lua state being torn down; their overrides go with it.
void PrepareForReload() { g_overrides.clear(); }
const Game::ReloadAutoRegister _reload{&PrepareForReload};

const Game::HookAutoRegister _hookKeyDispatch{Offsets::FUN_BINDING_KEY_DISPATCH,
                                              reinterpret_cast<void *>(&KeyDispatch_h),
                                              reinterpret_cast<void **>(&g_keyDispatchOriginal)};

const Game::HookAutoRegister _hookCommandExecute{
    Offsets::FUN_BINDING_COMMAND_EXECUTE, reinterpret_cast<void *>(&CommandExecute_h),
    reinterpret_cast<void **>(&g_commandExecuteOriginal)};

} // namespace

} // namespace Bindings::Api
