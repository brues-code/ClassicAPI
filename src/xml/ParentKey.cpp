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

// `parentKey="Name"` on XML frames, textures and font strings.
//
// Later clients let an XML element name itself on its parent: a child
// `<Frame parentKey="Child"/>`, `<Texture parentKey="Icon"/>` or a button's
// `<NormalTexture parentKey="ntx"/>` is reachable as `parent.Child`,
// `parent.Icon` or `button.ntx` without a global name. Vanilla's schema has no
// such attribute; the parser keeps it on the node but nothing reads it, so
// ported code that indexes `self.Child` gets nil.
//
// The engine loads each XML element through a node loader that lives on the
// object's layout sub-object, OFF_XML_LOADER_SUBOBJECT (0x24) bytes into it:
// `(object+0x24)->vtable[+8](node, status)`. The frame builder makes that call
// at 0x006EE4D9..E1 and the region factory at 0x006F2756..5E. The loaders:
//   frames       FUN_00769820 - every derived frame type's loader calls it first
//                (Button's 0x007788C0 at 0x007788D6, then reads "NormalTexture";
//                ScrollFrame, Slider, EditBox, ... likewise), so one hook covers
//                all frame types
//   textures     FUN_0076FE20 - reads "alphaMode" (0x00770179)
//   font strings FUN_00770F40 - reads "justifyH" (0x00771308)
// After the engine has loaded the node (frames through the shared
// Xml::FrameLoad observer, which owns that loader's hook; textures and font
// strings through co-hooks here), read `parentKey`
// with the engine's own attribute getter and, when present, set
// `parent[key] = object` on the Lua wrappers, where parent is the object's
// script-region parent (OFF_REGION_PARENT, which GetParent reads for frames
// and regions alike).
//
// Wrappers: an XML element with a `name` is registered under it by SetName
// (FUN_0076C650, from the pre-load step) before its loader runs. An unnamed
// one is registered lazily the first time Lua touches it, by
// ScriptRegister(object, NULL) -- GetParent (0x007A1460) does exactly that for
// the parent it returns. parentKey elements are usually unnamed, so this does
// the same lazy registration the engine would; it never registers an object
// that has a name (that is the engine's job, under the name). A wrapper is
// only used when its [0] points back at the object.
//
// Ordering matches later clients. The builder loads content, then child
// frames, then finalizes (OnLoad), so a child's key is bound while its parent
// is still being built and the parent's <OnLoad> already sees `self.Child`.
// Inherited template nodes go through the same loaders, so a parentKey inside
// a virtual template binds on every frame built from it.
//
// The frame loader hook also binds `<Scripts><OnLoad function="Name"/></Scripts>`,
// the later-client way to use an existing global function as a handler.
// Vanilla's schema has no `function` attribute: the element compiles as an
// empty body and the handler never runs (Zygor Guides Viewer 2.0's main frame
// binds its whole OnLoad this way). After the engine has loaded the node, each
// script element carrying `function` becomes `frame:SetScript(<element>,
// _G[function])`. The loader runs before the builder's finalize, so a bound
// OnLoad still fires at the usual point. `method="Name"` does the same with
// `frame[Name]`, the frame's own field - usually a function from its mixin
// (Xml::Mixin), which is in place before any node of the frame loads.
// A script with an `inherit="prepend"` / `"append"` element is left to
// Xml::ScriptInherit, which runs around this binding.
//
// A button's font elements take their font from `style` on later clients
// (`<NormalFont style="GameFontNormal"/>`) where vanilla reads `inherits`; the
// Button loader FUN_XML_BUTTON_LOAD loads each into an embedded font object
// and, with no `inherits`, leaves it empty. After that loader, a font element
// with `style` and no `inherits` is applied through the engine's own setter
// for that state (SetTextFontObject / SetHighlightFontObject /
// SetDisabledFontObject) with `_G[style]`.
//
// `parentArray="Name"` appends the object to `parent.Name` (created on first
// use) in load order, the same way.
//
// All three loaders also re-set <Anchor>s that use `relativeKey` or `x` / `y`
// (Xml::Anchors), once the earlier siblings' keys are bound.
//
// The texture and font string loaders also apply <KeyValues>
// (Xml::KeyValues, which reads frames itself).
//
// Every loader also loads an <Animations> child (Animation::Xml), before the
// builder finalizes, so OnLoad sees the groups.
//
// The texture loader also reads `mask="path"` (`<Texture file=".." mask=".."/>`,
// the later-client attribute) and applies it as texture:SetMask(path), and
// `atlas="name"` with `useAtlasSize`, applied as texture:SetAtlas(name,
// useAtlasSize).

