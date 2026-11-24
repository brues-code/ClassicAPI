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

// Path -> HTEXTURE, the load CSimpleTexture::SetTexture (FUN_00770200) makes.

#include <cstdint>
#include <vector>

namespace Texture {

// The texture flags word SetTexture's own call builds, with a filter and a
// repeat per axis (FUN_GX_TEXFLAGS_INIT).
uint32_t Flags(uint32_t filter, bool wrapU, bool wrapV);

// SetTexture's texture flags: the default filter, clamped, one mip level.
uint32_t DefaultFlags();

// CSimpleTexture::SetTexture (FUN_SIMPLETEXTURE_SET_TEXTURE, `__thiscall(tex,
// path, wrap, filter, shader)`) with what Lua's texture:SetTexture(file)
// passes: clamped, the default filter (VAR_TEXTURE_BLEND_DEFAULT), no pixel
// shader. A null path clears the texture. Goes through the engine's entry, so
// Texture::Wrap's co-hook sees it. Returns the engine's result.
uint32_t SetFile(void *texture, const char *path);

// The loaded handle, or nullptr when the file does not load, made with
// `flags` (SetTexture's own by default). The caller owns one reference and
// drops it with ReleaseHandle.
void *LoadByPath(const char *path, uint32_t flags = DefaultFlags());

// Whether an interface texture of this name would load from a loose file on
// disk rather than from the game's archives: TextureCreate's file names for
// it (`X.blp`, then `X.tga`; a .tga name the other way round) resolved as
// SetTexture opens them (FUN_SFILE_RESOLVE_OPEN with SFILE_OPEN_LOOSE), the
// first that resolves deciding. False when none resolves.
bool IsLooseFile(const char *path);

// The handle's device texture (FUN_TEXTURE_GET_RENDERABLE), what a render
// state binds. The texture is finished loading first, so null means it has
// none.
void *DeviceTexture(void *handle);

// The texel source of a texture made by CreateFromPixels: `width` x `height`
// ARGB8888 `texels`, and the smaller levels the device asks for. The device
// decides the levels from the texture's flags, not from its record: with a
// mipmapping filter it makes the whole chain and uploads from the client's
// texture-resolution skip down (benilla-re gx/texlevels), latching each level
// at that level's size. A smaller level is the box average of `texels`, made
// when the level is latched, so `texels` may change between uploads (a
// portrait re-bakes into its own).
class Pixels {
  public:
    Pixels(uint32_t width, uint32_t height, const uint32_t *texels)
        : width_(width), height_(height), texels_(texels) {}
    Pixels(const Pixels &) = delete;
    Pixels &operator=(const Pixels &) = delete;

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    // The texels at `width` x `height`: `texels` for the full size, else a
    // level kept in `slot` until that slot is latched again.
    const uint32_t *Level(uint32_t slot, uint32_t width, uint32_t height);

  private:
    static constexpr uint32_t kSlots = 16;
    uint32_t width_;
    uint32_t height_;
    const uint32_t *texels_;
    std::vector<uint32_t> levels_[kSlots];
};

// A texture of `pixels`, through the engine's own procedural path
// (FUN_TEXTURE_CREATE_WITH_CALLBACK) with the texture flags word a loader was
// handed (TextureMakeFlags is idempotent on it, as the path re-runs it
// unforced). The engine latches `pixels` whenever it uploads the texture, so
// they must outlive the handle. Never deduplicated: each call is a new
// texture.
void *CreateFromPixels(const char *name, Pixels &pixels, uint32_t flags);

// The engine's shared solid texture of one colour (0xAARRGGBB), what a
// <Texture>'s <Color> child loads (FUN_TEXTURE_CREATE_SOLID). The caller owns
// one reference and drops it with ReleaseHandle.
void *CreateSolid(uint32_t bgra);

} // namespace Texture
