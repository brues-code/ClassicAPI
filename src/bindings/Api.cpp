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
//
// Overrides — 4.3.4's OVERRIDEKEYBINDING: per key (case-insensitive), two
// lists (normal, priority) of {owner, command}; setting an owner's binding
// unlinks its old one from both and links the new one at the head of its
// priority's list, so the newest priority binding wins, then the newest
// normal one. 4.3.4 consults them inside its key lookup, before the saved
// binding, for every key it tries. 1.12's key dispatch (FUN_BINDING_KEY_DISPATCH)
// tries two keys — the pressed one, then the pressed one minus its first
// modifier — so the co-hook tries an override, then the saved binding
// (FUN_BINDINGS_GET_COMMAND_ACTION), for each in turn, and lets the engine
// run a saved binding it finds.
//
// Around whatever runs, the key-state override (Input::Modifier) hides the
// matched key's own modifiers, as 4.3.4's ExecKey does. Direct actions
// (Bindings::Command) run only for a key: the command co-hook acts while the
// manager's executing flag — set by the key dispatch, not by RunBinding — is
// up, which is 4.3.4's ExecKey / ExecCommand split.

#include "Game.h"
#include "Offsets.h"
#include "bindings/Command.h"
#include "input/Modifier.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <string>
#include <vector>

namespace Bindings::Api {

namespace {

using KeyDispatch_t = int(__fastcall *)(void *mgr, void *edx, const char *key, int isDown);
using CommandExecute_t = int(__fastcall *)(void *mgr, void *edx, const char *cmd, int isDown);
using GetCommandAction_t = const char *(__thiscall *)(void *mgr, const char *key);
using ScriptFn_t = int(__fastcall *)(void *L);
using ToBool_t = int(__fastcall *)(void *L, int idx, int dflt);

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

int ExecuteCommand(void *mgr, const char *command, int isDown) {
    if (Command::IsDirectAction(command))
        return Command::RunKey(command, isDown);
    return g_commandExecuteOriginal(mgr, nullptr, command, isDown);
}

// An override's command, run in the frame the engine dispatch gives a saved
// one: exec context saved and cleared, depth bumped, the manager's executing
// flag up (FUN_BINDING_KEY_DISPATCH, see Offsets.h).
int RunOverride(void *mgr, const char *command, int isDown) {
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

int __fastcall KeyDispatch_h(void *mgr, void *edx, const char *key, int isDown) {
    if (key == nullptr || *key == '\0')
        return g_keyDispatchOriginal(mgr, edx, key, isDown);

    // The engine's two keys: as pressed, then without its first modifier.
    const char *dash = std::strchr(key, '-');
    const char *candidates[2] = {key, (dash != nullptr && dash[1] != '\0') ? dash + 1 : nullptr};
    for (const char *candidate : candidates) {
        if (candidate == nullptr)
            break;
        if (const char *command = ActiveOverride(candidate)) {
            const Input::Modifier::KeyStateOverride keyState(candidate);
            return RunOverride(mgr, command, isDown);
        }
        if (SavedCommand(mgr, candidate) != nullptr) {
            const Input::Modifier::KeyStateOverride keyState(candidate);
            return g_keyDispatchOriginal(mgr, edx, key, isDown);
        }
    }
    return g_keyDispatchOriginal(mgr, edx, key, isDown);
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

bool LooseBool(void *L, int idx) {
    return reinterpret_cast<ToBool_t>(Offsets::FUN_LUA_TO_BOOLEAN_LOOSE)(L, idx, 0) != 0;
}

const char *OptString(void *L, int idx) {
    return Game::Lua::IsString(L, idx) ? Game::Lua::ToString(L, idx) : nullptr;
}

// SetBinding(key, command) through the engine's own Script_SetBinding: its
// key normalization, current-set write, UPDATE_BINDINGS and 1/nil result.
int SetBinding(void *L, const std::string &key, const std::string &command) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, key.c_str());
    Game::Lua::PushString(L, command.c_str());
    return reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_SET_BINDING)(L);
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
    SetOverrideBinding(owner, LooseBool(L, 2), Game::Lua::ToString(L, 3), OptString(L, 4));
    return 0;
}

int SetOverrideCommand(void *L, const char *prefix, const char *usage) {
    const void *owner = Owner(L);
    if (owner == nullptr || !Game::Lua::IsString(L, 3) || !Game::Lua::IsString(L, 4)) {
        Game::Lua::Error(L, usage);
        return 0;
    }
    const std::string command = std::string(prefix) + Game::Lua::ToString(L, 4);
    SetOverrideBinding(owner, LooseBool(L, 2), Game::Lua::ToString(L, 3), command.c_str());
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
    SetOverrideBinding(owner, LooseBool(L, 2), Game::Lua::ToString(L, 3), command.c_str());
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

// GetBindingAction(key [, checkOverride]): 4.3.4's GetCommandAction checks the
// override first when asked; the saved binding is the engine's own answer.
int __fastcall Script_GetBindingAction(void *L) {
    if (Game::Lua::IsString(L, 1) && LooseBool(L, 2)) {
        if (const char *command = ActiveOverride(Game::Lua::ToString(L, 1))) {
            Game::Lua::PushString(L, command);
            return 1;
        }
    }
    return reinterpret_cast<ScriptFn_t>(Offsets::FUN_SCRIPT_GET_BINDING_ACTION)(L);
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("SetBindingSpell", &Script_SetBindingSpell);
    Game::Lua::RegisterGlobalFunction("SetBindingItem", &Script_SetBindingItem);
    Game::Lua::RegisterGlobalFunction("SetBindingMacro", &Script_SetBindingMacro);
    Game::Lua::RegisterGlobalFunction("SetBindingClick", &Script_SetBindingClick);
    Game::Lua::RegisterGlobalFunction("SetOverrideBinding", &Script_SetOverrideBinding);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingSpell", &Script_SetOverrideBindingSpell);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingItem", &Script_SetOverrideBindingItem);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingMacro", &Script_SetOverrideBindingMacro);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingClick", &Script_SetOverrideBindingClick);
    Game::Lua::RegisterGlobalFunction("ClearOverrideBindings", &Script_ClearOverrideBindings);
    Game::Lua::RegisterGlobalFunction("GetBindingAction", &Script_GetBindingAction);
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