#include "xml/FrameLoad.h"

#include "Game.h"
#include "Offsets.h"
#include "animation/Xml.h"
#include "texture/Atlas.h"
#include "texture/Mask.h"
#include "xml/Anchors.h"
#include "xml/KeyValues.h"
#include "xml/Mixin.h"
#include "xml/ScriptInherit.h"

#include <cstdint>

namespace Xml::ParentKey {

namespace {

using Loader_t = uintptr_t(__fastcall *)(void *self, void *edx, const void *node, void *status);

Loader_t g_origTextureLoad = nullptr;
Loader_t g_origFontStringLoad = nullptr;
Loader_t g_origButtonLoad = nullptr;

using Xml::FirstChild;
using Xml::NextSibling;
using Xml::PushWrapper;
using Xml::TagIs;

// parent[key] = object. Leaves the stack as it found it; anything
// unexpected just skips the bind.
void BindParentKey(void *object, const void *node) {
    const char *key = Xml::Attr(node, "parentKey");
    if (key == nullptr || *key == '\0')
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    void *parent = Game::Read<void *>(object, Offsets::OFF_REGION_PARENT);
    if (parent == nullptr)
        return;

    const int top = Game::Lua::GetTop(L);
    if (PushWrapper(L, parent) && PushWrapper(L, object)) { // [parent, object]
        Game::Lua::PushString(L, key);                    // [parent, object, key]
        Game::Lua::PushValue(L, -2);                      // [parent, object, key, object]
        Game::Lua::SetTable(L, -4);                       // parent[key] = object
    }
    Game::Lua::SetTop(L, top);
}

// parentArray="Name": appends the object to parent.Name, creating the array
// on first use, in load order.
void BindParentArray(void *object, const void *node) {
    const char *key = Xml::Attr(node, "parentArray");
    if (key == nullptr || *key == '\0')
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    void *parent = Game::Read<void *>(object, Offsets::OFF_REGION_PARENT);
    if (parent == nullptr)
        return;

    const int top = Game::Lua::GetTop(L);
    if (PushWrapper(L, parent)) {                       // [parent]
        Game::Lua::PushString(L, key);
        Game::Lua::GetTable(L, top + 1);                // [parent, array?]
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
            Game::Lua::SetTop(L, top + 1);
            Game::Lua::NewTable(L);                     // [parent, array]
            Game::Lua::PushString(L, key);
            Game::Lua::PushValue(L, -2);
            Game::Lua::SetTable(L, top + 1);            // parent[key] = array
        }
        int n = 0;
        for (;;) {
            Game::Lua::RawGetI(L, top + 2, n + 1);
            const bool present = Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL;
            Game::Lua::SetTop(L, top + 2);
            if (!present)
                break;
            ++n;
        }
        if (PushWrapper(L, object)) {                   // [parent, array, object]
            Game::Lua::PushNumber(L, n + 1);
            Game::Lua::Insert(L, -2);                   // [parent, array, n+1, object]
            Game::Lua::RawSet(L, top + 2);              // array[n+1] = object
        }
    }
    Game::Lua::SetTop(L, top);
}

// ---- <Scripts> function= / method= -----------------------------------------

// frame:SetScript(<element>, _G[function]) for every <Scripts> child of `node`
// that carries a `function` attribute, or frame:SetScript(<element>,
// frame[method]) for one that carries `method`. A name that is not a
// function, or a script the frame type does not have, is skipped.
void BindScriptFunctions(void *object, const void *node) {
    const void *scripts = nullptr;
    for (const void *c = FirstChild(node); c != nullptr; c = NextSibling(c)) {
        if (TagIs(c, "Scripts")) {
            scripts = c;
            break;
        }
    }
    if (scripts == nullptr)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;

    const int top = Game::Lua::GetTop(L);
    bool havePushed = false;
    for (const void *e = FirstChild(scripts); e != nullptr; e = NextSibling(e)) {
        const char *fn = Xml::Attr(e, "function");
        const char *method = Xml::Attr(e, "method");
        const char *script = Xml::Tag(e);
        const bool byFunction = fn != nullptr && *fn != '\0';
        if (script == nullptr || (!byFunction && (method == nullptr || *method == '\0')))
            continue;
        if (Xml::ScriptInherit::Chained(scripts, script))
            continue; // Xml::ScriptInherit sets this script
        if (!havePushed) {
            if (!PushWrapper(L, object)) // [frame]
                break;
            havePushed = true;
        }
        Game::Lua::PushString(L, "SetScript");
        Game::Lua::GetTable(L, top + 1); // [frame, SetScript]
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION)
            break;
        Game::Lua::PushValue(L, top + 1); // [frame, SetScript, frame]
        Game::Lua::PushString(L, script); // [.., frame, script]
        Game::Lua::PushString(L, byFunction ? fn : method);
        Game::Lua::GetTable(L, byFunction ? Game::Lua::GLOBALS_INDEX
                                          : top + 1); // [.., frame, script, handler]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION)
            Game::Lua::PCall(L, 3, 0, 0);
        Game::Lua::SetTop(L, top + 1); // [frame]
    }
    Game::Lua::SetTop(L, top);
}

