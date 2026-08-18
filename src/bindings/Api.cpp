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

// Backports the direct-action and override binding APIs introduced in 2.0.
// Normal bindings are stored by vanilla's SetBinding implementation using the
// later-client SPELL/ITEM/MACRO/CLICK command strings. The resolved-command
// hook executes those four command families; every native binding command is
// passed through unchanged.
//
// Override bindings live in a separate owner-scoped layer in front of the
// native key lookup. They are deliberately never written to the saved binding
// table, so clearing an owner immediately reveals the next override or the
// unchanged native binding underneath it.

#include "Game.h"
#include "Offsets.h"
#include "macro/Execute.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bindings::Api {

namespace {

constexpr const char kSpellPrefix[] = "SPELL ";
constexpr const char kItemPrefix[] = "ITEM ";
constexpr const char kMacroPrefix[] = "MACRO ";
constexpr const char kClickPrefix[] = "CLICK ";
constexpr const char kDefaultMouseButton[] = "LeftButton";

using ScriptSetBinding_t = int(__fastcall *)(void *L);
using KeyDispatch_t = int(__thiscall *)(void *mgr, const char *key, int isDown);
using CommandExecute_t = int(__thiscall *)(void *mgr, const char *cmd, int isDown);

KeyDispatch_t KeyDispatch_o = nullptr;
CommandExecute_t CommandExecute_o = nullptr;

enum class CommandType {
    None,
    Spell,
    Item,
    Macro,
    Click,
};

struct ParsedCommand {
    CommandType type = CommandType::None;
    const char *argument = nullptr;
};

struct OverrideEntry {
    const void *owner = nullptr;
    std::string key;
    std::string command;
    bool priority = false;
    uint64_t sequence = 0;
};

std::vector<OverrideEntry> g_overrides;
std::unordered_map<std::string, std::string> g_activeOverrides;
uint64_t g_nextSequence = 0;

// Normalizes a binding key to the engine's own canonical form so a stored
// override matches the string FUN_BINDING_KEY_DISPATCH builds at keypress. The
// engine's key-string builder (FUN_004b6630) always emits modifiers in the
// fixed order ALT-CTRL-SHIFT- ahead of the base key, regardless of the order
// the caller passed them; vanilla SetBinding never reorders, so the override
// layer does it here. Both the store and the lookup run through this, so they
// stay consistent while also matching the engine's keypress form.
std::string CanonicalKey(const char *key) {
    std::string result = key ? key : "";
    for (char &character : result) {
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    }

    // Strip the three modifier prefixes in whatever order/repetition they
    // appear, then re-emit them in the engine's fixed order. Prefix-stripping
    // (rather than splitting on '-') keeps the minus base key correct, e.g.
    // "SHIFT--" -> modifier SHIFT + base "-".
    bool alt = false, ctrl = false, shift = false;
    size_t base = 0;
    for (bool matched = true; matched;) {
        matched = false;
        if (result.compare(base, 4, "ALT-") == 0) {
            alt = true;
            base += 4;
            matched = true;
        } else if (result.compare(base, 5, "CTRL-") == 0) {
            ctrl = true;
            base += 5;
            matched = true;
        } else if (result.compare(base, 6, "SHIFT-") == 0) {
            shift = true;
            base += 6;
            matched = true;
        }
    }

    if (!alt && !ctrl && !shift)
        return result;

    std::string canonical;
    if (alt)
        canonical += "ALT-";
    if (ctrl)
        canonical += "CTRL-";
    if (shift)
        canonical += "SHIFT-";
    canonical += result.substr(base);

    return canonical;
}

std::string MakeCommand(const char *prefix, const char *arg) {
    std::string cmd = prefix;
    cmd += arg;
    return cmd;
}

std::string MakeClickCommand(const char *btnName, const char *mouseBtn) {
    std::string cmd = MakeCommand(kClickPrefix, btnName);
    if (mouseBtn != nullptr) {
        cmd += ':';
        cmd += mouseBtn;
    }

    return cmd;
}

ParsedCommand ParseCommand(const char *cmd) {
    if (cmd == nullptr)
        return {};

    struct PrefixEntry {
        const char *prefix;
        size_t length;
        CommandType type;
    };

    static constexpr PrefixEntry prefixes[] = {
        {kSpellPrefix, sizeof(kSpellPrefix) - 1, CommandType::Spell},
        {kItemPrefix, sizeof(kItemPrefix) - 1, CommandType::Item},
        {kMacroPrefix, sizeof(kMacroPrefix) - 1, CommandType::Macro},
        {kClickPrefix, sizeof(kClickPrefix) - 1, CommandType::Click},
    };

    for (const PrefixEntry &entry : prefixes) {
        if (std::strncmp(cmd, entry.prefix, entry.length) == 0)
            return {entry.type, cmd + entry.length};
    }

    return {};
}

bool CallGlobalString(void *L, const char *func, const char *arg) {
    const int top = Game::Lua::GetTop(L);
    if (!Game::Lua::PushGlobalFunction(L, func)) {
        Game::Lua::SetTop(L, top);
        return false;
    }

    Game::Lua::PushString(L, arg);
    const bool ok = Game::Lua::PCall(L, 1, 0, 0) == 0;
    Game::Lua::SetTop(L, top);

    return ok;
}

bool CallTableString(void *L, const char *table, const char *func, const char *arg) {
    const int top = Game::Lua::GetTop(L);

    Game::Lua::PushString(L, table);
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, top);
        return false;
    }

    const int tableIndex = Game::Lua::GetTop(L);

    Game::Lua::PushString(L, func);
    Game::Lua::GetTable(L, tableIndex);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, top);
        return false;
    }

    Game::Lua::PushString(L, arg);
    const bool ok = Game::Lua::PCall(L, 1, 0, 0) == 0;
    Game::Lua::SetTop(L, top);

    return ok;
}

