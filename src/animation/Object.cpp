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

#include "animation/Object.h"

#include "HookSecureFunc.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/ScriptArgs.h"

#include <cstring>

namespace Animation {

namespace {

using Construct_t = void(__fastcall *)(void *object);
using Destruct_t = void(__fastcall *)(void *object);
using Lock_t = void(__fastcall *)(void *object, void *edx, const char *name);
using Unlock_t = void(__fastcall *)(void *object, void *edx, const char *name);
using BaseResolve_t = uint32_t *(__fastcall *)(void *object, void *edx, const char *name);
using Release_t = void(__stdcall *)(uint32_t *slot);
using LuaUnref_t = void(__fastcall *)(void *L, int t, int ref);
using GetName_t = const char *(__fastcall *)(const void *object);

// 4.3.4's CScriptObject type name, which every class's IsA(name) accepts.
constexpr const char *kObjectTypeName = "Object";

Object *Self(void *object) { return static_cast<Object *>(object); }

// ---- The vtable (the engine calls these on our objects) --------------------

void *__fastcall V_Delete(Object *object, void * /*edx*/, unsigned flags);

const char *__fastcall V_GetName(Object *object) { return object->Name(); }

// vtable+0x08: push the method `name` of the object's class or a base.
int __fastcall V_FindMethod(Object *object, void * /*edx*/, void *L, const char *name) {
    for (const Class *c = object->cls; c != nullptr; c = c->base) {
        if (c->methodsRef <= 0)
            continue;
        Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, c->methodsRef);
        Game::Lua::PushString(L, name);
        Game::Lua::RawGet(L, -2);
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL) {
            Game::Lua::Remove(L, -2);
            return 1;
        }
        Game::Lua::SetTop(L, -3);
    }
    return 0;
}

// vtable+0x0C: 4.3.4's GetScriptByName - the base OnEvent first.
uint32_t *__fastcall V_ResolveScript(Object *object, void * /*edx*/, const char *name) {
    uint32_t *slot =
        reinterpret_cast<BaseResolve_t>(Offsets::FUN_SCRIPT_OBJECT_BASE_RESOLVE_SCRIPT)(object, nullptr, name);
    return slot != nullptr ? slot : object->ScriptSlot(name);
}

int __fastcall V_IsA(Object *object, void * /*edx*/, int typeId) {
    for (const Class *c = object->cls; c != nullptr; c = c->base)
        if (c->typeId != 0 && c->typeId == typeId)
            return 1;
    return 0;
}

// vtable+0x14 differs per engine class; nothing calls it on ours.
int __fastcall V_Unused(Object * /*object*/) { return 0; }

int __fastcall V_IsAName(Object *object, void * /*edx*/, const char *typeName) {
    if (typeName == nullptr)
        return 0;
    for (const Class *c = object->cls; c != nullptr; c = c->base)
        if (Ascii::EqualCI(typeName, c->typeName))
            return 1;
    return Ascii::EqualCI(typeName, kObjectTypeName) ? 1 : 0;
}

const char *__fastcall V_GetObjectTypeName(Object *object) { return object->cls->typeName; }

void *g_vtable[] = {
    reinterpret_cast<void *>(&V_Delete),       reinterpret_cast<void *>(&V_GetName),
    reinterpret_cast<void *>(&V_FindMethod),   reinterpret_cast<void *>(&V_ResolveScript),
    reinterpret_cast<void *>(&V_IsA),          reinterpret_cast<void *>(&V_Unused),
    reinterpret_cast<void *>(&V_IsAName),      reinterpret_cast<void *>(&V_GetObjectTypeName),
};

// Slot 0, the engine's deleting destructor. The engine never deletes our
// objects - their owners (a region, a group) do, by their concrete type.
void *__fastcall V_Delete(Object *object, void * /*edx*/, unsigned /*flags*/) { return object; }

} // namespace

// ---- Object -------------------------------------------------------------------

