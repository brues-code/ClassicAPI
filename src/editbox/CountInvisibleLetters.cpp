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

// `editBox:SetCountInvisibleLetters(count)` / `editBox:IsCountInvisibleLetters()`.
//
// An edit box's SetMaxLetters limit counts the letters you can see. Color
// codes and a hyperlink's hidden `|H...|h` part don't count: with a limit of
// 5, `|cffff0000abcdefg|r` keeps `|cffff0000abcde`. Later clients let an edit
// box count those characters too, for text whose real limit is in bytes: 3.3.5
// does it for the macro text and the friends broadcast. Off is the default,
// as in their XML schema, and is how vanilla already counts. AceGUI-3.0's
// MultiLineEditBox turns it off explicitly.
//
// With it on, the letter-limit check counts each markup token by its length
// instead of skipping it. The engine counts through FUN_EDITBOX_COUNT_CHARS,
// which its cursor movement also uses to turn byte offsets into character
// positions, so only the limit check's three calls, recognized by their
// return addresses, get the new count. The cursor stays where it was.
//
// XML sets it with the EditBox attribute `countInvisibleLetters="true"`, read
// after the engine's own EditBox loader and parsed the way it parses
// `autoFocus`.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <intrin.h> // _ReturnAddress
#include <unordered_set>

namespace EditBox::CountInvisibleLetters {

namespace {

// Edit boxes that count invisible letters. Every object is rebuilt on
// /reload, so the set is cleared then.
std::unordered_set<void *> g_counting;

using CountChars_t = int(__thiscall *)(void *editBox, int start, int count);
CountChars_t g_origCountChars = nullptr;

bool FromLetterLimit(uintptr_t from) {
    return from == Offsets::RET_EDITBOX_MAX_LETTERS_COUNT_1 || from == Offsets::RET_EDITBOX_MAX_LETTERS_COUNT_2 ||
           from == Offsets::RET_EDITBOX_MAX_LETTERS_COUNT_3;
}

// The engine's count over [start, start + count), with markup tokens counted
// at their length rather than skipped.
int CountAll(void *editBox, int start, int count) {
    const auto *classes = Game::Read<const uint32_t *>(editBox, Offsets::OFF_EDITBOX_CHAR_CLASS);
    if (classes == nullptr)
        return 0;
    int letters = 0;
    for (int i = start; i < start + count; ++i) {
        const uint32_t entry = classes[i];
        if (entry == 0)
            continue; // not the first byte of a token
        const uint32_t cls = (entry >> 16) & 0xFF;
        if (cls == 2 || cls == 3 || cls == 6)
            letters += 1; // a letter you see, as the engine counts it
        else
            letters += static_cast<int>(entry & 0xFFFF); // markup: every character of it
    }
    return letters;
}

int __fastcall CountChars_h(void *editBox, void * /*edx*/, int start, int count) {
    if (!g_counting.empty() && FromLetterLimit(reinterpret_cast<uintptr_t>(_ReturnAddress())) &&
        g_counting.count(editBox) != 0)
        return CountAll(editBox, start, count);
    return g_origCountChars(editBox, start, count);
}

const Game::HookAutoRegister _countHook{Offsets::FUN_EDITBOX_COUNT_CHARS, reinterpret_cast<void *>(&CountChars_h),
                                        reinterpret_cast<void **>(&g_origCountChars)};

using Load_t = uintptr_t(__thiscall *)(void *self, const void *node, void *status);
using GetAttr_t = const char *(__thiscall *)(const void *node, const char *name);
using AttrToBool_t = int(__fastcall *)(const char *text);
Load_t g_origEditBoxLoad = nullptr;

// `self` is the loader sub-object inside the edit box.
uintptr_t __fastcall EditBoxLoad_h(void *self, void * /*edx*/, const void *node, void *status) {
    const uintptr_t result = g_origEditBoxLoad(self, node, status);
    if (node == nullptr)
        return result;
    const char *value =
        reinterpret_cast<GetAttr_t>(Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(node, "countInvisibleLetters");
    if (value == nullptr || *value == '\0')
        return result;
    void *editBox = static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
    if (reinterpret_cast<AttrToBool_t>(Offsets::FUN_XML_ATTR_TO_BOOL)(value))
        g_counting.insert(editBox);
    else
        g_counting.erase(editBox);
    return result;
}

const Game::HookAutoRegister _loadHook{Offsets::FUN_XML_EDITBOX_LOAD, reinterpret_cast<void *>(&EditBoxLoad_h),
                                       reinterpret_cast<void **>(&g_origEditBoxLoad)};

int __fastcall Script_SetCountInvisibleLetters(void *L) {
    void *editBox = Game::Lua::ResolveEditBox(L);
    if (editBox == nullptr)
        return 0;
    if (Game::Lua::ToBoolean(L, 2))
        g_counting.insert(editBox);
    else
        g_counting.erase(editBox);
    return 0;
}

int __fastcall Script_IsCountInvisibleLetters(void *L) {
    void *editBox = Game::Lua::ResolveEditBox(L);
    if (editBox == nullptr)
        return 0;
    Game::Lua::PushBool(L, g_counting.count(editBox) != 0);
    return 1;
}

void PrepareForReload() { g_counting.clear(); }

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetCountInvisibleLetters", &Script_SetCountInvisibleLetters},
    {"IsCountInvisibleLetters", &Script_IsCountInvisibleLetters},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_EDITBOX_METHOD_REGISTRY), g_methods,
                                    static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace EditBox::CountInvisibleLetters