void InvokeButtonClick(void *L, const char *btnName, size_t btnNameLength, const char *mouseBtn) {
    const int top = Game::Lua::GetTop(L);

    Game::Lua::PushLString(L, btnName, static_cast<unsigned int>(btnNameLength));
    Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, top);
        return;
    }

    const int frameIndex = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "Click");
    Game::Lua::GetTable(L, frameIndex);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION) {
        Game::Lua::PushValue(L, frameIndex);
        Game::Lua::PushString(L, mouseBtn);
        Game::Lua::PCall(L, 2, 0, 0);
    }

    Game::Lua::SetTop(L, top);
}

void ExecuteClick(void *L, const char *arg) {
    const char *separator = std::strrchr(arg, ':');
    if (separator == arg || (separator != nullptr && separator[1] == '\0'))
        return;

    const char *mouseBtn = kDefaultMouseButton;
    size_t btnNameLength = std::strlen(arg);
    if (separator != nullptr) {
        btnNameLength = static_cast<size_t>(separator - arg);
        mouseBtn = separator + 1;
    }

    if (btnNameLength == 0)
        return;

    InvokeButtonClick(L, arg, btnNameLength, mouseBtn);
}

// Returns true for every recognized direct-action command, including malformed
// or stale commands. Vanilla cannot execute these namespaces, so a bad command
// is consumed as a no-op instead of falling through to an unrelated handler.
bool ExecuteSpecialCommand(const char *cmd, int isDown) {
    const ParsedCommand parsed = ParseCommand(cmd);
    if (parsed.type == CommandType::None)
        return false;

    if (!isDown || parsed.argument == nullptr || *parsed.argument == '\0')
        return true;

    void *L = Game::Lua::State();
    if (L == nullptr)
        return true;

    switch (parsed.type) {
    case CommandType::Spell:
        CallGlobalString(L, "CastSpellByName", parsed.argument);
        break;
    case CommandType::Item:
        CallTableString(L, "C_Item", "UseItemByName", parsed.argument);
        break;
    case CommandType::Macro:
        Macro::Execute::Saved(L, parsed.argument);
        break;
    case CommandType::Click:
        ExecuteClick(L, parsed.argument);
        break;
    default:
        break;
    }

    return true;
}

