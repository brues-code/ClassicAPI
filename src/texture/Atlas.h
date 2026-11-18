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

// `Texture::Atlas` — the atlas name registry behind `C_Texture.*`,
// `texture:SetAtlas`, and the `|A:name:h:w|a` inline markup. An atlas is a name
// bound to a texture plus a sub-rect within it; resolving one is what lets a
// caller set both in a single step. See `AtlasData.h` for where the built-in
// bindings come from and why there are so few of them.
//
// The registry is seeded from that table on first use and extended at runtime by
// `C_Texture.RegisterAtlas`, so an addon that ships its own sprite sheet works
// without needing any binding here.
namespace Texture::Atlas {

// One resolved atlas. Built-in and addon-registered entries normalize to this
// same shape, so every reader treats them identically.
struct Info {
    std::string name; // canonical casing, reported as `elementName`
    std::string file; // texture path
    float width = 0.0f;
    float height = 0.0f;
    float left = 0.0f;
    float right = 1.0f;
    float top = 0.0f;
    float bottom = 1.0f;
    // Blizzard's own ids for a built-in entry. An addon-registered atlas has no
    // Blizzard id, so it gets a NEGATIVE synthetic one — the same
    // two-namespaces-in-one-int idiom `Map::Area` uses for uiMapIDs, which keeps
    // the two spaces from ever colliding.
    int atlasID = 0;
    int elementID = 0;
    bool tilesHorizontally = false;
    bool tilesVertically = false;
    // A built-in sprite drawn transposed from its rect (AtlasData::Entry).
    bool transposed = false;
    // Nine-slice data (AtlasData::Entry::slice): margins in the sprite's
    // pixels and Enum.UITextureSliceMode. `sliced` is false for an atlas
    // retail has no slice row for.
    bool sliced = false;
    float sliceLeft = 0.0f;
    float sliceTop = 0.0f;
    float sliceRight = 0.0f;
    float sliceBottom = 0.0f;
    int sliceMode = 0;
};

// Whether the atlas repeats on an axis (0 x, 1 y): it tiles there and fills
// its file on that axis, as retail packs every tiling atlas (UI-Frame-Metal's
// edges have their own Horizontal and Vertical files). A tiling atlas packed
// with others, as some built-in ones are, cannot repeat alone.
bool Repeats(const Info &info, int axis);

// Resolves an atlas by name, case-insensitively. Null when the name is unbound.
// The returned pointer stays valid for the life of the process.
const Info *Find(const char *name);

// True while `region` shows the atlas SetAtlas last gave it: its image has not
// been replaced since.
bool Shows(void *region);

// Maps texture coordinates given in the sprite's own space (UL, LL, UR, LR
// corners, each an (x, y) pair) onto the file, in place, while `region` shows
// an atlas; false, and the corners untouched, when it shows none. For engine
// code that writes a region's coordinates past the script SetTexCoord, which
// does this itself: a StatusBar's fill (StatusBar::Fill), whose retail
// counterpart crops within an atlas bar texture's sprite.
bool ToFile(void *region, float corners[8]);

// The inverse of ToFile: maps file coordinates back into the sprite's own
// space, in place, while `region` shows an atlas; false otherwise.
bool ToSprite(void *region, float corners[8]);

// The atlas `region` shows, or null (the same test as Shows).
const Info *Current(void *region);

// Records an unresolved atlas name for `_classicapi_DumpAtlasMisses()`. This is
// how the built-in table grows: load a real addon, read what it actually asked
// for, bind those names instead of guessing at thousands.
void RecordMiss(const char *name);

// Drops the per-region `GetAtlas` bookkeeping and the miss log before a
// `/reload`. Registered atlases are deliberately kept — addon Lua re-runs and
// re-registers by name, and holding the old entries keeps every name resolvable
// across the gap.
void PrepareForReload();

} // namespace Texture::Atlas
