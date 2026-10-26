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

// `Cooldown` frame type: 4.3.4's CGCooldown (wowdecomp15595:
// WoW/Source/Ui/CGCooldown.{h,cpp}, CGCooldown_Scripts.cpp), a CSimpleFrame
// subclass that draws its own sweep. No model, no template, no Lua driver.
//
// The class is built the way CGCooldown::Create builds it: one SMemAlloc block
// of the CSimpleFrame size plus CGCooldown's fields, the engine's CSimpleFrame
// constructor, then our own copies of CSimpleFrame's two vtables with
// CGCooldown's overrides in the slots 1.12 has for them (Offsets.h
// "Subclassing CSimpleFrame"):
//
//     4.3.4 virtual                  1.12 slot
//     ~CGCooldown                    +0x00 (and the layout thunk +0x04)
//     GetScriptMetaTable             +0x08 GetScriptMethod: own hash, then Frame's
//     IsA(type) / IsA(name)          +0x10 / +0x18
//     GetObjectTypeName              +0x1C, what GetObjectType/GetFrameType push
//     OnLayerUpdate(elapsed)         +0x38 OnFrameUpdate
//     OnFrameRender(batch, layer)    +0x44 EmitLayer
//     UpdateAlpha                    +0x28 SetFrameAlpha
//     OnFrameSizeChanged(rect)       layout +0x00
//     LoadXML(node, status)          layout +0x08
//
// Past 4.3.4's five methods, the later clients' (Clear, Pause, the getters,
// the draw flags and colours, OnCooldownDone) follow the contract in their
// FrameAPICooldownDocumentation.lua; the 4.3.4 state machine stays as it is.
//
// The methods live in a method hash of their own, exactly like each
// engine frame class's (Offsets.h "Per-class method hashes"), so only a
// Cooldown has them, and they reach `self` through the engine's own IsA gate
// with a type id taken from the engine's counter. The hash is released where
// the engine releases its own, at the end of CGGameUI::UnregisterScriptFunctions.
//
// Drawing. 1.12's layer batch draws quads (four-vertex strips) whose vertex
// arrays it keeps by pointer, so the geometry is queued once per layer rebuild
// and animated in place: the star fan (s_starIndices, eight triangles around
// vertex 9) is four strips {v2k, v9, v2k+1, v2k+2}, the flash and edge quads
// are one strip each in the order their index lists {0,2,1, 1,2,3} draw. A
// change to WHICH strips are queued (a texture loaded, the edge switched)
// marks the layer changed so it is rebuilt. Vertex z is 0, as 1.12's texture
// regions write it (CSimpleTexture::SetVertices 0x007705B0); 4.3.4 writes 1.
//
// Textures. star4.blp ships with 1.12 (byte-identical to 4.3.4's). edge.blp
// does not, and a texture under the embedded addon's path would not load:
// BLPs open through SFile, not the read the addon redirects. So the edge is
// 4.3.4's 16x16 image as texels in the DLL, through the engine's procedural
// texture path, created once per process.
//
// The stock CooldownFrameTemplate is a Model template (the indicator model and
// its OnUpdateModel / OnAnimFinished scripts), which vanilla's own buttons
// keep using. 4.3.4's CooldownFrameTemplate is `<Cooldown setAllPoints="true"
// hidden="true" virtual="true"/>`, so when a Cooldown inherits the stock
// Model template, it loads those two attributes in its place, the way
// CLayoutFrame::LoadXML and CSimpleFrame::LoadXML apply them.
// CooldownFrame_SetTimer is the embedded addon's (Util/Cooldown.lua).

#include "Game.h"
#include "Offsets.h"
#include "api/Documentation.h"
#include "frame/ExtraScripts.h"
#include "frame/RenderBatch.h"
#include "frame/ScriptArgs.h"
#include "texture/Load.h"
#include "time/Clock.h"
#include "xml/FrameLoad.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Frame::Cooldown {

namespace {

using RenderBatch::C2Vector;
using RenderBatch::C3Vector;

// ---- the object -------------------------------------------------------------

// CGCooldown's fields (CGCooldown.h, +0x1F0..), after the CSimpleFrame part,
// plus the star fan as 1.12's batch draws it.
struct Fields {
    uint32_t startTime; // ms, on the GetTime() epoch
    uint32_t duration;
    uint32_t elapsed;
    uint32_t flashing;
    uint32_t reverse;
    void *starTexture; // HTEXTURE, one reference
    C3Vector starVerts[10];
    C2Vector starTexCoords[10];
    uint32_t starColor; // BGRA
    C3Vector flashVerts[4];
    C2Vector flashTexCoords[4];
    uint32_t flashColor;
    void *edgeTexture; // the process-wide edge texture, not counted
    uint32_t drawEdge;
    C3Vector edgeVerts[4];
    C2Vector edgeTexCoords[4];
    uint32_t edgeColor;

    C3Vector starStripVerts[4][4];
    C2Vector starStripTexCoords[4][4];
    C3Vector flashStripVerts[4];
    C2Vector flashStripTexCoords[4];
    C3Vector edgeStripVerts[4];
    C2Vector edgeStripTexCoords[4];

