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

// A frame moved under a new parent takes the frames under it along in frame
// level, so the whole subtree sits above its new parent.
//
// Vanilla's SetParent gives the frame the new parent's strata (the strata
// setter recurses into its children) and the parent's level + 1, but it calls
// the level setter without the setter's children flag, so the frames under it
// keep the levels they had. A panel built in one place and moved into a raised
// window then draws its contents behind that window. Zygor Guides Viewer 2.0's
// options panel, shown in the Interface Options window that
// LibBetterBlizzOptions-1.0 raises to FULLSCREEN_DIALOG, sat at level 7 with
// its Ace3 widgets at levels 1-4, behind the window's own level 5.
//
// The engine's level setter already moves a frame's children when asked: each
// child of the same strata goes up or down by the same amount, recursively,
// so the layering inside the subtree is kept. 3.3.5's SetParent asks for that
// on both of its calls (with a parent, and with none); here the `push 0` of
// the flag at each call becomes `push 1`. Script SetFrameLevel and XML
// frameLevel still pass 0. (3.3.5 passes 1 there too; left as vanilla so
// addons that set child levels before the parent's keep working.) The bytes
// are checked before they are written, so a client whose code differs there
// is left alone.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Frame::Levels {

namespace {

// Each site: the `push 0` of the children flag and the instruction after it,
// as the client has them. The push's immediate is what gets replaced.
struct Site {
    uintptr_t push;      // address of the `push 0`
    uint8_t original[4]; // `push 0` + the next instruction
};

constexpr Site kSites[] = {
    // push 0; inc eax
    {Offsets::PATCH_SETPARENT_LEVEL_CHILDREN, {0x6A, 0x00, 0x40, 0x50}},
    // push 0; push 0
    {Offsets::PATCH_SETPARENT_NIL_LEVEL_CHILDREN, {0x6A, 0x00, 0x6A, 0x00}},
};

constexpr uint8_t kOne = 0x01;

// Runs on every in-game registration (each /reload too); a site already
// patched no longer matches and is skipped.
void Apply() {
    for (const Site &site : kSites) {
        const auto *code = reinterpret_cast<const uint8_t *>(site.push);
        if (std::memcmp(code, site.original, sizeof site.original) != 0)
            continue; // already applied, or not the code this was derived from
        Common::PatchBytes(reinterpret_cast<void *>(site.push + 1), &kOne, sizeof kOne);
    }
}

const Game::ModuleAutoRegister _autoreg{&Apply};

} // namespace

} // namespace Frame::Levels
