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

namespace Unit::TokenObserver {

// Descriptor-field observer callback — the engine's __AUCMirrorHandler__ node
// ABI (see the FUN_DESC_OBSERVER_REGISTER block in Offsets.h): return 1,
// callee cleans 0x10. `fieldOffset` is the bank-relative offset as registered;
// the corresponding unit event id is `fieldOffset >> 2` (the descriptor field
// index equals the unit event id — health field +0x40 → event 16 =
// `UNIT_HEALTH`).
using FieldCallback = int(__fastcall *)(uint32_t fieldOffset, uint32_t size,
                                        uint32_t guidLo, uint32_t guidHi,
                                        const uint32_t *oldValue, void *userArg);

// Register / unregister the engine's *complete* unit-event descriptor-field
// observer set for `guid`, dispatching to `cb` on each watched field change.
// Mirrors the engine's own `FUN_0051bbb0` / `FUN_0051bdb0` (identical field
// set + per-field watched sizes), so `cb` fires for exactly the fields the
// engine treats as token-scoped unit events (`UNIT_HEALTH`, `UNIT_MANA`,
// `UNIT_AURA`, `UNIT_LEVEL`, …).
//
// For a token no engine token map can name — today only `focustarget`. The
// tokens a unit can hold (focus, nameplateN, markN) are NOT observed here:
// `Unit::TokenEvents` routes the engine's own events to them, and with
// SuperWoW watching every unit, an observer of ours would fire their field
// events a second time. The registrar appends a fresh node per (guid, field)
// with no dedup, so our observer coexists with the engine's — a unit that is
// simultaneously the target keeps firing `"target"` while ours fires its own
// token. Unregister is a safe no-op once the object is gone (the engine frees
// observer nodes with the object).
//
// `cb` runs inside SMSG_UPDATE_OBJECT dispatch; firing engine events from it
// is sanctioned (the engine's own callback `FUN_0051bd50` does exactly that).
void Register(uint64_t guid, FieldCallback cb);
void Unregister(uint64_t guid, FieldCallback cb);

} // namespace Unit::TokenObserver
