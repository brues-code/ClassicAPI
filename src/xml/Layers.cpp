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

// `<MaskTexture>` in a frame's `<Layer>`, the later-client schema:
//
//   <Layer level="ARTWORK">
//     <Texture parentKey="Icon" file="..."/>
//     <MaskTexture file="Interface\CharacterFrame\TempPortraitAlphaMask">
//       <Anchors>...</Anchors>
//       <MaskedTextures>
//         <MaskedTexture childKey="Icon"/>      <!-- parent.Icon -->
//         <MaskedTexture target="$parentIcon"/> <!-- a global name -->
//       </MaskedTextures>
//     </MaskTexture>
//   </Layer>
//
// Vanilla's layer loader FUN_XML_LOAD_LAYERS builds only <Texture> and
// <FontString> and warns on any other child, so this replaces it with the
// same loop plus a <MaskTexture> branch. Every step is the engine's own: the
// `level` parse, the element builders, the parent-and-layer placement, and
// the warning (its format string and arguments). A MaskTexture is built
// exactly like a Texture, so `file`, `inherits`, `parentKey`, anchors and
// size all load through the texture loader. It is then turned into a mask
// source (Texture::Mask::MakeSource), the state frame:CreateMaskTexture
// leaves.
//
// <MaskedTextures> resolve after the whole <Layers> node has loaded, so a
// mask can name a texture from a later layer. `childKey` is a key on the
// mask's parent frame; `target` is a global name, with a leading `$parent`
// standing for the parent frame's name.

#include "Game.h"
#include "Offsets.h"
#include "texture/Mask.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Xml::Layers {

namespace {

using LoadLayers_t = void(__fastcall *)(void *frame, void *edx, const void *layers, void *status);
using AddElement_t = void *(__fastcall *)(const void *node, void *frame, void *status);
using SetParentAndLayer_t = void(__thiscall *)(void *region, void *frame, int layer, int show);
using LayerFromName_t = bool(__fastcall *)(const char *name, int *outLayer);
using GetName_t = const char *(__thiscall *)(void *object);

LoadLayers_t g_orig = nullptr;

const char *NameOf(void *object) {
    return Game::Read<GetName_t>(Game::Read<void *>(object, 0),
                                 Offsets::VTBL_SCRIPTOBJECT_GET_NAME)(object);
}

// The engine's "Frame %s: Unknown child node in %s element: %s".
void WarnUnknown(void *status, void *frame, const void *parent, const void *child) {
    const char *name = NameOf(frame);
    Xml::Warn(status, reinterpret_cast<const char *>(Offsets::STR_XML_FRAME_UNKNOWN_CHILD),
              name != nullptr ? name : reinterpret_cast<const char *>(Offsets::VAR_STR_UNNAMED),
              Xml::Tag(parent), Xml::Tag(child));
}

// The texture a <MaskedTexture> names, or nullptr.
void *MaskedTarget(void *L, void *frame, const void *entry) {
    const int top = Game::Lua::GetTop(L);
    void *base = nullptr;
    if (const char *key = Xml::Attr(entry, "childKey"); key != nullptr && *key != '\0') {
        if (Xml::PushWrapper(L, frame)) { // [frame]
            Game::Lua::PushString(L, key);
            Game::Lua::GetTable(L, top + 1); // [frame, frame[key]]
            base = Game::Lua::ResolveTexture(L, top + 2, /*raiseError=*/false);
        }
    } else if (const char *target = Xml::Attr(entry, "target");
               target != nullptr && *target != '\0') {
        constexpr char kParent[] = "$parent";
        constexpr size_t kParentLen = sizeof(kParent) - 1;
        std::string name = target;
        if (name.compare(0, kParentLen, kParent) == 0) {
            const char *parentName = NameOf(frame);
            name = std::string(parentName != nullptr ? parentName : "") + name.substr(kParentLen);
        }
        Game::Lua::PushString(L, name.c_str());
        Game::Lua::GetTable(L, Game::Lua::GLOBALS_INDEX); // [_G[name]]
        base = Game::Lua::ResolveTexture(L, top + 1, /*raiseError=*/false);
    }
    Game::Lua::SetTop(L, top);
    return base;
}

void BindMaskedTextures(void *frame, void *mask, const void *maskNode, void *status) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    for (const void *c = Xml::FirstChild(maskNode); c != nullptr; c = Xml::NextSibling(c)) {
        if (!Xml::TagIs(c, "MaskedTextures"))
            continue;
        for (const void *e = Xml::FirstChild(c); e != nullptr; e = Xml::NextSibling(e)) {
            if (!Xml::TagIs(e, "MaskedTexture")) {
                WarnUnknown(status, frame, c, e);
                continue;
            }
            void *base = MaskedTarget(L, frame, e);
            if (base != nullptr) {
                Texture::Mask::Add(base, mask);
                continue;
            }
            const char *key = Xml::Attr(e, "childKey");
            Xml::Warn(status, "MaskTexture: Couldn't find masked texture: %s",
                      key != nullptr ? key : Xml::Attr(e, "target"));
        }
    }
}

// FUN_XML_LOAD_LAYERS, `__thiscall` modelled as `__fastcall` with a dummy edx.
void __fastcall LoadLayers_h(void *frame, void *, const void *layers, void *status) {
    const auto addTexture = reinterpret_cast<AddElement_t>(Offsets::FUN_XML_ADD_TEXTURE_ELEMENT);
    const auto addFontString =
        reinterpret_cast<AddElement_t>(Offsets::FUN_XML_ADD_FONTSTRING_ELEMENT);
    const auto place =
        reinterpret_cast<SetParentAndLayer_t>(Offsets::FUN_REGION_SET_PARENT_AND_LAYER);

    std::vector<std::pair<void *, const void *>> masks;
    for (const void *layer = Xml::FirstChild(layers); layer != nullptr;
         layer = Xml::NextSibling(layer)) {
        if (!Xml::TagIs(layer, "Layer")) {
            WarnUnknown(status, frame, layers, layer);
            continue;
        }
        int level = Offsets::DRAWLAYER_ARTWORK;
        if (const char *name = Xml::Attr(layer, "level"); name != nullptr && *name != '\0')
            reinterpret_cast<LayerFromName_t>(Offsets::FUN_XML_DRAW_LAYER_FROM_NAME)(name, &level);
        for (const void *child = Xml::FirstChild(layer); child != nullptr;
             child = Xml::NextSibling(child)) {
            const bool isMask = Xml::TagIs(child, "MaskTexture");
            void *region = nullptr;
            if (isMask || Xml::TagIs(child, "Texture")) {
                region = addTexture(child, frame, status);
            } else if (Xml::TagIs(child, "FontString")) {
                region = addFontString(child, frame, status);
            } else {
                WarnUnknown(status, frame, layer, child);
                continue;
            }
            place(region, frame, level, Game::Read<int>(region, Offsets::OFF_REGION_DESIRED_SHOWN));
            if (isMask) {
                Texture::Mask::MakeSource(region);
                masks.emplace_back(region, child);
            }
        }
    }
    for (const auto &[mask, node] : masks)
        BindMaskedTextures(frame, mask, node, status);
}

const Game::HookAutoRegister _hook{Offsets::FUN_XML_LOAD_LAYERS,
                                   reinterpret_cast<void *>(&LoadLayers_h),
                                   reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace Xml::Layers
