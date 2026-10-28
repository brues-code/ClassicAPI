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

// `clipChildren` on XML frames, and `frame:SetClipsChildren(clips)` /
// `frame:DoesClipChildren()`.
//
// A frame that clips its children draws every frame under it only inside its
// own rect, and the mouse reaches them only there. Its own regions are not
// clipped.
//
// Retail keeps the clipped frames in their own strata and levels: the talent
// tree draws a button's glow at the button's level - 1, under its own parent,
// and its edges under the buttons, inside a clipping frame. So the clip has to
// apply per level, not by drawing the subtree in one piece the way 1.12's
// ScrollFrame draws its scroll child.
//
// 1.12 draws each level from five layer batches that all the level's frames
// fill. The level rebuild asks each frame for its regions of a layer
// (CALL_FRAME_LEVEL_EMIT_LAYER); that call is redirected so a frame under a
// clipping frame fills a batch of its own instead, one per level batch and
// nearest clipping frame, built with the engine's constructor and begun the
// first time the rebuild reaches it. The level batch gets one render callback
// that draws it, so the clipped regions draw right after the level's own
// regions of that layer. Their order against unclipped regions of the same
// level and layer changes, which the engine leaves to the texture sort anyway.
//
// The callback is the ScrollFrame's draw (0x00787220) with the clip rect in
// place of the ScrollFrame's: it saves the device transforms, intersects the
// rect with the current viewport, sets the orthographic transforms over it,
// narrows the viewport to it, draws the batch and restores. The rect is the
// hit rect of the nearest clipping frame intersected with each clipping frame
// above it, the rect the ScrollFrame clips to. A ScrollFrame inside a
// clipping frame adds its own draw callback to the clip batch, and that draw
// intersects with the narrowed viewport, so nested clips hold.
//
// A clipping frame under a ScrollFrame's scroll child is drawn by the
// ScrollFrame's draw, not by its level: that draw calls each child's own draw
// (CALL_FRAME_DRAW_SELF_CHILD), which is redirected to narrow the viewport to
// a clipping parent's hit rect around it.
//
// The mouse hit test (FUN_FRAME_HIT_TEST) gets the same rects: a point outside
// any clipping ancestor's hit rect misses, the test the engine makes against
// the ancestor of a scrolled frame.
//
// Turning the clip on or off, and moving a frame to another parent, marks the
// layers of every frame under it changed, so the next rebuild moves their
// regions. SetParent alone does not relink a frame whose level and strata
// stay the same.
//
// A frame with an alpha gradient (Frame::AlphaGradient) has its own regions
// and those of every frame under it drawn the same way, from a batch per level
// batch, nearest clipping frame and nearest frame with a gradient. Its draw
// applies the gradients around the clipped draw, and the child draw of a
// ScrollFrame applies a child's own.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "frame/AlphaGradient.h"
#include "frame/ClipChildren.h"
#include "frame/Destruction.h"
#include "frame/ParentChange.h"
#include "xml/FrameLoad.h"

#include <cstdint>
#include <map>
#include <tuple>

