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

// The soft-interact highlight: `softinteract` glows as the object under the
// cursor does, while the player can interact with it right now (in reach).
// `softenemy` / `softfriend` get no glow; their mark is the circle
// (SoftTargetCircle). No cvar of its own: it follows SoftTargetInteract.
//
// The mouseover's glow is the engine's light bits: PublishWorldMouseover
// sets bit 1 through CGObject_C::SetLight on an object with a model and
// clears it through ClearLight, which blacks the emissive only once no bit
// is left. The soft interact takes bit 2, past the engine's two, so the
// mouseover and the target leaving never take its glow, nor the reverse.
// SetLight runs every frame, not only on a change: a model loaded or
// swapped after the pick starts with a black emissive.

#include "Game.h"
#include "Offsets.h"
#include "object/Resolve.h"
#include "target/SoftInteract.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Target::SoftInteractHighlight {

namespace {

using Light_t = void(__thiscall *)(void *obj, int bit);

// Bits 0 and 1 are the engine's (target, mouseover).
constexpr int kBit = 2;

// The highlighted GUID, 0 for none.
uint64_t g_lit = 0;

bool HasModel(void *object) {
    return Game::Read<void *>(object, Offsets::OFF_CGOBJECT_OVERRIDE_MODEL) != nullptr ||
           Game::Read<void *>(object, Offsets::OFF_CGOBJECT_MODEL) != nullptr;
}

// The UI frame, after the scene, as the circles and icons pick.
void OnFrameTick() {
    if (!Object::RegistryReady()) {
        g_lit = 0;
        return;
    }
    const uint64_t guid = SoftInteract::Get();
    void *object = guid ? Object::ByGuid(Offsets::TYPEMASK_OBJECT, guid) : nullptr;
    const uint64_t lit =
        object != nullptr && HasModel(object) && SoftInteract::InReach(object) ? guid : 0;
    if (lit != g_lit) {
        if (void *old = Object::ByGuid(Offsets::TYPEMASK_OBJECT, g_lit))
            reinterpret_cast<Light_t>(Offsets::FUN_CGOBJECT_CLEAR_LIGHT)(old, kBit);
        g_lit = lit;
    }
    if (lit != 0)
        reinterpret_cast<Light_t>(Offsets::FUN_CGOBJECT_SET_LIGHT)(object, kBit);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

} // namespace

} // namespace Target::SoftInteractHighlight
