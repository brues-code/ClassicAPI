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

// F13..F24 as ordinary keys.
//
// The window procedure's translator drops VK_F13..VK_F24 (Offsets.h,
// FUN_INPUT_TRANSLATE_KEY), so the engine never sees them. Everything past
// the translator already copes with a new code once it has a name: binding
// names are validated as "F" plus digits (0x004B7890), bindings are stored and
// dispatched by name, and the held-key list is a list. So:
//   - the translator gives each key a code from KEY_F13 on;
//   - the three code→name decoders (chord builder, OnKeyDown, OnKeyUp) send
//     their UNKNOWN branch through a thunk that names those codes "F13".."F24"
//     and lets every other code fall to the engine's own UNKNOWN arm.
// A focused edit box takes these keys like F1..F12 (its switch falls through
// to `return 1` for both).

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <windows.h>

#include <cstdint>

namespace Input::FunctionKeys {

namespace {

constexpr const char *kNames[Offsets::KEY_EXTRA_FUNCTION_COUNT] = {
    "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
};

using TranslateKey_t = int(__fastcall *)(uint32_t vk, uint32_t *key);
TranslateKey_t g_translateKeyOriginal = nullptr;

int __fastcall TranslateKey_h(uint32_t vk, uint32_t *key) {
    if (vk >= VK_F13 && vk <= VK_F24) {
        *key = Offsets::KEY_F13 + (vk - VK_F13);
        return 1;
    }
    return g_translateKeyOriginal(vk, key);
}

const Game::HookAutoRegister _translateKeyHook{Offsets::FUN_INPUT_TRANSLATE_KEY,
                                               reinterpret_cast<void *>(&TranslateKey_h),
                                               reinterpret_cast<void **>(&g_translateKeyOriginal)};

// The name of one of our codes, or null.
const char *__fastcall Name(uint32_t key) {
    const uint32_t index = key - Offsets::KEY_F13;
    return index < Offsets::KEY_EXTRA_FUNCTION_COUNT ? kNames[index] : nullptr;
}

uintptr_t g_chordUnknown = Offsets::KEY_NAME_CHORD_UNKNOWN;
uintptr_t g_chordJoin = Offsets::KEY_NAME_CHORD_JOIN;
uintptr_t g_keyDownUnknown = Offsets::KEY_NAME_ON_KEY_DOWN_UNKNOWN;
uintptr_t g_keyDownJoin = Offsets::KEY_NAME_ON_KEY_DOWN_JOIN;
uintptr_t g_keyUpUnknown = Offsets::KEY_NAME_ON_KEY_UP_UNKNOWN;
uintptr_t g_keyUpJoin = Offsets::KEY_NAME_ON_KEY_UP_JOIN;

// The chord builder: code in ESI, name out in ESI.
__declspec(naked) void ChordThunk() {
    __asm {
        mov ecx, esi
        call Name
        test eax, eax
        jz unknown
        mov esi, eax
        jmp dword ptr [g_chordJoin]
    unknown:
        jmp dword ptr [g_chordUnknown]
    }
}

// OnKeyDown / OnKeyUp: code in ECX, name out in EAX.
__declspec(naked) void KeyDownThunk() {
    __asm {
        call Name
        test eax, eax
        jz unknown
        jmp dword ptr [g_keyDownJoin]
    unknown:
        jmp dword ptr [g_keyDownUnknown]
    }
}

__declspec(naked) void KeyUpThunk() {
    __asm {
        call Name
        test eax, eax
        jz unknown
        jmp dword ptr [g_keyUpJoin]
    unknown:
        jmp dword ptr [g_keyUpUnknown]
    }
}

// Glue boot always precedes the world, so this names the keys for both.
void PatchCode() {
    Common::RedirectJcc(Offsets::SITE_KEY_NAME_CHORD, Offsets::KEY_NAME_CHORD_UNKNOWN,
                        reinterpret_cast<const void *>(&ChordThunk));
    Common::RedirectJcc(Offsets::SITE_KEY_NAME_ON_KEY_DOWN, Offsets::KEY_NAME_ON_KEY_DOWN_UNKNOWN,
                        reinterpret_cast<const void *>(&KeyDownThunk));
    Common::RedirectJcc(Offsets::SITE_KEY_NAME_ON_KEY_UP, Offsets::KEY_NAME_ON_KEY_UP_UNKNOWN,
                        reinterpret_cast<const void *>(&KeyUpThunk));
}

const Game::GlueModuleAutoRegister _autoreg{&PatchCode};

} // namespace

} // namespace Input::FunctionKeys
