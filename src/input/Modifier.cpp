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
#include "Offsets.h"
#include "event/Custom.h"

#include <cfloat>
#include <cstdint>
#include <cstring>

#include <windows.h>

// `IsLeft/RightShiftKeyDown` / `IsLeft/RightControlKeyDown` /
// `IsLeft/RightAltKeyDown` / `IsModifierKeyDown` and the
// `MODIFIER_STATE_CHANGED(key, down)` event.
//
// 1.12 keeps one merged shift / ctrl / alt state. `IsShiftKeyDown`,
// `IsControlKeyDown` and `IsAltKeyDown` (0x00488590 / 0x004885C0 /
// 0x00488600) ask EventIsKeyDown (FUN_GAMEUI_IS_KEY_DOWN) for key 0 / 1 / 2,
// which looks the code up in the event context's held-key list. The window
// procedure translates VK_SHIFT / VK_CONTROL / VK_MENU to those codes and
// never reads the scan code, so the side is lost before the list is built
// (benilla-re core/input "Key codes", core/events "Held keys and repeats").
//
// 4.3.4 answers the per-side functions from that same list, with six keys.
// We keep the engine's list as the truth and add only the side it drops:
//
//   - A `WH_GETMESSAGE` hook on the engine's message thread sees each
//     `WM_KEY{,SYS}{DOWN,UP}` as it is dequeued and records which side moved
//     (the scan code / extended bit in `lParam`). It fires nothing: at that
//     point the engine has not seen the key yet — it only queues it for the
//     tick's input drain.
//   - Every read is the side bits gated by EventIsKeyDown for the merged key,
//     so IsLeftShiftKeyDown() can never be true while IsShiftKeyDown() is nil.
//   - `MODIFIER_STATE_CHANGED` fires from handlers on the engine's own key and
//     activation events (Reconcile). The dispatch updates the held-key list
//     before any handler runs, so each fired state is exactly what the query
//     functions return in that handler. The same pass drops side bits whose
//     merged key the engine no longer holds — including the whole list an
//     activation change empties — so a modifier released while WoW was in the
//     background does not come back as stale when the other side is pressed.
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

// The two side bits of the engine's merged key 0 / 1 / 2 (shift, ctrl, alt).
constexpr uint32_t kPairs[3] = {(1u << BIT_LSHIFT) | (1u << BIT_RSHIFT),
                                (1u << BIT_LCTRL) | (1u << BIT_RCTRL),
                                (1u << BIT_LALT) | (1u << BIT_RALT)};

constexpr const char *kModifierStateChanged = "MODIFIER_STATE_CHANGED";

// Static-init reservation — runs before DllMain. The
// `RebuildEventTable` hook appends our name to the engine's event-
// table input when the engine populates it.
const Event::Custom::AutoReserve _reserveModifierStateChanged{kModifierStateChanged};

// Which side of each modifier the message hook last saw go down. Only
// meaningful under the engine's held-key list (see HeldMask).
uint32_t g_sideMask = 0;
// The held mask `MODIFIER_STATE_CHANGED` last reported (see Reconcile).
uint32_t g_reportedMask = 0;

// 4.3.4's m_keyStateOverride / m_heldModifiers (see KeyStateOverride).
bool g_overrideActive = false;
uint32_t g_overrideMask = 0;

HHOOK g_msgHook = nullptr;

void PushDown(void *L, bool down) {
    if (down)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
}

// The engine's IsKeyDown for shift / ctrl / alt (0 / 1 / 2) answers from the
// override while a binding runs, as 4.3.4's does — so IsShiftKeyDown & co. and
// every engine modifier read agree with the L/R functions below.
using IsKeyDown_t = int(__fastcall *)(int which);
IsKeyDown_t g_isKeyDownOriginal = nullptr;

int __fastcall IsKeyDown_h(int which) {
    if (!g_overrideActive || which < 0 || which > 2)
        return g_isKeyDownOriginal(which);
    return (g_overrideMask & kPairs[which]) != 0;
}

const Game::HookAutoRegister _isKeyDownHook{Offsets::FUN_GAMEUI_IS_KEY_DOWN,
                                            reinterpret_cast<void *>(&IsKeyDown_h),
                                            reinterpret_cast<void **>(&g_isKeyDownOriginal)};

// The held modifiers by side: the engine's held-key list says WHETHER each
// merged key is down (the original EventIsKeyDown, not the override), the
// side mask says WHICH side.
uint32_t HeldMask() {
    uint32_t mask = 0;
    for (int key = 0; key < 3; ++key)
        if (g_isKeyDownOriginal(key))
            mask |= g_sideMask & kPairs[key];
    return mask;
}