    // Past 4.3.4 (FrameAPICooldownDocumentation.lua's contract).
    uint32_t done;     // this cooldown has ended (or was cleared); OnCooldownDone ran
    uint32_t paused;
    uint32_t pausedAt; // ms, the time the frame holds at while paused
    float modRate;     // scales the flash and the display duration, not the sweep
    uint32_t drawSwipe;
    uint32_t drawBling;
    uint32_t swipeBase; // BGRA before the frame alpha
    uint32_t edgeBase;
};

constexpr size_t kObjectSize = Offsets::SIMPLEFRAME_SIZE + sizeof(Fields);

Fields &F(void *frame) { return *Game::Ptr<Fields>(frame, Offsets::SIMPLEFRAME_SIZE); }

// The frame's resolved rect, CRect's {minY, minX, maxY, maxX} field order.
struct Rect {
    float minY, minX, maxY, maxX;
};
const Rect &RectOf(void *frame) { return *Game::Ptr<Rect>(frame, Offsets::OFF_REGION_RECT); }

void *FrameOfLayout(void *layout) {
    return static_cast<uint8_t *>(layout) - Offsets::OFF_FRAME_LAYOUT_SUBOBJECT;
}

// The alpha the frame draws with — 4.3.4's m_alpha * m_parentAlpha / 255.
// 1.12's alpha setter copies one byte down the tree instead of multiplying.
uint32_t DrawAlpha(void *frame) { return Game::Read<uint8_t>(frame, Offsets::OFF_FRAME_ALPHA); }

uint32_t Bgra(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void SetAlphaByte(uint32_t &color, uint32_t a) { color = (color & 0x00FFFFFFu) | (a << 24); }

// `base` with its alpha scaled by the frame's, as 4.3.4 scales its constants.
uint32_t WithAlpha(uint32_t base, uint32_t alpha) {
    uint32_t color = base;
    SetAlphaByte(color, (base >> 24) * alpha / 255);
    return color;
}

uint32_t ColorByte(double c) {
    if (!(c > 0.0))
        return 0;
    if (c >= 1.0)
        return 255;
    return static_cast<uint32_t>(c * 255.0 + 0.5);
}

// ---- vtables ----------------------------------------------------------------

// An MSVC vtable with its RTTI locator in front, as the engine's are laid out.
template <size_t N>
struct VTable {
    const void *locator;
    const void *slots[N];
};

VTable<Offsets::VTBL_SIMPLEFRAME_SLOTS> g_vtable;
VTable<Offsets::VTBL_SIMPLEFRAME_LAYOUT_SLOTS> g_layoutVtable;

// CSimpleFrame's own slot, for the base call of an override.
template <typename Fn>
Fn Base(uintptr_t slot) {
    return Game::Read<Fn>(reinterpret_cast<const void *>(Offsets::VTBL_SIMPLEFRAME), slot);
}
template <typename Fn>
Fn LayoutBase(uintptr_t slot) {
    return Game::Read<Fn>(reinterpret_cast<const void *>(Offsets::VTBL_SIMPLEFRAME_LAYOUT), slot);
}

// 1.12's CSimpleFrame::Show / Hide, as Script Show / Hide make them.
void Show(void *frame) {
    using Slot_t = void(__fastcall *)(void *self, void *edx);
    Game::Ref<uint32_t>(frame, Offsets::OFF_FRAME_SHOWN) = 1;
    Game::Read<Slot_t>(Game::Read<void *>(frame, 0), Offsets::VTBL_FRAME_SHOW)(frame, nullptr);
}

void Hide(void *frame) {
    using Slot_t = void(__fastcall *)(void *self, void *edx);
    Game::Ref<uint32_t>(frame, Offsets::OFF_FRAME_SHOWN) = 0;
    Game::Read<Slot_t>(Game::Read<void *>(frame, 0), Offsets::VTBL_FRAME_HIDE)(frame, nullptr);
}

void NotifyLayerChanged(void *frame, int layer) {
    using Fn_t = void(__fastcall *)(void *self, void *edx, int layer);
    reinterpret_cast<Fn_t>(Offsets::FUN_FRAME_NOTIFY_LAYER_CHANGED)(frame, nullptr, layer);
}

// ---- textures ---------------------------------------------------------------

const char *const kStarTextureName = "interface\\cooldown\\star4.blp";

// interface\cooldown\edge.blp (4.3.4): 16x16 DXT5, decoded the standard way
// (565 widened by bit replication), as 4.3.4's device samples it. Each of rows
// 0-7 is one colour with alpha only in column 8, a line from the centre to the
// top edge fading out; rows 8-15 are transparent black.
constexpr uint32_t kEdgeSize = 16;
constexpr uint32_t kEdgeColumn = 8;
const uint32_t kEdgeRowColor[8] = {0xFFD300, 0xFFD300, 0xFFD300, 0xDEBA00,
                                   0xB59200, 0x806900, 0x4C4000, 0x181800};
const uint8_t kEdgeColumnAlpha[8] = {0xFF, 0xEB, 0xC9, 0xA7, 0x7E, 0x5A, 0x36, 0x12};
uint32_t g_edgeTexels[kEdgeSize * kEdgeSize];
void *g_edgeTexture = nullptr;

void *EdgeTexture() {
    if (g_edgeTexture != nullptr)
        return g_edgeTexture;
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < kEdgeSize; ++x)
            g_edgeTexels[y * kEdgeSize + x] =
                kEdgeRowColor[y] | (x == kEdgeColumn ? uint32_t{kEdgeColumnAlpha[y]} << 24 : 0);
    g_edgeTexture = Texture::CreateFromPixels("interface\\cooldown\\edge.blp", kEdgeSize,
                                              kEdgeSize, g_edgeTexels);
    return g_edgeTexture;
}

// ---- CGCooldown --------------------------------------------------------------

// CGCooldown::s_flash* — the flash's seven keys, 1000 ms apart / 6.
const float s_flashScale[7] = {1.0f, 0.6993007f, 0.5405405f, 0.6289308f,
                               0.7518797f, 0.625f, 0.8695652f};
const float s_flashAlpha[7] = {0.0f, 0.5f, 1.0f, 0.75f, 0.5f, 0.25f, 0.0f};
const float s_flashRotation[7] = {0.0f, -0.1303761f, -0.25918141f, -0.39269909f,
                                  -0.51836282f, -0.6518805f, -0.78539819f};

void SetXY(C3Vector &v, float x, float y) {
    v.x = x;
    v.y = y;
}

// The strips the batch draws, from 4.3.4's arrays.
void PublishStrips(Fields &f) {
    for (int k = 0; k < 4; ++k) {
        const int order[4] = {2 * k, 9, 2 * k + 1, 2 * k + 2};
        for (int i = 0; i < 4; ++i) {
            f.starStripVerts[k][i] = f.starVerts[order[i]];
            f.starStripTexCoords[k][i] = f.starTexCoords[order[i]];
        }
    }
    const int quad[4] = {0, 2, 1, 3};
    for (int i = 0; i < 4; ++i) {
        f.flashStripVerts[i] = f.flashVerts[quad[i]];
        f.flashStripTexCoords[i] = f.flashTexCoords[quad[i]];
        f.edgeStripVerts[i] = f.edgeVerts[quad[i]];
        f.edgeStripTexCoords[i] = f.edgeTexCoords[quad[i]];
    }
}

// OFFSET (4.3.4): 0x009FDC20
void AnimateOctants01(Fields &f, const Rect &r, uint32_t octant, float t) {
    const float cx = (r.minX + r.maxX) * 0.5f;
    const float cy = 0.5f * (r.maxY + r.minY);
    C3Vector *v = f.starVerts;
    if (f.reverse) {
        if (octant > 0) {
            SetXY(v[2], r.maxX, r.maxY - (r.maxY - cy) * t);
            SetXY(v[1], r.maxX, r.maxY);
            for (int i = 3; i < 9; ++i)
                SetXY(v[i], r.maxX, v[1].y);
        } else {
            const float x = cx + (r.maxX - cx) * t;
            SetXY(v[1], x, r.maxY);
            SetXY(v[2], x, r.maxY);
            for (int i = 3; i < 9; ++i)
                SetXY(v[i], v[1].x, r.maxY);
        }
        return;
    }
    if (octant > 0) {
        SetXY(v[1], r.maxX, r.maxY - (r.maxY - cy) * t);
        v[0] = v[1];
    } else {
        SetXY(v[0], (r.maxX - cx) * t + cx, r.maxY);
        SetXY(v[1], r.maxX, r.maxY);
    }
    SetXY(v[2], r.maxX, cy);
    SetXY(v[3], r.maxX, r.minY);
    SetXY(v[4], cx, r.minY);
    SetXY(v[5], r.minX, r.minY);
    SetXY(v[6], r.minX, cy);
    SetXY(v[7], r.minX, r.maxY);
}

// OFFSET (4.3.4): 0x009FDEB0
void AnimateOctants23(Fields &f, const Rect &r, uint32_t octant, float t) {
    const float cx = (r.minX + r.maxX) * 0.5f;
    const float cy = 0.5f * (r.maxY + r.minY);
    C3Vector *v = f.starVerts;
    if (f.reverse) {
        if (octant >= 3) {
            const float x = r.maxX - (r.maxX - cx) * t;
            SetXY(v[4], x, r.minY);
            SetXY(v[1], r.maxX, r.maxY);
            SetXY(v[2], r.maxX, cy);
            SetXY(v[3], r.maxX, r.minY);
            SetXY(v[5], x, r.minY);
            for (int i = 6; i < 9; ++i)
                SetXY(v[i], v[4].x, r.minY);
        } else {
            SetXY(v[3], r.maxX, cy - (cy - r.minY) * t);
            SetXY(v[1], r.maxX, r.maxY);
            SetXY(v[2], r.maxX, cy);
            for (int i = 4; i < 9; ++i)
                SetXY(v[i], r.maxX, v[3].y);
        }
        return;
    }
    if (octant >= 3) {
        SetXY(v[3], r.maxX - (r.maxX - cx) * t, r.minY);
        v[0] = v[3];
        v[1] = v[3];
        v[2] = v[3];
    } else {
        SetXY(v[2], r.maxX, cy - (cy - r.minY) * t);
        v[0] = v[2];
        v[1] = v[2];
        SetXY(v[3], r.maxX, r.minY);
    }
    SetXY(v[4], cx, r.minY);
    SetXY(v[5], r.minX, r.minY);
    SetXY(v[6], r.minX, cy);
    SetXY(v[7], r.minX, r.maxY);
}

// OFFSET (4.3.4): 0x009FE1D0
void AnimateOctants45(Fields &f, const Rect &r, uint32_t octant, float t) {
    const float cx = (r.minX + r.maxX) * 0.5f;
    const float cy = 0.5f * (r.maxY + r.minY);
    C3Vector *v = f.starVerts;
    if (f.reverse) {
        if (octant >= 5) {
            SetXY(v[6], r.minX, (cy - r.minY) * t + r.minY);
            SetXY(v[1], r.maxX, r.maxY);
            SetXY(v[2], r.maxX, cy);
            SetXY(v[3], r.maxX, r.minY);
            SetXY(v[4], cx, r.minY);
            SetXY(v[5], r.minX, r.minY);
            SetXY(v[7], r.minX, v[6].y);
            SetXY(v[8], r.minX, v[6].y);
        } else {
            const float x = cx - (cx - r.minX) * t;
            SetXY(v[5], x, r.minY);
            SetXY(v[1], r.maxX, r.maxY);
            SetXY(v[2], r.maxX, cy);
            SetXY(v[3], r.maxX, r.minY);
            SetXY(v[4], cx, r.minY);
            SetXY(v[6], x, r.minY);
            SetXY(v[7], v[5].x, r.minY);
            SetXY(v[8], v[5].x, r.minY);
        }
        return;
    }
    if (octant >= 5) {
        SetXY(v[5], r.minX, (cy - r.minY) * t + r.minY);
        for (int i = 0; i < 5; ++i)
            v[i] = v[5];
    } else {
        SetXY(v[4], cx - (cx - r.minX) * t, r.minY);
        for (int i = 0; i < 4; ++i)
            v[i] = v[4];
        SetXY(v[5], r.minX, r.minY);
    }
    SetXY(v[6], r.minX, cy);
    SetXY(v[7], r.minX, r.maxY);
}

// OFFSET (4.3.4): 0x009FE4F0
void AnimateOctants67(Fields &f, const Rect &r, uint32_t octant, float t) {
    const float cx = (r.minX + r.maxX) * 0.5f;
    const float cy = 0.5f * (r.maxY + r.minY);
    C3Vector *v = f.starVerts;
    if (f.reverse) {
        if (octant < 7) {
            SetXY(v[7], r.minX, (r.maxY - cy) * t + cy);
            SetXY(v[1], r.maxX, r.maxY);
            SetXY(v[2], r.maxX, cy);
            SetXY(v[3], r.maxX, r.minY);
            SetXY(v[4], cx, r.minY);
            SetXY(v[5], r.minX, r.minY);
            SetXY(v[6], r.minX, cy);
            SetXY(v[8], r.minX, v[7].y);
            return;
        }
        SetXY(v[8], (cx - r.minX) * t + r.minX, r.maxY);
        SetXY(v[1], r.maxX, r.maxY);
        SetXY(v[2], r.maxX, cy);
        SetXY(v[3], r.maxX, r.minY);
        SetXY(v[4], cx, r.minY);
        SetXY(v[5], r.minX, r.minY);
        SetXY(v[6], r.minX, cy);
        SetXY(v[7], r.minX, r.maxY);
        return;
    }
    if (octant < 7) {
        SetXY(v[6], r.minX, cy + (r.maxY - cy) * t);
        for (int i = 0; i < 6; ++i)
            v[i] = v[6];
        SetXY(v[7], r.minX, r.maxY);
        return;
    }
    SetXY(v[7], (cx - r.minX) * t + r.minX, r.maxY);
    for (int i = 0; i < 7; ++i)
        v[i] = v[7];
}

// The four corners of a quad turned by `angle` about the texture centre and
// scaled, 4.3.4's texcoord rotation for the flash and the edge.
void RotateQuadTexCoords(C2Vector *tc, float angle, float scale) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float a = c * -0.5f;
    const float b = -0.5f * s;
    const float d = c * 0.5f;
    const float e = s * 0.5f;
    tc[0] = {(a - b) * scale + 0.5f, (b + a) * scale + 0.5f};
    tc[1] = {(d - b) * scale + 0.5f, (e + a) * scale + 0.5f};
    tc[2] = {(a - e) * scale + 0.5f, (b + d) * scale + 0.5f};
    tc[3] = {(d - e) * scale + 0.5f, (d + e) * scale + 0.5f};
}