const OverrideEntry *FindEffectiveOverride(const std::string &key) {
    const OverrideEntry *best = nullptr;
    for (const OverrideEntry &entry : g_overrides) {
        if (entry.key != key)
            continue;

        if (best == nullptr || entry.priority > best->priority ||
            (entry.priority == best->priority && entry.sequence > best->sequence))
            best = &entry;
    }

    return best;
}

void SetOverride(const void *owner, bool priority, const std::string &key, const std::string &cmd) {
    auto existing =
        std::find_if(g_overrides.begin(), g_overrides.end(),
                     [&](const OverrideEntry &entry) {
                         return entry.owner == owner && entry.key == key;
                     });

    if (existing == g_overrides.end()) {
        g_overrides.push_back(
            {owner, key, cmd, priority, ++g_nextSequence});
        return;
    }

    existing->command = cmd;
    existing->priority = priority;
    existing->sequence = ++g_nextSequence;
}

void RemoveOverride(const void *owner, const std::string &key) {
    g_overrides.erase(std::remove_if(g_overrides.begin(), g_overrides.end(),
                                     [&](const OverrideEntry &entry) {
                                         return entry.owner == owner &&
                                                entry.key == key;
                                     }),
                      g_overrides.end());
}

int ExecuteCommand(void *mgr, const char *cmd, int isDown) {
    if (ExecuteSpecialCommand(cmd, isDown))
        return 1;

    return CommandExecute_o(mgr, cmd, isDown);
}

int __fastcall CommandExecute_h(void *mgr, void *, const char *cmd, int isDown) {
    return ExecuteCommand(mgr, cmd, isDown);
}

// Runs an override-resolved command with the same execution context the engine
// wraps around a native keypress. FUN_BINDING_KEY_DISPATCH saves the frame-
// script exec-context, zeroes it for the nested command, marks the manager as
// mid-command (`+0xD8`), then restores the context and floors the nesting
// depth. Native-table bindings already run inside this (the engine sets it up
// before calling FUN_BINDING_COMMAND_EXECUTE, which we co-hook); overrides
// bypass that path, so we mirror it here. Without it, a native Bindings.xml
// command dispatched through an override would run without the context its
// handler expects. Special SPELL/ITEM/MACRO/CLICK commands run harmlessly
// inside the same wrapper.
int DispatchOverrideCommand(void *mgr, const char *cmd, int isDown) {
    volatile uint32_t *context =
        reinterpret_cast<volatile uint32_t *>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    volatile uint32_t *depth = reinterpret_cast<volatile uint32_t *>(
        Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH);
    volatile uint32_t *executing = reinterpret_cast<volatile uint32_t *>(
        reinterpret_cast<char *>(mgr) + Offsets::OFF_BINDING_MANAGER_EXECUTING);

    const uint32_t savedContext = *context;
    *depth = *depth + 1;
    if (*depth != 0)
        *context = 0;

    *executing = 1;
    const int result = ExecuteCommand(mgr, cmd, isDown);
    *executing = 0;

    if (*depth != 0)
        *context = savedContext;
    if (static_cast<int32_t>(*depth) - 1 > 0)
        *depth = *depth - 1;
    else
        *depth = 0;

    return result;
}

int __fastcall KeyDispatch_h(void *mgr, void *, const char *key, int isDown) {
    const std::string canonicalKey = CanonicalKey(key);

    // Keep a key-down paired with its override command even if that override is
    // cleared while the command executes. The newly revealed native binding
    // must not receive only the key-up half of the same physical press.
    if (!isDown) {
        auto active = g_activeOverrides.find(canonicalKey);
        if (active != g_activeOverrides.end()) {
            DispatchOverrideCommand(mgr, active->second.c_str(), 0);
            g_activeOverrides.erase(active);
            return 1;
        }
    }

    const OverrideEntry *entry = FindEffectiveOverride(canonicalKey);
    if (entry == nullptr)
        return KeyDispatch_o(mgr, key, isDown);

    if (isDown)
        g_activeOverrides[canonicalKey] = entry->command;

    return DispatchOverrideCommand(mgr, entry->command.c_str(), isDown);
}

