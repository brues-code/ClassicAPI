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

#pragma once

#include <cstdint>

namespace Input::Modifier {

// The held left/right modifier bitmap the `Is*KeyDown` globals read (bit
// 0 LSHIFT, 1 RSHIFT, 2 LCTRL, 3 RCTRL, 4 LALT, 5 RALT): the engine's own
// held-key state for shift / ctrl / alt, split by the side the thread message
// hook in Modifier.cpp saw. C++ pollers (the `#showtooltip`
// evaluator) compare it across ticks to re-evaluate `[mod:...]`
// conditionals the frame a modifier changes, instead of waiting for the
// periodic pass.
//
// While a KeyStateOverride is in scope this returns the held mask minus the
// binding key's own modifiers instead — 4.3.4's CGGameUI::IsKeyDown under
// CGUIBindings::m_keyStateOverride.
uint32_t CurrentMask();

// The engine's EventIsKeyDown (FUN_GAMEUI_IS_KEY_DOWN), called the way its
// own callers do — through the hook above, so a KeyStateOverride applies.
// `key` is 0 shift, 1 ctrl, 2 alt, or any other KEY code.
bool IsKeyDown(int key);

// Presses (`down`) or releases a modifier for a gamepad button that emulates
// it (GamePadEmulateShift / Ctrl / Alt). `key` is 0 shift, 1 ctrl, 2 alt.
// 3.80.2 sends the right-side key (VK_RSHIFT / VK_RCONTROL / VK_RMENU) down
// its OS key path, so the press is a real key press here too: a record in
// the window input ring, which the next tick's input drain dispatches like
// the keyboard's (IsShiftKeyDown, the key walk, chords). It shows as the
// right side: IsRightShiftKeyDown, MODIFIER_STATE_CHANGED("RSHIFT", 1).
// A repeat of the current state does nothing.
void Emulate(int key, bool down);

// CGUIBindings::AddModifiers: consumes leading modifier names from `text`
// ("SHIFT", "LCTRL", "RALT", … each optionally followed by '-') and returns
// their bits. `text` is left at the first character that is not one.
uint32_t ParseModifiers(const char *&text);

// 4.3.4's key-state override around a key binding's command
// (CGUIBindings::ExecKey; Bindings::Api applies it to direct actions only):
// for its lifetime every modifier read — CurrentMask,
// the Is*KeyDown functions, and the engine's own IsKeyDown — sees the keys
// held now minus the modifiers named in `boundKey`, the binding that matched
// (StripModifiers). Nestable; restores the previous state.
class KeyStateOverride {
  public:
    explicit KeyStateOverride(const char *boundKey);
    ~KeyStateOverride();
    KeyStateOverride(const KeyStateOverride &) = delete;
    KeyStateOverride &operator=(const KeyStateOverride &) = delete;

  private:
    bool m_wasActive;
    uint32_t m_previousMask;
};

} // namespace Input::Modifier
