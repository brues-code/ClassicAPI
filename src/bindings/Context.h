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

// Key-binding contexts as the 3.80.2 client has them (Enum.BindingContext,
// C_KeyBindings.ActivateBindingContext & co.). See Context.cpp.
namespace Bindings::Context {

// The command a press of `key` runs from a context slot of the live set: the
// first active context, in 3.80.2's priority order, whose slot holds a
// command. Null when none does; the key's own binding (the engine's table,
// context None) applies then. The pointer lives until bindings next change.
const char *Resolve(const char *key);

// The command in slot `context` (1..9) of `key` in the live set, else null.
const char *Slot(const char *key, int context);

// A context argument as 3.80.2's SetBinding and GetBindingAction read one: a
// number from 0 to 9; anything else (absent included) is -1, none.
int OptionalArg(void *L, int arg);

} // namespace Bindings::Context
