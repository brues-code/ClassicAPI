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

// Shared interceptor for the Lua collector's object lifecycle. Every
// collectable object is born in one of three engine functions and dies in
// one (see `FUN_LUA_C_LINK` in Offsets.h for the xref proof):
//
//   luaC_link      tables, closures, protos, closed upvalues, threads
//   newlstr        strings
//   luaS_newudata  full userdata
//   freeobj        every free, from the sweep
//
// MinHook allows one hook per address, and both per-addon memory accounting
// (`Gc::Tag`) and script profiling need these, so — like `Event::SignalHook` —
// this module owns the four hooks and fans out. Declare a file-scope
// `static const Gc::Hooks::AutoSubscribe` in your module; either callback may
// be null.
//
// `onCreate` runs after the engine has linked the object (its type tag is
// set); `onFree` runs before the engine frees it (it is still readable).
// Both sit on the allocator's hottest path: keep them O(1) and allocation-free
// in the common case.

namespace Gc::Hooks {

using Callback = void (*)(const void *object);

struct AutoSubscribe {
    AutoSubscribe(Callback onCreate, Callback onFree);
    Callback onCreate;
    Callback onFree;
    AutoSubscribe *next;
};

} // namespace Gc::Hooks
