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

// Shared observer for the engine's frame XML loader FUN_XML_FRAME_LOAD
// (0x00769820) — `__thiscall(loader, node, status)`, where loader is the
// object's layout sub-object (object + OFF_XML_LOADER_SUBOBJECT). Every
// derived frame type's loader calls it first, and template inheritance
// re-enters it per inherited node, so one hook sees every frame element.
//
// MinHook allows ONE hook per address and more than one feature reads frame
// nodes (`Xml::ParentKey`'s parentKey / `<Scripts function=>`, and
// `Frame::Attributes`' `<Attributes>`), so — like `Frame::RunnerHook` and
// `Tick::WorldTick` — this module owns the hook and fans out. Subscribers run
// after the engine has loaded the node, in static-init order, which must not
// matter; `AutoSubscribeBefore` ones run before it (`Frame::Cooldown` loads
// its base template there). The hook also runs the loader through `Xml::Inherits::LoadFrame`,
// which reads `inherits` as a list. Declare a file-scope
//   static const Xml::FrameLoad::AutoSubscribe _sub{&OnFrameNode};

namespace Xml {

// XML node accessors (OFF_XML_NODE_*): children are an intrusive list.
const void *FirstChild(const void *node);
const void *NextSibling(const void *node);
const char *Tag(const void *node);
bool TagIs(const void *node, const char *tag); // case-insensitive, like the engine's
// The engine's attribute getter; nullptr when the attribute is absent.
const char *Attr(const void *node, const char *name);
// The template definition node `inherits=` resolves `name` to
// (FUN_XML_TEMPLATE_LOOKUP); nullptr when no template has that name.
const void *Template(const char *name);
// Loads `node` (with its own `inherits`) onto the frame `object` through the
// object's node loader `(object+0x24)->vtable[+8]` — the call the frame
// loader makes for an inherited node.
void LoadNode(void *object, const void *node, void *status);

// The engine's load-status sink: `status->Add(STATUS_WARNING, fmt, ...)`,
// the same warning the loaders log ("Couldn't find inherited node: %s").
void Warn(void *status, const char *fmt, const char *a, const char *b = nullptr);

// Pushes `object`'s Lua wrapper and returns true, registering an unnamed,
// never-exposed object the way the engine's own lazy path does. Pushes
// nothing and returns false for a named object the engine has not registered
// yet (that is its pre-load's job, under the name), or when the wrapper's [0]
// does not point back at `object`.
bool PushWrapper(void *L, void *object);

namespace FrameLoad {

using Observer = void (*)(void *object, const void *node, void *status);

struct AutoSubscribe {
    explicit AutoSubscribe(Observer fn);
    Observer fn;
    AutoSubscribe *next;
};

// The same, run BEFORE the engine loads the node (and its inherits).
struct AutoSubscribeBefore {
    explicit AutoSubscribeBefore(Observer fn);
    Observer fn;
    AutoSubscribeBefore *next;
};

} // namespace FrameLoad

} // namespace Xml
