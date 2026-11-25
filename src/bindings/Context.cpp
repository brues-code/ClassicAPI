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

// Key-binding contexts, as the 3.80.2 client has them:
//
//   - A command takes a context from `bindingContext="Name"` on its
//     <Binding> in Bindings.xml (an Enum.BindingContext name, any case; a
//     number or an unknown name is None). 3.80.2 keeps it in the command
//     record (+0x8B, parsed in FUN_14124C1C0).
//   - Every key has a command slot per context, so one key runs different
//     commands in different contexts with no conflict. A binding goes into the
//     slot of its command's context (SetBinding's third argument names one
//     explicitly; it must then match the command's, except for an unbind).
//   - A key press takes the first ACTIVE context, in a fixed priority order,
//     whose slot holds a command, else slot None (3.80.2's resolver
//     FUN_141247C50). An unknown command found that way is no binding: no
//     retry in a lower context.
//   - C_KeyBindings.ActivateBindingContext / DeactivateBindingContext set a
//     flag per context (no stack, no event); every UI load starts with only
//     None active (FUN_141248B90).
//
// 1.12's binding manager has one command per key, and its key records cannot
// grow slots. So slot None IS the engine's table, untouched, and slots 1..9
// live here, per binding set. FUN_BINDINGS_SET_KEY is the one writer of a
// set's keys (Lua SetBinding, the binding-file loader, console `bind`), so
// its co-hook routes a context command's binding here and everything else to
// the engine; LoadBindings / SaveBindings / CopyBindings carry the slots along
// (Bindings::Files). The key dispatch (Bindings::Api) asks Resolve before the
// engine's table; GetBinding / GetBindingKey list a context command's keys
// through the engine's own key enumerator FUN_BINDINGS_GET_COMMAND_KEY.
//
// Differences from 3.80.2:
//   - Saved as 1.12 saves a set: every binding of the set, one `bind KEY
//     COMMAND` line each (3.80.2 writes the same line for a context binding,
//     the command implying its context). 3.80.2's files hold only the slots
//     that differ from the default set, with `bind KEY NONE <Context>` for a
//     cleared one; a 1.12 file holds the whole set, so it needs neither.
//   - No taint gate on Activate / Deactivate: 1.12 has no taint enforcement.
//   - Value 9 is GamepadModeInGameCore, the name retail's camelot client gives
//     the slot 3.80.2 reserves (ReservedFutureFeatureBinding01).
//   - A binding file line whose command's Bindings.xml has not loaded yet (an
//     addon loaded on demand) goes into None, as 3.80.2 binds a command it
//     does not know.

#include "bindings/Context.h"

#include "bindings/Files.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "lua/Arg.h"
#include "xml/FrameLoad.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bindings::Context {