// OFFSET (4.3.4): 0x009FED40
void AnimateVerts(void *frame) {
    Fields &f = F(frame);
    const Rect &r = RectOf(frame);
    const float cx = (r.minX + r.maxX) * 0.5f;
    const float cy = (r.minY + r.maxY) * 0.5f;

    if (!f.flashing) {
        const float elapsed = static_cast<float>(f.elapsed);
        const float duration = static_cast<float>(f.duration);
        const float octantTime = duration * 0.125f;
        const float invOctantTime = 1.0f / octantTime;
        const int32_t octant = static_cast<int32_t>(invOctantTime * elapsed);
        const float t = invOctantTime * (elapsed - octantTime * static_cast<float>(octant));

        const int top = f.reverse ? 0 : 8;
        f.starVerts[top] = {cx, r.maxY, 0.0f};
        f.starTexCoords[top] = {0.5f, 0.0f};
        f.starVerts[9] = {cx, cy, 0.0f};
        f.starTexCoords[9] = {0.5f, 0.5f};

        const uint32_t o = static_cast<uint32_t>(octant);
        if (octant < 4) {
            if (octant < 2)
                AnimateOctants01(f, r, o, t);
            else
                AnimateOctants23(f, r, o, t);
        } else if (octant < 6) {
            AnimateOctants45(f, r, o, t);
        } else {
            AnimateOctants67(f, r, o, t);
        }

        // CLayoutFrame::m_layoutScale, the frame's effective scale.
        if (f.drawEdge)
            RotateQuadTexCoords(f.edgeTexCoords, elapsed / duration * -6.2831855f,
                                Game::Read<float>(frame, Offsets::OFF_LAYOUT_SCALE));
        PublishStrips(f);
        return;
    }

    // 4.3.4 draws a reverse frame's flash as the full star fan here, but its
    // reverse never reaches the flash (it holds at the end). Retail's reverse
    // flashes like any cooldown, and the star is transparent while flashing,
    // so both directions animate the flash.
    // On 4.3.4's 1000 ms flash timeline; a modRate shortens the flash itself.
    const float elapsed = static_cast<float>(f.elapsed) * (1000.0f / static_cast<float>(f.duration));
    // The update that ends the flash (elapsed 1000) hides the frame and then
    // animates once more with key 6, where 4.3.4 reads one float past each
    // table; the last segment stands in for it.
    int32_t index = static_cast<int32_t>(elapsed * 0.0059999996f);
    if (index > 5)
        index = 5;
    const float t = (elapsed - static_cast<float>(index) * 166.66667f) * 0.0059999996f;
    const float alpha = static_cast<float>(DrawAlpha(frame));
    const float key =
        (s_flashAlpha[index + 1] - s_flashAlpha[index]) * t + s_flashAlpha[index];
    SetAlphaByte(f.flashColor, static_cast<uint8_t>(static_cast<int32_t>(alpha * key)));
    const float angle =
        (s_flashRotation[index + 1] - s_flashRotation[index]) * t + s_flashRotation[index];
    const float scale = (s_flashScale[index + 1] - s_flashScale[index]) * t + s_flashScale[index];
    RotateQuadTexCoords(f.flashTexCoords, angle, scale);
    PublishStrips(f);
}

