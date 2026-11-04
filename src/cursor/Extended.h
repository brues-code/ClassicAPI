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

#include "Offsets.h"

#include <cstdint>

namespace Cursor::Extended {

// The cursor modes later clients added that 1.12 has no image for, numbered
// on from the engine's last mode (CURSOR_MODE_CUSTOM): 4.3.4's RepairNPC,
// Innkeeper and quest cursors, retail's StableMaster, and the fish hook for
// the fishing bobber. As with the engine's modes, each has an Unable form,
// kUnableOffset further on.
constexpr uint32_t kRepairNpc = Offsets::CURSOR_MODE_CUSTOM + 1;
constexpr uint32_t kInnkeeper = kRepairNpc + 1;
constexpr uint32_t kQuest = kRepairNpc + 2;
constexpr uint32_t kQuestRepeatable = kRepairNpc + 3;
constexpr uint32_t kQuestTurnIn = kRepairNpc + 4;
constexpr uint32_t kStableMaster = kRepairNpc + 5;
constexpr uint32_t kFishing = kRepairNpc + 6;
constexpr uint32_t kUnableOffset = 7;

// Whether the `expandedCursors` cvar asks for these modes. The pickers that
// give them (cursor/Npc, cursor/Fishing) keep 1.12's cursor while it is 0.
bool Enabled();

// Whether `mode` is one of these, in either form.
bool Is(uint32_t mode);

// Whether `mode` is one of these in its Unable form.
bool IsUnable(uint32_t mode);

// Whether `mode` is a quest giver's cursor (Quest, QuestRepeatable,
// QuestTurnIn), in either form.
bool IsQuest(uint32_t mode);

// The image file of one of these modes, `Interface\Cursor\<its name>.blp`.
const char *Path(uint32_t mode);

// Shows one of these modes as the cursor. The engine's SetCursorMode cannot,
// so its hook (cursor/Hover) sends them here.
void Show(uint32_t mode);

} // namespace Cursor::Extended
