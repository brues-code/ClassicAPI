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

// `inherit="prepend"` / `inherit="append"` on a <Scripts> handler element,
// the later-client way to add to a template's handler instead of replacing
// it:
//
//   <Button inherits="TruncatedTooltipScriptTemplate">
//     <Scripts>
//       <OnEnter inherit="prepend">self.Highlight:Show();</OnEnter>
//
// `prepend` runs the inherited handler first, then this one; `append` runs
// this one first. Retail's AchievementTemplate shows the order: its
// `<OnLoad inherit="prepend">` calls SetBackdropBorderColor, which does
// nothing until the inherited BackdropTemplate OnLoad has built the border.
// Elements apply in document order, each onto the result of the ones before
// it, so two `append` OnShows in one node both run.
//
// Vanilla's loader treats the element like any other: the node's handler
// replaces the template's. Template nodes load through the same frame loader,
// on the same object, before the node's own content, so the handler a
// template set is the frame's current one when that template's load returns:
//
//   before the node   a node with a chained element opens an entry
//   after a template  Capture keeps the frame's handler for each chained
//                     script (the last template's wins, as in the engine)
//   after the node    Apply builds each chained script's handler and sets it
//                     with frame:SetScript
//
// A handler comes from `function=` (_G[name]) or `method=` (frame[name]), as
// in Xml::ParentKey; a body element uses the handler the engine compiled for
// that script, which is the node's last body (none when the engine kept the
// template's because the node has no body). Two handlers join in a C
// closure that calls both with the arguments it receives, so the engine's
// script runner sees one ordinary function.

#include "xml/ScriptInherit.h"

#include "Game.h"
#include "xml/FrameLoad.h"

#include <utility>
#include <vector>

namespace Xml::ScriptInherit {

namespace {

constexpr int kNoRef = -2; // LUA_NOREF

enum class Mode { None, Prepend, Append };

Mode ModeOf(const void *element) {
    const char *v = Xml::Attr(element, "inherit");
    if (v == nullptr)
        return Mode::None;
    if (Game::StrCmpI(v, "prepend") == 0)
        return Mode::Prepend;
    if (Game::StrCmpI(v, "append") == 0)
        return Mode::Append;
    return Mode::None;
}

const void *ScriptsOf(const void *node) {
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c)) {
        if (Xml::TagIs(c, "Scripts"))
            return c;
    }
    return nullptr;
}

struct Captured {
    const char *script;
    int ref; // registry ref to the inherited handler, or kNoRef
};

// A node whose chained scripts wait for its templates' handlers.
struct Entry {
    void *object;
    const void *node;
    std::vector<Captured> scripts;
};

std::vector<Entry> g_open;

// Pushes frame:GetScript(script), or nil.
void PushHandler(void *L, int frame, const char *script) {
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "GetScript");
    Game::Lua::GetTable(L, frame);
    Game::Lua::PushValue(L, frame);
    Game::Lua::PushString(L, script);
    if (Game::Lua::PCall(L, 2, 1, 0) != 0 || Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, top);
        Game::Lua::PushNil(L);
    }
}

// frame:SetScript(script, <top>), popping the handler.
void SetHandler(void *L, int frame, const char *script) {
    const int handler = Game::Lua::GetTop(L);
    Game::Lua::PushString(L, "SetScript");
    Game::Lua::GetTable(L, frame);
    Game::Lua::PushValue(L, frame);
    Game::Lua::PushString(L, script);
    Game::Lua::PushValue(L, handler);
    Game::Lua::PCall(L, 3, 0, 0);
    Game::Lua::SetTop(L, handler - 1);
}

// The joined handler: upvalue 1, then upvalue 2, each with every argument.
int __fastcall Chain(void *L) {
    const int n = Game::Lua::GetTop(L);
    Game::Lua::CheckStack(L, n + 1);
    for (int i = 1; i <= 2; ++i) {
        Game::Lua::PushValue(L, Game::Lua::UpvalueIndex(i));
        for (int a = 1; a <= n; ++a)
            Game::Lua::PushValue(L, a);
        Game::Lua::Call(L, n, 0);
    }
    return 0;
}

// Pushes the handler `element` names, or nil. `compiled` is the stack index
// of the engine's handler for the element's script.
void PushElementHandler(void *L, int frame, int compiled, const void *element) {
    const char *fn = Xml::Attr(element, "function");
    const char *method = Xml::Attr(element, "method");
    if (fn != nullptr && *fn != '\0') {
        Game::Lua::PushString(L, fn);
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX);
    } else if (method != nullptr && *method != '\0') {
        Game::Lua::PushString(L, method);
        Game::Lua::GetTable(L, frame);
    } else {
        Game::Lua::PushValue(L, compiled);
    }
}