// OFFSET (4.3.4): 0x009FEA30
void ResetVerts(void *frame) {
    Fields &f = F(frame);
    const Rect &r = RectOf(frame);
    for (int i = 0; i < 9; ++i)
        f.starVerts[i] = {0.0f, 0.0f, 0.0f};

    f.flashVerts[0] = {r.minX, r.maxY, 0.0f};
    f.flashVerts[1] = {r.maxX, r.maxY, 0.0f};
    f.flashVerts[2] = {r.minX, r.minY, 0.0f};
    f.flashVerts[3] = {r.maxX, r.minY, 0.0f};
    f.flashTexCoords[0] = {0.0f, 0.0f};
    f.flashTexCoords[1] = {1.0f, 0.0f};
    f.flashTexCoords[2] = {0.0f, 1.0f};
    f.flashTexCoords[3] = {1.0f, 1.0f};

    if (f.drawEdge) {
        for (int i = 0; i < 4; ++i) {
            f.edgeVerts[i] = f.flashVerts[i];
            f.edgeTexCoords[i] = f.flashTexCoords[i];
        }
    }
    PublishStrips(f);
}

// OFFSET (4.3.4): 0x009FF460
void UpdateColors(void *frame) {
    Fields &f = F(frame);
    const uint32_t alpha = DrawAlpha(frame);
    if (!f.flashing) {
        // 4.3.4's constants (black at 160, white) are the bases' defaults.
        f.starColor = WithAlpha(f.swipeBase, alpha);
        f.edgeColor = WithAlpha(f.edgeBase, alpha);
        f.flashColor = Bgra(0, 0, 0, 0);
    } else {
        f.starColor = Bgra(0, 0, 0, 0);
        f.edgeColor = Bgra(0, 0, 0, 0);
        f.flashColor = Bgra(0x50, 0xA0, 0xFF, alpha);
    }
}

// OFFSET (4.3.4): 0x009FFAD0. A paused frame holds at the moment of the call,
// which is what CooldownFrame_SetDisplayAsPercentage (Pause, then SetCooldown
// back-dated by the shown fraction) relies on.
void SetCooldown(void *frame, uint32_t start, uint32_t duration, float modRate) {
    Fields &f = F(frame);
    f.startTime = start;
    f.duration = duration;
    f.modRate = modRate;
    f.elapsed = 0;
    f.flashing = 0;
    f.done = 0;
    if (f.paused)
        f.pausedAt = Time::Clock::NowMs();
    if (f.starTexture == nullptr) {
        f.starTexture = Texture::LoadByPath(kStarTextureName);
        NotifyLayerChanged(frame, 2);
    }
    if (f.edgeTexture == nullptr) {
        f.edgeTexture = EdgeTexture();
        NotifyLayerChanged(frame, 3);
    }
    UpdateColors(frame);
    ResetVerts(frame);
    Show(frame);
}

// Ends the cooldown without the bling or OnCooldownDone.
void Clear(void *frame) {
    Fields &f = F(frame);
    f.startTime = 0;
    f.duration = 0;
    f.elapsed = 0;
    f.flashing = 0;
    f.done = 1;
    Hide(frame);
}

// The timer stops at the moment of the pause; resuming moves the start past
// the paused span, so the sweep (or the bling) picks up where it stopped.
void SetPaused(void *frame, bool paused) {
    Fields &f = F(frame);
    if (paused == (f.paused != 0))
        return;
    const uint32_t now = Time::Clock::NowMs();
    if (paused)
        f.pausedAt = now;
    else
        f.startTime += Time::Clock::Elapsed(f.pausedAt, now);
    f.paused = paused;
}

// ---- the overrides ------------------------------------------------------------

using DeletingDestructor_t = void *(__fastcall *)(void *self, void *edx, uint32_t flags);
using GetScriptMethod_t = int(__fastcall *)(void *self, void *edx, void *L, const char *name);
using IsA_t = bool(__fastcall *)(void *self, void *edx, int typeId);
using IsObjectType_t = bool(__fastcall *)(void *self, void *edx, const char *name);
using SetFrameAlpha_t = void(__fastcall *)(void *self, void *edx, uint8_t alpha);
using OnFrameUpdate_t = void(__fastcall *)(void *self, void *edx, float elapsedSec);
using EmitLayer_t = void(__fastcall *)(void *self, void *edx, void *batch, int layer);
using OnFrameSizeChanged_t = void(__fastcall *)(void *layout, void *edx, const float *oldRect);
using LoadXML_t = uintptr_t(__fastcall *)(void *layout, void *edx, const void *node,
                                          void *status);
using PushMethod_t = int(__fastcall *)(void *L, const char *name, void *hash);

// The class's method hash and type id (Offsets.h "Per-class method hashes").
uint32_t g_hash[Offsets::METHOD_HASH_SIZE / 4];
int g_typeId = 0;