// ---- button font element style= ---------------------------------------------

struct ButtonFontSlot {
    const char *element;
    const char *setter;
};
const ButtonFontSlot kButtonFontSlots[] = {
    {"NormalFont", "SetTextFontObject"},
    {"HighlightFont", "SetHighlightFontObject"},
    {"DisabledFont", "SetDisabledFontObject"},
};

// button:<setter>(_G[style]) for each font element of `node` that has `style`
// and no `inherits`. A style that names no global object is skipped.
void BindButtonFontStyles(void *self, const void *node) {
    if (self == nullptr || node == nullptr)
        return;
    void *L = nullptr;
    void *object = static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
    int top = 0;
    bool havePushed = false;
    for (const void *c = FirstChild(node); c != nullptr; c = NextSibling(c)) {
        const ButtonFontSlot *slot = nullptr;
        for (const auto &s : kButtonFontSlots) {
            if (TagIs(c, s.element)) {
                slot = &s;
                break;
            }
        }
        if (slot == nullptr)
            continue;
        const char *style = Xml::Attr(c, "style");
        const char *inherits = Xml::Attr(c, "inherits");
        if (style == nullptr || *style == 0 || (inherits != nullptr && *inherits != 0))
            continue;
        if (!havePushed) {
            L = Game::Lua::State();
            if (L == nullptr)
                return;
            top = Game::Lua::GetTop(L);
            if (!PushWrapper(L, object)) // [button]
                return;
            havePushed = true;
        }
        Game::Lua::PushString(L, slot->setter);
        Game::Lua::GetTable(L, top + 1); // [button, setter]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION) {
            Game::Lua::PushValue(L, top + 1); // [button, setter, button]
            Game::Lua::PushString(L, style);
            Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // [.., button, _G[style]]
            if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL)
                Game::Lua::PCall(L, 2, 0, 0);
        }
        Game::Lua::SetTop(L, top + 1); // [button]
    }
    if (havePushed)
        Game::Lua::SetTop(L, top);
}

