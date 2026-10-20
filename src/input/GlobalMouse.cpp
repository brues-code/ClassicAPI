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

// `GLOBAL_MOUSE_DOWN(button)` / `GLOBAL_MOUSE_UP(button)` and
// `IsMouseButtonDown([button])`.
//
// The engine already has the whole pipeline. Its window procedure queues each
// button message; once per tick PostMouseDownEvent / PostMouseUpEvent update
// the held-button mask (VAR_HELD_MOUSE_BUTTONS) and send event ids
// EVENT_ID_MOUSEDOWN / _MOUSEUP to every handler registered on the event bus
// (benilla-re core/input, core/events). The interface is one such handler, at
// priority 1.0. So:
//
//   - The events are two handlers of our own on the same bus, registered once
//     through EventRegisterEx, the same way Console::Completion registers its
//     key handler. At GLOBAL_MOUSE_EVENT_PRIORITY they run before the
//     interface, so they fire even for a click a frame takes, and they always
//     answer 1 so the dispatch goes on. The loading-screen and screen-fade
//     vetoes and the console sit above them, so a click those swallow fires
//     nothing.
//   - A lost mouse capture makes the engine release every held button through
//     PostMouseUpEvent, so GLOBAL_MOUSE_UP fires for those too.
//   - IsMouseButtonDown reads the engine's mask, which a window deactivation
//     clears — no button stays "held" after an alt-tab.
//
// The payload names come from the engine's own literals through
// Frame::ClickEvents::ButtonNameForMask, whose masks are the same bits.
//
// `GetMouseButtonClicked()` is NOT here — it is not a mouse-device query. The
// engine scopes it to a click DISPATCH, so it lives with that dispatch in
// [Frame::ClickEvents](../frame/ClickEvents.cpp).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "frame/ClickEvents.h"

#include <cstdint>
#include <cstring>

namespace Input::GlobalMouse {

namespace {

const Game::Doc::Field kButtonPayload[] = {
    Game::Doc::Req("button", "string",
                   "\"LeftButton\", \"RightButton\", \"MiddleButton\", \"Button4\" or "
                   "\"Button5\"."),
};
const Game::Doc::Event kDownDoc{
    "UIParent", "Fires when a mouse button is pressed, whether or not a frame takes the click.",
    kButtonPayload};
const Game::Doc::Event kUpDoc{
    "UIParent", "Fires when a mouse button is released, whether or not a frame takes the click.",
    kButtonPayload};
const Event::Custom::AutoReserve _reserveDown{"GLOBAL_MOUSE_DOWN", &kDownDoc};
const Event::Custom::AutoReserve _reserveUp{"GLOBAL_MOUSE_UP", &kUpDoc};

// IsMouseButtonDown's 1-based IDs (1 Left, 2 Right, 3 Middle, 4 Button4,
// 5 Button5) to the engine's button bits.
constexpr uint32_t kButtonBits[] = {0x01, 0x04, 0x02, 0x08, 0x10};
constexpr uint32_t kAnyButton = 0x1F;

uint32_t HeldButtons() {
    return *reinterpret_cast<const volatile uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_HELD_MOUSE_BUTTONS));
}

void Fire(const Event::Custom::AutoReserve &reserve, const void *payload) {
    const uint32_t button = *reinterpret_cast<const uint32_t *>(
        static_cast<const uint8_t *>(payload) + Offsets::OFF_MOUSE_EVENT_BUTTON);
    const int slot = reserve.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "%s", Frame::ClickEvents::ButtonNameForMask(button));
}

// Event-bus handlers: `int __fastcall(payload, param)`; nonzero lets the
// dispatch reach the lower-priority handlers.
int __fastcall OnMouseDown(const void *payload, void * /*param*/) {
    Fire(_reserveDown, payload);
    return 1;
}

int __fastcall OnMouseUp(const void *payload, void * /*param*/) {
    Fire(_reserveUp, payload);
    return 1;
}

int __fastcall Script_IsMouseButtonDown(void *L) {
    const uint32_t held = HeldButtons();
    bool down;
    if (Game::Lua::Type(L, 1) <= Game::Lua::TYPE_NIL) {
        down = (held & kAnyButton) != 0; // no argument: any button
    } else {
        uint32_t bit = 0;
        if (Game::Lua::IsNumber(L, 1)) {
            const int id = static_cast<int>(Game::Lua::ToNumber(L, 1));
            if (id >= 1 && id <= 5)
                bit = kButtonBits[id - 1];
        } else if (Game::Lua::IsString(L, 1)) {
            const char *name = Game::Lua::ToString(L, 1);
            for (uint32_t mask = 0x01; mask <= 0x10; mask <<= 1)
                if (std::strcmp(name, Frame::ClickEvents::ButtonNameForMask(mask)) == 0)
                    bit = mask;
        }
        // An unknown button is simply not held; it does not fall back to the
        // any-button check.
        down = (held & bit) != 0;
    }
    Game::Lua::PushBool(L, down);
    return 1;
}

using Handler_t = int(__fastcall *)(const void *payload, void *param);
using RegisterEx_t = void(__fastcall *)(int eventId, Handler_t handler, void *param,
                                        float priority);

void RegisterHandlers() {
    static bool registered = false; // the bus does not dedup
    if (registered)
        return;
    registered = true;
    auto registerEx = reinterpret_cast<RegisterEx_t>(Offsets::FUN_EVENT_REGISTER_EX);
    const float priority = static_cast<float>(Offsets::GLOBAL_MOUSE_EVENT_PRIORITY);
    registerEx(Offsets::EVENT_ID_MOUSEDOWN, &OnMouseDown, nullptr, priority);
    registerEx(Offsets::EVENT_ID_MOUSEUP, &OnMouseUp, nullptr, priority);
}

const Game::Doc::Field kIsDownArgs[] = {
    Game::Doc::Opt("button", "string", nullptr,
                   "A button name, or an ID from 1 (LeftButton) to 5 (Button5). "
                   "Omitted means any button."),
};
const Game::Doc::Field kIsDownRets[] = {
    Game::Doc::Req("isDown", "bool"),
};
const Game::Doc::Function kIsMouseButtonDown{
    "Whether a mouse button is held now.", kIsDownArgs, kIsDownRets, "UIParent"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsMouseButtonDown", &Script_IsMouseButtonDown,
                                      &kIsMouseButtonDown);
    RegisterHandlers();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Input::GlobalMouse
