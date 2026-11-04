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

// The cursor modes later clients added, shown on 1.12's cursor, and the
// `expandedCursors` cvar that turns them on (0 by default: they change what
// 1.12 shows).
//
// The engine's cursor is a table of 42 images of 32x32, one per mode;
// SendCursorImage copies slot VAR_CURSOR_MODE to the device. SetCursorMode
// stores the mode it is given and sends, so a mode past 41 must never reach
// it. Slot 41 is how the engine shows an image outside the table:
// SetCustomCursor loads a named BLP into it, stores mode 41, sends it and
// keeps the name, and returns at once while that name is already showing. The
// game object classifier uses it for a record-named cursor.
//
// Show() calls SetCustomCursor with the mode's path, so a file of that name (a
// patch MPQ, a loose file) wins, and a repeat each frame costs one name
// compare. 1.12 has none of these files, so on a miss Show() does what
// SetCustomCursor does after its load, with the image embedded in the DLL
// (Texture::Builtin), which a texture load of the same path also gets.
//
// The files are 4.3.4's Interface\Cursor images, under 4.3.4's names. 4.3.4
// already ships StableMaster.blp and Fishing.blp, but only as art a record
// could name. StableMaster is a mode in retail's table (Enum.Cursormode
// StablemasterCursor, listed after Ping and Enchant), not in 4.3.4's; no
// client's table has Fishing.

#include "cursor/Extended.h"

#include "Common.h"
#include "Game.h"
#include "cvar/Factory.h"
#include "texture/Builtin.h"

#include <cstring>

namespace Cursor::Extended {

namespace {

using SetCustomCursor_t = uint32_t(__fastcall *)(const char *path);
using SendCursorImage_t = void(__cdecl *)();

// In mode order, the Unable forms after the others.
const char *const kPaths[] = {
    "Interface\\Cursor\\RepairNPC.blp",
    "Interface\\Cursor\\Innkeeper.blp",
    "Interface\\Cursor\\Quest.blp",
    "Interface\\Cursor\\QuestRepeatable.blp",
    "Interface\\Cursor\\QuestTurnIn.blp",
    "Interface\\Cursor\\StableMaster.blp",
    "Interface\\Cursor\\Fishing.blp",
    "Interface\\Cursor\\UnableRepairNPC.blp",
    "Interface\\Cursor\\UnableInnkeeper.blp",
    "Interface\\Cursor\\UnableQuest.blp",
    "Interface\\Cursor\\UnableQuestRepeatable.blp",
    "Interface\\Cursor\\UnableQuestTurnIn.blp",
    "Interface\\Cursor\\UnableStableMaster.blp",
    "Interface\\Cursor\\UnableFishing.blp",
};
static_assert(sizeof kPaths / sizeof kPaths[0] == 2 * kUnableOffset, "one path per mode");

CVar::Factory::Handle g_enabled = nullptr;

void Register() {
    g_enabled = CVar::Factory::Register(
        "expandedCursors",
        "Show what an NPC or object offers on the cursor: anvil for repairs, ! and ? for quests, "
        "hook for your bobber",
        Offsets::CVAR_CATEGORY_GAME, "0");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

bool Enabled() { return CVar::Factory::GetInt(g_enabled) != 0; }

bool Is(uint32_t mode) { return mode >= kRepairNpc && mode < kRepairNpc + 2 * kUnableOffset; }

bool IsUnable(uint32_t mode) { return Is(mode) && mode >= kRepairNpc + kUnableOffset; }

bool IsQuest(uint32_t mode) {
    if (!Is(mode))
        return false;
    const uint32_t kind = IsUnable(mode) ? mode - kUnableOffset : mode;
    return kind >= kQuest && kind <= kQuestTurnIn;
}

const char *Path(uint32_t mode) { return kPaths[mode - kRepairNpc]; }

void Show(uint32_t mode) {
    const char *path = Path(mode);
    if (reinterpret_cast<SetCustomCursor_t>(Offsets::FUN_SET_CUSTOM_CURSOR)(path) != 0)
        return;
    const Texture::Builtin::Image *image = Texture::Builtin::Find(path);
    if (image == nullptr || image->width != Offsets::CURSOR_IMAGE_SIZE ||
        image->height != Offsets::CURSOR_IMAGE_SIZE)
        return;
    Game::Ref<uint32_t>(Offsets::VAR_CURSOR_MODE) = Offsets::CURSOR_MODE_CUSTOM;
    std::memcpy(reinterpret_cast<void *>(Offsets::VAR_CURSOR_IMAGE_CUSTOM), image->texels,
                Offsets::CURSOR_IMAGE_SIZE * Offsets::CURSOR_IMAGE_SIZE * sizeof(uint32_t));
    reinterpret_cast<SendCursorImage_t>(Offsets::FUN_SEND_CURSOR_IMAGE)();
    Common::BoundedCopy(reinterpret_cast<char *>(Offsets::VAR_CUSTOM_CURSOR_NAME), path,
                        Offsets::CURSOR_FILE_SIZE);
}

} // namespace Cursor::Extended
