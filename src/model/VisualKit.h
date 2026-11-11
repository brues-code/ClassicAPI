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
#include <vector>

// A spell visual kit on a model in a Model frame's scene (model/VisualKit.cpp).
namespace Model::VisualKit {

// The SpellVisualKit record, or null.
const uint8_t *Record(uint32_t kitID);

// Hangs the kit's effect models on `model`, a model in `frame`'s scene, lit
// by the frame's light, and applies its tint and alpha procedures. A oneShot
// effect stops when its sequence ends; the others loop. When `effects` is
// given, each attached effect model is appended to it with one reference the
// caller then owns; otherwise the effects live as long as `model` holds them.
void Apply(void *frame, void *model, const uint8_t *kit, bool oneShot,
           std::vector<void *> *effects = nullptr);

// The world's KitEffectLoopCallback (0x0060ED00) as a done callback: a
// sequence that ended cleanly plays again.
const void *LoopCallback();

// Stops a lasting effect that Apply handed over, taking its reference, as an
// aura's removal stops a state kit's effects (0x00614150 with force): a
// loaded model with Decay (159) plays it and stops when it ends; any other
// stops at once. A stopped effect hides, and goes once its particles are gone.
void Stop(void *frame, void *effect);

// Whether the kit sets the model's tint or alpha (procedure kinds 1 and 14).
bool SetsColor(const uint8_t *kit);

// The kit's tint and alpha procedures on `model`, as Apply runs them.
void ApplyProcedures(void *model, const uint8_t *kit);

} // namespace Model::VisualKit
