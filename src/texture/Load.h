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

// A one-mip `width` x `height` texture of ARGB8888 `texels`, through the
// engine's own procedural path (FUN_TEXTURE_CREATE_WITH_CALLBACK) with the
// texture flags word a loader was handed (TextureMakeFlags is idempotent on
// it, as the path re-runs it unforced). The engine reads `texels` whenever it
// uploads the texture, so they must outlive the handle. Never deduplicated:
// each call is a new texture.
void *CreateFromPixels(const char *name, uint32_t width, uint32_t height,
                       const uint32_t *texels, uint32_t flags);

// The engine's shared solid texture of one colour (0xAARRGGBB), what a
// <Texture>'s <Color> child loads (FUN_TEXTURE_CREATE_SOLID). The caller owns
// one reference and drops it with ReleaseHandle.
void *CreateSolid(uint32_t bgra);

} // namespace Texture
