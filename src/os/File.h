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

// `Os::File` — whole-file reads and writes of files on disk, through the
// engine's own OS wrappers (FUN_OS_CREATE_FILE and its siblings). They take a
// UTF-8 path and convert it for the W calls, as every engine file under WTF is
// opened.
//
// Use these for any path that holds an account, realm or character name. The
// CRT and std::fstream take a narrow path in the ANSI code page, so a name
// outside ASCII names a different folder from the one the engine made.
//
// For files in the game's archives or the loose-file index, use
// `Storm::File`.

#include <string>

namespace Os::File {

// The whole file at `path`. False, with `out` empty, when it does not open.
bool Read(const std::string &path, std::string &out);

// Replaces the file at `path` with `data`, creating its folders. The data goes
// to `<path>.tmp` first and replaces the old file only when it was all
// written, so a failed write never truncates the old file.
bool Write(const std::string &path, const std::string &data);

// The same for a file the engine opened (a HANDLE from FUN_OS_CREATE_FILE, -1
// when it did not open), such as the ones the account-data section calls
// hand their callbacks.
bool ReadOpen(void *file, std::string &out);
bool WriteOpen(void *file, const std::string &data);

} // namespace Os::File