uintptr_t TypeIdVar() { return reinterpret_cast<uintptr_t>(&g_typeId); }

const Frame::ExtraScripts::Script g_onCooldownDone{"oncooldowndone", TypeIdVar()};

// From the update walk, where the engine runs OnUpdate.
void RunOnCooldownDone(void *frame) {
    const uint32_t *slot = g_onCooldownDone.Slot(frame);
    if (slot == nullptr || slot[0] == 0)
        return;
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    Frame::ScriptArgs::RunSlotWithValues(L, slot, frame, Game::Lua::GetTop(L) + 1, 0);
}

// CGCooldown::~CGCooldown, then CSimpleFrame's deleting destructor.
void *__fastcall DeletingDestructor(void *self, void *, uint32_t flags) {
    Fields &f = F(self);
    Texture::ReleaseHandle(f.starTexture);
    f.starTexture = nullptr;
    f.edgeTexture = nullptr;
    return Base<DeletingDestructor_t>(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR)(self, nullptr,
                                                                               flags);
}

void *__fastcall LayoutDeletingDestructor(void *layout, void *, uint32_t flags) {
    return DeletingDestructor(FrameOfLayout(layout), nullptr, flags);
}

int __fastcall GetScriptMethod(void *self, void *, void *L, const char *name) {
    if (reinterpret_cast<PushMethod_t>(Offsets::FUN_FRAMESCRIPT_PUSH_METHOD)(L, name, g_hash))
        return 1;
    return Base<GetScriptMethod_t>(Offsets::VTBL_FRAME_GET_SCRIPT_METHOD)(self, nullptr, L, name);
}

bool __fastcall IsA(void *self, void *, int typeId) {
    return (g_typeId != 0 && typeId == g_typeId) ||
           Base<IsA_t>(Offsets::VTBL_FRAME_IS_A)(self, nullptr, typeId);
}

bool __fastcall IsObjectType(void *self, void *, const char *name) {
    return (name != nullptr && _stricmp(name, "Cooldown") == 0) ||
           Base<IsObjectType_t>(Offsets::VTBL_FRAME_IS_OBJECT_TYPE)(self, nullptr, name);
}

const char *__fastcall GetObjectType(void *, void *) { return "Cooldown"; }

// OFFSET (4.3.4): 0x009FF570 (UpdateAlpha)
void __fastcall SetFrameAlpha(void *self, void *, uint8_t alpha) {
    Base<SetFrameAlpha_t>(Offsets::VTBL_FRAME_SET_FRAME_ALPHA)(self, nullptr, alpha);
    UpdateColors(self);
}

// OFFSET (4.3.4): 0x009FF5A0 (OnLayerUpdate)
void __fastcall OnFrameUpdate(void *self, void *, float elapsedSec) {
    Base<OnFrameUpdate_t>(Offsets::VTBL_FRAME_ON_FRAME_UPDATE)(self, nullptr, elapsedSec);
    Fields &f = F(self);
    // 4.3.4: `now <= start ? 0 : now - start`, compared wrap-safe. A paused
    // frame reads the time it was paused at.
    const uint32_t now = f.paused ? f.pausedAt : Time::Clock::NowMs();
    f.elapsed = Time::Clock::Reached(now, f.startTime) ? Time::Clock::Elapsed(f.startTime, now) : 0;
    bool finished = false;
    if (f.elapsed >= f.duration) {
        if (f.flashing) {
            f.flashing = 0;
            Hide(self);
        } else if (f.done) {
            // Shown again after the end or a Clear: nothing left to play.
            Hide(self);
        } else {
            f.done = 1;
            // Not the zero-length timer a new frame (or SetCooldown(t, 0)) runs.
            finished = f.duration != 0;
            // Retail ends a reverse cooldown like any other, where 4.3.4 holds
            // it at `duration - 1` (live probe: reverse SetCooldown(T, 2) ran
            // OnCooldownDone at T + 2, still shown at T + 2.5, hidden by T + 4).
            if (f.drawBling) {
                // Retail: the sweep ends at start + duration whatever the
                // modRate; the flash lasts 1000 * modRate ms, and the getters
                // then report the flash (live probe: SetCooldown(T, 4, 0.5)
                // finished at T + 4 with times T + 4000, 500).
                f.startTime = now;
                f.flashing = 1;
                const float flash = 1000.0f * f.modRate;
                f.duration = flash >= 1.0f ? static_cast<uint32_t>(flash) : 1;
                f.elapsed = 0;
                UpdateColors(self);
            } else {
                Hide(self);
            }
        }
    }
    AnimateVerts(self);
    // Last: the handler may start another cooldown on this frame.
    if (finished)
        RunOnCooldownDone(self);
}

// OFFSET (4.3.4): 0x009FE830 (OnFrameRender)
void __fastcall EmitLayer(void *self, void *, void *batch, int layer) {
    Base<EmitLayer_t>(Offsets::VTBL_FRAME_EMIT_LAYER)(self, nullptr, batch, layer);
    Fields &f = F(self);
    using RenderBatch::QueueStrip;
    if (layer == 2) {
        if (f.starTexture != nullptr) {
            for (int k = 0; f.drawSwipe && k < 4; ++k)
                QueueStrip(batch, f.starTexture, RenderBatch::BLEND_ALPHA, f.starStripVerts[k],
                           f.starStripTexCoords[k], &f.starColor);
            QueueStrip(batch, f.starTexture, RenderBatch::BLEND_ADD, f.flashStripVerts,
                       f.flashStripTexCoords, &f.flashColor);
        }
    } else if (layer == 3) {
        if (f.edgeTexture != nullptr && f.drawEdge)
            QueueStrip(batch, f.edgeTexture, RenderBatch::BLEND_OPAQUE, f.edgeStripVerts,
                       f.edgeStripTexCoords, &f.edgeColor);
    }
}

// OFFSET (4.3.4): 0x009FF580
void __fastcall OnFrameSizeChanged(void *layout, void *, const float *oldRect) {
    LayoutBase<OnFrameSizeChanged_t>(Offsets::VTBL_LAYOUT_ON_FRAME_SIZE_CHANGED)(layout, nullptr,
                                                                                 oldRect);
    ResetVerts(FrameOfLayout(layout));
}

// The stock CooldownFrameTemplate: a Model template (Cooldown.xml, 1.12).
bool IsStockTemplate(const void *node) {
    if (!Xml::TagIs(node, "Model"))
        return false;
    const char *name = Xml::Attr(node, "name");
    return name != nullptr && _stricmp(name, "CooldownFrameTemplate") == 0;
}

// 4.3.4's CooldownFrameTemplate, `setAllPoints="true" hidden="true"`, applied
// as CLayoutFrame::LoadXML and CSimpleFrame::LoadXML apply the two.
void LoadModernTemplate(void *layout) {
    using GetLayoutParent_t = void *(__fastcall *)(void *self, void *edx);
    using SetAllPoints_t = void(__fastcall *)(void *self, void *edx, void *relative, int resize);
    void *parent = Game::Read<GetLayoutParent_t>(Game::Read<void *>(layout, 0),
                                                 Offsets::VTBL_LAYOUT_GET_LAYOUT_PARENT)(layout,
                                                                                         nullptr);
    reinterpret_cast<SetAllPoints_t>(Offsets::FUN_LAYOUT_SET_ALL_POINTS)(layout, nullptr, parent,
                                                                         1);
    Hide(FrameOfLayout(layout));
}

