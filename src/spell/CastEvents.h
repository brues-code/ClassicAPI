// This file is part of ClassicAPI.
//
// ClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// ClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.

#pragma once

// Backport of the TBC+ `UNIT_SPELLCAST_*` events to 1.12, for the local
// player. The underlying cast/channel detection already exists in
// `Spell::Cast` (it hooks every relevant path for `UnitCastingInfo` /
// `UnitChannelInfo`); this module turns those state transitions into the
// retail-named Lua events so ported cast-bar / rotation addons work.
//
// MinHook allows one hook per target and `Spell::Cast` already owns the
// cast-related hooks, so this module doesn't hook anything — the hook
// owners call into it. Cast start/stop/channel/pushback are derived by
// polling `Spell::Cast`'s player state once per frame (`PollPlayer`,
// called from that module's `OnWorldTick`); succeeded/interrupted come
// from the SPELL_GO / abort paths in later slices.
//
// Event contract (arg1 is always the unit token):
//   (unitTarget, castGUID, spellID, spellName, rank)
// The first three match the modern Classic-Era signature, so addons
// written against it read `unit, castGUID, spellID` and work; `spellName`
// / `rank` are ClassicAPI extension tail args. `castGUID` is a synthesized
// unique-per-cast string ("Cast-<n>") — not retail's GUID format, just
// stable across a cast's events so START can be paired with STOP.
//
// Every fire is gated on `Event::Custom::HasListeners`, so when no addon
// registers for these the module costs one pointer-compare per transition
// and does no arg synthesis.

#include <cstdint>

namespace Spell::CastEvents {

// Derive player cast/channel events from a once-per-frame snapshot of
// `Spell::Cast`'s player state. Called from `Spell::Cast::OnWorldTick`
// after its own upkeep, passing the live `g_cast` / `g_channel` fields.
// Detects, against the previous snapshot:
//   - regular cast start / stop / same-spell recast  → UNIT_SPELLCAST_START / _STOP
//   - accumulated pushback growth                     → UNIT_SPELLCAST_DELAYED
//   - channel start / stop                            → UNIT_SPELLCAST_CHANNEL_START / _STOP
void PollPlayer(int castSpellID, int castStartMs, int castDelayMs,
                int channelSpellID, int channelStartMs);

// Fire `UNIT_SPELLCAST_SUCCEEDED` for the local player. Called from the
// SPELL_GO hook (`Aura::Source`) when the caster is the player — that
// packet is "the spell went off," so it fires for instants (which never
// touch the cast state above) as well as completed cast-time / channel
// spells. Reuses the in-flight cast's castGUID when the spellID matches,
// else mints a fresh one (instant cast). For channels it defers to
// `PollPlayer`, which fires SUCCEEDED right after CHANNEL_START so the
// order matches modern (CHANNEL_START → SUCCEEDED).
void OnPlayerSucceeded(int spellID);

// Fire `UNIT_SPELLCAST_CHANNEL_UPDATE` for the local player. Called from the
// MSG_CHANNEL_UPDATE hook (`Spell::Cast`) when a pushback re-anchors the
// player's active channel end. Reuses the tracked channel's castGUID (set by
// CHANNEL_START); no-ops if the channel isn't tracked yet (an UPDATE never
// precedes its START). The end time itself is read via
// `C_Spell.UnitChannelInfo`, matching modern — this event is only the "re-read
// now" trigger.
void OnPlayerChannelUpdate(int spellID);

// `UNIT_SPELLCAST_SENT` is handled internally — the module subscribes to
// the shared `Net::SendObserver` and fires it when CMSG_CAST_SPELL leaves
// the client (the earliest point in a cast's life). The castGUID minted
// there threads forward: the matching START / SUCCEEDED / FAILED reuse it,
// so all of a cast's events share one guid.

} // namespace Spell::CastEvents
