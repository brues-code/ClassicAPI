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

// Loose Blizzard_ addons the client does not ship.
//
// At every glue entry the engine renames each `Interface\AddOns\Blizzard*`
// folder that holds its own .toc to `<name>.old`
// (FUN_ADDON_MOVE_ASIDE_BLIZZARD), so a stale extracted copy of the client's
// own UI cannot replace the copy in its archives. A Blizzard_ addon the
// archives do not hold replaces nothing, and the rename only keeps a user
// from installing it (a newer client's Blizzard_Gamepad, say). So the co-hook
// lets the engine rename a folder only when the archives hold that addon's
// .toc; any other folder stays and the addon scan registers it like any
// addon. The embedded addons count as held (Addons::Embedded answers the
// exists check for them), so a loose copy of one still moves aside.
//
// Nothing else gates a Blizzard_ name: the TOC parser (0x0051C9B0) and the
// load check (0x0051E780) test no name, and only a `## Secure:` addon has its
// .toc signature checked.

#include "Game.h"
#include "Offsets.h"
#include "storm/File.h"

#include <cstdio>

namespace AddOns::LooseBlizzard {

namespace {

using MoveAside_t = int(__fastcall *)(void *entry, char *moved);
MoveAside_t MoveAside_o = nullptr;

int __fastcall MoveAside_h(void *entry, char *moved) {
    const char *name =
        reinterpret_cast<const char *>(entry) + Offsets::OFF_DIR_ENTRY_NAME;
    char toc[0x104];
    std::snprintf(toc, sizeof(toc), "Interface\\AddOns\\%s\\%s.toc", name, name);
    if (!Storm::File::InArchives(toc))
        return 0;
    return MoveAside_o(entry, moved);
}

const Game::HookAutoRegister _hookMoveAside{
    Offsets::FUN_ADDON_MOVE_ASIDE_BLIZZARD,
    reinterpret_cast<void *>(&MoveAside_h),
    reinterpret_cast<void **>(&MoveAside_o)};

} // namespace

} // namespace AddOns::LooseBlizzard
