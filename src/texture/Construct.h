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

// Shared observer for the CSimpleTexture constructor (FUN_SIMPLETEXTURE_CTOR),
// which builds every texture region, in fresh or recycled pool memory alike.
// Listeners run BEFORE the constructor, on the memory it is about to build, so
// state keyed by a region pointer is dropped before a new region takes the
// address.
//
// MinHook allows ONE hook per address and more than one feature keeps state
// keyed by a texture region (Texture::Mask, Texture::Wrap), so this module owns
// the hook and fans out. Declare a file-scope
//   static const Texture::Construct::AutoSubscribe _sub{&OnTextureConstructed};
namespace Texture::Construct {

using Listener = void (*)(void *region);

struct AutoSubscribe {
    explicit AutoSubscribe(Listener fn);
    Listener fn;
    AutoSubscribe *next;
};

} // namespace Texture::Construct