uintptr_t __fastcall ButtonLoad_h(void *self, void *edx, const void *node, void *status) {
    const uintptr_t r = g_origButtonLoad(self, edx, node, status);
    BindButtonFontStyles(self, node);
    return r;
}

void OnFrameNode(void *object, const void *node, void *status) {
    BindParentKey(object, node);
    BindParentArray(object, node);
    Xml::Anchors::Apply(object, node, status);
    Xml::ScriptInherit::Apply(object, node);
    BindScriptFunctions(object, node);
    Xml::ScriptInherit::Capture(object);
    Animation::Xml::LoadAnimations(object, node, status);
}

const Xml::FrameLoad::AutoSubscribe _frameNode{&OnFrameNode};

// Region loaders: `self` is the layout sub-object inside the object.
void *RegionObject(void *self) {
    return static_cast<uint8_t *>(self) - Offsets::OFF_XML_LOADER_SUBOBJECT;
}

uintptr_t __fastcall TextureLoad_h(void *self, void *edx, const void *node, void *status) {
    if (self == nullptr || node == nullptr)
        return g_origTextureLoad(self, edx, node, status);
    Xml::Mixin::Begin(RegionObject(self), node, status, false);
    const uintptr_t r = g_origTextureLoad(self, edx, node, status);
    Xml::Mixin::End(RegionObject(self));
    {
        BindParentKey(RegionObject(self), node);
        BindParentArray(RegionObject(self), node);
        Xml::Anchors::Apply(RegionObject(self), node, status);
        Xml::KeyValues::ApplyToRegion(RegionObject(self), node, status);
        Animation::Xml::LoadAnimations(RegionObject(self), node, status);
        if (const char *mask = Xml::Attr(node, "mask"); mask != nullptr && *mask != '\0')
            Texture::Mask::SetPath(RegionObject(self), mask);
        if (const char *atlas = Xml::Attr(node, "atlas"); atlas != nullptr && *atlas != '\0') {
            const char *useSize = Xml::Attr(node, "useAtlasSize");
            Texture::Atlas::SetFromXml(RegionObject(self), atlas,
                                       useSize != nullptr && Game::StringToFlag(useSize));
        }
    }
    return r;
}

uintptr_t __fastcall FontStringLoad_h(void *self, void *edx, const void *node, void *status) {
    if (self == nullptr || node == nullptr)
        return g_origFontStringLoad(self, edx, node, status);
    Xml::Mixin::Begin(RegionObject(self), node, status, false);
    const uintptr_t r = g_origFontStringLoad(self, edx, node, status);
    Xml::Mixin::End(RegionObject(self));
    {
        BindParentKey(RegionObject(self), node);
        BindParentArray(RegionObject(self), node);
        Xml::Anchors::Apply(RegionObject(self), node, status);
        Xml::KeyValues::ApplyToRegion(RegionObject(self), node, status);
        Animation::Xml::LoadAnimations(RegionObject(self), node, status);
    }
    return r;
}

const Game::HookAutoRegister _buttonHook{
    Offsets::FUN_XML_BUTTON_LOAD,
    reinterpret_cast<void *>(&ButtonLoad_h),
    reinterpret_cast<void **>(&g_origButtonLoad)};

const Game::HookAutoRegister _textureHook{
    Offsets::FUN_XML_TEXTURE_LOAD,
    reinterpret_cast<void *>(&TextureLoad_h),
    reinterpret_cast<void **>(&g_origTextureLoad)};

const Game::HookAutoRegister _fontStringHook{
    Offsets::FUN_XML_FONTSTRING_LOAD,
    reinterpret_cast<void *>(&FontStringLoad_h),
    reinterpret_cast<void **>(&g_origFontStringLoad)};

} // namespace

} // namespace Xml::ParentKey