namespace Frame::ClipChildren {

namespace {

using EmitLayer_t = void(__fastcall *)(void *frame, void *edx, void *batch, int layer);
using BatchOp_t = void(__fastcall *)(void *batch);
using Callback_t = void(__fastcall *)(void *arg);
using AddCallback_t = void(__fastcall *)(void *batch, void *edx, Callback_t fn, void *arg);
using SetFrameFlag_t = void(__fastcall *)(void *frame, void *edx, uint32_t mask, int on);
using NotifyLayerChanged_t = void(__fastcall *)(void *frame, void *edx, int layer);
using HitTest_t = int(__fastcall *)(void *frame, void *edx, const float *point);
using Matrix_t = void(__fastcall *)(float *m);
using GetViewport_t = void(__fastcall *)(float *minX, float *maxX, float *minY, float *maxY, float *minZ,
                                         float *maxZ);
using SetViewport_t = void(__stdcall *)(float minX, float maxX, float minY, float maxY, float minZ, float maxZ);
using SetupOrtho_t = void(__fastcall *)(const float *rect, const float *point, int unused);
using Convert_t = void(__fastcall *)(float *outX, float *outY, float x, float y);

// A hit rect: {bottom, left, top, right}.
enum { BOTTOM, LEFT, TOP, RIGHT };

// One level batch's regions under one clipping frame and one frame with a
// gradient, either of which may be none.
struct Record {
    alignas(4) uint8_t batch[Offsets::RENDERBATCH_SIZE];
    void *clipRoot;
    void *gradientRoot;
    bool live;
    bool sorted;
};

using Key = std::tuple<const void *, const void *, const void *>;
std::map<Key, Record *> g_records;
int g_clipping = 0; // frames with the flag; nothing to look up while 0

bool Clips(const void *frame) {
    return (Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_CLIPS_CHILDREN) != 0;
}

void *Parent(const void *frame) { return Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT); }

const float *HitRect(const void *frame) {
    return reinterpret_cast<const float *>(static_cast<const uint8_t *>(frame) + Offsets::OFF_FRAME_HIT_RECT);
}

void *NearestRoot(const void *frame) {
    for (void *p = Parent(frame); p != nullptr; p = Parent(p))
        if (Clips(p))
            return p;
    return nullptr;
}

bool Grouping() { return g_clipping != 0 || Frame::AlphaGradient::Any(); }

void __fastcall DrawClipped(void *arg);

bool HasCallback(const void *levelBatch, const Record *r) {
    uintptr_t node = Game::Read<uintptr_t>(levelBatch, Offsets::OFF_RENDERBATCH_CALLBACKS);
    for (; node != 0 && (node & 1) == 0; node = *reinterpret_cast<const uintptr_t *>(node + 4))
        if (*reinterpret_cast<const Callback_t *>(node + 8) == &DrawClipped &&
            *reinterpret_cast<const Record *const *>(node + 0xC) == r)
            return true;
    return false;
}

// The batch `frame`'s regions of this layer go into: the batch under its
// nearest clipping frame and nearest frame with a gradient, begun and hooked
// onto the level batch the first time this rebuild reaches it, or the level
// batch itself.
void *BatchFor(void *frame, void *levelBatch) {
    if (!Grouping())
        return levelBatch;
    void *clipRoot = g_clipping != 0 ? NearestRoot(frame) : nullptr;
    void *gradientRoot = Frame::AlphaGradient::Nearest(frame);
    if (clipRoot == nullptr && gradientRoot == nullptr)
        return levelBatch;
    Record *&r = g_records[Key(levelBatch, clipRoot, gradientRoot)];
    if (r == nullptr) {
        r = new Record{};
        r->clipRoot = clipRoot;
        r->gradientRoot = gradientRoot;
        r->live = true;
        reinterpret_cast<BatchOp_t>(Offsets::FUN_RENDERBATCH_CONSTRUCT)(r->batch);
    }
    if (!HasCallback(levelBatch, r)) {
        reinterpret_cast<BatchOp_t>(Offsets::FUN_RENDERBATCH_BEGIN)(r->batch);
        r->sorted = false;
        reinterpret_cast<AddCallback_t>(Offsets::FUN_RENDERBATCH_ADD_CALLBACK)(levelBatch, nullptr, &DrawClipped,
                                                                                r);
    }
    return r->batch;
}

void __stdcall EmitLayer(void *frame, void *batch, int layer) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    Game::Read<EmitLayer_t>(vtable, Offsets::VTBL_FRAME_EMIT_LAYER)(frame, nullptr, BatchFor(frame, batch), layer);
}

// Replaces the rebuild's `mov eax,[ebx]; push ecx; push esi; mov ecx,ebx;
// call [eax+0x44]`: frame in ebx, level batch in esi, layer in ecx.
__declspec(naked) void EmitLayerThunk() {
    __asm {
        push ecx
        push esi
        push ebx
        call EmitLayer
        ret
    }
}