// Run from the engine's key / activation handlers, after the dispatch has
// updated the held-key list: drop side bits the engine no longer holds, and
// fire `MODIFIER_STATE_CHANGED` for each bit that differs from the last
// report.
void Reconcile() {
    const uint32_t held = HeldMask();
    g_sideMask = held;
    const uint32_t changed = held ^ g_reportedMask;
    g_reportedMask = held;
    if (changed == 0)
        return;
    // Read the slot at fire time (never cache it) — `/reload` rebuilds the
    // event table and the reservation re-claims, which may shift the index.
    const int slot = _reserveModifierStateChanged.Slot();
    if (slot < 0)
        return;
    for (int b = 0; b < 6; ++b)
        if (changed & (1u << b))
            Event::Custom::Fire(slot, "%s%d", kKeyName[b],
                                static_cast<int>((held >> b) & 1));
}

template <int BitIdx>
int __fastcall Script_IsKeyDown(void *L) {
    const bool down = (CurrentMask() & (1u << BitIdx)) != 0;
    PushDown(L, down);
    return 1;
}

int __fastcall Script_IsModifierKeyDown(void *L) {
    const bool down = (CurrentMask() & MASK_ANY_MODIFIER) != 0;
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
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
        g_sideMask |= 1u << bitIdx;
    else
        g_sideMask &= ~(1u << bitIdx);
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

// Event-bus handlers; each returns 1 so the dispatch goes on. A key event's
// payload starts with the key code, and only 0 / 1 / 2 are modifiers.
int __fastcall OnKey(const void *payload, void * /*param*/) {
    if (static_cast<uint32_t>(*static_cast<const int *>(payload)) <= 2)
        Reconcile();
    return 1;
}

int __fastcall OnActivate(const void * /*payload*/, void * /*param*/) {
    Reconcile();
    return 1;
}

using Handler_t = int(__fastcall *)(const void *payload, void *param);
using RegisterEx_t = void(__fastcall *)(int eventId, Handler_t handler, void *param,
                                        float priority);

void InstallHook() {
    if (g_msgHook != nullptr)
        return;
    g_msgHook = SetWindowsHookExA(WH_GETMESSAGE, &GetMsgHook, nullptr,
                                  GetCurrentThreadId());
    // The bus does not dedup, so these share the once-only guard. Highest
    // priority: the engine's held-key update runs before every handler, so
    // no other handler's veto (the open console swallows keys) may skip ours.
    auto registerEx = reinterpret_cast<RegisterEx_t>(Offsets::FUN_EVENT_REGISTER_EX);
    registerEx(Offsets::EVENT_ID_KEYDOWN, &OnKey, nullptr, FLT_MAX);
    registerEx(Offsets::EVENT_ID_KEYUP, &OnKey, nullptr, FLT_MAX);
    registerEx(Offsets::EVENT_ID_KEYDOWN_REPEATING, &OnKey, nullptr, FLT_MAX);
    registerEx(Offsets::EVENT_ID_ACTIVATE, &OnActivate, nullptr, FLT_MAX);
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
    return g_overrideActive ? g_overrideMask : HeldMask();
}

uint32_t ParseModifiers(const char *&text) {
    static const struct {
        const char *name;
        uint32_t bits;
    } kNames[] = {
        {"LSHIFT", 1u << BIT_LSHIFT},
        {"RSHIFT", 1u << BIT_RSHIFT},
        {"SHIFT", (1u << BIT_LSHIFT) | (1u << BIT_RSHIFT)},
        {"LCTRL", 1u << BIT_LCTRL},
        {"RCTRL", 1u << BIT_RCTRL},
        {"CTRL", (1u << BIT_LCTRL) | (1u << BIT_RCTRL)},
        {"LALT", 1u << BIT_LALT},
        {"RALT", 1u << BIT_RALT},
        {"ALT", (1u << BIT_LALT) | (1u << BIT_RALT)},
    };
    uint32_t modifiers = 0;
    while (*text != '\0') {
        bool matched = false;
        for (const auto &entry : kNames) {
            const size_t length = std::strlen(entry.name);
            if (_strnicmp(text, entry.name, length) == 0) {
                modifiers |= entry.bits;
                text += length;
                matched = true;
                break;
            }
        }
        if (!matched)
            break;
        if (*text == '-')
            ++text;
    }
    return modifiers;
}

KeyStateOverride::KeyStateOverride(const char *boundKey)
    : m_wasActive(g_overrideActive), m_previousMask(g_overrideMask) {
    const char *rest = boundKey != nullptr ? boundKey : "";
    g_overrideMask = HeldMask() & ~ParseModifiers(rest);
    g_overrideActive = true;
}

KeyStateOverride::~KeyStateOverride() {
    g_overrideActive = m_wasActive;
    g_overrideMask = m_previousMask;
}

} // namespace Input::Modifier
