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

// `texture:SetHorizTile` / `GetHorizTile` / `SetVertTile` / `GetVertTile` and
// the XML `horizTile` / `vertTile` attributes: the image repeated at its own
// size across the texture, as 4.3.4 tiles it.
//
// 4.3.4 keeps a tile flag per axis. After a size change its layer batch build
// (CRenderBatch::QueueTexture) sets every non-zero coordinate on a tiled axis
// to the texture's length over the image's: the length in script units,
// DDCToNDCWidth(aspect * 1024 * length) / layoutScale, over the device
// texture's width or height in texels. One texel is one unit of GetWidth.
//
// 1.12's layer batch keeps a pointer to the eight coordinates and reads them
// on every draw (FUN_00772FD0), so the count is written straight into them
// whenever an input changes: after each corner store (every layout, through
// Texture::CornerStore), after each coordinate write (both SetTexCoord
// forms), on a tile change, and after the batch build's per-texture emit
// (FUN_SIMPLETEXTURE_EMIT), where the image is first loaded and its size
// known. The script units are the engine's own push of a script coordinate,
// DivideByScreenScaleX(aspect * 1024 * length), over the effective scale.
// Counting after a coordinate write goes past 4.3.4, which waits for the next
// size change: NineSlice calls SetTexCoord after SetHorizTile.
//
// A tiled axis also repeats (Texture::Wrap). Retail fixes the tiling SetAtlas
// clears with SetVertTile(true) alone (Blizzard_LegacyChallengeButton.lua), so
// the flag carries the wrap, and 4.3.4's CSimpleStatusBar::SetBarTexture
// passes the tile flags as the wrap.
//
// Not counted: a texture showing an atlas (an atlas here is part of its file,
// which a repeat cannot repeat alone) and one with SetTexCoordModifiesRect on
// (its coordinates size its rect). A StatusBar's tiled bar texture takes the
// 4.3.4 layout, which moves the texture instead of cropping its coordinates
// (StatusBar::Fill).
//
// XML: the attributes apply when present and non-empty, as in 4.3.4's loader.
// A <TexCoords> `left` / `right` other than 0 / 1 on a horizontally tiled
// texture, or `top` / `bottom` on a vertically tiled one, is 4.3.4's error
// "Invalid TexCoords value (horizTile is on)". 4.3.4 then ignores the element;
// here the engine has applied it already, so the coordinates go back to the
// whole image.

#include "Game.h"
#include "Offsets.h"
#include "texture/Atlas.h"
#include "texture/CornerStore.h"
#include "texture/Wrap.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>

namespace Texture::Tile {

namespace {

using GetRect_t = int(__fastcall *)(void *anchor, void *edx, float *outRect);
using ScreenAspect_t = float(__cdecl *)();
using DivideByScreenScaleX_t = float(__stdcall *)(float value);
using SetTexCoord_t = void(__fastcall *)(void *texture, void *edx, const float *values);
using Emit_t = void(__fastcall *)(void *texture, void *edx, void *batch);

// UL, LL, UR, LR: the whole image.
constexpr float kWholeImage[8] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

// A length in rect units as script units.
float ScriptUnits(float length) {
    const auto aspect = reinterpret_cast<ScreenAspect_t>(Offsets::FUN_COORD_GET_SCALED_SCREEN_ASPECT);
    const auto divide = reinterpret_cast<DivideByScreenScaleX_t>(Offsets::FUN_COORD_DIVIDE_BY_SCREEN_SCALE_X);
    return divide(aspect() * Game::Read<float>(Offsets::FLOAT_SCRIPT_COORD_SCALE) * length);
}

// 4.3.4's rule on one axis (0 x, 1 y) of the four corner pairs.
void Count(float *coords, int axis, float length, uint32_t texels) {
    if (texels == 0 || !(length > 0.0f))
        return;
    const float count = length / static_cast<float>(texels);
    for (int i = 0; i < 4; ++i)
        if (coords[i * 2 + axis] != 0.0f)
            coords[i * 2 + axis] = count;
}

void Apply(void *texture) {
    bool h, v;
    if (!Texture::Wrap::Tiles(texture, h, v))
        return;
    if (Game::Read<int>(texture, Offsets::OFF_REGION_TEXCOORD_MODIFIES_RECT) != 0 || Texture::Atlas::Shows(texture))
        return;
    void *image = Game::Read<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
    const void *device = image != nullptr ? Game::Read<void *>(image, Offsets::OFF_HTEXTURE_DEVICE) : nullptr;
    if (device == nullptr)
        return; // the emit of the next batch build counts it
    float rect[4]; // bottom, left, top, right
    if (reinterpret_cast<GetRect_t>(Offsets::FUN_REGION_GET_RECT)(
            Game::Ptr<uint8_t>(texture, Offsets::OFF_REGION_ANCHOR), nullptr, rect) == 0)
        return;
    const float scale = Game::Read<float>(texture, Offsets::OFF_REGION_EFFECTIVE_SCALE);
    if (!(scale > 0.0f))
        return;
    float *coords = Game::Ptr<float>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS);
    if (h)
        Count(coords, 0, ScriptUnits(rect[3] - rect[1]) / scale,
              Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_WIDTH));
    if (v)
        Count(coords, 1, ScriptUnits(rect[2] - rect[0]) / scale,
              Game::Read<uint32_t>(device, Offsets::OFF_GXTEX_HEIGHT));
}

