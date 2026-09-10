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

#include "input/Modifier.h"

#include "Game.h"
#include "event/Custom.h"

#include <cstdint>

#include <windows.h>

// `IsLeft/RightShiftKeyDown` / `IsLeft/RightControlKeyDown` /
// `IsLeft/RightAltKeyDown` / `IsModifierKeyDown` and the
// `MODIFIER_STATE_CHANGED(key, down)` event.
//
// 1.12's own modifier-key path collapses left and right into a single
// shift/ctrl/alt boolean (the engine's `IsShiftKeyDown` chain queries
// `GetKeyState(VK_SHIFT)` — `0x10`, the L+R-merged virtual key — at
// `0x00436024`, never the L/R-distinguishing `VK_LSHIFT` / `VK_RSHIFT`
// virtual keys). The information physically exists at the OS level but
// the engine doesn't expose it.
//
// We capture L/R distinction by installing a `WH_GETMESSAGE` hook on
// our thread (which is the engine's main message-pump thread). The
// hook intercepts every message dequeued by `GetMessage` /
// `PeekMessage` *before* it's dispatched — including
// `WM_KEY{,SYS}{DOWN,UP}`. The VK code in `wParam` plus the scancode +
// extended-key bit in `lParam` uniquely identify L vs R for each
// modifier; we maintain a cached 6-bit bitmap that the seven query
// functions read, and a transition (old != new) fires
// `MODIFIER_STATE_CHANGED`.
//
// Why a thread-message hook instead of a `SetWindowLongPtr` subclass:
// the engine recreates its main window on some renderer state changes
// (verified empirically — toggling vsync makes a previously-installed
// `WNDPROC` subclass go silent, because the new HWND has its own
// pristine WNDPROC and our subclass is left dangling on a destroyed
// window). A thread-level hook attaches to the message *thread*, not a
// specific HWND, so it survives any number of window recreations.

namespace Input::Modifier {

namespace {

constexpr int BIT_LSHIFT = 0;
constexpr int BIT_RSHIFT = 1;
constexpr int BIT_LCTRL  = 2;
constexpr int BIT_RCTRL  = 3;
constexpr int BIT_LALT   = 4;
constexpr int BIT_RALT   = 5;
constexpr uint32_t MASK_ANY_MODIFIER = 0x3F;

const char *const kKeyName[6] = {
    "LSHIFT", "RSHIFT", "LCTRL", "RCTRL", "LALT", "RALT",
};

// L/R-specific virtual keys, one per bit, for the focus-regain resync.
// `GetAsyncKeyState` accepts the L/R-distinguishing VKs directly (unlike
// the keyboard-message path, where `VK_SHIFT` arrives merged and has to be
// disambiguated via the scancode).
const int kBitVk[6] = {
    VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
};

constexpr const char *kModifierStateChanged = "MODIFIER_STATE_CHANGED";

// Static-init reservation — runs before DllMain. The
// `RebuildEventTable` hook appends our name to the engine's event-
// table input when the engine populates it.
const Event::Custom::AutoReserve _reserveModifierStateChanged{kModifierStateChanged};

uint32_t g_modifierMask = 0;
HHOOK g_msgHook = nullptr;
HHOOK g_wndProcHook = nullptr;

void PushDown(void *L, bool down) {
    if (down)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
}

// Apply a single modifier bit transition: update the cached mask and, on a
// genuine change, fire `MODIFIER_STATE_CHANGED`. Both the keyboard-message
// path and the focus-regain resync funnel through here so the "only
// transitions fire" invariant holds regardless of source.
void SetBit(int bitIdx, bool down) {
    const uint32_t bit = 1u << bitIdx;
    const bool wasDown = (g_modifierMask & bit) != 0;
    if (down == wasDown)
        return;

    if (down) g_modifierMask |= bit;
    else      g_modifierMask &= ~bit;

    // Read the slot at fire time (never cache it) — `/reload` rebuilds the
    // event table and the reservation re-claims, which may shift the index.
    const int slot = _reserveModifierStateChanged.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "%s%d", kKeyName[bitIdx], static_cast<int>(down));
}

template <int BitIdx>
int __fastcall Script_IsKeyDown(void *L) {
    const bool down = (g_modifierMask & (1u << BitIdx)) != 0;
    PushDown(L, down);
    return 1;
}

int __fastcall Script_IsModifierKeyDown(void *L) {
    const bool down = (g_modifierMask & MASK_ANY_MODIFIER) != 0;
    PushDown(L, down);
    return 1;
}

// Decode the L/R bit position from a key message.
//   - `VK_SHIFT` (the merged virtual key the OS sends for either key)
//     resolves via `MapVirtualKeyA(scancode, MAPVK_VSC_TO_VK_EX)` —
//     the only reliable way to distinguish LSHIFT/RSHIFT from a
//     keyboard message, since shift has no extended-key bit.
//   - `VK_CONTROL` / `VK_MENU` use the `KF_EXTENDED` (bit 24) flag in
//     `lParam`: extended = right key, plain = left key. (Some
//     keyboards' AltGr generates RMENU + LCONTROL; we accept that as-is
//     since it's how the OS reports the keystate.)
//   - The OS occasionally delivers VK_L*/VK_R* directly (e.g. via
//     `SendInput`-injected synthetic events); accept them too.
// Returns -1 for non-modifier keys.
int VkToBitIdx(WPARAM wParam, LPARAM lParam) {
    const UINT vk = static_cast<UINT>(wParam);
    const UINT scancode = (lParam >> 16) & 0xFF;
    const bool extended = ((lParam >> 24) & 1) != 0;

    if (vk == VK_SHIFT) {
        const UINT actual = MapVirtualKeyA(scancode, 3 /* MAPVK_VSC_TO_VK_EX */);
        if (actual == VK_LSHIFT) return BIT_LSHIFT;
        if (actual == VK_RSHIFT) return BIT_RSHIFT;
        return BIT_LSHIFT;  // best-effort fallback; treat as left
    }
    if (vk == VK_LSHIFT)   return BIT_LSHIFT;
    if (vk == VK_RSHIFT)   return BIT_RSHIFT;
    if (vk == VK_CONTROL)  return extended ? BIT_RCTRL : BIT_LCTRL;
    if (vk == VK_LCONTROL) return BIT_LCTRL;
    if (vk == VK_RCONTROL) return BIT_RCTRL;
    if (vk == VK_MENU)     return extended ? BIT_RALT : BIT_LALT;
    if (vk == VK_LMENU)    return BIT_LALT;
    if (vk == VK_RMENU)    return BIT_RALT;
    return -1;
}

void ProcessKeyMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    const int bitIdx = VkToBitIdx(wParam, lParam);
    if (bitIdx < 0)
        return;
    const bool nowDown = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    SetBit(bitIdx, nowDown);
}

