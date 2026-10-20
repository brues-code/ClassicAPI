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

// Modified clicks — 4.3.4's MODIFIEDCLICK system (decomp Ui/MODIFIEDCLICK.cpp,
// UIBindings.cpp, UIBindings_Scripts.cpp), which 1.12's binding manager lacks.
//
// An action ("SELFCAST", "CHATLINK", …) is bound to a modifier set and/or a
// mouse button ("ALT", "SHIFT-BUTTON1", "NONE"), once per binding set (0
// default, 1 account, 2 character, 3 current), exactly like key bindings. 1.12
// has 4.3.4's binding sets, file format and load / save / copy steps, so each
// gains the modified-click half 4.3.4 has, through a co-hook on the step:
//
//   - Load (Bindings.xml): `<ModifiedClick action="X" default="Y"/>` defines
//     an action and its default. We register the actions from the same file
//     first. The engine's loop tests each root tag inline and logs "Unknown
//     node type" for anything but <Binding>; 4.3.4's loop has a ModifiedClick
//     branch there, so the loader runs with a status that drops exactly that
//     warning for a ModifiedClick node and forwards everything else.
//   - LoadBindings (a binding file into a set): the set starts from the
//     defaults, then `modifiedclick BINDING ACTION` lines apply.
//   - SaveBindings: `modifiedclick` lines for every action not left at its
//     default.
//   - CopyBindings: the set's bindings are copied with the key bindings.
//
// The actions live as long as the binding manager: the engine builds it on
// every world load and /reload, so the table is cleared with the Lua state.
// Key state comes from Input::Modifier (the left/right bits 4.3.4's
// CGGameUI::IsKeyDown reads) and the clicked button from Frame::ClickEvents
// (4.3.4's CSimpleTop::m_mouseButtonName).

#include "bindings/ModifiedClick.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "baselib/Ascii.h"
#include "frame/ClickEvents.h"
#include "input/Modifier.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Bindings::ModifiedClick {

