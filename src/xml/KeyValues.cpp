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

// `<KeyValues>` on XML elements, the later-client way to set fields on an
// element's Lua table at load time:
//
//     <Frame name="Foo">
//         <KeyValues>
//             <KeyValue key="layoutIndex" value="3" type="number"/>
//             <KeyValue key="tooltip" value="GameTooltip" type="global"/>
//         </KeyValues>
//     </Frame>
//
// Each entry is `object[key] = value`. `type` and `keyType` (default
// "string") are one of nil / boolean / number / string / global, the later
// clients' KEYVALUETYPE; "global" is `_G[text]`. Booleans and numbers go
// through the same loader helpers as `<Attributes>` (FUN_XML_ATTR_TO_BOOL,
// FUN_SSTR_TO_FLOAT).
//
// The schema allows it on every layout element and on animation groups and
// animations. Frames are read here, through the shared Xml::FrameLoad
// observer; textures and font strings from their loader co-hooks in
// Xml::ParentKey; groups and animations from Animation::Xml. Every one of
// those runs once per inherited template node before the element's own node,
// so a template's values are set first and the element's own win, and all of
// them run before the builder's finalize, so OnLoad sees the fields.

#include "xml/KeyValues.h"

#include "Game.h"
#include "Offsets.h"
#include "baselib/Ascii.h"
#include "xml/FrameLoad.h"

namespace Xml::KeyValues {

namespace {

enum class Kind { Nil, Boolean, Number, String, Global, Invalid };

// A missing type is the schema's default, "string".
Kind KindOf(const char *type) {
    if (type == nullptr || Ascii::EqualCI(type, "string"))
        return Kind::String;
    if (Ascii::EqualCI(type, "nil"))
        return Kind::Nil;
    if (Ascii::EqualCI(type, "boolean"))
        return Kind::Boolean;
    if (Ascii::EqualCI(type, "number"))
        return Kind::Number;
    if (Ascii::EqualCI(type, "global"))
        return Kind::Global;
    return Kind::Invalid;
}

void Push(void *L, Kind kind, const char *text) {
    using ToFloat_t = float(__stdcall *)(const char *);
    switch (kind) {
    case Kind::Boolean:
        Game::Lua::PushBool(L, Game::StringToFlag(text));
        break;
    case Kind::Number:
        Game::Lua::PushNumber(L, reinterpret_cast<ToFloat_t>(Offsets::FUN_SSTR_TO_FLOAT)(text));
        break;
    case Kind::String:
        Game::Lua::PushString(L, text);
        break;
    case Kind::Global:
        Game::Lua::PushString(L, text);
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
        break;
    default:
        Game::Lua::PushNil(L);
        break;
    }
}

const void *FindKeyValues(const void *node) {
    for (const void *c = FirstChild(node); c != nullptr; c = NextSibling(c))
        if (TagIs(c, "KeyValues"))
            return c;
    return nullptr;
}

} // namespace

void Apply(void *L, int index, const void *node, void *status, const char *owner) {
    const void *keyValues = FindKeyValues(node);
    if (keyValues == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    for (const void *e = FirstChild(keyValues); e != nullptr; e = NextSibling(e)) {
        if (!TagIs(e, "KeyValue")) {
            Warn(status, "%s: Unknown KeyValues element %s", owner, Tag(e));
            continue;
        }
        const char *key = Attr(e, "key");
        if (key == nullptr) {
            Warn(status, "%s: KeyValue element missing key", owner);
            continue;
        }
        const char *keyTypeName = Attr(e, "keyType");
        const char *typeName = Attr(e, "type");
        const Kind keyKind = KindOf(keyTypeName);
        const Kind kind = KindOf(typeName);
        if (keyKind == Kind::Invalid || kind == Kind::Invalid) {
            Warn(status, "%s: KeyValue %s has unknown type %s", owner, key,
                 keyKind == Kind::Invalid ? keyTypeName : typeName);
            continue;
        }
        const char *value = Attr(e, "value");
        if (value == nullptr && kind != Kind::Nil) {
            Warn(status, "%s: KeyValue %s missing value", owner, key);
            continue;
        }

        Push(L, keyKind, key); // [key]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL) {
            // A nil key would raise from SetTable; nothing can be stored.
            Warn(status, "%s: KeyValue %s has a nil key", owner, key);
            Game::Lua::SetTop(L, top);
            continue;
        }
        Push(L, kind, value);            // [key, value]
        Game::Lua::SetTable(L, index);   // object[key] = value
        Game::Lua::SetTop(L, top);
    }
}

void ApplyToRegion(void *object, const void *node, void *status) {
    if (FindKeyValues(node) == nullptr)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    if (PushWrapper(L, object))
        Apply(L, top + 1, node, status, Game::Lua::ObjectDisplayName(object));
    Game::Lua::SetTop(L, top);
}

void PushInfo(void *L, const void *node) {
    Game::Lua::NewTable(L); // [list]
    const void *keyValues = FindKeyValues(node);
    if (keyValues == nullptr)
        return;
    int n = 0;
    for (const void *e = FirstChild(keyValues); e != nullptr; e = NextSibling(e)) {
        const char *key = Attr(e, "key");
        if (!TagIs(e, "KeyValue") || key == nullptr)
            continue;
        const char *keyType = Attr(e, "keyType");
        const char *type = Attr(e, "type");
        const char *value = Attr(e, "value");
        Game::Lua::PushNumber(L, static_cast<double>(++n)); // [list, n]
        Game::Lua::NewTable(L);                             // [list, n, entry]
        Game::Lua::SetFieldString(L, "key", key);
        Game::Lua::SetFieldString(L, "keyType", keyType != nullptr ? keyType : "string");
        Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "string");
        Game::Lua::SetFieldString(L, "value", value != nullptr ? value : "");
        Game::Lua::SetTable(L, -3); // list[n] = entry
    }
}

namespace {

const FrameLoad::AutoSubscribe _frameNode{&ApplyToRegion};

} // namespace

} // namespace Xml::KeyValues
