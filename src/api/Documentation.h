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

// Recording side of the API documentation. The registrars in Game.cpp call
// these; `Documentation.cpp` keeps the list, resolves systems, and exports
// Blizzard-shaped `APIDocumentation` tables through `C_APIDocumentation`.
// The descriptor model is the `Game::Doc` block in Game.h.
namespace Api::Documentation {

enum Env : unsigned { ENV_NONE = 0, ENV_GAME = 1, ENV_GLUE = 2 };

// Bracket one `Run*Registrations()` pass. Recording happens only inside a
// pass, and only the FIRST pass per environment records — every later pass
// (each `/reload`, each logout→login) is a no-op. `EndPass` seals the
// environment and logs one summary line to the debug log.
void BeginPass(Env env);
void EndPass();

void RecordGlobal(const char *name, const Game::Doc::Function *doc);
void RecordTable(const char *table, const char *name, const Game::Doc::Function *doc);
void RecordFrameMethods(const void *registry, const Game::Lua::FrameMethodEntry *table,
                        int count, const Game::Doc::Method *docs, int docCount);
void RecordEnum(const char *parent, const char *sub, const Game::Lua::EnumIntegerEntry *entries,
                int count, const char *system);

} // namespace Api::Documentation