// Sets each chained script of `e` from its elements, in document order,
// starting from the captured inherited handler.
void Join(void *L, int frame, const Entry &e) {
    const void *scripts = ScriptsOf(e.node);
    for (const Captured &c : e.scripts) {
        PushHandler(L, frame, c.script);
        const int compiled = Game::Lua::GetTop(L);
        if (c.ref >= 0)
            Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, c.ref);
        else
            Game::Lua::PushNil(L);
        const int chain = Game::Lua::GetTop(L);
        // The engine keeps the template's handler when the node has no body
        // for this script; that is not a handler of the node's own.
        if (Game::Lua::ToPointer(L, compiled) == Game::Lua::ToPointer(L, chain)) {
            Game::Lua::PushNil(L);
            Game::Lua::Insert(L, compiled);
            Game::Lua::Remove(L, compiled + 1);
        }
        for (const void *el = Xml::FirstChild(scripts); el != nullptr; el = Xml::NextSibling(el)) {
            if (!Xml::TagIs(el, c.script))
                continue;
            PushElementHandler(L, frame, compiled, el); // [.., chain, h]
            if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
                Game::Lua::SetTop(L, chain);
                continue;
            }
            const Mode mode = ModeOf(el);
            if (mode != Mode::None && Game::Lua::Type(L, chain) == Game::Lua::TYPE_FUNCTION) {
                Game::Lua::PushValue(L, chain); // [.., chain, h, chain]
                if (mode == Mode::Prepend)
                    Game::Lua::Insert(L, -2); // [.., chain, chain, h]
                Game::Lua::PushCClosure(L, &Chain, 2);
            }
            Game::Lua::Insert(L, chain); // [.., new, chain]
            Game::Lua::SetTop(L, chain);
        }
        if (Game::Lua::Type(L, chain) == Game::Lua::TYPE_FUNCTION)
            SetHandler(L, frame, c.script);
        Game::Lua::SetTop(L, compiled - 1);
    }
}

void Release(void *L, const Entry &e) {
    for (const Captured &c : e.scripts)
        Game::Lua::Unref(L, Game::Lua::REGISTRY_INDEX, c.ref);
}

void OnBefore(void *object, const void *node, void *) {
    const void *scripts = ScriptsOf(node);
    if (scripts == nullptr)
        return;
    Entry e{object, node, {}};
    for (const void *el = Xml::FirstChild(scripts); el != nullptr; el = Xml::NextSibling(el)) {
        const char *script = Xml::Tag(el);
        if (script == nullptr || ModeOf(el) == Mode::None)
            continue;
        bool seen = false;
        for (const Captured &c : e.scripts)
            seen = seen || Game::StrCmpI(c.script, script) == 0;
        if (!seen)
            e.scripts.push_back({script, kNoRef});
    }
    if (!e.scripts.empty())
        g_open.push_back(std::move(e));
}

const Xml::FrameLoad::AutoSubscribeBefore _before{&OnBefore};

} // namespace

bool Chained(const void *scripts, const char *script) {
    for (const void *el = Xml::FirstChild(scripts); el != nullptr; el = Xml::NextSibling(el)) {
        if (Xml::TagIs(el, script) && ModeOf(el) != Mode::None)
            return true;
    }
    return false;
}

void Apply(void *object, const void *node) {
    if (g_open.empty() || g_open.back().object != object || g_open.back().node != node)
        return;
    const Entry e = std::move(g_open.back());
    g_open.pop_back();
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    if (Xml::PushWrapper(L, object))
        Join(L, top + 1, e);
    Release(L, e);
    Game::Lua::SetTop(L, top);
}

void Capture(void *object) {
    if (g_open.empty() || g_open.back().object != object)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    if (Xml::PushWrapper(L, object)) {
        for (Captured &c : g_open.back().scripts) {
            Game::Lua::Unref(L, Game::Lua::REGISTRY_INDEX, c.ref);
            PushHandler(L, top + 1, c.script);
            c.ref = Game::Lua::Type(L, -1) == Game::Lua::TYPE_FUNCTION
                        ? Game::Lua::Ref(L, Game::Lua::REGISTRY_INDEX)
                        : kNoRef;
            Game::Lua::SetTop(L, top + 1);
        }
    }
    Game::Lua::SetTop(L, top);
}

} // namespace Xml::ScriptInherit
