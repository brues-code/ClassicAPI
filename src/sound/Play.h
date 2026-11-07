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

// Sounds other modules start (sound/Play.cpp). Each returns the Sound record,
// or null when nothing played (no row, or sounds held off while the UI loads
// or the world is entered or left). A record is only a handle: pass it to
// Stop, never read it.
namespace Sound::Play {

// The row once, as PlaySound(soundKitID) plays it.
void *PlayKitID(int soundKitID);
// The row as a loop until Stop.
void *PlayKitLooped(int soundKitID);
// Fades a sound from PlayKitID or PlayKitLooped out over `fadeSeconds` (at
// once below 0.1 s). A sound that has ended is left alone.
void Stop(void *sound, float fadeSeconds);

} // namespace Sound::Play