// The clip rect for frames under `root`: its hit rect intersected with every
// clipping frame's above it. False when one has no valid layout, which
// GetHitRect (0x0076B220) refuses the same way.
bool ClipRect(const void *root, float rect[4]) {
    bool first = true;
    for (const void *p = root; p != nullptr; p = Parent(p)) {
        if (!Clips(p))
            continue;
        if ((Game::Read<uint8_t>(p, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT + Offsets::OFF_LAYOUT_FLAGS) & 1) == 0)
            return false;
        const float *hit = HitRect(p);
        if (first) {
            for (int i = 0; i < 4; ++i)
                rect[i] = hit[i];
            first = false;
            continue;
        }
        if (rect[BOTTOM] < hit[BOTTOM])
            rect[BOTTOM] = hit[BOTTOM];
        if (rect[LEFT] < hit[LEFT])
            rect[LEFT] = hit[LEFT];
        if (hit[TOP] < rect[TOP])
            rect[TOP] = hit[TOP];
        if (hit[RIGHT] < rect[RIGHT])
            rect[RIGHT] = hit[RIGHT];
    }
    return !first;
}

// The ScrollFrame's draw (0x00787220) with `rect` in place of its hit rect
// and `draw(arg)` in place of its scroll child's.
void DrawInside(float rect[4], Callback_t draw, void *arg) {
    float projection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_PROJECTION)(projection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_GET_VIEW)(view);
    float vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ;
    reinterpret_cast<GetViewport_t>(Offsets::FUN_GX_GET_VIEWPORT)(&vpMinX, &vpMaxX, &vpMinY, &vpMaxY, &vpMinZ,
                                                                 &vpMaxZ);

    const auto toDdc = reinterpret_cast<Convert_t>(Offsets::FUN_COORD_NDC_TO_DDC);
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    toDdc(&minX, &minY, vpMinX, vpMinY);
    toDdc(&maxX, &maxY, vpMaxX, vpMaxY);
    if (minX > rect[LEFT])
        rect[LEFT] = minX;
    if (maxX < rect[RIGHT])
        rect[RIGHT] = maxX;
    if (minY > rect[BOTTOM])
        rect[BOTTOM] = minY;
    if (maxY < rect[TOP])
        rect[TOP] = maxY;

    const float point[2] = {0, 0};
    reinterpret_cast<SetupOrtho_t>(Offsets::FUN_SETUP_ORTHO_TRANSFORMS)(rect, point, 0);
    const auto toNdc = reinterpret_cast<Convert_t>(Offsets::FUN_COORD_DDC_TO_NDC);
    toNdc(&rect[LEFT], &rect[BOTTOM], rect[LEFT], rect[BOTTOM]);
    toNdc(&rect[RIGHT], &rect[TOP], rect[RIGHT], rect[TOP]);
    if (rect[LEFT] < 0.0f) {
        rect[RIGHT] -= rect[LEFT];
        rect[LEFT] = 0.0f;
    }
    if (rect[RIGHT] > 1.0f) {
        rect[LEFT] -= rect[RIGHT] - 1.0f;
        rect[RIGHT] = 1.0f;
    }
    if (rect[BOTTOM] < 0.0f) {
        rect[TOP] -= rect[BOTTOM];
        rect[BOTTOM] = 0.0f;
    }
    if (rect[TOP] > 1.0f) {
        rect[BOTTOM] -= rect[TOP] - 1.0f;
        rect[TOP] = 1.0f;
    }
    if (rect[LEFT] < rect[RIGHT] && rect[BOTTOM] < rect[TOP]) {
        reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(rect[LEFT], rect[RIGHT], rect[BOTTOM],
                                                                      rect[TOP], 0.0f, 1.0f);
        draw(arg);
    }

    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_PROJECTION)(projection);
    reinterpret_cast<Matrix_t>(Offsets::FUN_GX_SET_VIEW)(view);
    reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_SET_VIEWPORT)(vpMinX, vpMaxX, vpMinY, vpMaxY, vpMinZ, vpMaxZ);
}