Object::Object(const Class &cls, void *parent, const char *name) : cls(&cls), parent(parent) {
    reinterpret_cast<Construct_t>(Offsets::FUN_SCRIPT_OBJECT_CONSTRUCT)(this);
    vtable = g_vtable;
    if (name != nullptr && *name != '\0')
        this->name = CreateName(name, parent);
    reinterpret_cast<Lock_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER)(this, nullptr, Name());
}

bool Object::s_detached = false;

Object::~Object() {
    if (s_detached)
        return;
    for (auto &slot : scripts)
        reinterpret_cast<Release_t>(Offsets::FUN_SCRIPT_RELEASE)(slot);
    while (holders > 1)
        reinterpret_cast<Unlock_t>(Offsets::FUN_SCRIPT_OBJECT_UNLOCK)(this, nullptr, nullptr);
    if (holders > 0)
        reinterpret_cast<Unlock_t>(Offsets::FUN_SCRIPT_OBJECT_UNLOCK)(this, nullptr, Name());
    reinterpret_cast<Destruct_t>(Offsets::FUN_SCRIPT_OBJECT_DESTRUCT)(this);
}

uint32_t *Object::ScriptSlot(const char *scriptName) {
    if (scriptName == nullptr)
        return nullptr;
    for (const Class *c = cls; c != nullptr; c = c->base) {
        for (int i = 0; i < c->scriptCount; ++i)
            if (Ascii::EqualCI(scriptName, c->scriptNames[i]))
                return scripts[i];
        if (c->scriptCount > 0)
            break; // only one class in a chain declares the scripts
    }
    return nullptr;
}

void Object::RunScript(int index, int nargs) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int first = Game::Lua::GetTop(L) - nargs + 1;
    if (HasScript(index))
        Frame::ScriptArgs::RunSlotWithValues(L, scripts[index], this, first, nargs);
    Game::Lua::SetTop(L, first - 1);
}

// ---- Helpers --------------------------------------------------------------------

int TypeId(Class &cls) { return Game::Lua::FrameScriptTypeId(reinterpret_cast<uintptr_t>(&cls.typeId)); }

Object *Resolve(void *L, int idx, Class &cls) {
    return static_cast<Object *>(Game::Lua::ResolveTypedObject(L, idx, reinterpret_cast<uintptr_t>(&cls.typeId)));
}

void Push(void *L, Object *object) { Game::Lua::PushObject(L, object); }

bool IsOurs(const void *object) { return object != nullptr && Game::Read<void **>(object, 0) == g_vtable; }

std::string CreateName(const char *source, const void *parent) {
    constexpr const char kParent[] = "$parent";
    constexpr size_t kParentLen = sizeof(kParent) - 1;
    if (std::strlen(source) < kParentLen || !Ascii::EqualCI(std::string(source, kParentLen).c_str(), kParent))
        return source;
    std::string name = "Top";
    for (const void *p = parent; p != nullptr;) {
        const char *pname = IsOurs(p) ? static_cast<const Object *>(p)->Name()
                                      : Game::Read<GetName_t>(Game::Read<void *>(p, 0),
                                                              Offsets::VTBL_SCRIPTOBJECT_GET_NAME)(p);
        if (pname != nullptr && *pname != '\0') {
            name = pname;
            break;
        }
        p = IsOurs(p) ? static_cast<const Object *>(p)->parent : Game::Read<void *>(p, Offsets::OFF_REGION_PARENT);
    }
    return name + (source + kParentLen);
}

void RegisterClass(Class &cls) {
    void *L = Game::Lua::State();
    TypeId(cls);
    Game::Lua::NewTable(L);
    for (int i = 0; i < cls.methodCount; ++i) {
        Game::Lua::PushString(L, cls.methods[i].name);
        Game::Lua::PushCClosure(L, cls.methods[i].func, 0);
        Game::Lua::RawSet(L, -3);
    }
    cls.methodsRef = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
}

// ---- The base methods -----------------------------------------------------------

