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

// SetIgnoreParentAlpha(ignore) / IsIgnoringParentAlpha() on frames and on
// textures and font strings, as the 3.80.2 client has them on both its Frame
// and its Region tables (Frame 0x140A8D120 / 0x140A892F0, Region 0x140A998A0 /
// 0x140A98FA0; docs/notes/gamepad-input-region-api-3802.md §6), and the XML
// attribute `ignoreParentAlpha`.
//   - A frame draws at its own alpha instead of its own times its parent's
//     (Frame::Alpha); its children multiply by that.
//   - A region draws at its own vertex alpha instead of that times its
//     frame's (3.80.2's SimpleRegion refresh FUN_1405AD470: parent alpha
//     0xFF when ignoring). 1.12 meets the frame's alpha in one place, the
//     region colour fold CScriptRegion::UpdateFrameAlpha, which every
//     region's recolor runs. Its co-hook gives an ignoring region's fold an
//     owner byte of 0xFF for the call, so the fold's own opaque branch also
//     drops the colour arrays of a white region, as for an opaque frame.
//     The font string's recolor reads the byte again for its shadow, so it
//     runs inside the same swap (OwnerOpaque, from Text::InlineTexturePool's
//     hook of it). A change recolors the region, as the frame alpha setter
//     recolors its regions.
// Both setters need their argument (any value but nil, read as
// lua_toboolean reads it).

#include "frame/IgnoreParentAlpha.h"

#include "Game.h"
#include "Offsets.h"
#include "frame/Alpha.h"
#include "frame/RegionDestruction.h"
#include "frame/RegionScripts.h"
#include "lua/Arg.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>
#include <unordered_set>

namespace Frame::IgnoreParentAlpha {

namespace {

using Frame::RegionScripts::IsRegion;
using Recolor_t = void(__thiscall *)(void *region, int arg);
using Fold_t = void(__fastcall *)(void *region, void *edx, uint32_t changed);

std::unordered_set<const void *> g_regions; // regions that ignore their frame's alpha

} // namespace

OwnerOpaque::OwnerOpaque(void *region) {
    if (g_regions.count(region) == 0)
        return;
    uint8_t *owner = Game::Read<uint8_t *>(region, Offsets::OFF_REGION_PARENT);
    if (owner == nullptr)
        return;
    m_byte = owner + Offsets::OFF_FRAME_ALPHA;
    m_saved = *m_byte;
    *m_byte = 0xFF;
}

OwnerOpaque::~OwnerOpaque() {
    if (m_byte != nullptr)
        *m_byte = m_saved;
}

namespace {

Fold_t g_origFold = nullptr;

void __fastcall Fold_h(void *region, void *edx, uint32_t changed) {
    const OwnerOpaque opaque(region);
    g_origFold(region, edx, changed);
}

const Game::HookAutoRegister _foldHook{Offsets::FUN_REGION_UPDATE_FRAME_ALPHA, reinterpret_cast<void *>(&Fold_h),
                                       reinterpret_cast<void **>(&g_origFold)};

void OnRegionDestroyed(void *region) { g_regions.erase(region); }

const Frame::RegionDestruction::AutoSubscribe _regionDestroyed{&OnRegionDestroyed};

void SetRegion(void *region, bool ignore) {
    if (ignore == (g_regions.count(region) != 0))
        return;
    if (ignore)
        g_regions.insert(region);
    else
        g_regions.erase(region);
    const void *vtable = Game::Read<const void *>(region, 0);
    Game::Read<Recolor_t>(vtable, Offsets::VTBL_REGION_RECOLOR)(region, 0);
}

void Set(void *object, bool ignore) {
    if (IsRegion(object))
        SetRegion(object, ignore);
    else
        Frame::Alpha::SetIgnoreParentAlpha(object, ignore);
}

bool Ignores(const void *object) {
    return IsRegion(object) ? g_regions.count(object) != 0 : Frame::Alpha::IgnoresParentAlpha(object);
}

int __fastcall Script_SetIgnoreParentAlpha(void *L) {
    void *object = Game::Lua::ResolveObject(L, 1);
    if (object == nullptr)
        return 0;
    bool ignore = false;
    if (!Game::Lua::Arg::Bool(L, 2, "Usage: self:SetIgnoreParentAlpha(ignore)", ignore))
        return 0;
    Set(object, ignore);
    return 0;
}

int __fastcall Script_IsIgnoringParentAlpha(void *L) {
    void *object = Game::Lua::ResolveObject(L, 1);
    if (object == nullptr)
        return 0;
    Game::Lua::PushBool(L, Ignores(object));
    return 1;
}

// `ignoreParentAlpha="true"`; an inherited template's value is read first,
// so the node's own wins.
void OnNode(void *object, const void *node, void * /*status*/) {
    if (const char *value = Xml::Attr(node, "ignoreParentAlpha"))
        Set(object, Game::StringToFlag(value));
}

const Xml::FrameLoad::AutoSubscribe _frameNode{&OnNode};
const Xml::TypeLoad::AutoSubscribe _textureNode{Xml::TypeLoad::TEXTURE, &OnNode};
const Xml::TypeLoad::AutoSubscribe _fontStringNode{Xml::TypeLoad::FONT_STRING, &OnNode};

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetIgnoreParentAlpha", &Script_SetIgnoreParentAlpha},
    {"IsIgnoringParentAlpha", &Script_IsIgnoringParentAlpha},
};

const Game::Doc::Field kIgnoreArg[] = {Game::Doc::Req("ignore", "bool")};
const Game::Doc::Field kIgnoreRet[] = {Game::Doc::Req("isIgnoring", "bool")};
const Game::Doc::Function kSetIgnoreParentAlpha{
    "Sets whether the object draws at its own alpha only, not multiplied by its parent frame's.", kIgnoreArg, {}};
const Game::Doc::Function kIsIgnoringParentAlpha{"Returns whether the object ignores its parent frame's alpha.", {},
                                                 kIgnoreRet};

const Game::Doc::Method g_methodDocs[] = {
    {"SetIgnoreParentAlpha", &kSetIgnoreParentAlpha},
    {"IsIgnoringParentAlpha", &kIsIgnoringParentAlpha},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(reinterpret_cast<void *>(Offsets::VAR_REGION_METHOD_REGISTRY), g_methods,
                                    g_methodDocs);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Frame::IgnoreParentAlpha