void __fastcall DrawClipped(void *arg) {
    Record *r = static_cast<Record *>(arg);
    float rect[4];
    if (!r->live || (r->clipRoot != nullptr && !ClipRect(r->clipRoot, rect)))
        return;
    if (!r->sorted) {
        reinterpret_cast<BatchOp_t>(Offsets::FUN_RENDERBATCH_SORT)(r->batch);
        r->sorted = true;
    }
    const auto render = reinterpret_cast<Callback_t>(Offsets::FUN_FRAME_LAYER_RENDER);
    const int stages = Frame::AlphaGradient::Begin(r->gradientRoot, true);
    if (r->clipRoot != nullptr)
        DrawInside(rect, render, r->batch);
    else
        render(r->batch);
    Frame::AlphaGradient::End(stages);
}

void __fastcall DrawFrame(void *frame) {
    const void *vtable = Game::Read<const void *>(frame, 0);
    Game::Read<Callback_t>(vtable, Offsets::VTBL_FRAME_DRAW_SELF)(frame);
}

// A frame a ScrollFrame draws draws each child through its own draw
// (CALL_FRAME_DRAW_SELF_CHILD). A child of a clipping frame draws inside the
// clipping frame's hit rect; the viewport already holds every clip above it.
// A child with a gradient draws with it; the gradients above it already apply.
void __stdcall DrawChild(void *child) {
    const void *parent = Parent(child);
    const int stages = Frame::AlphaGradient::Begin(child, false);
    if (g_clipping == 0 || parent == nullptr || !Clips(parent)) {
        DrawFrame(child);
    } else if ((Game::Read<uint8_t>(parent, Offsets::OFF_FRAME_LAYOUT_SUBOBJECT + Offsets::OFF_LAYOUT_FLAGS) &
                1) != 0) {
        float rect[4];
        for (int i = 0; i < 4; ++i)
            rect[i] = HitRect(parent)[i];
        DrawInside(rect, &DrawFrame, child);
    }
    Frame::AlphaGradient::End(stages);
}

// Replaces `mov edx,[ecx]; call [edx+0x40]`: the child in ecx.
__declspec(naked) void DrawChildThunk() {
    __asm {
        push ecx
        call DrawChild
        ret
    }
}

HitTest_t g_origHitTest = nullptr;

// FUN_FRAME_HIT_TEST, then each clipping ancestor's hit rect with the edges
// the engine tests: left and bottom inside, right and top outside.
int __fastcall HitTest_h(void *frame, void *edx, const float *point) {
    const int hit = g_origHitTest(frame, edx, point);
    if (hit == 0 || g_clipping == 0)
        return hit;
    for (const void *p = Parent(frame); p != nullptr; p = Parent(p)) {
        if (!Clips(p))
            continue;
        const float *rect = HitRect(p);
        if (point[0] < rect[LEFT] || rect[RIGHT] <= point[0] || point[1] < rect[BOTTOM] || rect[TOP] <= point[1])
            return 0;
    }
    return hit;
}

const Game::HookAutoRegister _hitTestHook{Offsets::FUN_FRAME_HIT_TEST, reinterpret_cast<void *>(&HitTest_h),
                                          reinterpret_cast<void **>(&g_origHitTest)};

void MarkLayers(void *frame) {
    for (int layer = 0; layer < Offsets::FRAME_LAYER_COUNT; ++layer)
        reinterpret_cast<NotifyLayerChanged_t>(Offsets::FUN_FRAME_NOTIFY_LAYER_CHANGED)(frame, nullptr, layer);
}

// Marks every layer of each frame under `frame` changed.
void MarkSubtree(void *frame) {
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST, [](void *child) {
        MarkLayers(child);
        MarkSubtree(child);
    });
}

void Set(void *frame, bool on) {
    if (Clips(frame) == on)
        return;
    reinterpret_cast<SetFrameFlag_t>(Offsets::FUN_FRAME_SET_FRAME_FLAG)(
        frame, nullptr, Offsets::FRAME_FLAG_CLIPS_CHILDREN, on);
    g_clipping += on ? 1 : -1;
    // A gradient draws only while its frame clips, its own regions included.
    if ((Game::Read<uint32_t>(frame, Offsets::OFF_FRAME_FLAGS) & Offsets::FRAME_FLAG_ALPHA_GRADIENT) != 0)
        MarkLayers(frame);
    MarkSubtree(frame);
}