// --- what moves the count -----------------------------------------------------

void OnCornersStored(void *region) { Apply(region); }

const Texture::CornerStore::AutoSubscribe _cornerSub{&OnCornersStored};

SetTexCoord_t g_origSetRect = nullptr;
SetTexCoord_t g_origSetCorners = nullptr;
Emit_t g_origEmit = nullptr;

void __fastcall SetTexCoordRect_h(void *texture, void *edx, const float *rect) {
    g_origSetRect(texture, edx, rect);
    Apply(texture);
}

void __fastcall SetTexCoordCorners_h(void *texture, void *edx, const float *corners) {
    g_origSetCorners(texture, edx, corners);
    Apply(texture);
}

void __fastcall Emit_h(void *texture, void *edx, void *batch) {
    g_origEmit(texture, edx, batch);
    Apply(texture);
}

const Game::HookAutoRegister _setRectHook{Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD,
                                          reinterpret_cast<void *>(&SetTexCoordRect_h),
                                          reinterpret_cast<void **>(&g_origSetRect)};
const Game::HookAutoRegister _setCornersHook{Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD_CORNERS,
                                             reinterpret_cast<void *>(&SetTexCoordCorners_h),
                                             reinterpret_cast<void **>(&g_origSetCorners)};
const Game::HookAutoRegister _emitHook{Offsets::FUN_SIMPLETEXTURE_EMIT, reinterpret_cast<void *>(&Emit_h),
                                       reinterpret_cast<void **>(&g_origEmit)};

// --- Lua -----------------------------------------------------------------------

// Counted against the held image before Settle swaps in the repeating copy of
// the same file, which has no size until it is drawn.
void SetAxis(void *texture, bool horizontal, bool tiling) {
    bool h, v;
    Texture::Wrap::Tiles(texture, h, v);
    (horizontal ? h : v) = tiling;
    Texture::Wrap::SetTiles(texture, h, v);
    Apply(texture);
    Texture::Wrap::Settle(texture);
}

int __fastcall Script_SetHorizTile(void *L) {
    if (void *texture = Game::Lua::ResolveTexture(L))
        SetAxis(texture, true, Game::Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_SetVertTile(void *L) {
    if (void *texture = Game::Lua::ResolveTexture(L))
        SetAxis(texture, false, Game::Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_GetHorizTile(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    bool h, v;
    Texture::Wrap::Tiles(texture, h, v);
    Game::Lua::PushBool(L, h);
    return 1;
}

int __fastcall Script_GetVertTile(void *L) {
    void *texture = Game::Lua::ResolveTexture(L);
    if (texture == nullptr)
        return 0;
    bool h, v;
    Texture::Wrap::Tiles(texture, h, v);
    Game::Lua::PushBool(L, v);
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetHorizTile", &Script_SetHorizTile},
    {"GetHorizTile", &Script_GetHorizTile},
    {"SetVertTile", &Script_SetVertTile},
    {"GetVertTile", &Script_GetVertTile},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_TEXTURE_METHOD_REGISTRY), g_methods);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// --- XML -----------------------------------------------------------------------

// The tile attribute a node's <TexCoords> breaks, as 4.3.4's loader tests it,
// or null.
const char *Conflict(const void *node, bool h, bool v) {
    for (const void *child = Xml::FirstChild(node); child != nullptr; child = Xml::NextSibling(child)) {
        if (!Xml::TagIs(child, "TexCoords"))
            continue;
        float value = 0.0f;
        if (h && ((Xml::FloatAttr(child, "left", value) && value != 0.0f) ||
                  (Xml::FloatAttr(child, "right", value) && value != 1.0f)))
            return "horizTile";
        if (v && ((Xml::FloatAttr(child, "top", value) && value != 0.0f) ||
                  (Xml::FloatAttr(child, "bottom", value) && value != 1.0f)))
            return "vertTile";
    }
    return nullptr;
}

void OnTextureNode(void *object, const void *node, void *status) {
    const char *h = Xml::Attr(node, "horizTile");
    const char *v = Xml::Attr(node, "vertTile");
    const bool hasH = h != nullptr && *h != '\0';
    const bool hasV = v != nullptr && *v != '\0';
    bool tileH, tileV;
    Texture::Wrap::Tiles(object, tileH, tileV);
    if (hasH)
        tileH = Game::StringToFlag(h);
    if (hasV)
        tileV = Game::StringToFlag(v);
    if (hasH || hasV)
        Texture::Wrap::SetTiles(object, tileH, tileV);
    if (tileH || tileV) {
        if (const char *flag = Conflict(node, tileH, tileV)) {
            const char *name = Game::Read<const char *>(object, Offsets::OFF_SCRIPT_REGION_NAME);
            Xml::Error(status, "Texture %s: Invalid TexCoords value (%s is on)",
                       name != nullptr ? name : "<unnamed>", flag);
            reinterpret_cast<SetTexCoord_t>(Offsets::FUN_SIMPLETEXTURE_SET_TEXCOORD_CORNERS)(object, nullptr,
                                                                                             kWholeImage);
        }
        Apply(object);
    }
    if (hasH || hasV)
        Texture::Wrap::Settle(object);
}

const Xml::TypeLoad::AutoSubscribe _xmlSub{Xml::TypeLoad::TEXTURE, &OnTextureNode};

} // namespace

} // namespace Texture::Tile
