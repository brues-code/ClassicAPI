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

// Scripts on textures and font strings: `region:SetScript`, `GetScript`,
// `HookScript`, `HasScript` and `<Scripts>` inside a `<Texture>` or
// `<FontString>`, as 3.80.2 has them (docs/notes/region-mouse-3802.md).
//
// 3.80.2's region takes eight scripts (FUN_140593DF0): OnLoad, OnShow,
// OnHide, OnEnter, OnLeave, OnMouseDown, OnMouseUp and OnMouseWheel. Any
// other name raises "%s doesn't have a "%s" script", as the frame's do.
//
// A 1.12 region is a script object like a frame, and every script object
// names its scripts through vtable +0x0C (VTBL_FRAME_SCRIPT_RESOLVER): the
// engine's XML script compiler (FUN_SCRIPT_OBJECT_SET_SCRIPT_TEXT) and
// SetScript / GetScript ask it for the {handler ref, context} slot of a name.
// The texture's and the font string's vtables hold the base resolver there,
// which knows only OnEvent; this module points both at its own, which hands
// out a slot per region and script from a table here. Line and MaskTexture
// copy the texture's vtable when they are first made, which is after the
// DLL loads, so they take the same resolver.
//
// The Lua methods are the engine's frame ones (0x007748D0 SetScript,
// 0x00774780 GetScript, 0x00774680 HasScript) on a region: the frame's own
// check `self` against the Frame type, so these do the same steps after the
// Region check. HookScript is Frame HookScript's (frame/Modern.cpp).
// SetScript never turns a region's mouse on (3.80.2's FUN_1405BDB60 does
// that only for a Frame); `<Scripts>` in XML does (Frame::RegionInput).
//
// XML: each handler element of a region's `<Scripts>` compiles through the
// engine's compiler, or binds `function=` (_G[name]) or `method=` (the
// region's own field), as Xml::ParentKey does for frames. A template's
// `<Scripts>` loads first, so the node's own handler wins. OnLoad runs once,
// after the region's outermost node load, with its mixins in place.
// The same `<Scripts>` turns the region's input on (Frame::RegionInput::
// AutoEnable), before OnLoad runs, as 3.80.2's one XML apply does both.
// `inherit="prepend"` / `"append"` and `intrinsicOrder` are frame-only.
//
// OnShow and OnHide run when the region becomes visible and when it stops
// (Frame::RegionVisibility): its own Show / Hide, and its owner frame's.
// 3.80.2 queues OnShow and runs it after the show walk (FUN_14055EC20); 1.12
// runs it in the walk, right after the engine's call for that region, as the
// walk reads each next region after the call. Nothing runs while the owner
// loads from XML (OFF_FRAME_LOADING), as for the frame's own OnShow.

#include "frame/RegionScripts.h"

#include "Common.h"
#include "Game.h"
#include "HookSecureFunc.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "frame/RegionDestruction.h"
#include "frame/RegionInput.h"
#include "frame/RegionVisibility.h"
#include "frame/ScriptSlots.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>
#include <unordered_map>

namespace Frame::RegionScripts {

namespace {

using SetScriptText_t = bool(__fastcall *)(void *object, void *edx, const char *name, const char *text);

const char *const kNames[SCRIPT_COUNT] = {
    "OnLoad", "OnShow", "OnHide", "OnEnter", "OnLeave", "OnMouseDown", "OnMouseUp", "OnMouseWheel",
};

struct Cells {
    uint32_t slot[SCRIPT_COUNT][2] = {};
};
// Node-based: a slot's address stays valid while the engine holds it.
std::unordered_map<const void *, Cells> g_cells;

int IndexOf(const char *name) {
    for (int i = 0; i < SCRIPT_COUNT; ++i)
        if (Ascii::EqualCI(name, kNames[i]))
            return i;
    return -1;
}

// vtable +0x0C, `__thiscall(region, name) -> slot`, `ret 4`.
uint32_t *__fastcall Resolve(void *region, void * /*edx*/, const char *name) {
    const int i = IndexOf(name);
    return i >= 0 ? g_cells[region].slot[i] : nullptr;
}

// Before any region exists: the DLL loads before the client builds its UI.
struct ResolverPatch {
    ResolverPatch() {
        for (uintptr_t vtable : {uintptr_t{Offsets::VTBL_SIMPLETEXTURE}, uintptr_t{Offsets::VTBL_SIMPLEFONTSTRING}})
            Common::RedirectSlot(vtable + Offsets::VTBL_FRAME_SCRIPT_RESOLVER,
                                 Offsets::FUN_SCRIPT_OBJECT_BASE_RESOLVE_SCRIPT, reinterpret_cast<const void *>(&Resolve));
    }
};
const ResolverPatch g_patch;

void OnRegionDestroyed(void *region) {
    const auto it = g_cells.find(region);
    if (it == g_cells.end())
        return;
    for (auto &slot : it->second.slot)
        Frame::ScriptSlots::Release(slot);
    g_cells.erase(it);
}

const Frame::RegionDestruction::AutoSubscribe _destructionSub{&OnRegionDestroyed};

// Every handler ref dies with the Lua state.
void PrepareForReload() { g_cells.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};
const Game::GlueBootAutoRegister _glueBoot{&PrepareForReload};

void OnVisibility(void *region, bool visible) {
    const Script script = visible ? ON_SHOW : ON_HIDE;
    if (!Has(region, script))
        return;
    const void *owner = Game::Read<void *>(region, Offsets::OFF_REGION_PARENT);
    if (owner != nullptr && Game::Read<int>(owner, Offsets::OFF_FRAME_LOADING) != 0)
        return;
    Run(region, script);
}

const Frame::RegionVisibility::AutoSubscribe _visibilitySub{&OnVisibility};

// ---- XML ---------------------------------------------------------------------

const void *ScriptsOf(const void *node) {
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c))
        if (Xml::TagIs(c, "Scripts"))
            return c;
    return nullptr;
}

