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

#include "Game.h"
#include "event/Custom.h"
#include "frame/GuidEvent.h"
#include "unit/Identity.h"

#include <cstdint>
#include <type_traits>

// Firing a unit event for a unit — 4.3.4's Script_SendUnitSignal: once for each
// token naming the unit, as arg1, then once to the frames that registered the
// unit's GUID (Frame::GuidEvent), with the GUID as arg1. Every unit event
// ClassicAPI fires goes through here, so a GUID listener hears what a token
// listener hears. Name it `::Unit::Signal` from inside a namespace that has a
// `Signal` of its own.
namespace Unit::Signal {

// Whether anything hears the event in `slot` for `guid`.
inline bool HasListeners(int slot, uint64_t guid) {
    return Event::Custom::HasListeners(slot) || Frame::GuidEvent::Has(slot, guid);
}

// Whether anything hears the event in `slot` for any unit.
inline bool HasListeners(int slot) {
    return Event::Custom::HasListeners(slot) || Frame::GuidEvent::HasAny(slot);
}

namespace detail {

// A printf-style payload value as the Lua value the engine's runner makes of it.
inline void Push(void *L, const char *s) { Game::Lua::PushString(L, s); }
inline void Push(void *L, int v) { Game::Lua::PushNumber(L, static_cast<double>(v)); }
inline void Push(void *L, unsigned v) { Game::Lua::PushNumber(L, static_cast<double>(v)); }
inline void Push(void *L, double v) { Game::Lua::PushNumber(L, v); }

template <class Fn> int Trampoline(void *L, void *ctx) { return (*static_cast<Fn *>(ctx))(L); }

} // namespace detail

// The GUID listeners alone, for a fire whose tokens went out already (the
// engine's own dispatch). `push(L)` pushes the payload after the unit and
// returns its count.
template <class PushFn> void FireGuidValues(uint64_t guid, int slot, PushFn &&push) {
    if (!Frame::GuidEvent::Has(slot, guid))
        return;
    using Fn = std::remove_reference_t<PushFn>;
    Frame::GuidEvent::Deliver(slot, guid, &detail::Trampoline<Fn>,
                              const_cast<void *>(static_cast<const void *>(&push)));
}

// FireGuidValues with a printf-style payload (the values after the unit).
template <class... Args> void FireGuid(uint64_t guid, int slot, Args... args) {
    FireGuidValues(guid, slot, [&](void *L) {
        (detail::Push(L, args), ...);
        return static_cast<int>(sizeof...(args));
    });
}

// The whole fire with a printf-style payload: `format` names the unit and
// then each of `args`, as Event::Custom::Fire takes it.
template <class... Args>
void Fire(uint64_t guid, int slot, const char *format, Args... args) {
    if (Event::Custom::HasListeners(slot))
        Unit::Identity::ForEachToken(guid, [&](const char *token) {
            Event::Custom::Fire(slot, format, token, args...);
        });
    FireGuid(guid, slot, args...);
}

// A unit event the client fires for "player" alone, not for your other
// tokens: "player" as arg1, then the frames that registered your GUID.
template <class... Args> void FirePlayer(int slot, const char *format, Args... args) {
    Event::Custom::Fire(slot, format, "player", args...);
    FireGuid(Unit::Identity::PlayerGuid(), slot, args...);
}

// The whole fire with a Lua payload: `push(L)` pushes the values after the
// unit and returns their count. It runs for each fire, since a handler may
// change what it pushes (a reused table).
template <class PushFn> void FireValues(uint64_t guid, int slot, PushFn &&push) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    if (Event::Custom::HasListeners(slot)) {
        const int top = Game::Lua::GetTop(L);
        Unit::Identity::ForEachToken(guid, [&](const char *token) {
            Game::Lua::PushString(L, token);
            const int nargs = 1 + push(L);
            Event::Custom::FireValues(slot, L, top + 1, nargs);
            Game::Lua::SetTop(L, top);
        });
    }
    FireGuidValues(guid, slot, push);
}

} // namespace Unit::Signal