namespace {

// The object at self, of any of our classes.
Object *AnySelf(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return nullptr;
    void *object = Game::Lua::ResolveObject(L, 1);
    return IsOurs(object) ? Self(object) : nullptr;
}

} // namespace

int __fastcall Script_GetObjectType(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    Game::Lua::PushString(L, object->cls->typeName);
    return 1;
}

int __fastcall Script_IsObjectType(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:IsObjectType(\"TYPE\")", object->DisplayName());
        return 0;
    }
    if (V_IsAName(object, nullptr, Game::Lua::ToString(L, 2)))
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __fastcall Script_GetName(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    Game::Lua::PushString(L, object->Name());
    return 1;
}

int __fastcall Script_GetParent(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    if (object->parent != nullptr)
        Game::Lua::PushObject(L, object->parent);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// 4.3.4's HasScript: 1 when the class has the script, else nil.
int __fastcall Script_HasScript(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:HasScript(\"type\")", object->DisplayName());
        return 0;
    }
    if (V_ResolveScript(object, nullptr, Game::Lua::ToString(L, 2)) != nullptr)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

// The engine's Script GetScript (0x00774780) on our object: the slot's
// context becomes current, then the handler (nil for none) is pushed.
int __fastcall Script_GetScript(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:GetScript(\"type\")", object->DisplayName());
        return 0;
    }
    const uint32_t *slot = V_ResolveScript(object, nullptr, Game::Lua::ToString(L, 2));
    if (slot == nullptr) {
        Game::Lua::Error(L, "%s doesn't have a \"%s\" script", object->DisplayName(), Game::Lua::ToString(L, 2));
        return 0;
    }
    if (slot[1] != 0 && Game::Read<int>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH) != 0)
        Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT) = slot[1];
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, static_cast<int>(slot[0]));
    return 1;
}

// The engine's Script SetScript (0x007748D0) on our object: release the old
// handler, store a ref of the new one (0 for nil) with the current context.
int __fastcall Script_SetScript(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    const int handlerType = Game::Lua::Type(L, 3);
    if (!Game::Lua::IsString(L, 2) ||
        (handlerType != Game::Lua::TYPE_FUNCTION && handlerType != Game::Lua::TYPE_NIL &&
         handlerType != -1 /* none */)) {
        Game::Lua::Error(L, "Usage: %s:SetScript(\"type\", function)", object->DisplayName());
        return 0;
    }
    uint32_t *slot = V_ResolveScript(object, nullptr, Game::Lua::ToString(L, 2));
    if (slot == nullptr) {
        Game::Lua::Error(L, "%s doesn't have a \"%s\" script", object->DisplayName(), Game::Lua::ToString(L, 2));
        return 0;
    }
    reinterpret_cast<Release_t>(Offsets::FUN_SCRIPT_RELEASE)(slot);
    Game::Lua::SetTop(L, 3);
    const int ref = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
    slot[0] = ref > 0 ? static_cast<uint32_t>(ref) : 0;
    slot[1] = Game::Read<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    return 0;
}

// Frame HookScript's (frame/Modern.cpp) on our object: no handler yet -> the
// hook is the handler; else the hooksecurefunc wrapper over (old, hook).
int __fastcall Script_HookScript(void *L) {
    Object *object = AnySelf(L);
    if (object == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2) || Game::Lua::Type(L, 3) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: %s:HookScript(\"type\", function)", object->DisplayName());
        return 0;
    }
    Game::Lua::SetTop(L, 3); // (self, type, hook)
    Game::Lua::PushValue(L, 1);
    Game::Lua::PushValue(L, 2);
    Game::Lua::PushCClosure(L, &Script_GetScript, 0);
    Game::Lua::Insert(L, -3);
    Game::Lua::Call(L, 2, 1); // (self, type, hook, old)
    if (Game::Lua::Type(L, 4) == Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Insert(L, 3);        // (self, type, old, hook)
        HookSecureFunc::PushWrapper(L); // (self, type, wrapper)
    } else {
        Game::Lua::SetTop(L, 3);
    }
    return Script_SetScript(L);
}

} // namespace Animation