int SetBindingCommand(void *L, const char *prefix, const char *usage) {
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, usage);
        return 0;
    }

    const std::string key = Game::Lua::ToString(L, 1);
    const std::string command = MakeCommand(prefix, Game::Lua::ToString(L, 2));

    // Preserve the stock SetBinding return value, binding-set selection,
    // key normalization, UPDATE_BINDINGS event, and SaveBindings behavior.
    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, key.c_str());
    Game::Lua::PushString(L, command.c_str());
    auto setBinding =
        reinterpret_cast<ScriptSetBinding_t>(Offsets::FUN_SCRIPT_SET_BINDING);

    return setBinding(L);
}

const char *OptionalMouseButton(void *L, int index, const char *usage) {
    if (Game::Lua::GetTop(L) < index || Game::Lua::Type(L, index) == Game::Lua::TYPE_NIL)
        return nullptr;

    if (!Game::Lua::IsString(L, index)) {
        Game::Lua::Error(L, usage);
        return nullptr;
    }

    return Game::Lua::ToString(L, index);
}

int __fastcall Script_SetBindingSpell(void *L) {
    return SetBindingCommand(L, kSpellPrefix, "Usage: SetBindingSpell(key, spell)");
}

int __fastcall Script_SetBindingItem(void *L) {
    return SetBindingCommand(L, kItemPrefix, "Usage: SetBindingItem(key, item)");
}

int __fastcall Script_SetBindingMacro(void *L) {
    return SetBindingCommand(L, kMacroPrefix, "Usage: SetBindingMacro(key, macro)");
}

int __fastcall Script_SetBindingClick(void *L) {
    constexpr const char *usage = "Usage: SetBindingClick(key, buttonName [, mouseButton])";
    if (!Game::Lua::IsString(L, 1) || !Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, usage);
        return 0;
    }

    const std::string key = Game::Lua::ToString(L, 1);
    const char *buttonName = Game::Lua::ToString(L, 2);
    const char *mouseButton = OptionalMouseButton(L, 3, usage);
    const std::string command = MakeClickCommand(buttonName, mouseButton);

    Game::Lua::SetTop(L, 0);
    Game::Lua::PushString(L, key.c_str());
    Game::Lua::PushString(L, command.c_str());
    auto setBinding =
        reinterpret_cast<ScriptSetBinding_t>(Offsets::FUN_SCRIPT_SET_BINDING);

    return setBinding(L);
}

bool ReadOverrideHeader(void *L, const char *usage, const void **owner,
                        bool *priority, std::string *key) {
    *owner = Game::Lua::ResolveObject(L, 1);
    if (*owner == nullptr || Game::Lua::Type(L, 2) != Game::Lua::TYPE_BOOLEAN ||
        !Game::Lua::IsString(L, 3)) {
        Game::Lua::Error(L, usage);
        return false;
    }

    *priority = Game::Lua::ToBoolean(L, 2) != 0;
    *key = CanonicalKey(Game::Lua::ToString(L, 3));
    return true;
}

int SetOverrideBindingCommand(void *L, const char *prefix, const char *usage) {
    const void *owner = nullptr;
    bool priority = false;

    std::string key;
    if (!ReadOverrideHeader(L, usage, &owner, &priority, &key))
        return 0;

    if (!Game::Lua::IsString(L, 4)) {
        Game::Lua::Error(L, usage);
        return 0;
    }

    SetOverride(owner, priority, key,
                MakeCommand(prefix, Game::Lua::ToString(L, 4)));

    return 0;
}

int __fastcall Script_SetOverrideBinding(void *L) {
    constexpr const char *usage = "Usage: SetOverrideBinding(owner, isPriority, key [, command])";
    const void *owner = nullptr;
    bool priority = false;

    std::string key;
    if (!ReadOverrideHeader(L, usage, &owner, &priority, &key))
        return 0;

    if (Game::Lua::GetTop(L) < 4 ||
        Game::Lua::Type(L, 4) == Game::Lua::TYPE_NIL) {
        RemoveOverride(owner, key);
        return 0;
    }

    if (!Game::Lua::IsString(L, 4)) {
        Game::Lua::Error(L, usage);
        return 0;
    }

    SetOverride(owner, priority, key, Game::Lua::ToString(L, 4));
    return 0;
}

