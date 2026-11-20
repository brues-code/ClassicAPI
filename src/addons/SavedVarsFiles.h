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

#include <cstddef>

namespace AddOns::SavedVarsFiles {

// An addon's SavedVariables files, read and written as the 3.80.2 client does.
//
// Read (3.80.2 FUN_141af7790): when `<Name>.lua` is missing, `<Name>.lua.bak`
// runs in its place; with neither, nothing runs. 3.80.2 reads a scope only
// when the addon declares it; 1.12 runs any existing file. The fallback is
// limited to declared scopes, so a backup left from a scope the addon no
// longer declares never loads (1.12's own writer left many stale backups: its
// rename to `.bak` fails once one exists). 1.12's loader FUN_0051F240 tests
// each path with FUN_FILE_EXISTS before it runs it (account 0x0051F4A2;
// character 0x0051F4F0, then the realm-less 0x0051F529), so those three tests
// are redirected, and a backup switches the engine's own path buffer.
//
// Write (3.80.2 FUN_141af8a80): each file is written to `<path>.tmp`; after a
// complete write the old file becomes `<path>.bak` (replacing an older
// backup) and the `.tmp` becomes the file. A name with a character other than
// A-Z, a-z, 0-9 or `_` is not written. 1.12's writer FUN_ADDON_SAVE_VARIABLES
// renames the old file to `.bak` first (a MoveFileW that fails once a `.bak`
// exists, so the file is then overwritten in place) and writes the file
// directly. Its backup rename, create, per-name write and close calls, for the
// account and the character file, are redirected.
//
// Buffering: 3.80.2 streams each file through a 0x1000-byte buffer; 1.12's
// serializer (FUN_SV_WRITE_VARIABLE and the value writer under it) calls
// FUN_OS_WRITE_FILE for every piece: CR LF, the name, " = ", then the value
// text in parts. Those ten calls and the writer's two line-end writes are
// redirected; for the file being saved they fill a 0x1000-byte buffer that
// goes out when full and at the close. A failed write keeps the old file and
// deletes the `.tmp`, as in 3.80.2.

// When the file at `path` exists, returns true. When only `<path>.bak`
// exists, appends `.bak` to `path` (when it fits in `cap` bytes) and returns
// true. Otherwise returns false. Uses the engine's file-exists test, as its
// SavedVariables loader does. The caller decides that the scope is declared.
bool ResolveExisting(char *path, size_t cap);

} // namespace AddOns::SavedVarsFiles