namespace {

namespace Arg = Game::Lua::Arg;

constexpr int kCount = 10;
constexpr int kNone = 0;

// Enum.BindingContext, 3.80.2's values and names but for 9 (see above).
const Game::Lua::EnumIntegerEntry kBindingContext[kCount] = {
    {"None", 0},
    {"HousingEditor", 1},
    {"HousingEditorBasicDecorMode", 2},
    {"HousingEditorExpertDecorMode", 3},
    {"HousingEditorCustomizeMode", 4},
    {"HousingEditorCleanupMode", 5},
    {"HousingEditorLayoutMode", 6},
    {"HousingEditorBasicAndExpertDecorMode", 7},
    {"HousingEditorExteriorCustomizationMode", 8},
    {"GamepadModeInGameCore", 9},
};

// The resolver's order: FUN_141247C50 walks {0,9,1,7,2,3,4,5,6,8}
// (0x143CDC978) from the end, so the most specific housing modes come first
// and None last.
constexpr int kPriority[kCount] = {8, 6, 5, 4, 3, 2, 7, 1, 9, kNone};

bool g_active[kCount] = {true}; // DAT_1468BE700

// Command name (upper case) -> its context, for every <Binding> with one.
std::unordered_map<std::string, int> g_commandContext;

// One key's context slots in a set; [kNone] stays empty (the engine's table).
struct KeySlots {
    std::string key;
    std::string command[kCount];
};
std::vector<KeySlots> g_sets[Offsets::BINDING_SET_COUNT];

// The context SetBinding's third argument names, for the FUN_BINDINGS_SET_KEY
// call it makes; -1 when none.
int g_explicitContext = -1;

std::string Upper(const char *s) {
    std::string out(s);
    for (char &c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool IsNone(const char *command) {
    return command == nullptr || *command == '\0' || Ascii::EqualCI(command, "NONE");
}

int ContextOf(const char *command) {
    if (IsNone(command))
        return kNone;
    const auto it = g_commandContext.find(Upper(command));
    return it != g_commandContext.end() ? it->second : kNone;
}

int ContextByName(const char *name) {
    for (const auto &entry : kBindingContext)
        if (Ascii::EqualCI(name, entry.key))
            return entry.value;
    return kNone;
}

KeySlots *Find(int set, const char *key) {
    for (KeySlots &slots : g_sets[set])
        if (Ascii::EqualCI(slots.key.c_str(), key))
            return &slots;
    return nullptr;
}

// --- Bindings.xml: bindingContext -----------------------------------------------

void LoadFile(const char * /*path*/, const void *root, void * /*status*/) {
    for (const void *node = Xml::FirstChild(root); node != nullptr; node = Xml::NextSibling(node)) {
        if (!Xml::TagIs(node, "Binding"))
            continue;
        const char *name = Xml::Attr(node, "name");
        const char *context = Xml::Attr(node, "bindingContext");
        if (name == nullptr || *name == '\0' || context == nullptr)
            continue;
        if (const int value = ContextByName(context); value != kNone)
            g_commandContext[Upper(name)] = value;
    }
}

const Files::OnLoadFile _loadFile{&LoadFile};

// --- the set writer -------------------------------------------------------------

using SetKey_t = int(__fastcall *)(void *mgr, void *edx, int set, const char *key, const char *command);
SetKey_t g_setKeyOriginal = nullptr;

// FUN_BINDINGS_SET_KEY's key aliases, compared and replaced as it does.
struct Alias {
    uintptr_t name;
    char key;
};
constexpr Alias kAliases[] = {
    {Offsets::STR_KEY_ALIAS_LEFTBRACKET, '['}, {Offsets::STR_KEY_ALIAS_RIGHTBRACKET, ']'},
    {Offsets::STR_KEY_ALIAS_SLASH, '/'},       {Offsets::STR_KEY_ALIAS_BACKSLASH, '\\'},
    {Offsets::STR_KEY_ALIAS_SEMICOLON, ';'},   {Offsets::STR_KEY_ALIAS_APOSTROPHE, '\''},
    {Offsets::STR_KEY_ALIAS_COMMA, ','},       {Offsets::STR_KEY_ALIAS_PERIOD, '.'},
    {Offsets::STR_KEY_ALIAS_TILDE, '`'},       {Offsets::STR_KEY_ALIAS_PLUS, '='},
    {Offsets::STR_KEY_ALIAS_MINUS, '-'},
};

std::string NormalizeKey(const char *key) {
    for (const Alias &alias : kAliases)
        if (Ascii::EqualCI(key, reinterpret_cast<const char *>(alias.name)))
            return std::string(1, alias.key);
    return key;
}

// Called through the address, so Bindings::Api's validator co-hook (PAD names)
// applies as it does for the engine's own call.
bool IsValidKey(const char *key) {
    using IsValid_t = int(__fastcall *)(const char *key);
    return reinterpret_cast<IsValid_t>(Offsets::FUN_IS_VALID_BINDING_KEY)(key) != 0;
}

// 3.80.2's SetBinding core (FUN_14124D7C0) for a context slot.
int __fastcall SetKey_h(void *mgr, void *edx, int set, const char *key, const char *command) {
    const int explicitContext = g_explicitContext;
    g_explicitContext = -1;
    if (set < 0 || set >= Offsets::BINDING_SET_COUNT || key == nullptr)
        return g_setKeyOriginal(mgr, edx, set, key, command);
    const bool none = IsNone(command);
    if (explicitContext >= 0 && !none && ContextOf(command) != explicitContext)
        return 0;
    const int context = explicitContext >= 0 ? explicitContext : ContextOf(command);
    if (context == kNone)
        return g_setKeyOriginal(mgr, edx, set, key, command);

    const std::string normalized = NormalizeKey(key);
    if (!IsValidKey(normalized.c_str()))
        return 0;
    std::vector<KeySlots> &keys = g_sets[set];
    KeySlots *slots = Find(set, normalized.c_str());
    if (none) {
        if (slots == nullptr)
            return 1;
        slots->command[context].clear();
        bool empty = true;
        for (const std::string &c : slots->command)
            empty = empty && c.empty();
        if (empty)
            keys.erase(keys.begin() + (slots - keys.data()));
        return 1;
    }
    if (slots == nullptr) {
        keys.push_back(KeySlots{normalized, {}});
        slots = &keys.back();
    }
    slots->command[context] = command;
    return 1;
}

const Game::HookAutoRegister _setKeyHook{Offsets::FUN_BINDINGS_SET_KEY,
                                         reinterpret_cast<void *>(&SetKey_h),
                                         reinterpret_cast<void **>(&g_setKeyOriginal)};

// Lua SetBinding(key [, command, context]): the context reaches SetKey_h. The
// engine raises only for a first argument that is not a string or number,
// and then never reaches the writer, so the context is set only past that.
using Script_t = int(__fastcall *)(void *L);
Script_t g_scriptSetBindingOriginal = nullptr;

int __fastcall ScriptSetBinding_h(void *L) {
    if (Game::Lua::IsString(L, 1))
        g_explicitContext = OptionalArg(L, 3);
    const int result = g_scriptSetBindingOriginal(L);
    g_explicitContext = -1;
    return result;
}

const Game::HookAutoRegister _scriptSetBindingHook{
    Offsets::FUN_SCRIPT_SET_BINDING, reinterpret_cast<void *>(&ScriptSetBinding_h),
    reinterpret_cast<void **>(&g_scriptSetBindingOriginal)};

// --- the key list GetBinding / GetBindingKey walk --------------------------------

using CommandKey_t = const char *(__fastcall *)(void *mgr, void *edx, const char *command, int index);
CommandKey_t g_commandKeyOriginal = nullptr;

// A context command's keys come from its own context's slots of the live set,
// as 3.80.2's GetBindingKey reads the command's own context only.
const char *__fastcall CommandKey_h(void *mgr, void *edx, const char *command, int index) {
    const int context = ContextOf(command);
    if (context == kNone)
        return g_commandKeyOriginal(mgr, edx, command, index);
    for (const KeySlots &slots : g_sets[Offsets::BINDING_SET_CURRENT])
        if (Ascii::EqualCI(slots.command[context].c_str(), command) && index-- == 0)
            return slots.key.c_str();
    return nullptr;
}

const Game::HookAutoRegister _commandKeyHook{Offsets::FUN_BINDINGS_GET_COMMAND_KEY,
                                             reinterpret_cast<void *>(&CommandKey_h),
                                             reinterpret_cast<void **>(&g_commandKeyOriginal)};

// --- binding files ----------------------------------------------------------------

// LoadBindings empties the set before it binds the file's lines.
void LoadSetBegin(int set) { g_sets[set].clear(); }

void SaveSet(int set, void *file) {
    for (const KeySlots &slots : g_sets[set])
        for (int context = 1; context < kCount; ++context)
            if (!slots.command[context].empty())
                Files::WriteLine(file, "bind " + slots.key + " " + slots.command[context] + "\r\n");
}

void CopySet(int source, int dest) { g_sets[dest] = g_sets[source]; }

const Files::OnLoadSetBegin _loadSetBegin{&LoadSetBegin};
const Files::OnSaveSet _saveSet{&SaveSet};
const Files::OnCopySet _copySet{&CopySet};

// The binding manager, its commands and its sets are rebuilt with every Lua
// state, and 3.80.2 starts each UI with only None active.
void PrepareForReload() {
    g_commandContext.clear();
    for (std::vector<KeySlots> &keys : g_sets)
        keys.clear();
    for (int context = 0; context < kCount; ++context)
        g_active[context] = context == kNone;
}
const Game::ReloadAutoRegister _reload{&PrepareForReload};

// --- C_KeyBindings ------------------------------------------------------------------

constexpr const char *kActivateUsage = "Usage: C_KeyBindings.ActivateBindingContext(newContext)";
constexpr const char *kDeactivateUsage = "Usage: C_KeyBindings.DeactivateBindingContext(context)";
constexpr const char *kIsActiveUsage = "Usage: local isActive = C_KeyBindings.IsBindingContextActive(context)";
constexpr const char *kForActionUsage =
    "Usage: local context = C_KeyBindings.GetBindingContextForAction(action)";

int ReadContext(void *L, const char *usage) {
    uint32_t context = 0;
    Arg::Enum(L, 1, usage, kCount - 1, context);
    return static_cast<int>(context);
}

int __fastcall Script_ActivateBindingContext(void *L) {
    g_active[ReadContext(L, kActivateUsage)] = true;
    return 0;
}

int __fastcall Script_DeactivateBindingContext(void *L) {
    g_active[ReadContext(L, kDeactivateUsage)] = false;
    return 0;
}

int __fastcall Script_IsBindingContextActive(void *L) {
    Game::Lua::PushBool(L, g_active[ReadContext(L, kIsActiveUsage)]);
    return 1;
}

int __fastcall Script_GetBindingContextForAction(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Arg::Usage(L, 1, kForActionUsage);
        return 0;
    }
    Game::Lua::PushNumber(L, ContextOf(Game::Lua::ToString(L, 1)));
    return 1;
}

const Game::Doc::Field kContextArgs[] = {Game::Doc::Req("context", "BindingContext")};
const Game::Doc::Field kNewContextArgs[] = {Game::Doc::Req("newContext", "BindingContext")};
const Game::Doc::Field kIsActiveRets[] = {Game::Doc::Req("isActive", "bool")};
const Game::Doc::Field kActionArgs[] = {Game::Doc::Req("action", "cstring")};
const Game::Doc::Field kContextRets[] = {
    Game::Doc::Req("context", "BindingContext", "None when the action has no context."),
};

const Game::Doc::Function kActivate{
    "Turns on a binding context: keys bound in it run its commands, ahead of their other bindings.",
    kNewContextArgs, {}, "KeyBindings"};
const Game::Doc::Function kDeactivate{"Turns off a binding context.", kContextArgs, {}, "KeyBindings"};
const Game::Doc::Function kIsActive{"Returns whether a binding context is on.", kContextArgs, kIsActiveRets,
                                    "KeyBindings"};
const Game::Doc::Function kForAction{"Returns the binding context a key binding action belongs to.",
                                     kActionArgs, kContextRets, "KeyBindings"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "BindingContext", kBindingContext, "KeyBindings");
    Game::Lua::RegisterTableFunction("C_KeyBindings", "ActivateBindingContext", &Script_ActivateBindingContext,
                                     &kActivate);
    Game::Lua::RegisterTableFunction("C_KeyBindings", "DeactivateBindingContext",
                                     &Script_DeactivateBindingContext, &kDeactivate);
    Game::Lua::RegisterTableFunction("C_KeyBindings", "IsBindingContextActive", &Script_IsBindingContextActive,
                                     &kIsActive);
    Game::Lua::RegisterTableFunction("C_KeyBindings", "GetBindingContextForAction",
                                     &Script_GetBindingContextForAction, &kForAction);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

const char *Resolve(const char *key) {
    const KeySlots *slots = key != nullptr ? Find(Offsets::BINDING_SET_CURRENT, key) : nullptr;
    if (slots == nullptr)
        return nullptr;
    for (const int context : kPriority) {
        if (context == kNone)
            break;
        const std::string &command = slots->command[context];
        if (g_active[context] && !IsNone(command.c_str()))
            return command.c_str();
    }
    return nullptr;
}

const char *Slot(const char *key, int context) {
    if (key == nullptr || context <= kNone || context >= kCount)
        return nullptr;
    const KeySlots *slots = Find(Offsets::BINDING_SET_CURRENT, NormalizeKey(key).c_str());
    return slots != nullptr && !slots->command[context].empty() ? slots->command[context].c_str() : nullptr;
}

int OptionalArg(void *L, int arg) {
    if (!Game::Lua::IsNumber(L, arg))
        return -1;
    const double value = Game::Lua::ToNumber(L, arg);
    return value >= 0.0 && value < kCount ? static_cast<int>(value) : -1;
}

} // namespace Bindings::Context
