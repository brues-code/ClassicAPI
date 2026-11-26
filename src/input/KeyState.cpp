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

// IsKeyDown(keyOrMouseName [, excludeCurrentBindingState]), as the 3.80.2
// client has it (0x141B6DF50, docs/notes/gamepad-input-region-api-3802.md §1):
//   1. a name its key table knows (FUN_140596500) answers that key's held
//      state, a boolean;
//   2. any other name that is a mouse button (FUN_140597C00) answers its bit
//      of the held-button mask;
//   3. anything else returns nothing.
// The key table, case-insensitive except the `F` of F1..F19:
//   - one character above a space: that character, case kept ("a" is not
//     "A", and 1.12 holds letters upper case);
//   - LSHIFT RSHIFT LCTRL RCTRL LALT RALT: that side. With the second
//     argument false, a running binding's own modifiers read up, as every
//     modifier read does under Input::Modifier's KeyStateOverride. LMETA
//     and RMETA are keys 1.12 never holds. No bare SHIFT, CTRL or ALT;
//   - SPACE, the NUMPAD names and NUMPAD<c> (c at most '9', so NUMPAD0..9),
//     ESCAPE..NUMLOCK, PRINTSCREEN, F1..F19 (the number read as strtol
//     reads it): 1.12's own code for that key, read from the engine's
//     held-key list (EventIsKeyDown). F13..F19 are Input::FunctionKeys'
//     codes;
//   - OEMCOMMA, OEMPERIOD, OEM1..OEM8: known, never held (1.12 gives those
//     keys their characters).
// 3.80.2 also refuses non-modifier keys to tainted code while a protected
// edit box has focus; 1.12 has no protected edit boxes.
//
// A gamepad UI asks with the binding names of the pad (HoldBindings,
// ClassActionFlyout's shoulder buttons), which 3.80.2's table does not know.
// Before the mouse parse, a pad binding name (PAD1, PADLSHOULDER, ...)
// answers that button of the active device's mapped state; a chord
// ("SHIFT-PAD1") stays unknown, as 3.80.2 treats any chord.

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/ClickEvents.h"
#include "gamepad/Device.h"
#include "gamepad/GamePad.h"
#include "gamepad/Names.h"
#include "gamepad/State.h"
#include "input/Modifier.h"
#include "lua/Arg.h"

#include <cstdint>
#include <cstdlib>

