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

#include "AutoList.h"

#include <cstdint>

// `Aura::ComboDuration` — combo-point-scaled durations for finisher
// auras (Rupture, Kidney Shot, Slice and Dice, Rip, …).
//
// The 1.12 server computes a finisher's real duration at cast time as
// `base + (max - base) * comboPoints / 5` from the spell's
// SpellDuration.dbc row and never transmits the result for auras the
// player puts on OTHER units (verified with a whole-protocol sniff:
// SMSG_UPDATE_AURA_DURATION is self-scoped, and nothing else carries
// it). So this module mirrors the computation client-side:
//
//   - a co-hook on the NetClient send (`FUN_NET_SEND`) snapshots the
//     player's combo points the instant CMSG_CAST_SPELL leaves — the
//     one moment the count is still intact (the server clears it
//     before SMSG_SPELL_GO comes back);
//   - `TryComboScaledMs` consumes that snapshot when `Aura::Source`'s
//     SpellGo hook stores the aura, computing the scaled duration from
//     SpellDuration.dbc and applying the player's duration SpellMods on
//     top, matching the server's order of operations. Spells whose
//     duration index DANGLES (client data bug — Turtle's reworked Rip
//     is the known case) fall back to built-in values gated on that
//     exact condition; a Lua-registered override
//     (`C_UnitAuras.RegisterComboDuration`) beats both, as an escape
//     hatch for other servers whose values differ from the client DBC.
namespace Aura::ComboDuration {

// Combo-scaled duration (ms) for a player-cast spell, or 0 when it
// doesn't apply: no combo points captured for this cast, the spell's
// duration row isn't combo-scaled (base == max) and has no override,
// or the row is missing. A 0 return means "use the regular base
// duration path".
uint32_t TryComboScaledMs(const uint8_t *spellRecord, uint32_t spellId);

// Extension point for finishers whose SpellDuration row DANGLES — a
// non-zero DurationIndex pointing at a row the client DBC doesn't have.
// A resolver returns true and fills *baseMs/*maxMs if it knows the spell,
// false otherwise. Consulted by `TryComboScaledMs` ONLY in that dangling
// condition (after any Lua-registered override), so the activation gate is
// the client-data bug itself, not a server fingerprint — the deliberate
// design choice (a realm-sniffing approach was rejected). Server-custom
// data (e.g. Turtle's reworked Rip) registers its values here instead of
// living in this stock module: declare a file-scope
// `static const Aura::ComboDuration::AutoDanglingResolver`.
using DanglingResolver = bool (*)(uint32_t spellId, int32_t *baseMs,
                                  int32_t *maxMs);
using AutoDanglingResolver = AutoList<struct AutoDanglingResolverTag, DanglingResolver>;

} // namespace Aura::ComboDuration