// `function=` or `method=`: the handler on top of the stack, or false with
// nothing pushed when it names no function.
bool PushBoundHandler(void *L, void *region, const char *function, const char *method) {
    const int top = Game::Lua::GetTop(L);
    if (function != nullptr && *function != '\0') {
        Game::Lua::PushString(L, function);
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    } else {
        if (!Xml::PushWrapper(L, region))
            return false;
        Game::Lua::PushString(L, method);
        Game::Lua::GetTable(L, -2);
        Game::Lua::Remove(L, -2);
    }
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION)
        return true;
    Game::Lua::SetTop(L, top);
    return false;
}

void LoadScripts(void *region, const void *node) {
    const void *scripts = ScriptsOf(node);
    if (scripts == nullptr)
        return;
    Frame::RegionInput::AutoEnable(region, scripts);
    for (const void *e = Xml::FirstChild(scripts); e != nullptr; e = Xml::NextSibling(e)) {
        const char *script = Xml::Tag(e);
        uint32_t *slot = script != nullptr ? Resolve(region, nullptr, script) : nullptr;
        if (slot == nullptr)
            continue;
        const char *function = Xml::Attr(e, "function");
        const char *method = Xml::Attr(e, "method");
        if ((function != nullptr && *function != '\0') || (method != nullptr && *method != '\0')) {
            void *L = Game::Lua::State();
            if (L == nullptr || !PushBoundHandler(L, region, function, method))
                continue;
            Frame::ScriptSlots::Release(slot);
            slot[0] = static_cast<uint32_t>(Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX));
            slot[1] = 0;
            continue;
        }
        const char *body = Game::Read<const char *>(e, Offsets::OFF_XML_NODE_TEXT);
        if (body != nullptr && *body != '\0')
            reinterpret_cast<SetScriptText_t>(Offsets::FUN_SCRIPT_OBJECT_SET_SCRIPT_TEXT)(region, nullptr, script,
                                                                                         body);
    }
}

template <Xml::TypeLoad::Loader K> void OnRegionNode(void *region, const void *node, void * /*status*/) {
    LoadScripts(region, node);
    if (!Xml::TypeLoad::Nested(K))
        Run(region, ON_LOAD);
}

const Xml::TypeLoad::AutoSubscribe _textureSub{Xml::TypeLoad::TEXTURE, &OnRegionNode<Xml::TypeLoad::TEXTURE>};
const Xml::TypeLoad::AutoSubscribe _fontStringSub{Xml::TypeLoad::FONT_STRING,
                                                  &OnRegionNode<Xml::TypeLoad::FONT_STRING>};

// ---- Lua ---------------------------------------------------------------------

void *Self(void *L) {
    void *region = Game::Lua::ResolveRegion(L);
    return region != nullptr && IsRegion(region) ? region : nullptr;
}

void ErrorNoScript(void *L, void *region) {
    Game::Lua::Error(L, "%s doesn't have a \"%s\" script", Game::Lua::ObjectDisplayName(region),
                     Game::Lua::ToString(L, 2));
}

// 1 when the region has the script, else nil (the engine's 0x00774680).
int __fastcall Script_HasScript(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:HasScript(\"type\")", Game::Lua::ObjectDisplayName(region));
        return 0;
    }
    Game::Lua::PushFlag(L, IndexOf(Game::Lua::ToString(L, 2)) >= 0);
    return 1;
}

// The engine's GetScript: the slot's context becomes current, then the
// handler (nil for none) is pushed.
int __fastcall Script_GetScript(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:GetScript(\"type\")", Game::Lua::ObjectDisplayName(region));
        return 0;
    }
    const uint32_t *slot = Resolve(region, nullptr, Game::Lua::ToString(L, 2));
    if (slot == nullptr) {
        ErrorNoScript(L, region);
        return 0;
    }
    if (slot[1] != 0 && Game::Read<int>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT_DEPTH) != 0)
        Game::Ref<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT) = slot[1];
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, static_cast<int>(slot[0]));
    return 1;
}

