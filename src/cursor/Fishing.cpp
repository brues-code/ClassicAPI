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

// With `expandedCursors` at 1 (Cursor::Extended::Enabled), the cursor over
// the player's fishing bobber is a fish hook in place of the Interact gear.
//
// A game object's cursor is data. GetCursor (FUN_GAMEOBJECT_GET_CURSOR) gives
// the cursor its record names, else its type handler's: BaseHandlerGetCursor
// for most types, which gives a lock's LockType cursor, else Interact. The
// record's name comes from the server, and tortoise-wow sends it empty; even
// 3.3.5's data names none for the bobber (AzerothCore's template 35591), and
// a bobber has no lock, so every client shows the gear. Later clients ship
// the hook (Interface\Cursor\Fishing.blp) only for data to name.
//
// The fishing node handler (type 17) gets its own cursor slot, the way
// 4.3.4's guild bank type overrides its cursor. The slot still runs
// BaseHandlerGetCursor, which decides everything (0 for a bobber the player
// is not channelling, Unable when not usable), and swaps only the image. The
// classifier passes the mode to SetCursorMode, whose hook (cursor/Hover)
// shows it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cursor/Extended.h"

#include <cstdint>

namespace Cursor::Fishing {

namespace {

using HandlerGetCursor_t = uint32_t(__thiscall *)(void *handler);

uint32_t __fastcall FishingNodeCursor(void *handler, void * /*edx*/) {
    const uint32_t mode =
        reinterpret_cast<HandlerGetCursor_t>(Offsets::FUN_GO_HANDLER_BASE_GET_CURSOR)(handler);
    if (!Cursor::Extended::Enabled())
        return mode;
    if (mode == Offsets::CURSOR_MODE_INTERACT)
        return Cursor::Extended::kFishing;
    if (mode == Offsets::CURSOR_MODE_INTERACT + Offsets::CURSOR_MODE_UNABLE_OFFSET)
        return Cursor::Extended::kFishing + Cursor::Extended::kUnableOffset;
    return mode;
}

void Patch() {
    Common::RedirectSlot(Offsets::SLOT_FISHING_NODE_HANDLER_GET_CURSOR,
                         Offsets::FUN_GO_HANDLER_BASE_GET_CURSOR,
                         reinterpret_cast<const void *>(&FishingNodeCursor));
}

const Game::ModuleAutoRegister _autoreg{&Patch};

} // namespace

} // namespace Cursor::Fishing