// Reconcile the cached mask with the true physical key state. Needed on
// focus regain: keyboard messages only arrive while WoW has focus, so a
// modifier released while WoW was in the background is never seen by the
// `WH_GETMESSAGE` hook and the bit stays stuck down (and the release event
// never fires). `GetAsyncKeyState` reads global async keyboard state
// regardless of focus, so polling it on activation recovers the missed
// transitions — `SetBit` fires `MODIFIER_STATE_CHANGED` for each drift.
void ResyncFromPhysicalState() {
    for (int b = 0; b < 6; ++b) {
        const bool down = (GetAsyncKeyState(kBitVk[b]) & 0x8000) != 0;
        SetBit(b, down);
    }
}

LRESULT CALLBACK GetMsgHook(int code, WPARAM wParam, LPARAM lParam) {
    // Only process when the message is being removed from the queue
    // (`PM_REMOVE` / `GetMessage`); `PM_NOREMOVE` peeks would deliver
    // the same key twice if we processed both.
    if (code == HC_ACTION && wParam == PM_REMOVE) {
        const MSG *m = reinterpret_cast<const MSG *>(lParam);
        if (m->message == WM_KEYDOWN || m->message == WM_KEYUP ||
            m->message == WM_SYSKEYDOWN || m->message == WM_SYSKEYUP) {
            ProcessKeyMessage(m->message, m->wParam, m->lParam);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// `WM_ACTIVATEAPP` is *sent* (via the window manager), not *posted* to the
// queue, so `WH_GETMESSAGE` never sees it — a `WH_CALLWNDPROC` hook catches
// sent messages before the target WNDPROC runs. On app activation
// (`wParam != FALSE`) we resync from physical key state to recover any
// modifier released while WoW was in the background. Like the message hook,
// this is thread-level (not per-`HWND`), so it survives window recreation.
LRESULT CALLBACK CallWndProcHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const CWPSTRUCT *cwp = reinterpret_cast<const CWPSTRUCT *>(lParam);
        if (cwp->message == WM_ACTIVATEAPP && cwp->wParam != FALSE)
            ResyncFromPhysicalState();
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void InstallHook() {
    if (g_msgHook == nullptr) {
        g_msgHook = SetWindowsHookExA(WH_GETMESSAGE, &GetMsgHook, nullptr,
                                       GetCurrentThreadId());
    }
    if (g_wndProcHook == nullptr) {
        g_wndProcHook = SetWindowsHookExA(WH_CALLWNDPROC, &CallWndProcHook,
                                          nullptr, GetCurrentThreadId());
    }
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsLeftShiftKeyDown",
                                      &Script_IsKeyDown<BIT_LSHIFT>);
    Game::Lua::RegisterGlobalFunction("IsRightShiftKeyDown",
                                      &Script_IsKeyDown<BIT_RSHIFT>);
    Game::Lua::RegisterGlobalFunction("IsLeftControlKeyDown",
                                      &Script_IsKeyDown<BIT_LCTRL>);
    Game::Lua::RegisterGlobalFunction("IsRightControlKeyDown",
                                      &Script_IsKeyDown<BIT_RCTRL>);
    Game::Lua::RegisterGlobalFunction("IsLeftAltKeyDown",
                                      &Script_IsKeyDown<BIT_LALT>);
    Game::Lua::RegisterGlobalFunction("IsRightAltKeyDown",
                                      &Script_IsKeyDown<BIT_RALT>);
    Game::Lua::RegisterGlobalFunction("IsModifierKeyDown",
                                      &Script_IsModifierKeyDown);

    InstallHook();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

uint32_t CurrentMask() {
    return g_modifierMask;
}

} // namespace Input::Modifier
