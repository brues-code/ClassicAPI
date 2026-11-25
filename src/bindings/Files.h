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

#include <string>

// The binding manager's file steps (4.3.4's CGUIBindings), one hook each,
// shared by the modules that keep binding data the engine lacks
// (Bindings::ModifiedClick, Bindings::Context). Subscribe with a file-scope
// `static const Bindings::Files::<List>` in the module's TU; the set numbers
// passed are always a valid Offsets::BINDING_SET_*.
namespace Bindings::Files {

// Before the engine loads a Bindings.xml (FrameXML's or an addon's): the
// file's root <Bindings> node, whose children are the root nodes the engine
// walks. Never called when the file does not read or parse; the engine
// loader reports that itself.
using LoadFileFn = void (*)(const char *path, const void *root, void *status);
using OnLoadFile = AutoList<struct OnLoadFileTag, LoadFileFn>;

// A root node name a subscriber reads, so the engine loader's "Unknown node
// type" warning skips it (4.3.4's loader has a branch for each).
using KnownNode = AutoList<struct KnownNodeTag, const char *>;

// LoadBindings (FUN_BINDINGS_LOAD_SET): `begin` runs before the engine
// empties the set and binds each `bind KEY COMMAND` line through
// FUN_BINDINGS_SET_KEY; `end` after, with the file text.
using LoadSetBeginFn = void (*)(int set);
using OnLoadSetBegin = AutoList<struct OnLoadSetBeginTag, LoadSetBeginFn>;
using LoadSetEndFn = void (*)(int set, const char *text);
using OnLoadSetEnd = AutoList<struct OnLoadSetEndTag, LoadSetEndFn>;

// SaveBindings (FUN_BINDINGS_SAVE_SET), after the engine wrote the set's
// `bind` lines: append lines with WriteLine.
using SaveSetFn = void (*)(int set, void *file);
using OnSaveSet = AutoList<struct OnSaveSetTag, SaveSetFn>;

// CopyBindings (FUN_BINDINGS_COPY_SET), after the engine copied its keys.
using CopySetFn = void (*)(int source, int dest);
using OnCopySet = AutoList<struct OnCopySetTag, CopySetFn>;

// Appends `line` (with its "\r\n") to the binding file being saved.
void WriteLine(void *file, const std::string &line);

} // namespace Bindings::Files