// OFFSET (4.3.4): 0x009FDBD0 — the frame's own loader, then `reverse` and
// `drawEdge`, and the later clients' `drawSwipe` and `drawBling`.
uintptr_t __fastcall LoadXML(void *layout, void *, const void *node, void *status) {
    if (node != nullptr && IsStockTemplate(node)) {
        LoadModernTemplate(layout);
        return 0;
    }
    const uintptr_t result =
        LayoutBase<LoadXML_t>(Offsets::VTBL_XML_NODE_LOADER)(layout, nullptr, node, status);
    if (node == nullptr)
        return result;
    Fields &f = F(FrameOfLayout(layout));
    if (const char *reverse = Xml::Attr(node, "reverse"))
        f.reverse = Game::StringToFlag(reverse);
    if (const char *drawEdge = Xml::Attr(node, "drawEdge"))
        f.drawEdge = Game::StringToFlag(drawEdge);
    if (const char *drawSwipe = Xml::Attr(node, "drawSwipe"))
        f.drawSwipe = Game::StringToFlag(drawSwipe);
    if (const char *drawBling = Xml::Attr(node, "drawBling"))
        f.drawBling = Game::StringToFlag(drawBling);
    return result;
}

void BuildVTables() {
    if (g_vtable.slots[0] != nullptr)
        return;
    std::memcpy(&g_vtable, reinterpret_cast<const void *>(Offsets::VTBL_SIMPLEFRAME - 4),
                sizeof(g_vtable));
    std::memcpy(&g_layoutVtable,
                reinterpret_cast<const void *>(Offsets::VTBL_SIMPLEFRAME_LAYOUT - 4),
                sizeof(g_layoutVtable));
    auto set = [](const void **slots, uintptr_t slot, const void *fn) { slots[slot / 4] = fn; };
    const void **v = g_vtable.slots;
    set(v, Offsets::VTBL_FRAME_DELETING_DESTRUCTOR, reinterpret_cast<const void *>(&DeletingDestructor));
    set(v, Offsets::VTBL_FRAME_GET_SCRIPT_METHOD, reinterpret_cast<const void *>(&GetScriptMethod));
    set(v, Offsets::VTBL_FRAME_IS_A, reinterpret_cast<const void *>(&IsA));
    set(v, Offsets::VTBL_FRAME_IS_OBJECT_TYPE, reinterpret_cast<const void *>(&IsObjectType));
    set(v, Offsets::VTBL_FRAME_GET_OBJECT_TYPE, reinterpret_cast<const void *>(&GetObjectType));
    set(v, Offsets::VTBL_FRAME_SET_FRAME_ALPHA, reinterpret_cast<const void *>(&SetFrameAlpha));
    set(v, Offsets::VTBL_FRAME_ON_FRAME_UPDATE, reinterpret_cast<const void *>(&OnFrameUpdate));
    set(v, Offsets::VTBL_FRAME_EMIT_LAYER, reinterpret_cast<const void *>(&EmitLayer));
    const void **l = g_layoutVtable.slots;
    set(l, Offsets::VTBL_LAYOUT_ON_FRAME_SIZE_CHANGED,
        reinterpret_cast<const void *>(&OnFrameSizeChanged));
    set(l, Offsets::VTBL_LAYOUT_DELETING_DESTRUCTOR,
        reinterpret_cast<const void *>(&LayoutDeletingDestructor));
    set(l, Offsets::VTBL_XML_NODE_LOADER, reinterpret_cast<const void *>(&LoadXML));
}

// OFFSET (4.3.4): 0x008C6390 (CGCooldown::Create) — the "Cooldown" factory.
void *__fastcall CreateCooldown(void *parent) {
    using SMemAlloc_t = void *(__stdcall *)(size_t size, const char *file, int line, int flags);
    using Constructor_t = void(__fastcall *)(void *self, void *edx, void *parent);
    BuildVTables();
    // Zero-filled: 4.3.4's constructor zeroes every CGCooldown field.
    void *frame = reinterpret_cast<SMemAlloc_t>(Offsets::FUN_STORM_SMEM_ALLOC)(
        kObjectSize, __FILE__, __LINE__, 0x08);
    if (frame == nullptr)
        return nullptr;
    reinterpret_cast<Constructor_t>(Offsets::FUN_SIMPLEFRAME_CONSTRUCTOR)(frame, nullptr, parent);
    Game::Ref<const void *>(frame, 0) = g_vtable.slots;
    Game::Ref<const void *>(frame, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT) = g_layoutVtable.slots;
    Fields &f = F(frame);
    f.modRate = 1.0f;
    f.drawSwipe = 1;
    f.drawBling = 1;
    f.swipeBase = Bgra(0, 0, 0, 160);
    f.edgeBase = Bgra(0xFF, 0xFF, 0xFF, 0xFF);
    return frame;
}

// ---- the methods (CGCooldown_Scripts.cpp, then the later clients') -------------------

void *ResolveCooldown(void *L) { return Game::Lua::ResolveTypedObject(L, 1, TypeIdVar()); }

// Seconds to ms on the GetTime() epoch, wrapping like the tick does.
uint32_t Ms(double seconds) { return static_cast<uint32_t>(static_cast<int64_t>(seconds * 1000.0)); }

float ModRateArg(void *L, int index) {
    return Game::Lua::IsNumber(L, index) ? static_cast<float>(Game::Lua::ToNumber(L, index)) : 1.0f;
}

// OFFSET (4.3.4): 0x009FFBD0
int __fastcall Script_SetCooldown(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L, "Usage: %s:SetCooldown(start, duration[, modRate])",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    SetCooldown(frame, Ms(Game::Lua::ToNumber(L, 2)), Ms(Game::Lua::ToNumber(L, 3)),
                ModRateArg(L, 4));
    return 0;
}

int __fastcall Script_SetCooldownDuration(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetCooldownDuration(duration[, modRate])",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    SetCooldown(frame, Time::Clock::NowMs(), Ms(Game::Lua::ToNumber(L, 2)), ModRateArg(L, 3));
    return 0;
}

// The start that makes the sweep end at expirationTime.
int __fastcall Script_SetCooldownFromExpirationTime(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3)) {
        Game::Lua::Error(L,
                         "Usage: %s:SetCooldownFromExpirationTime(expirationTime, duration[, modRate])",
                         Game::Lua::ObjectDisplayName(frame));
        return 0;
    }
    const double duration = Game::Lua::ToNumber(L, 3);
    SetCooldown(frame, Ms(Game::Lua::ToNumber(L, 2) - duration), Ms(duration), ModRateArg(L, 4));
    return 0;
}

int __fastcall Script_Clear(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame != nullptr)
        Clear(frame);
    return 0;
}