// Its batches, when a clipping frame or a frame with a gradient goes. They are
// emptied (their quads point into regions about to be freed) and kept: a level
// batch may still hold their callback until its next rebuild, which makes it a
// no-op.
void OnFrameDestroyed(void *frame) {
    if (Clips(frame))
        --g_clipping;
    for (auto it = g_records.begin(); it != g_records.end();) {
        if (it->second->clipRoot != frame && it->second->gradientRoot != frame) {
            ++it;
            continue;
        }
        it->second->live = false;
        reinterpret_cast<BatchOp_t>(Offsets::FUN_RENDERBATCH_BEGIN)(it->second->batch);
        it = g_records.erase(it);
    }
}

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

void OnParentChanged(void *frame) {
    if (!Grouping())
        return;
    MarkLayers(frame);
    MarkSubtree(frame);
}

const Frame::ParentChange::AutoSubscribe _parentSub{&OnParentChanged};

void OnFrameNode(void *object, const void *node, void * /*status*/) {
    const char *value = Xml::Attr(node, "clipChildren");
    if (value == nullptr || *value == '\0')
        return;
    Set(object, Game::StringToFlag(value));
}

const Xml::FrameLoad::AutoSubscribe _xmlSub{&OnFrameNode};

int __fastcall Script_SetClipsChildren(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Set(frame, Game::Lua::ToFlag(L, 2));
    return 0;
}

int __fastcall Script_DoesClipChildren(void *L) {
    void *frame = Game::Lua::ResolveFrame(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, Clips(frame));
    return 1;
}

const Game::Lua::FrameMethodEntry g_methods[] = {
    {"SetClipsChildren", &Script_SetClipsChildren},
    {"DoesClipChildren", &Script_DoesClipChildren},
};

const Game::Doc::Field kSetArgs[] = {
    Game::Doc::Req("clipsChildren", "boolean",
                   "When true, the frame's child frames draw and take the mouse only inside the frame."),
};
const Game::Doc::Field kDoesRets[] = {
    Game::Doc::Req("clipsChildren", "boolean", "True when the frame clips its child frames."),
};
const Game::Doc::Function kSetClipsChildren{"Sets whether the frame clips its child frames to its own rect.",
                                            kSetArgs, {}};
const Game::Doc::Function kDoesClipChildren{"Returns whether the frame clips its child frames to its own rect.",
                                            {}, kDoesRets};

const Game::Doc::Method g_methodDocs[] = {
    {"SetClipsChildren", &kSetClipsChildren},
    {"DoesClipChildren", &kDoesClipChildren},
};

const uint8_t kEmitLayerCall[] = {0x8B, 0x03, 0x51, 0x56, 0x8B, 0xCB, 0xFF, 0x50, 0x44};
const uint8_t kDrawChildCall[] = {0x8B, 0x11, 0xFF, 0x52, 0x40};

void RegisterLuaFunctions() {
    Common::ReplaceWithCall(Offsets::CALL_FRAME_LEVEL_EMIT_LAYER, kEmitLayerCall, sizeof kEmitLayerCall,
                            reinterpret_cast<const void *>(&EmitLayerThunk));
    Common::ReplaceWithCall(Offsets::CALL_FRAME_DRAW_SELF_CHILD, kDrawChildCall, sizeof kDrawChildCall,
                            reinterpret_cast<const void *>(&DrawChildThunk));
    Game::Lua::RegisterFrameMethods(
        reinterpret_cast<void *>(Offsets::VAR_FRAME_METHOD_REGISTRY), g_methods,
        static_cast<int>(sizeof(g_methods) / sizeof(g_methods[0])), g_methodDocs,
        static_cast<int>(sizeof(g_methodDocs) / sizeof(g_methodDocs[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Regroup(void *frame) {
    MarkLayers(frame);
    MarkSubtree(frame);
}

} // namespace Frame::ClipChildren
