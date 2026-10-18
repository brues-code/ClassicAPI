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

// `GetClickFrame(name)` — backport of the C global WoW registers for the
// `/click` handler to resolve a frame by its global name. Vanilla 1.12
// never shipped it; 3.3.5's `Script_GetClickFrame` (`0x00564130`) is the
// reference.
//
// It lives in the DLL, not addon Lua, because Blizzard shipped it as an
// engine-registered C global — the backport mirrors where the real
// function lives, not the fact that Lua could fake it.
//
// Behaviour, matching the 3.3.5 resolver `FUN_00562990` (minus its private
// name→frame memoization cache, which is only a perf optimization over the
// global lookup):
//   1. `_G[name]` — a plain global-table lookup.
//   2. The value must be a frame wrapper table backed by a
//      `CFrameScriptObject`.
//   3. Its real `GetName()` must equal `name`. This is the load-bearing
//      check: it stops a reassigned global (`_G["Foo"] = someOtherFrame`)
//      from resolving to a differently-named frame.
// Return the frame on a match, otherwise nil.
//
// `GetName()` is the object's vtable slot +4 (`__thiscall`, returns
// `const char *`) — verified from 1.12's own `Script_GetName`
// (`0x0079FF60` / `0x007A1390`), which resolves self the same way and reads
// the name from that slot. It's a base virtual present on every
// `CFrameScriptObject` subtype, so calling it after `ResolveObject` is
// safe without the RTTI subtype gate `Script_GetName` does first.

#include "frame/ClickFrame.h"

#include "Game.h"
#include "Offsets.h"

#include <cstring>
#include <string>

namespace Frame::ClickFrame {

namespace {
// CFrameScriptObject::GetName — vtable slot +4, thiscall, no args.
using GetName_t = const char *(__thiscall *)(void *self);

const char *ObjectName(void *obj) {
    auto vtable = *reinterpret_cast<void ***>(obj);
    auto getName = reinterpret_cast<GetName_t>(vtable[1]);
    return getName(obj);
}
} // namespace

static int __fastcall Script_GetClickFrame(void *L) {
    using namespace Game::Lua;

    if (GetTop(L) < 1) {
        Error(L, "Usage: GetClickFrame(\"name\")");
        return 0;
    }
    const char *name = ToString(L, 1);
    if (name == nullptr) {
        PushNil(L);
        return 1;
    }

    // getglobal(name): push _G[name].
    PushString(L, name);
    GetTable(L, GLOBALS_INDEX);

    if (Type(L, -1) != TYPE_TABLE) {
        PushNil(L);
        return 1;
    }

    void *obj = ResolveObject(L, -1);
    if (obj != nullptr) {
        const char *objName = ObjectName(obj);
        if (objName != nullptr && std::strcmp(objName, name) == 0)
            // The wrapper table is still on top — return it as-is.
            return 1;
    }

    PushNil(L);
    return 1;
}

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetClickFrame", &Script_GetClickFrame);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

void *Find(void *L, const char *name, size_t length) {
    using namespace Game::Lua;
    if (L == nullptr || name == nullptr || length == 0)
        return nullptr;
    const std::string key(name, length);
    const int top = GetTop(L);
    PushString(L, key.c_str());
    RawGet(L, GLOBALS_INDEX);
    void *frame = nullptr;
    if (Type(L, -1) == TYPE_TABLE) {
        void *obj = ResolveObject(L, -1);
        if (obj != nullptr && IsA(obj, Offsets::VAR_FRAME_LUA_TYPE_ID)) {
            const char *objName = ObjectName(obj);
            if (std::strcmp(objName != nullptr ? objName : "<unnamed>", key.c_str()) == 0)
                frame = obj;
        }
    }
    SetTop(L, top);
    return frame;
}

} // namespace Frame::ClickFrame
