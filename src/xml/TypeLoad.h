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

// Shared observer for the engine's per-type XML node loaders: the texture and
// font string loaders, and the frame types' own loaders that run after
// FUN_XML_FRAME_LOAD and read the type's children and attributes. Each has
// the sub-object convention `__thiscall(loader, node, status)`, `ret 8`, with
// the object at loader - OFF_XML_LOADER_SUBOBJECT, and an inherited template
// goes through the same loader first, so a node's attributes are seen after
// its templates'.
//
// MinHook allows ONE hook per address and several features read each kind of
// node, so — like Xml::FrameLoad for frame nodes — this module owns the hooks
// and fans out. Subscribers run after the engine has loaded the node, in
// static-init order, which must not matter. The texture and font string
// loaders run inside the node's mixin scope (Xml::Mixin::Begin / End), as
// Xml::FrameLoad's frames do. Declare a file-scope
//   static const Xml::TypeLoad::AutoSubscribe _sub{Xml::TypeLoad::TEXTURE, &OnTextureNode};

namespace Xml::TypeLoad {

enum Loader {
    TEXTURE,     // FUN_XML_TEXTURE_LOAD
    FONT_STRING, // FUN_XML_FONTSTRING_LOAD
    BUTTON,      // FUN_XML_BUTTON_LOAD (CheckButton's loader calls it first)
    EDIT_BOX,    // FUN_XML_EDITBOX_LOAD
    STATUS_BAR,  // FUN_XML_STATUSBAR_LOAD
    LOADER_COUNT
};

using Observer = void (*)(void *object, const void *node, void *status);

struct AutoSubscribe {
    AutoSubscribe(Loader loader, Observer fn);
    Observer fn;
    AutoSubscribe *next;
};

} // namespace Xml::TypeLoad
