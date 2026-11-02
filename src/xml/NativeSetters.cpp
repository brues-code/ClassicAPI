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

// Later-client XML attributes whose setter is 1.12's own. Attributes backed by
// a ClassicAPI module are read in that module; these have nothing behind them
// but an engine setter, so they are read here.
//
// `alpha` on a Texture or FontString (4.3.4's Region attribute; 1.12 reads it
// on frames only). 1.12's frame loader reads its own `alpha` when present and
// non-empty, through SStrToFloat, clamped to 0..1 and truncated to a byte
// (0x00769A10); the same conversion goes here. The value is then set as
// Texture:SetAlpha and FontString:SetAlpha set it (0x0079B580, 0x0079CB70):
// the region's color, with its alpha byte replaced. 4.3.4 sets it the same way,
// after the node's <Color>.
//
// `scale` on a frame other than a Model (retail's Frame attribute), set as
// Frame:SetScale sets it (0x00775140): a negative scale is refused, anything
// else goes to SetFrameScale without force.
//
// `registerForDrag` on a frame (retail's LayoutFrame attribute), passed to
// Frame:RegisterForDrag itself (FUN_SCRIPT_FRAME_REGISTERFORDRAG), which
// parses the names and stores the mask. The engine has no setter to call
// instead.

#include "Game.h"
#include "Offsets.h"
#include "xml/FrameLoad.h"
#include "xml/TypeLoad.h"

#include <cstdint>

namespace Xml::NativeSetters {

namespace {

using GetColor_t = void(__fastcall *)(void *region, void *edx, uint32_t *bgra);
using SetColor_t = void(__fastcall *)(void *region, void *edx, const uint32_t *bgra);
using SetFrameScale_t = bool(__fastcall *)(void *frame, void *edx, float scale, int force);
using Tokenize_t = void(__stdcall *)(const char **cursor, char *out, unsigned outSize, const char *delims,
                                     int *outQuoted);

void OnRegionNode(void *object, const void *node, void * /*status*/) {
    float alpha = 0.0f;
    if (!FloatAttr(node, "alpha", alpha))
        return;
    if (!(alpha > 0.0f))
        alpha = 0.0f;
    else if (alpha > 1.0f)
        alpha = 1.0f;
    uint32_t bgra = 0;
    reinterpret_cast<GetColor_t>(Offsets::FUN_REGION_GET_COLOR)(object, nullptr, &bgra);
    bgra = (bgra & 0x00FFFFFF) | (static_cast<uint32_t>(alpha * 255.0f) << 24);
    reinterpret_cast<SetColor_t>(Offsets::FUN_FONTSTRING_SET_COLOR)(object, nullptr, &bgra);
}

const Xml::TypeLoad::AutoSubscribe _textureSub{Xml::TypeLoad::TEXTURE, &OnRegionNode};
const Xml::TypeLoad::AutoSubscribe _fontStringSub{Xml::TypeLoad::FONT_STRING, &OnRegionNode};

// frame:RegisterForDrag(name, ...) with the attribute's names, split on spaces
// and commas as `registerForClicks` is, run as its own Lua call so the
// loader's stack is left alone.
void RegisterForDrag(void *frame, const char *value) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    auto tokenize = reinterpret_cast<Tokenize_t>(Offsets::FUN_STORM_STR_TOKENIZE);
    const int top = Game::Lua::GetTop(L);
    Game::Lua::PushCClosure(L, reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_FRAME_REGISTERFORDRAG), 0);
    Game::Lua::PushObject(L, frame);
    int args = 1;
    char token[1024];
    const char *cursor = value;
    while (cursor != nullptr && *cursor != '\0') {
        token[0] = '\0';
        tokenize(&cursor, token, sizeof token, " ,", nullptr);
        if (token[0] != '\0') {
            Game::Lua::PushString(L, token);
            ++args;
        }
    }
    Game::Lua::PCall(L, args, 0, 0);
    Game::Lua::SetTop(L, top);
}

// A Model's `scale` is its model scale (1.12's and 4.3.4's Model loaders read
// it so; retail calls it `modelScale` beside the frame's `scale`).
void OnFrameNode(void *object, const void *node, void * /*status*/) {
    float scale = 0.0f;
    if (FloatAttr(node, "scale", scale) && !(scale < 0.0f) && !Game::Lua::IsA(object, Offsets::VAR_MODEL_LUA_TYPE_ID))
        reinterpret_cast<SetFrameScale_t>(Offsets::FUN_FRAME_SET_SCALE)(object, nullptr, scale, 0);
    if (const char *drag = Xml::Attr(node, "registerForDrag"); drag != nullptr && *drag != '\0')
        RegisterForDrag(object, drag);
}

const Xml::FrameLoad::AutoSubscribe _frameSub{&OnFrameNode};

} // namespace

} // namespace Xml::NativeSetters