// The timer as it stands, in ms: the cooldown, then the flash once it ends
// (retail reports the flash's start and length after OnCooldownDone).
int __fastcall Script_GetCooldownTimes(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    const Fields &f = F(frame);
    Game::Lua::PushNumber(L, static_cast<double>(f.startTime));
    Game::Lua::PushNumber(L, static_cast<double>(f.duration));
    return 2;
}

int __fastcall Script_GetCooldownDuration(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(F(frame).duration));
    return 1;
}

// Retail: SetCooldown(T, 4, 0.5) reads 8000 here and 4000 from GetCooldownDuration.
int __fastcall Script_GetCooldownDisplayDuration(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    const Fields &f = F(frame);
    const double duration = static_cast<double>(f.duration);
    Game::Lua::PushNumber(L, f.modRate > 0.0f ? duration / f.modRate : duration);
    return 1;
}

int __fastcall Script_Pause(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame != nullptr)
        SetPaused(frame, true);
    return 0;
}

int __fastcall Script_Resume(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame != nullptr)
        SetPaused(frame, false);
    return 0;
}

int __fastcall Script_SetPaused(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame != nullptr)
        SetPaused(frame, Game::Lua::ToFlag(L, 2, true));
    return 0;
}

int __fastcall Script_IsPaused(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).paused != 0);
    return 1;
}

// The swipe strips are queued only while the flag is set.
int __fastcall Script_SetDrawSwipe(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Fields &f = F(frame);
    const uint32_t drawSwipe = Game::Lua::ToFlag(L, 2, true);
    if (drawSwipe != f.drawSwipe) {
        f.drawSwipe = drawSwipe;
        NotifyLayerChanged(frame, 2);
    }
    return 0;
}

int __fastcall Script_GetDrawSwipe(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).drawSwipe != 0);
    return 1;
}

int __fastcall Script_SetDrawBling(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    F(frame).drawBling = Game::Lua::ToFlag(L, 2, true);
    return 0;
}

int __fastcall Script_GetDrawBling(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).drawBling != 0);
    return 1;
}

// (r, g, b[, a]) into a base colour; a missing alpha is opaque.
bool ReadColor(void *L, void *frame, const char *method, uint32_t &base) {
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: %s:%s(r, g, b[, a])", Game::Lua::ObjectDisplayName(frame),
                         method);
        return false;
    }
    const double a = Game::Lua::IsNumber(L, 5) ? Game::Lua::ToNumber(L, 5) : 1.0;
    base = Bgra(ColorByte(Game::Lua::ToNumber(L, 2)), ColorByte(Game::Lua::ToNumber(L, 3)),
                ColorByte(Game::Lua::ToNumber(L, 4)), ColorByte(a));
    UpdateColors(frame);
    return true;
}

int __fastcall Script_SetSwipeColor(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame != nullptr)
        ReadColor(L, frame, "SetSwipeColor", F(frame).swipeBase);
    return 0;
}

int __fastcall Script_SetEdgeColor(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame != nullptr)
        ReadColor(L, frame, "SetEdgeColor", F(frame).edgeBase);
    return 0;
}

// OFFSET (4.3.4): 0x009FF650 / 0x009FF6A0
int __fastcall Script_SetReverse(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    F(frame).reverse = Game::Lua::ToFlag(L, 2, true);
    return 0;
}

int __fastcall Script_GetReverse(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).reverse != 0);
    return 1;
}

// OFFSET (4.3.4): 0x009FF6E0 / 0x009FF730. The edge strip is queued only
// while the flag is set, so a change rebuilds the layer.
int __fastcall Script_SetDrawEdge(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Fields &f = F(frame);
    const uint32_t drawEdge = Game::Lua::ToFlag(L, 2, true);
    if (drawEdge != f.drawEdge) {
        f.drawEdge = drawEdge;
        NotifyLayerChanged(frame, 3);
    }
    return 0;
}

int __fastcall Script_GetDrawEdge(void *L) {
    void *frame = ResolveCooldown(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).drawEdge != 0);
    return 1;
}

// ---- registration -------------------------------------------------------------------

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetCooldown", &Script_SetCooldown},
    {"SetReverse", &Script_SetReverse},
    {"GetReverse", &Script_GetReverse},
    {"SetDrawEdge", &Script_SetDrawEdge},
    {"GetDrawEdge", &Script_GetDrawEdge},
    {"SetCooldownDuration", &Script_SetCooldownDuration},
    {"SetCooldownFromExpirationTime", &Script_SetCooldownFromExpirationTime},
    {"Clear", &Script_Clear},
    {"GetCooldownTimes", &Script_GetCooldownTimes},
    {"GetCooldownDuration", &Script_GetCooldownDuration},
    {"GetCooldownDisplayDuration", &Script_GetCooldownDisplayDuration},
    {"Pause", &Script_Pause},
    {"Resume", &Script_Resume},
    {"SetPaused", &Script_SetPaused},
    {"IsPaused", &Script_IsPaused},
    {"SetDrawSwipe", &Script_SetDrawSwipe},
    {"GetDrawSwipe", &Script_GetDrawSwipe},
    {"SetDrawBling", &Script_SetDrawBling},
    {"GetDrawBling", &Script_GetDrawBling},
    {"SetSwipeColor", &Script_SetSwipeColor},
    {"SetEdgeColor", &Script_SetEdgeColor},
};

const Game::Doc::Field kSetCooldownArgs[] = {
    Game::Doc::Req("start", "number", "Cooldown start time, in seconds as returned by GetTime."),
    Game::Doc::Req("duration", "number", "Cooldown length in seconds."),
    Game::Doc::Opt("modRate", "number", "1",
                   "Scales the end flash and GetCooldownDisplayDuration; the sweep still ends at start + duration."),
};
const Game::Doc::Function kSetCooldown{
    "Shows the frame and runs the cooldown sweep from a start time and duration in seconds, "
    "then the end flash.",
    kSetCooldownArgs, {}};
const Game::Doc::Field kSetCooldownDurationArgs[] = {
    Game::Doc::Req("duration", "number", "Cooldown length in seconds."),
    Game::Doc::Opt("modRate", "number", "1", "As in SetCooldown."),
};
const Game::Doc::Function kSetCooldownDuration{
    "Starts a cooldown of the given length now.", kSetCooldownDurationArgs, {}};
const Game::Doc::Field kSetCooldownFromExpirationTimeArgs[] = {
    Game::Doc::Req("expirationTime", "number", "When the cooldown ends, in seconds as returned by GetTime."),
    Game::Doc::Req("duration", "number", "Cooldown length in seconds."),
    Game::Doc::Opt("modRate", "number", "1", "As in SetCooldown."),
};
const Game::Doc::Function kSetCooldownFromExpirationTime{
    "Starts a cooldown that ends at the given time.", kSetCooldownFromExpirationTimeArgs, {}};
const Game::Doc::Function kClear{
    "Ends the cooldown and hides the frame, without the end flash or OnCooldownDone.", {}, {}};
