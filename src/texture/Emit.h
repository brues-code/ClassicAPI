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

#include "AutoList.h"

// Shared observer for a texture's emit into its layer batch
// (FUN_SIMPLETEXTURE_EMIT), the layer rebuild's per-texture call. Listeners
// run after the engine's emit, with `appended` true when it added the
// texture's quad (it adds none without an image). The emit loads the image,
// so after it the device texture exists.
//
// MinHook allows ONE hook per address and more than one feature follows the
// emit (Texture::Tile's repeat count, Texture::Slice's pieces), so this
// module owns the hook and fans out. Declare a file-scope
//   static const Texture::Emit::AutoSubscribe _sub{&OnEmitted};
namespace Texture::Emit {

using Listener = void (*)(void *texture, void *batch, bool appended);

using AutoSubscribe = AutoList<struct AutoSubscribeTag, Listener>;

} // namespace Texture::Emit
