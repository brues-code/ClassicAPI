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

// `GLOBAL_MOUSE_DOWN` / `GLOBAL_MOUSE_UP` — modern WoW fires these
// on every raw mouse-button press/release on the game window, with a
// `button` string payload (`"LeftButton"` / `"RightButton"` /
// `"MiddleButton"` / `"Button4"` / `"Button5"`). Distinct from
// per-frame `OnMouseDown` scripts — these fire even when the click
// misses any UI frame.
//
// `IsMouseButtonDown([button])` — query the current held state of a
// specific button by name or 1-based ID, or "any button held" when
// called with no args. State is maintained from the same hook stream
// as the events (single source of truth — events firing and the
// state being readable happen at the same point in the pipeline).
//
// `GetMouseButtonClicked()` is NOT here — it is not a mouse-device
// query at all. The engine scopes it to a click DISPATCH, bracketing
// the value around the handler rather than tracking the device, so it
// lives with that dispatch in [Frame::ClickEvents](../frame/ClickEvents.cpp).
//
// Same `WH_GETMESSAGE` thread-message hook pattern as
// [Input::Modifier](Modifier.cpp): we intercept the WM_*BUTTON*
// messages on the engine's main thread *before* dispatch. The hook
// scopes naturally to WoW's message queue, so clicks while
// alt-tabbed elsewhere don't deliver — no foreground check needed.
// XBUTTON side-key distinction comes from `GET_XBUTTON_WPARAM`.

#include "Game.h"
#include "event/Custom.h"

#include <cstdint>
#include <cstring>

#include <windows.h>

namespace Input::GlobalMouse {

namespace {

const Event::Custom::AutoReserve _reserveDown{"GLOBAL_MOUSE_DOWN"};
const Event::Custom::AutoReserve _reserveUp{"GLOBAL_MOUSE_UP"};

// Held-state bitmap — bit `i` set means the button with 1-based
// `IsMouseButtonDown` ID `i+1` is currently held. Modern API IDs:
//   1 = LeftButton, 2 = RightButton, 3 = MiddleButton,
//   4 = Button4    (XBUTTON1), 5 = Button5 (XBUTTON2).
constexpr int BIT_LEFT_BUTTON   = 0;
constexpr int BIT_RIGHT_BUTTON  = 1;
constexpr int BIT_MIDDLE_BUTTON = 2;
constexpr int BIT_BUTTON4       = 3;
constexpr int BIT_BUTTON5       = 4;
constexpr uint32_t MASK_ANY_BUTTON = 0x1F;

uint32_t g_mouseButtonMask = 0;

HHOOK g_msgHook = nullptr;

// Maps WM_*BUTTON{DOWN,UP} → ("LeftButton"|..., isDown). Returns
// nullptr for non-mouse messages so the caller can short-circuit.
const char *DecodeButton(UINT msg, WPARAM wParam, bool *outDown) {
    switch (msg) {
        case WM_LBUTTONDOWN: *outDown = true;  return "LeftButton";
        case WM_LBUTTONUP:   *outDown = false; return "LeftButton";
        case WM_RBUTTONDOWN: *outDown = true;  return "RightButton";
        case WM_RBUTTONUP:   *outDown = false; return "RightButton";
        case WM_MBUTTONDOWN: *outDown = true;  return "MiddleButton";
        case WM_MBUTTONUP:   *outDown = false; return "MiddleButton";
        case WM_XBUTTONDOWN:
            *outDown = true;
            return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? "Button4" : "Button5";
        case WM_XBUTTONUP:
            *outDown = false;
            return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? "Button4" : "Button5";
        default:
            return nullptr;
    }
}

// Maps a "LeftButton"/.../"Button5" name to its bit index. -1 for
// any other string.
int NameToBitIdx(const char *name) {
    if (name == nullptr)
        return -1;
    if (std::strcmp(name, "LeftButton") == 0)   return BIT_LEFT_BUTTON;
    if (std::strcmp(name, "RightButton") == 0)  return BIT_RIGHT_BUTTON;
    if (std::strcmp(name, "MiddleButton") == 0) return BIT_MIDDLE_BUTTON;
    if (std::strcmp(name, "Button4") == 0)      return BIT_BUTTON4;
    if (std::strcmp(name, "Button5") == 0)      return BIT_BUTTON5;
    return -1;
}

void ProcessMouseMessage(UINT msg, WPARAM wParam) {
    bool down = false;
    const char *name = DecodeButton(msg, wParam, &down);
    if (name == nullptr)
        return;

    const int bitIdx = NameToBitIdx(name);
    if (bitIdx >= 0) {
        const uint32_t bit = 1u << bitIdx;
        if (down) g_mouseButtonMask |= bit;
        else      g_mouseButtonMask &= ~bit;
    }

    const int slot = (down ? _reserveDown : _reserveUp).Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "%s", name);
}

int __fastcall Script_IsMouseButtonDown(void *L) {
    bool down;
    if (Game::Lua::GetTop(L) == 0) {
        // No-arg form: any button held.
        down = (g_mouseButtonMask & MASK_ANY_BUTTON) != 0;
    } else {
        int bitIdx = -1;
        if (Game::Lua::IsNumber(L, 1)) {
            const int id = static_cast<int>(Game::Lua::ToNumber(L, 1));
            if (id >= 1 && id <= 5)
                bitIdx = id - 1;
        } else if (Game::Lua::IsString(L, 1)) {
            bitIdx = NameToBitIdx(Game::Lua::ToString(L, 1));
        }
        // Unrecognized button (bad id, unknown name) → false, matching
        // the modern API's "did this specific button happen to be held"
        // semantic. Bad input doesn't promote to the any-button check.
        down = (bitIdx >= 0) && (g_mouseButtonMask & (1u << bitIdx)) != 0;
    }
    Game::Lua::PushBool(L, down);
    return 1;
}

LRESULT CALLBACK GetMsgHook(int code, WPARAM wParam, LPARAM lParam) {
    // Only process when the message is being removed from the queue
    // (`PM_REMOVE` / `GetMessage`); `PM_NOREMOVE` peeks would deliver
    // the same click twice if we processed both.
    if (code == HC_ACTION && wParam == PM_REMOVE) {
        const MSG *m = reinterpret_cast<const MSG *>(lParam);
        ProcessMouseMessage(m->message, m->wParam);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void InstallHook() {
    if (g_msgHook != nullptr)
        return;
    g_msgHook = SetWindowsHookExA(WH_GETMESSAGE, &GetMsgHook, nullptr,
                                   GetCurrentThreadId());
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsMouseButtonDown",
                                      &Script_IsMouseButtonDown);
    InstallHook();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Input::GlobalMouse