const Game::Doc::Field kTimesRet[] = {
    Game::Doc::Req("start", "number", "Start time in milliseconds."),
    Game::Doc::Req("duration", "number", "Length in milliseconds."),
};
const Game::Doc::Function kGetCooldownTimes{
    "Returns the timer's start time and length in milliseconds: the cooldown's, then the end "
    "flash's once the cooldown ends. Clear sets both to 0.",
    {}, kTimesRet};
const Game::Doc::Field kDurationRet[] = {
    Game::Doc::Req("duration", "number", "Milliseconds."),
};
const Game::Doc::Function kGetCooldownDuration{
    "Returns the timer's length in milliseconds, as GetCooldownTimes does.", {}, kDurationRet};
const Game::Doc::Function kGetCooldownDisplayDuration{
    "Returns the timer's length in milliseconds divided by its modRate.", {}, kDurationRet};
const Game::Doc::Function kPause{"Stops the cooldown where it is.", {}, {}};
const Game::Doc::Function kResume{"Continues a paused cooldown from where it stopped.", {}, {}};
const Game::Doc::Field kPausedArg[] = {
    Game::Doc::Req("paused", "bool"),
};
const Game::Doc::Function kSetPaused{"Pauses or resumes the cooldown.", kPausedArg, {}};
const Game::Doc::Field kPausedRet[] = {
    Game::Doc::Req("isPaused", "bool"),
};
const Game::Doc::Function kIsPaused{"Returns whether the cooldown is paused.", {}, kPausedRet};
const Game::Doc::Field kBoolArg[] = {
    Game::Doc::Opt("value", "bool", nullptr, "The flag value; strings and numbers are read as in XML."),
};
const Game::Doc::Field kBoolRet[] = {
    Game::Doc::Req("value", "bool", "The current flag value."),
};
const Game::Doc::Function kSetReverse{
    "Sets whether the sweep reveals instead of covers.",
    kBoolArg, {}};
const Game::Doc::Function kGetReverse{
    "Returns whether the sweep reveals instead of covers.", {}, kBoolRet};
const Game::Doc::Function kSetDrawEdge{
    "Sets whether a bright line follows the edge of the sweep.", kBoolArg, {}};
const Game::Doc::Function kGetDrawEdge{
    "Returns whether a bright line follows the edge of the sweep.", {}, kBoolRet};
const Game::Doc::Function kSetDrawSwipe{"Sets whether the dark sweep is drawn.", kBoolArg, {}};
const Game::Doc::Function kGetDrawSwipe{"Returns whether the dark sweep is drawn.", {}, kBoolRet};
const Game::Doc::Function kSetDrawBling{
    "Sets whether the flash plays when the cooldown ends.", kBoolArg, {}};
const Game::Doc::Function kGetDrawBling{
    "Returns whether the flash plays when the cooldown ends.", {}, kBoolRet};
const Game::Doc::Field kColorArgs[] = {
    Game::Doc::Req("r", "number"),
    Game::Doc::Req("g", "number"),
    Game::Doc::Req("b", "number"),
    Game::Doc::Opt("a", "number", "1"),
};
const Game::Doc::Function kSetSwipeColor{"Sets the colour of the sweep.", kColorArgs, {}};
const Game::Doc::Function kSetEdgeColor{
    "Sets the colour of the line on the edge of the sweep.", kColorArgs, {}};

const Game::Doc::Method g_methodDocs[] = {
    {"SetCooldown", &kSetCooldown},
    {"SetReverse", &kSetReverse},
    {"GetReverse", &kGetReverse},
    {"SetDrawEdge", &kSetDrawEdge},
    {"GetDrawEdge", &kGetDrawEdge},
    {"SetCooldownDuration", &kSetCooldownDuration},
    {"SetCooldownFromExpirationTime", &kSetCooldownFromExpirationTime},
    {"Clear", &kClear},
    {"GetCooldownTimes", &kGetCooldownTimes},
    {"GetCooldownDuration", &kGetCooldownDuration},
    {"GetCooldownDisplayDuration", &kGetCooldownDisplayDuration},
    {"Pause", &kPause},
    {"Resume", &kResume},
    {"SetPaused", &kSetPaused},
    {"IsPaused", &kIsPaused},
    {"SetDrawSwipe", &kSetDrawSwipe},
    {"GetDrawSwipe", &kGetDrawSwipe},
    {"SetDrawBling", &kSetDrawBling},
    {"GetDrawBling", &kGetDrawBling},
    {"SetSwipeColor", &kSetSwipeColor},
    {"SetEdgeColor", &kSetEdgeColor},
};

// The engine's static initializer for a class hash (Model's 0x0076F5F0).
void InitMethodHash() {
    if (g_hash[0] != 0)
        return;
    g_hash[0] = Offsets::VTBL_METHOD_HASH_ALLOCATOR;
    g_hash[1] = 0xC; // node link offset
    g_hash[2] = reinterpret_cast<uintptr_t>(&g_hash[2]);
    g_hash[3] = reinterpret_cast<uintptr_t>(&g_hash[2]) | 1;
    g_hash[9] = 0xFFFFFFFF; // no buckets yet
}

void RegisterLuaFunctions() {
    using RegisterFactory_t = int(__fastcall *)(const char *type, void *(__fastcall *)(void *),
                                                int unique);
    // The engine's frame-type table outlives the Lua state, so on a /reload
    // the name is already there and the registrar keeps the first one. This
    // pass runs before FrameXML loads (CGGameUI::Initialize calls
    // LoadScriptFunctions first), like the engine's own game-UI list
    // FUN_00495940, where 4.3.4 registers "Cooldown".
    InitMethodHash();
    reinterpret_cast<RegisterFactory_t>(Offsets::FUN_FRAMEXML_REGISTER_FACTORY)(
        "Cooldown", &CreateCooldown, 0);
    Api::Documentation::NameWidgetSystem(g_hash, "FrameAPICooldown");
    Game::Lua::RegisterFrameMethods(
        g_hash, g_methods, static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])),
        g_methodDocs, static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// The hash's functions are references into the game Lua state, so they are
// released with the engine's own class hashes, at the end of the game UI's
// unregister pass while that state is still open.
using UnregisterScriptFunctions_t = void(__cdecl *)();
using UnregisterMethods_t = void(__fastcall *)(void *hash);
UnregisterScriptFunctions_t UnregisterScriptFunctions_o = nullptr;

void __cdecl UnregisterScriptFunctions_h() {
    UnregisterScriptFunctions_o();
    reinterpret_cast<UnregisterMethods_t>(Offsets::FUN_FRAMESCRIPT_UNREGISTER_METHODS)(g_hash);
}

const Game::HookAutoRegister _unregisterHook{
    Offsets::FUN_GAMEUI_UNREGISTER_SCRIPT_FUNCTIONS,
    reinterpret_cast<void *>(&UnregisterScriptFunctions_h),
    reinterpret_cast<void **>(&UnregisterScriptFunctions_o)};

} // namespace

} // namespace Frame::Cooldown
