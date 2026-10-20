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

#include "Game.h"

#include <cstdint>
#include <string>

// Animation groups and animations as real 1.12 script objects.
//
// 4.3.4's CSimpleAnimGroup and CSimpleAnim are CScriptObjects: the engine
// hands them to Lua as the same kind of table as a frame (`t[0]` = the
// object, metatable `__framescript_meta`), finds their methods through the
// object's vtable, resolves `self` with the object's IsA, and stores their
// scripts in {handler, context} slots it finds through the object's script
// resolver. 1.12 has the same machinery (benilla-re ui/methods, ui/script):
//   - ConstructScriptObject (0x00701AE0) builds the 0x24-byte header below;
//     DestructScriptObject (0x00701B40) releases it;
//   - LockScriptObject / UnlockScriptObject (0x00701BD0 / 0x00701CD0) make and
//     free the object's table and its name global;
//   - `__framescript_meta.__index` (0x007020B0) calls vtable+0x08 (L, name)
//     to push a method; ResolveTypedObject's type test calls vtable+0x10;
//   - SetScriptObjectScript (0x007025C0) compiles an XML script body into
//     the slot vtable+0x0C returns, and FrameScriptReleaseScript (0x00702670)
//     frees a slot.
// So an animation object is an engine script object whose vtable is ours.
// It has no C++ virtual functions - its first bytes are the engine header -
// and its class (Class) carries what differs per type.
namespace Animation {

// The 1.12 script-object header, as ConstructScriptObject fills it.
struct ScriptObjectHeader {
    void **vtable;
    int32_t holders;     // +0x04, LockScriptObject's count
    int32_t luaRef;      // +0x08, the table's registry ref (-2 when none)
    uint32_t onEvent[2]; // +0x0C, the base OnEvent slot
    uint32_t events[4];  // +0x14, the event index array
};
static_assert(sizeof(ScriptObjectHeader) == 0x24, "the engine's script-object header");

struct Class {
    const char *typeName; // GetObjectType, IsObjectType
    const Class *base;
    // The class's own script names, lower case; a slot per name.
    const char *const *scriptNames;
    int scriptCount;
    const Game::Lua::FrameMethodEntry *methods;
    int methodCount;
    int typeId;     // from the engine's type counter, on first use
    int methodsRef; // registry ref of the method table, per Lua state
};

constexpr int kMaxScripts = 8;

class Object : public ScriptObjectHeader {
  public:
    const Class *cls;
    // The script object this one hangs from: a group's region, an
    // animation's group (GetParent, "$parent").
    void *parent;
    std::string name;
    uint32_t scripts[kMaxScripts][2] = {}; // {handler ref, context}

    // The object's script slot for `name` (any case), or nullptr.
    uint32_t *ScriptSlot(const char *name);
    bool HasScript(int index) const { return scripts[index][0] != 0; }
    // Runs script `index` with `nargs` values pushed on top of the stack,
    // and pops them.
    void RunScript(int index, int nargs);

    const char *Name() const { return name.empty() ? nullptr : name.c_str(); }
    // While set, a destroyed object leaves the engine alone: its table and
    // handlers belonged to a Lua state that is gone (/reload leftovers).
    static bool s_detached;
    const char *DisplayName() const { return name.empty() ? "<unnamed>" : name.c_str(); }

  protected:
    // Registers the object's table, under its name when it has one ("$parent"
    // resolved from `parent`).
    Object(const Class &cls, void *parent, const char *name);
    ~Object();
};

// The class's type id (the engine's counter, assigned on first use).
int TypeId(Class &cls);
// The object at `idx` when it is of `cls` or a class derived from it, else
// raises the engine's errors (as every method does) and returns nullptr.
Object *Resolve(void *L, int idx, Class &cls);
// Pushes the object's table.
void Push(void *L, Object *object);
// Whether a script object is one of ours.
bool IsOurs(const void *object);
// 4.3.4's CScriptObject::CreateName: "$parent" at the start of `source`
// becomes the nearest named ancestor's name, "Top" when none.
std::string CreateName(const char *source, const void *parent);

// Builds every class's method table in the current Lua state.
void RegisterClass(Class &cls);

// The base methods every class has (4.3.4's CScriptObject methods), and the
// script methods groups and animations share.
int __fastcall Script_GetObjectType(void *L);
int __fastcall Script_IsObjectType(void *L);
int __fastcall Script_GetName(void *L);
int __fastcall Script_GetParent(void *L);
int __fastcall Script_HasScript(void *L);
int __fastcall Script_GetScript(void *L);
int __fastcall Script_SetScript(void *L);
int __fastcall Script_HookScript(void *L);

} // namespace Animation