namespace {

// 4.3.4's modifier bits, = Input::Modifier's left/right key bits (and what
// Input::Modifier::ParseModifiers returns).
constexpr uint32_t kLShift = 0x01, kRShift = 0x02, kLCtrl = 0x04, kRCtrl = 0x08,
                   kLAlt = 0x10, kRAlt = 0x20;
constexpr int kKeyCount = 6;

// MODIFIEDCLICK_DATA. Flag 1 = the set holds the default (never saved).
constexpr uint32_t kFlagDefault = 0x1;
struct Data {
    uint32_t flags = 0;
    uint32_t modifiers = 0;
    const char *button = nullptr;
};

struct Click {
    std::string action;
    Data data[Offsets::BINDING_SET_COUNT];
};

std::vector<Click> g_clicks;

Click *Find(const char *action) {
    if (action == nullptr)
        return nullptr;
    for (Click &click : g_clicks)
        if (_stricmp(click.action.c_str(), action) == 0)
            return &click;
    return nullptr;
}

bool KeyDown(int key) { return (Input::Modifier::CurrentMask() & (1u << key)) != 0; }

bool PrefixIs(const char *s, const char *prefix) {
    return _strnicmp(s, prefix, std::strlen(prefix)) == 0;
}

// CGUIBindings::AddMetaPrefix, appended to `out`.
void AddMetaPrefix(uint32_t modifiers, std::string &out) {
    const auto side = [&](uint32_t left, uint32_t right, const char *both, const char *l,
                          const char *r) {
        if ((modifiers & (left | right)) == (left | right))
            out += both;
        else if (modifiers & left)
            out += l;
        else if (modifiers & right)
            out += r;
    };
    side(kLAlt, kRAlt, "ALT-", "LALT-", "RALT-");
    side(kLCtrl, kRCtrl, "CTRL-", "LCTRL-", "RCTRL-");
    side(kLShift, kRShift, "SHIFT-", "LSHIFT-", "RSHIFT-");
}

// BUTTONn <-> mouse-button name (IndexToMouseButton / MouseButtonToIndex).
// 1..5 are the engine's own name literals, the strings GetMouseButtonClicked
// and the click scope hold.
const char *const kExtraButtons[] = {"Button6", "Button7", "Button8", "Button9"};

const char *ButtonForIndex(int index) {
    switch (index) {
    case 1: return reinterpret_cast<const char *>(Offsets::VAR_BUTTON_NAME_LEFT);
    case 2: return reinterpret_cast<const char *>(Offsets::VAR_BUTTON_NAME_RIGHT);
    case 3: return reinterpret_cast<const char *>(Offsets::VAR_BUTTON_NAME_MIDDLE);
    case 4: return reinterpret_cast<const char *>(Offsets::VAR_BUTTON_NAME_4);
    case 5: return reinterpret_cast<const char *>(Offsets::VAR_BUTTON_NAME_5);
    case 6: case 7: case 8: case 9: return kExtraButtons[index - 6];
    default: return nullptr;
    }
}

int IndexForButton(const char *button) {
    for (int index = 1; index <= 9; ++index)
        if (_stricmp(ButtonForIndex(index), button) == 0)
            return index;
    return 0;
}

// MODIFIEDCLICK::SetBinding.
bool SetBinding(Click &click, int set, const char *binding) {
    const char *button = nullptr;
    uint32_t modifiers = 0;
    if (binding != nullptr && *binding != '\0' && _stricmp(binding, "NONE") != 0) {
        modifiers = Input::Modifier::ParseModifiers(binding);
        if (PrefixIs(binding, "BUTTON")) {
            binding += 6;
            button = ButtonForIndex(std::atoi(binding));
            ++binding;
        }
        if (*binding != '\0')
            return false;
    }
    Data &data = click.data[set];
    if (set != Offsets::BINDING_SET_DEFAULT)
        data.flags &= ~kFlagDefault;
    else
        data.flags |= kFlagDefault;
    data.button = button;
    data.modifiers = modifiers;
    return true;
}

// MODIFIEDCLICK::GetBinding.
std::string GetBinding(const Click &click, int set) {
    const Data &data = click.data[set];
    if (data.modifiers == 0 && data.button == nullptr)
        return "NONE";
    std::string out;
    AddMetaPrefix(data.modifiers, out);
    if (data.button != nullptr)
        out += "BUTTON" + std::to_string(IndexForButton(data.button));
    else
        out.pop_back(); // the prefix's trailing '-'
    return out;
}

// MODIFIEDCLICK::IsActive.
bool IsActive(const Click &click, int set) {
    const Data &data = click.data[set];
    if (data.modifiers != 0) {
        int key = 0;
        while (!((1u << key) & data.modifiers) || !KeyDown(key)) {
            if (++key >= kKeyCount)
                return false;
        }
        if (data.button == nullptr)
            return true;
    } else if (data.button == nullptr) {
        return false;
    }
    const char *current = Frame::ClickEvents::CurrentButton();
    return current == nullptr || std::strcmp(current, data.button) == 0;
}

// One side of a modifier test: left only, right only, or either.
bool SideHeld(uint32_t modifiers, uint32_t left, uint32_t right, int leftKey, int rightKey) {
    const uint32_t bits = modifiers & (left | right);
    if (bits == left)
        return KeyDown(leftKey);
    if (bits == right || (bits == (left | right) && !KeyDown(leftKey)))
        return KeyDown(rightKey);
    return true;
}

// --- Bindings.xml: <ModifiedClick action default> ---------------------------

using LoadFile_t = int(__fastcall *)(void *mgr, void *edx, const char *path, void *md5,
                                     void *status);
LoadFile_t g_loadFileOriginal = nullptr;

using TreeLoad_t = void *(__fastcall *)(const char *data, uint32_t size);
using TreeFree_t = void(__fastcall *)(void *tree);

// CGUIBindings::LoadModifiedClick for every root node of the file.
void LoadModifiedClicks(const char *path, void *status) {
    void *data = nullptr;
    size_t size = 0;
    if (!reinterpret_cast<AddOns::EngineIO::FileReadFn>(Offsets::FUN_FILE_READ)(0, path, &data,
                                                                                 &size, 0, 1, 0))
        return;
    void *tree = reinterpret_cast<TreeLoad_t>(Offsets::FUN_XML_TREE_LOAD)(
        static_cast<const char *>(data), static_cast<uint32_t>(size));
    reinterpret_cast<AddOns::EngineIO::SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE)(data, __FILE__,
                                                                                 __LINE__, 0);
    if (tree == nullptr)
        return; // the engine loader reports the parse error
    const void *root = Game::Read<const void *>(tree, 0);
    for (const void *node = root ? Xml::FirstChild(root) : nullptr; node != nullptr;
         node = Xml::NextSibling(node)) {
        if (!Xml::TagIs(node, "ModifiedClick"))
            continue;
        const char *action = Xml::Attr(node, "action");
        if (action == nullptr || *action == '\0') {
            Xml::Warn(status, "Found modified click with no action in %s", path);
            continue;
        }
        if (Find(action) != nullptr) {
            Xml::Warn(status, "Modified click %s is defined more than once in %s", action, path);
            continue;
        }
        g_clicks.push_back(Click{action, {}});
        const char *binding = Xml::Attr(node, "default");
        if (binding != nullptr && *binding != '\0')
            SetBinding(g_clicks.back(), Offsets::BINDING_SET_DEFAULT, binding);
    }
    reinterpret_cast<TreeFree_t>(Offsets::FUN_XML_TREE_FREE)(tree);
}

