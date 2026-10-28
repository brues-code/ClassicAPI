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
};

// Resolves an atlas by name, case-insensitively. Null when the name is unbound.
// The returned pointer stays valid for the life of the process.
const Info *Find(const char *name);

// Records an unresolved atlas name for `_classicapi_DumpAtlasMisses()`. This is
// how the built-in table grows: load a real addon, read what it actually asked
// for, bind those names instead of guessing at thousands.
void RecordMiss(const char *name);

// Drops the per-region `GetAtlas` bookkeeping and the miss log before a
// `/reload`. Registered atlases are deliberately kept — addon Lua re-runs and
// re-registers by name, and holding the old entries keeps every name resolvable
// across the gap.
void PrepareForReload();

// The XML `<Texture atlas="name" useAtlasSize="true">`: texture:SetAtlas(name,
// useAtlasSize) on `region`, run as its own Lua call so the caller's stack is
// left alone.
void SetFromXml(void *region, const char *name, bool useAtlasSize);

} // namespace Texture::Atlas
