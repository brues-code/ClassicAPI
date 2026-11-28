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

// Function containers: `C_FunctionContainers.CreateCallback(func)` and the
// callback values the event-callback registry stores, as the 3.80.2 client
// keeps them (LuaFunctionContainer).
//
// A container holds a registry ref to one Lua function and the Lua state's
// generation it was made in. It is shared: the userdata CreateCallback returns
// and every registration that stores it hold one reference each, so a Cancel
// through the userdata reaches the registrations, and a registration keeps the
// container after the userdata is collected. A registration of a plain
// function makes a container of its own. A cancelled container (ref -1) or one
// from an earlier Lua state is skipped by everything that would call it.
namespace Profile::AddOnProfiler {
struct Record;
}

namespace Event::FunctionContainer {

struct Container;

// True for a value FromCallbackArg takes: a Lua function or a container
// userdata. Makes nothing.
bool IsCallbackArg(void *L, int idx);

// The container for the callback value at `idx`, with one reference the
// caller owns (Release it). A Lua function gets a new container; a container
// userdata gives its own. nullptr for any other value, a C function included.
Container *FromCallbackArg(void *L, int idx);

void Retain(Container *c);
void Release(Container *c);

// Pushes a new container userdata for `c`, holding a reference of its own.
// Each push is a new userdata, equal to the others by __eq only, as 3.80.2's
// pusher (FUN_140e2ddd0) boxes one per call.
void Push(void *L, Container *c);

// True once Cancel() was called, or for a container from an earlier Lua state.
bool IsCancelled(const Container *c);

// Pushes the container's function and gives true, or pushes nothing and gives
// false for a cancelled or stale container or a ref that holds no function.
bool PushFunction(void *L, const Container *c);

// The addon profiler's owner of the container: Profile::AddOnProfiler::Owner()
// as it was made. Its calls from C run inside a Profile::AddOnProfiler::Scope
// for it.
Profile::AddOnProfiler::Record *Owner(const Container *c);

} // namespace Event::FunctionContainer
