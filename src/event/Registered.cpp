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

// `GetFramesRegisteredForEvent(event)` → the Frame objects currently
// subscribed to `event`, returned as multiple return values (matching
// modern WoW's vararg signature — not a table). Empty result set returns
// nothing.
//
// The engine already keeps exactly this list: every `frame:RegisterEvent`
// appends the frame to a subscriber chain hanging off the matching event
// table entry. We find the entry by name (same table `Event::Custom`
// walks), then walk the chain the same way `Frame::RegisterEvent`
// (`FUN_00702140`) does — node next at `+0x04`, subscriber frame at
// `+0x08`, with a low-bit-set / null value marking the end sentinel — and
// push each frame via the shared `UI::FrameObject::Push` so callers get
// the canonical wrapper table (real frame methods, `:GetName()`, etc.).
//
// Event names resolve through `Event::Custom::LookupByName`, so this also
// works for our `AutoReserve`-claimed custom events, not just engine ones.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "ui/FrameObject.h"

#include <cstdint>

namespace Event::Registered {

namespace {

int __fastcall Script_GetFramesRegisteredForEvent(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const char *event = Game::Lua::ToString(L, 1);
    if (event == nullptr)
        return 0;

    const int slot = Event::Custom::LookupByName(event);
    if (slot < 0)
        return 0;

    auto *base = Game::Read<uint8_t *>(Offsets::VAR_EVENT_TABLE_BASE_PTR);
    if (base == nullptr)
        return 0;

    const uint8_t *entry = base + slot * Offsets::EVENT_ENTRY_STRIDE;
    const uintptr_t head = Game::Read<uintptr_t>(entry, Offsets::OFF_EVENT_ENTRY_HEAD);

    // First pass: count subscribers so we can reserve stack up front — an
    // event with many registered frames would otherwise risk overflowing
    // the default Lua stack slack as we push.
    int count = 0;
    for (uintptr_t node = head; (node & 1) == 0 && node != 0;
         node = Game::Read<uintptr_t>(node + Offsets::OFF_EVENT_NODE_NEXT)) {
        if (Game::Read<void *>(node + Offsets::OFF_EVENT_NODE_FRAME))
            ++count;
    }
    if (count == 0)
        return 0;
    Game::Lua::CheckStack(L, count);

    int pushed = 0;
    for (uintptr_t node = head; (node & 1) == 0 && node != 0;
         node = Game::Read<uintptr_t>(node + Offsets::OFF_EVENT_NODE_NEXT)) {
        void *frame = Game::Read<void *>(node + Offsets::OFF_EVENT_NODE_FRAME);
        if (frame != nullptr) {
            UI::FrameObject::Push(L, frame);
            ++pushed;
        }
    }
    return pushed;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("event", "cstring"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Vararg("SimpleFrame",
                      "Nothing when no frame is registered or the event name is unknown."),
};
const Game::Doc::Function kGetFramesRegisteredForEvent{
    "Returns the frames registered for an event.", kArgs, kRets, "FrameGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetFramesRegisteredForEvent",
                                      &Script_GetFramesRegisteredForEvent,
                                      &kGetFramesRegisteredForEvent);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Event::Registered
