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

#include "unit/PolledEvent.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/UnitEvent.h"
#include "unit/Identity.h"

namespace Unit::PolledEvent {

void Poller::Want(const std::string &token) {
    for (const std::string &have : tokens_)
        if (Ascii::EqualCI(have.c_str(), token.c_str()))
            return;
    tokens_.push_back(token);
}

void Poller::Tick() {
    if (Game::Read<uint8_t>(static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) == 0) {
        answers_.clear();
        return;
    }
    const int slot = reserve_.Slot();
    if (!Event::Custom::HasListeners(slot))
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    // The tokens first: firing runs scripts, which can change the listeners.
    tokens_.clear();
    Event::Custom::ForEachListener(slot, [&](uint8_t *frame) {
        if (const std::vector<std::string> *units = Frame::UnitEvent::Units(frame, name_))
            for (const std::string &unit : *units)
                Want(unit);
    });

    const int top = Game::Lua::GetTop(L);
    for (const std::string &token : tokens_) {
        const uint64_t guid = Unit::Identity::GuidForToken(token.c_str());
        if (guid == 0)
            continue;
        const bool answer = check_(guid);
        const auto [it, first] = answers_.try_emplace(guid, answer);
        if (!first) {
            if (it->second == answer)
                continue;
            it->second = answer;
        }
        Unit::Identity::ForEachToken(guid, [&](const char *unitToken) {
            Game::Lua::PushString(L, unitToken);
            Game::Lua::PushBool(L, answer);
            Event::Custom::FireValues(slot, L, top + 1, 2);
            Game::Lua::SetTop(L, top);
        });
    }
}

} // namespace Unit::PolledEvent
