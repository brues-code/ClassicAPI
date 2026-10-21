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

// The hover cursor's verdict for an object, without the hover.
//
// The world frame picks the cursor for the object under the mouse in
// ClassifyHoveredObject (0x4828D0): the object's IsMouseoverable, then by
// type word a per-type classifier that ends in SetCursorMode. That verdict
// is the engine's whole "what can I do with this" rule — NPC services,
// loot, skinning, attack, game object use, and the Unable form beyond reach
// — so it is reused, not rebuilt:
//   - units / players and corpses: their classifiers only call
//     SetCursorMode (FUN_WORLD_FRAME_CLASSIFY_*), so they run with that call
//     captured by a hook and nothing is shown;
//   - game objects: GetCursor, the classifier's own source of the mode (the
//     classifier adds only a custom-cursor texture load for mode 41).
// The spell-targeting and mouselook legs of 0x4828D0 are left out: they
// answer for the mouse, not for the object.

#include "cursor/Hover.h"

#include "Game.h"
#include "Offsets.h"

namespace Cursor::Hover {

namespace {

using SetCursorMode_t = void(__fastcall *)(uint32_t mode);
using Classify_t = void(__fastcall *)(void *worldFrame, void *edx, void *object);
using ObjectQuery_t = uint32_t(__fastcall *)(void *object, void *edx);
using IsUsable_t = uint8_t(__fastcall *)(void *object, void *edx);

SetCursorMode_t SetCursorMode_o = nullptr;
bool g_capturing = false;
uint32_t g_captured = 0;

void __fastcall SetCursorMode_h(uint32_t mode) {
    if (g_capturing) {
        g_captured = mode;
        return;
    }
    SetCursorMode_o(mode);
}

uint32_t Capture(uintptr_t classifier, void *worldFrame, void *object) {
    g_captured = 0;
    g_capturing = true;
    reinterpret_cast<Classify_t>(classifier)(worldFrame, nullptr, object);
    g_capturing = false;
    return g_captured;
}

uint32_t TypeWord(const void *object) {
    const auto *fields = Game::Read<const uint8_t *>(object, Offsets::OFF_UNIT_GUID_PTR);
    return fields == nullptr ? 0 : Game::Read<uint32_t>(fields, Offsets::OFF_OBJECT_FIELD_TYPE);
}

bool IsMouseoverable(void *object) {
    auto *const *vtable = *static_cast<void *const *const *>(object);
    auto fn = reinterpret_cast<ObjectQuery_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_IS_MOUSEOVERABLE / sizeof(void *)]);
    return fn(object, nullptr) != 0;
}

const Game::HookAutoRegister _hook{Offsets::FUN_SET_CURSOR_MODE,
                                   reinterpret_cast<void *>(&SetCursorMode_h),
                                   reinterpret_cast<void **>(&SetCursorMode_o)};

} // namespace

uint32_t Mode(void *object) {
    void *worldFrame = Game::Read<void *>(Offsets::VAR_GAME_STATE_PTR);
    if (object == nullptr || worldFrame == nullptr || !IsMouseoverable(object))
        return 0;
    switch (TypeWord(object)) {
    case Offsets::OBJECT_TYPE_WORD_UNIT:
    case Offsets::OBJECT_TYPE_WORD_PLAYER:
        return Capture(Offsets::FUN_WORLD_FRAME_CLASSIFY_UNIT, worldFrame, object);
    case Offsets::OBJECT_TYPE_WORD_CORPSE:
        return Capture(Offsets::FUN_WORLD_FRAME_CLASSIFY_CORPSE, worldFrame, object);
    case Offsets::OBJECT_TYPE_WORD_GAMEOBJECT:
        return reinterpret_cast<ObjectQuery_t>(Offsets::FUN_GAMEOBJECT_GET_CURSOR)(object,
                                                                                    nullptr);
    default:
        return 0;
    }
}

bool IsUsable(void *object) {
    return reinterpret_cast<IsUsable_t>(Offsets::FUN_GAMEOBJECT_IS_USABLE)(object, nullptr) != 0;
}

} // namespace Cursor::Hover