int __fastcall Script_SetOverrideBindingSpell(void *L) {
    return SetOverrideBindingCommand(
        L, kSpellPrefix,
        "Usage: SetOverrideBindingSpell(owner, isPriority, key, spell)");
}

int __fastcall Script_SetOverrideBindingItem(void *L) {
    return SetOverrideBindingCommand(
        L, kItemPrefix,
        "Usage: SetOverrideBindingItem(owner, isPriority, key, item)");
}

int __fastcall Script_SetOverrideBindingMacro(void *L) {
    return SetOverrideBindingCommand(
        L, kMacroPrefix,
        "Usage: SetOverrideBindingMacro(owner, isPriority, key, macro)");
}

int __fastcall Script_SetOverrideBindingClick(void *L) {
    constexpr const char *usage =
        "Usage: SetOverrideBindingClick(owner, "
        "isPriority, key, buttonName [, mouseButton])";

    const void *owner = nullptr;
    bool priority = false;

    std::string key;
    if (!ReadOverrideHeader(L, usage, &owner, &priority, &key))
        return 0;

    if (!Game::Lua::IsString(L, 4)) {
        Game::Lua::Error(L, usage);
        return 0;
    }

    const char *buttonName = Game::Lua::ToString(L, 4);
    const char *mouseButton = OptionalMouseButton(L, 5, usage);
    SetOverride(owner, priority, key,
                MakeClickCommand(buttonName, mouseButton));

    return 0;
}

int __fastcall Script_ClearOverrideBindings(void *L) {
    const void *owner = Game::Lua::ResolveObject(L, 1);
    if (owner == nullptr) {
        Game::Lua::Error(L, "Usage: ClearOverrideBindings(owner)");
        return 0;
    }

    g_overrides.erase(std::remove_if(g_overrides.begin(), g_overrides.end(),
                                     [&](const OverrideEntry &entry) {
                                         return entry.owner == owner;
                                     }),
                      g_overrides.end());
    return 0;
}

void RegisterLuaFunctions() {
    // A fresh FrameScript state makes every owner pointer from the previous
    // state stale. Permanent bindings remain in vanilla's binding table.
    g_overrides.clear();
    g_activeOverrides.clear();
    g_nextSequence = 0;

    Game::Lua::RegisterGlobalFunction("SetBindingSpell", &Script_SetBindingSpell);
    Game::Lua::RegisterGlobalFunction("SetBindingItem", &Script_SetBindingItem);
    Game::Lua::RegisterGlobalFunction("SetBindingMacro", &Script_SetBindingMacro);
    Game::Lua::RegisterGlobalFunction("SetBindingClick", &Script_SetBindingClick);
    Game::Lua::RegisterGlobalFunction("SetOverrideBinding",
                                      &Script_SetOverrideBinding);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingSpell",
                                      &Script_SetOverrideBindingSpell);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingItem",
                                      &Script_SetOverrideBindingItem);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingMacro",
                                      &Script_SetOverrideBindingMacro);
    Game::Lua::RegisterGlobalFunction("SetOverrideBindingClick",
                                      &Script_SetOverrideBindingClick);
    Game::Lua::RegisterGlobalFunction("ClearOverrideBindings",
                                      &Script_ClearOverrideBindings);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

const Game::HookAutoRegister _hookKeyDispatch{
    Offsets::FUN_BINDING_KEY_DISPATCH, reinterpret_cast<void *>(&KeyDispatch_h),
    reinterpret_cast<void **>(&KeyDispatch_o)};

const Game::HookAutoRegister _hookCommandExecute{
    Offsets::FUN_BINDING_COMMAND_EXECUTE,
    reinterpret_cast<void *>(&CommandExecute_h),
    reinterpret_cast<void **>(&CommandExecute_o)};

} // namespace

} // namespace Bindings::Api