// The status the engine loader is given: a CStatus whose only used slot,
// Add (vtable +0xC), forwards to the real status. Load passes at most two
// string arguments after the format, and Add is __cdecl, so taking exactly
// two is safe whatever the format consumes.
struct LoadStatus {
    const void *const *vtable;
    void *real;
};

void __cdecl LoadStatusAdd(LoadStatus *self, int level, const char *fmt, const char *a,
                           const char *b) {
    if (fmt == reinterpret_cast<const char *>(Offsets::STR_BINDINGS_UNKNOWN_NODE) &&
        a != nullptr && Ascii::EqualCI(a, "ModifiedClick"))
        return; // a node this loader now knows (LoadModifiedClicks)
    using Add_t = void(__cdecl *)(void *status, int level, const char *fmt, ...);
    const void *vtable = Game::Read<const void *>(self->real, 0);
    Game::Read<Add_t>(vtable, Offsets::VTBL_XML_STATUS_ADD)(self->real, level, fmt, a, b);
}

static_assert(Offsets::VTBL_XML_STATUS_ADD == 3 * sizeof(void *));
const void *const kLoadStatusVtable[4] = {nullptr, nullptr, nullptr,
                                          reinterpret_cast<const void *>(&LoadStatusAdd)};

int __fastcall LoadFile_h(void *mgr, void *edx, const char *path, void *md5, void *status) {
    if (status == nullptr)
        status = reinterpret_cast<void *>(Offsets::VAR_BINDINGS_LOAD_STATUS);
    if (path != nullptr)
        LoadModifiedClicks(path, status);
    LoadStatus filtered{kLoadStatusVtable, status};
    return g_loadFileOriginal(mgr, edx, path, md5, &filtered);
}

// --- binding files: modifiedclick lines -----------------------------------

using LoadSet_t = void(__fastcall *)(int set, const char *text);
LoadSet_t g_loadSetOriginal = nullptr;

using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize,
                                     const char *delims, int *outQuoted);