namespace Input::KeyState {

namespace {

enum class Kind { Unknown, Side, Never, Code };

struct Key {
    Kind kind = Kind::Unknown;
    int value = 0; // the side bit (Side) or the 1.12 key code (Code)
};

Key Side(int bit) { return {Kind::Side, bit}; }
Key Code(int code) { return {Kind::Code, code}; }
constexpr Key kNever{Kind::Never, 0};

// FUN_140596500, giving 1.12's code where 3.80.2 gives its own. The names
// are distinct, so the order of the tests is the client's only for reading.
Key Parse(const char *name) {
    if (name == nullptr || name[0] == '\0')
        return {};
    if (name[1] == '\0' && static_cast<signed char>(name[0]) > ' ')
        return Code(static_cast<signed char>(name[0]));
    static const struct {
        const char *name;
        Key key;
    } kNames[] = {
        {"LSHIFT", Side(0)},
        {"RSHIFT", Side(1)},
        {"LCTRL", Side(2)},
        {"RCTRL", Side(3)},
        {"LALT", Side(4)},
        {"RALT", Side(5)},
        {"LMETA", kNever},
        {"RMETA", kNever},
        {"SPACE", Code(0x20)},
        {"NUMPADPLUS", Code(0x10A)},
        {"NUMPADMINUS", Code(0x10B)},
        {"NUMPADMULTIPLY", Code(0x10C)},
        {"NUMPADDIVIDE", Code(0x10D)},
        {"NUMPADDECIMAL", Code(0x10E)},
        {"NUMPADEQUALS", Code(Offsets::KEY_NUMPADEQUALS)},
    };
    for (const auto &entry : kNames)
        if (Ascii::EqualCI(name, entry.name))
            return entry.key;
    if (Ascii::StartsWithCI(name, "NUMPAD"))
        return static_cast<unsigned char>(name[6]) > '9' ? Key{} : Code(static_cast<signed char>(name[6]) + 0xD0);
    // ESCAPE..NUMLOCK in 1.12's order from KEY_ESCAPE (3.80.2's
    // 0x10F..0x11E, the same order).
    static const char *const kNamed[] = {"ESCAPE", "ENTER",  "BACKSPACE", "TAB",    "LEFT",   "UP",
                                         "RIGHT",  "DOWN",   "INSERT",    "DELETE", "HOME",   "END",
                                         "PAGEUP", "PAGEDOWN", "CAPSLOCK", "NUMLOCK"};
    for (int i = 0; i < static_cast<int>(sizeof(kNamed) / sizeof(kNamed[0])); ++i)
        if (Ascii::EqualCI(name, kNamed[i]))
            return Code(Offsets::KEY_ESCAPE + i);
    if (Ascii::EqualCI(name, "PRINTSCREEN"))
        return Code(Offsets::KEY_PRINTSCREEN);
    if (Ascii::EqualCI(name, "OEMCOMMA") || Ascii::EqualCI(name, "OEMPERIOD"))
        return kNever;
    if (Ascii::StartsWithCI(name, "OEM")) {
        const long n = std::strtol(name + 3, nullptr, 10);
        if (n >= 1 && n <= 8)
            return kNever;
    }
    if (name[0] == 'F') {
        const long n = std::strtol(name + 1, nullptr, 10);
        if (n >= 1 && n <= 12)
            return Code(Offsets::KEY_F1 + static_cast<int>(n) - 1);
        if (n >= 13 && n <= 19)
            return Code(Offsets::KEY_F13 + static_cast<int>(n) - 13);
    }
    return {};
}

bool KeyDown(const Key &key, bool exclude) {
    switch (key.kind) {
    case Kind::Side: {
        const uint32_t held = exclude ? Input::Modifier::HeldMask() : Input::Modifier::CurrentMask();
        return (held >> key.value) & 1u;
    }
    case Kind::Code:
        // The binding override touches only 0..2, which the table never
        // gives; these read the engine's held-key list as they are.
        return key.value > 0 && Input::Modifier::IsKeyDown(key.value);
    default:
        return false;
    }
}

// A pad binding name: the button's bit in the active device's mapped state.
bool PadDown(uint8_t button) {
    const uint32_t id = GamePad::ActiveID();
    const GamePad::Device::Slot *slot = GamePad::Device::SlotByID(id);
    if (!GamePad::Started() || slot == nullptr || !slot->connected)
        return false;
    return (GamePad::State::MappedOf(id).buttons >> button) & 1u;
}

int __fastcall Script_IsKeyDown(void *L) {
    if (!Game::Lua::IsString(L, 1))
        Game::Lua::Error(L, "Usage: IsKeyDown(keyOrMouseName [, excludeCurrentBindingState])");
    const char *name = Game::Lua::ToString(L, 1);
    const bool exclude = Game::Lua::ToBoolean(L, 2) != 0;
    const Key key = Parse(name);
    if (key.kind != Kind::Unknown) {
        Game::Lua::PushBool(L, KeyDown(key, exclude));
        return 1;
    }
    const uint8_t button = GamePad::Names::BindingToIndex(name);
    if (button != GamePad::Names::kNone) {
        Game::Lua::PushBool(L, PadDown(button));
        return 1;
    }
    const uint32_t mask = Frame::ClickEvents::MaskForButtonName(name);
    if (mask == 0)
        return 0;
    const uint32_t held = Game::Read<uint32_t>(static_cast<uintptr_t>(Offsets::VAR_HELD_MOUSE_BUTTONS));
    Game::Lua::PushBool(L, (held & mask) != 0);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("keyOrMouseName", "cstring",
                   "A key name (A, SPACE, LSHIFT, F1, ...), a mouse button (LeftButton, Button4, ...) or a gamepad "
                   "button (PAD1, PADLSHOULDER, ...)."),
    Game::Doc::Opt("excludeBindingState", "bool", "false",
                   "True reads the modifier keys as held, even the ones the key binding that runs now was pressed "
                   "with."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Opt("down", "bool", nullptr, "Nil when the name is not a key or a button."),
};
const Game::Doc::Function kIsKeyDown{"Returns whether a key, mouse button or gamepad button is held.", kArgs, kRets,
                                     "InputGlobals"};

void RegisterLuaFunctions() { Game::Lua::RegisterGlobalFunction("IsKeyDown", &Script_IsKeyDown, &kIsKeyDown); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Input::KeyState
