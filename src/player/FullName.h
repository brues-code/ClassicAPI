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

#include <string>

// A player's name as one string that also says which realm they are from: the
// bare name for a player on the session's realm, "Name-Realm" for one from
// another. It is the form the engine itself shows for a player from another
// realm: the chat handler (0x0049D8B3 on) and the battleground join notice add
// "-" and the realm with its spaces and tabs removed. So a sender name taken
// from a chat event looks the player up as it is. A character name can't hold
// '-', so the first one splits the two.
//
// Compare these with Game::StrCmpI, as the engine compares names: ASCII case
// does not matter.
namespace Player::FullName {

// `realm` as a name cache record holds it (`OFF_PLAYER_INFO_REALM`): "" for a
// player on this realm. A realm that names the session's own counts as "".
// Empty for an empty name.
std::string Make(const char *name, const char *realm);

// "Name" or "Name-Realm" text, from a chat event or typed by the user, in the
// same form as Make. Empty when there is no name before the '-'.
std::string Parse(const char *text);

} // namespace Player::FullName