// The modified-click half of 4.3.4's LoadBindings: start from the defaults,
// then apply `modifiedclick BINDING ACTION` lines.
void __fastcall LoadSet_h(int set, const char *text) {
    g_loadSetOriginal(set, text);
    if (set < 0 || set >= Offsets::BINDING_SET_COUNT)
        return;
    if (set != Offsets::BINDING_SET_DEFAULT)
        for (Click &click : g_clicks)
            click.data[set] = click.data[Offsets::BINDING_SET_DEFAULT];

    auto tokenize = reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE);
    const char *lineDelims = reinterpret_cast<const char *>(Offsets::VAR_MACRO_LINE_DELIMS);
    const char *spaceDelims = reinterpret_cast<const char *>(Offsets::VAR_STR_SPACE_DELIM);
    char line[1024];
    char key[32];
    const char *cursor = text;
    while (cursor != nullptr && *cursor != '\0') {
        tokenize(&cursor, line, sizeof line, lineDelims, nullptr);
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!PrefixIs(p, "modifiedclick "))
            continue;
        const char *action = p + 14;
        tokenize(&action, key, sizeof key, spaceDelims, nullptr);
        if (action != nullptr)
            if (Click *click = Find(action))
                SetBinding(*click, set, key);
    }
}

using SaveSet_t = void(__fastcall *)(void *mgr, void *edx, int set, void *file);
SaveSet_t g_saveSetOriginal = nullptr;

using WriteFile_t = int(__fastcall *)(void *file, const void *buffer, uint32_t size,
                                      uint32_t *written);

void __fastcall SaveSet_h(void *mgr, void *edx, int set, void *file) {
    g_saveSetOriginal(mgr, edx, set, file);
    if (set < 0 || set >= Offsets::BINDING_SET_COUNT)
        return;
    auto write = reinterpret_cast<WriteFile_t>(Offsets::FUN_OS_WRITE_FILE);
    for (const Click &click : g_clicks) {
        if (click.data[set].flags & kFlagDefault)
            continue;
        const std::string line =
            "modifiedclick " + GetBinding(click, set) + " " + click.action + "\r\n";
        uint32_t written = 0;
        write(file, line.data(), static_cast<uint32_t>(line.size()), &written);
    }
}

using CopySet_t = void(__fastcall *)(void *mgr, void *edx, int source, int dest);
CopySet_t g_copySetOriginal = nullptr;

void __fastcall CopySet_h(void *mgr, void *edx, int source, int dest) {
    g_copySetOriginal(mgr, edx, source, dest);
    if (source < 0 || source >= Offsets::BINDING_SET_COUNT || dest < 0 ||
        dest >= Offsets::BINDING_SET_COUNT)
        return;
    for (Click &click : g_clicks)
        click.data[dest] = click.data[source];
}

const Game::HookAutoRegister _loadFileHook{Offsets::FUN_BINDINGS_LOAD_FILE,
                                           reinterpret_cast<void *>(&LoadFile_h),
                                           reinterpret_cast<void **>(&g_loadFileOriginal)};
const Game::HookAutoRegister _loadSetHook{Offsets::FUN_BINDINGS_LOAD_SET,
                                          reinterpret_cast<void *>(&LoadSet_h),
                                          reinterpret_cast<void **>(&g_loadSetOriginal)};
const Game::HookAutoRegister _saveSetHook{Offsets::FUN_BINDINGS_SAVE_SET,
                                          reinterpret_cast<void *>(&SaveSet_h),
                                          reinterpret_cast<void **>(&g_saveSetOriginal)};
const Game::HookAutoRegister _copySetHook{Offsets::FUN_BINDINGS_COPY_SET,
                                          reinterpret_cast<void *>(&CopySet_h),
                                          reinterpret_cast<void **>(&g_copySetOriginal)};

// The binding manager is rebuilt with every Lua state; so are its actions.
void PrepareForReload() { g_clicks.clear(); }
const Game::ReloadAutoRegister _reload{&PrepareForReload};

// --- Lua --------------------------------------------------------------------

int __fastcall Script_GetNumModifiedClickActions(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(g_clicks.size()));
    return 1;
}

