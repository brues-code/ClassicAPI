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

// A script's three bindings — see ScriptBindings.cpp.

namespace Frame::ScriptBindings {

// LE_SCRIPT_BINDING_TYPE_*, in the order the script runs them.
enum Binding { INTRINSIC_PRECALL = 0, EXTRINSIC = 1, INTRINSIC_POSTCALL = 2, BINDING_COUNT = 3 };

// After an intrinsic definition `def` has loaded onto `frame`, moves the
// handler each of its <Scripts> elements set into that script's intrinsic
// binding: postcall for `intrinsicOrder="postcall"`, precall otherwise.
void CaptureIntrinsic(void *frame, const void *def);

// frame:HookScript(script, hook, binding) for an intrinsic binding: the hook
// becomes the binding, or runs after the one already there. The script name
// is at stack index 2 and the hook at 3. False when the frame has no such
// script.
bool HookIntrinsic(void *L, void *frame, Binding binding);

// The dispatcher closure body, so Frame::ScriptArgs can look at the
// bindings it runs (upvalues 1..3, in binding order).
int __fastcall Dispatcher(void *L);

} // namespace Frame::ScriptBindings