// The engine's SetScript: a function or nil only, the old handler released,
// a ref of the new one (0 for nil) stored with the current context.
int __fastcall Script_SetScript(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    const int handlerType = Game::Lua::Type(L, 3);
    if (!Game::Lua::IsString(L, 2) ||
        (handlerType != Game::Lua::TYPE_FUNCTION && handlerType != Game::Lua::TYPE_NIL)) {
        Game::Lua::Error(L, "Usage: %s:SetScript(\"type\", function)", Game::Lua::ObjectDisplayName(region));
        return 0;
    }
    uint32_t *slot = Resolve(region, nullptr, Game::Lua::ToString(L, 2));
    if (slot == nullptr) {
        ErrorNoScript(L, region);
        return 0;
    }
    Frame::ScriptSlots::Release(slot);
    Game::Lua::SetTop(L, 3);
    const int ref = Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX);
    slot[0] = ref > 0 ? static_cast<uint32_t>(ref) : 0;
    slot[1] = Game::Read<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    return 0;
}

// No handler yet: the hook becomes the handler. Otherwise the handler becomes
// the hooksecurefunc wrapper over (old, hook), under the slot's context.
int __fastcall Script_HookScript(void *L) {
    void *region = Self(L);
    if (region == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2) || Game::Lua::Type(L, 3) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: %s:HookScript(\"type\", function)", Game::Lua::ObjectDisplayName(region));
        return 0;
    }
    Game::Lua::SetTop(L, 3); // (self, type, hook)
    const uint32_t hookWord = Game::Read<uint32_t>(Offsets::VAR_FRAMESCRIPT_EXEC_CONTEXT);
    Game::Lua::PushValue(L, 1);
    Game::Lua::PushValue(L, 2);
    Game::Lua::PushCClosure(L, &Script_GetScript, 0);
    Game::Lua::Insert(L, -3);
    Game::Lua::Call(L, 2, 1); // (self, type, hook, old)
    // Script_GetScript raised for an unknown name, so the slot exists.
    const uint32_t *slot = Resolve(region, nullptr, Game::Lua::ToString(L, 2));
    return HookSecureFunc::SetHookedScript(L, hookWord, slot[1], &Script_SetScript);
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetScript", &Script_SetScript},
    {"GetScript", &Script_GetScript},
    {"HookScript", &Script_HookScript},
    {"HasScript", &Script_HasScript},
};

const Game::Doc::Field kTypeArg[] = {
    Game::Doc::Req("scriptTypeName", "string",
                   "OnLoad, OnShow, OnHide, OnEnter, OnLeave, OnMouseDown, OnMouseUp or OnMouseWheel."),
};
const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("scriptTypeName", "string",
                   "OnLoad, OnShow, OnHide, OnEnter, OnLeave, OnMouseDown, OnMouseUp or OnMouseWheel."),
    Game::Doc::Opt("script", "function", nullptr, "The handler. Nil removes it."),
};
const Game::Doc::Field kHookArgs[] = {
    Game::Doc::Req("scriptTypeName", "string",
                   "OnLoad, OnShow, OnHide, OnEnter, OnLeave, OnMouseDown, OnMouseUp or OnMouseWheel."),
    Game::Doc::Req("script", "function", "Runs after the current handler, with the same arguments."),
};
const Game::Doc::Field kGetRets[] = {
    Game::Doc::Opt("script", "function", nullptr, "The handler, or nil when there is none."),
};
const Game::Doc::Field kHasRets[] = {
    Game::Doc::Opt("hasScript", "number", nullptr, "1 when the region takes the script, otherwise nil."),
};
const Game::Doc::Function kSetScript{
    "Sets the region's handler for a script. It does not turn on the region's mouse input.", kSetArgs, {}};
const Game::Doc::Function kGetScript{"Returns the region's handler for a script.", kTypeArg, kGetRets};
const Game::Doc::Function kHookScript{"Adds a handler that runs after the region's current one.", kHookArgs, {}};
const Game::Doc::Function kHasScript{"Returns whether the region takes a script of this name.", kTypeArg,
                                     kHasRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetScript", &kSetScript},
    {"GetScript", &kGetScript},
    {"HookScript", &kHookScript},
    {"HasScript", &kHasScript},
};

void RegisterLuaFunctions() {
    for (uintptr_t registry : {uintptr_t{Offsets::VAR_TEXTURE_METHOD_REGISTRY},
                               uintptr_t{Offsets::VAR_FONTSTRING_METHOD_REGISTRY}})
        Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(registry), g_methods, g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

bool IsRegion(const void *object) {
    if (object == nullptr)
        return false;
    const void *vtable = Game::Read<const void *>(object, 0);
    return Game::Read<const void *>(vtable, Offsets::VTBL_FRAME_SCRIPT_RESOLVER) ==
           reinterpret_cast<const void *>(&Resolve);
}

const uint32_t *Slot(const void *region, Script script) {
    const auto it = g_cells.find(region);
    return it != g_cells.end() ? it->second.slot[script] : nullptr;
}

} // namespace Frame::RegionScripts