int __fastcall Script_GetModifiedClickAction(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetModifiedClickAction(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    Game::Lua::PushString(L, index >= 0 && index < static_cast<int>(g_clicks.size())
                                 ? g_clicks[index].action.c_str()
                                 : nullptr);
    return 1;
}

int __fastcall Script_SetModifiedClick(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: SetModifiedClick(\"action\", \"binding\")");
        return 0;
    }
    const char *action = Game::Lua::ToString(L, 1);
    const char *binding = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    Click *click = Find(action);
    if (click == nullptr || !SetBinding(*click, Offsets::BINDING_SET_CURRENT, binding)) {
        char message[256];
        std::snprintf(message, sizeof message,
                      "SetModifiedClick(): Unknown action (%s) or binding (%s)", action,
                      binding != nullptr ? binding : "(null)");
        Game::Lua::Error(L, "%s", message);
    }
    return 0;
}

int __fastcall Script_GetModifiedClick(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: GetModifiedClick(\"action\")");
        return 0;
    }
    const Click *click = Find(Game::Lua::ToString(L, 1));
    if (click == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushString(L, GetBinding(*click, Offsets::BINDING_SET_CURRENT).c_str());
    return 1;
}

int __fastcall Script_IsModifiedClick(void *L) {
    const char *action = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (IsModifiedClick(action))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

const Game::Doc::Field kIndexArgs[] = {Game::Doc::Req("index", "luaIndex")};
const Game::Doc::Field kActionArgs[] = {Game::Doc::Req("action", "string")};
const Game::Doc::Field kSetArgs[] = {Game::Doc::Req("action", "string"),
                                     Game::Doc::Opt("binding", "string")};
const Game::Doc::Field kIsArgs[] = {Game::Doc::Opt("action", "string")};
const Game::Doc::Field kCountRets[] = {Game::Doc::Req("count", "number")};
const Game::Doc::Field kActionRets[] = {Game::Doc::Opt("action", "string")};
const Game::Doc::Field kBindingRets[] = {Game::Doc::Opt("binding", "string")};
const Game::Doc::Field kIsRets[] = {Game::Doc::Opt("isModified", "number")};

const Game::Doc::Function kGetNum{"Returns the number of modified-click actions.", {},
                                  kCountRets, "BindingsGlobals"};
const Game::Doc::Function kGetAction{"Returns the name of the modified-click action at an index.",
                                     kIndexArgs, kActionRets, "BindingsGlobals"};
const Game::Doc::Function kSet{"Sets the modifier keys and mouse button for an action.",
                               kSetArgs, {}, "BindingsGlobals"};
const Game::Doc::Function kGet{"Returns the modifier keys and mouse button set for an action.",
                               kActionArgs, kBindingRets, "BindingsGlobals"};
const Game::Doc::Function kIs{"Returns 1 when the keys and mouse button for an action are held.",
                              kIsArgs, kIsRets, "BindingsGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetNumModifiedClickActions",
                                      &Script_GetNumModifiedClickActions, &kGetNum);
    Game::Lua::RegisterGlobalFunction("GetModifiedClickAction", &Script_GetModifiedClickAction,
                                      &kGetAction);
    Game::Lua::RegisterGlobalFunction("SetModifiedClick", &Script_SetModifiedClick, &kSet);
    Game::Lua::RegisterGlobalFunction("GetModifiedClick", &Script_GetModifiedClick, &kGet);
    Game::Lua::RegisterGlobalFunction("IsModifiedClick", &Script_IsModifiedClick, &kIs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool IsModifiedClick(const char *action) {
    if (action != nullptr) {
        const char *rest = action;
        const uint32_t modifiers = Input::Modifier::ParseModifiers(rest);
        if (modifiers == 0) {
            const Click *click = Find(action);
            return click != nullptr && IsActive(*click, Offsets::BINDING_SET_CURRENT);
        }
        return SideHeld(modifiers, kLShift, kRShift, 0, 1) &&
               SideHeld(modifiers, kLCtrl, kRCtrl, 2, 3) &&
               SideHeld(modifiers, kLAlt, kRAlt, 4, 5);
    }
    if ((Input::Modifier::CurrentMask() & 0x3F) != 0)
        return true;
    for (const Click &click : g_clicks)
        if (IsActive(click, Offsets::BINDING_SET_CURRENT))
            return true;
    return false;
}

} // namespace Bindings::ModifiedClick
