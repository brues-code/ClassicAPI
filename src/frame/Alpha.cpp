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

// Frame alpha as 3.x draws it: a frame's own alpha times its parent's.
//
// Vanilla keeps one alpha byte per frame (Offsets::OFF_FRAME_ALPHA) and copies
// it down: SetAlpha - and the XML `alpha` attribute, which runs the same setter
// - writes the value into every child frame that exists right then, replacing
// the child's own, and SetParent takes nothing from the new parent. So:
//   - a frame built or parented inside one that is already transparent draws
//     at full alpha. Zygor Guides Viewer's border glow is an alpha="0.0" frame
//     holding its glow textures in a child; on vanilla the glow shows at full
//     strength from login until its first flash sets the parent's alpha again;
//   - a child's own alpha is lost the moment its parent's changes;
//   - GetAlpha on a child reports whatever its parent last copied in, so an
//     animation fading a child starts from its parent's value.
// 3.x keeps each frame's own alpha and draws it multiplied by its parent's.
//
// Co-hooks of the setter (FUN_FRAME_SET_FRAME_ALPHA), SetParent
// (FUN_FRAME_SET_PARENT) and frame GetAlpha, and a Frame::Destruction listener:
//   - SetAlpha and XML `alpha` record the frame's own alpha;
//   - the byte holds own x the parent's drawn alpha. When it changes, the
//     frame's regions recolor and each child is recomputed from its own alpha,
//     through the child's own vtable setter, as the engine's copy-down calls
//     it - so the Model family's override, which hands the byte to its model,
//     still runs;
//   - SetParent recomputes the frame and everything under it from the new
//     parent. The frame constructor calls it too, after installing the base
//     vtable and initializing the byte and both lists;
//   - GetAlpha returns the own alpha.
// A frame whose alpha was never set has own alpha 1.
//
// Engine code that reads the byte directly gets the drawn value. That's what
// the region recolors and the Model want. The chat bubble and nameplate fades
// compare it with their own targets. They're WorldFrame children, where drawn
// and own are the same while WorldFrame is opaque.
//
// Default nameplates are exempt: the plate itself (IsNamePlate, by vtable)
// and anything parented under one (UnderNamePlate). The engine fades plates
// itself, and nameplate addons layer their own frames over the plate and
// manage alpha the 1.12.1 way — a child's own SetAlpha is absolute there.
// The plate runs an emulated setter (DrawPlate): byte, recolor, copy-down,
// except children with an explicitly-set alpha keep it, so an overlay the
// addon set is never stomped by a later engine fade the addon's write-once
// cache won't repair. The plate's own writes of the legacy constants (0x7F
// dimmed, 0xFF target) resolve through the `nameplateNotSelectedAlpha` /
// `nameplateSelectedAlpha` CVars (defaults 0.5 / 1, byte-identical).

#include "frame/Alpha.h"

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "frame/Destruction.h"

#include <cstdint>
#include <unordered_map>

namespace Frame::Alpha {

namespace {

using SetFrameAlpha_t = void(__thiscall *)(void *frame, uint32_t alpha);
using SetParent_t = void(__thiscall *)(void *frame, void *parent);
using Recolor_t = void(__thiscall *)(void *region, int arg);
using ScriptFn_t = int(__fastcall *)(void *L);

SetFrameAlpha_t g_origSetAlpha = nullptr;
SetParent_t g_origSetParent = nullptr;
ScriptFn_t g_origGetAlpha = nullptr;
CVar::Factory::Handle g_dimCVar = nullptr;
CVar::Factory::Handle g_selCVar = nullptr;
// Set while running the original setter body directly: its internal
// recursion reaches these hooks through the vtable, and those propagated
// writes must not record an "explicit" alpha.
bool g_inOriginal = false;

void __fastcall SetFrameAlpha_h(void *frame, void *edx, uint32_t alpha);

// Own alpha of every live frame that set one, including 255: presence (not
// the value) marks "explicitly managed", which is what a plate fade consults
// before overwriting a child. Every frame class's destructor runs
// FUN_FRAME_DESTRUCTOR, whose listener drops the frame's entry, so a frame
// that outlives a /reload keeps its own alpha.
std::unordered_map<const void *, uint8_t> g_own;

// Set while a frame is being given its drawn alpha through its vtable setter.
bool g_propagating = false;
int g_depth = 0;

// Deeper than any real frame tree; stops a corrupt child list from recursing.
constexpr int kMaxDepth = 64;

uint8_t *DrawnByte(void *frame) {
    return static_cast<uint8_t *>(frame) + Offsets::OFF_FRAME_ALPHA;
}

uint8_t Own(const void *frame) {
    const auto it = g_own.find(frame);
    return it == g_own.end() ? 255 : it->second;
}

void SetOwn(const void *frame, uint8_t alpha) {
    g_own[frame] = alpha;
}

// Truncating, as 3.3.5's CSimpleFrame::GetEffectiveAlpha (0x00482E60) and
// UpdateAlpha (0x00491680) compute `m_alpha * m_parentAlpha / 255`.
uint8_t Mul(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(static_cast<uint32_t>(a) * b / 255);
}

uint8_t DrawnFor(void *frame, uint8_t own) {
    void *parent = Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT);
    return parent != nullptr ? Mul(own, *DrawnByte(parent)) : own;
}

// A live default nameplate, by its vtable pointer
// (VAR_NAMEPLATE_VTABLE).
bool IsNamePlate(const void *frame) {
    return *reinterpret_cast<void *const *>(frame) ==
           reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_NAMEPLATE_VTABLE));
}

// Anything parented under a plate, at any depth. Nameplate addons layer
// their own frames over the default plate (a `CreateFrame` Button whose
// parent is the plate) and manage alpha the 1.12.1 way, where a child's
// own SetAlpha is absolute — multiplication would glue the overlay to the
// engine-dimmed plate underneath. The whole subtree keeps vanilla
// semantics; only the plate itself additionally resolves the legacy
// constants through the CVars.
bool UnderNamePlate(const void *frame) {
    const void *f =
        Game::Read<void *>(frame, Offsets::OFF_REGION_PARENT);
    for (int i = 0; i < kMaxDepth && f != nullptr; ++i) {
        if (IsNamePlate(f))
            return true;
        f = Game::Read<void *>(f, Offsets::OFF_REGION_PARENT);
    }
    return false;
}

// The legacy constants the plate fade writes (OnUpdate): 0x7F dimmed,
// 0xFF for the target.
constexpr uint8_t kLegacyDim = 0x7F;
constexpr uint8_t kLegacyFull = 0xFF;

// A 0-to-1 CVar as a byte. The defaults truncate to the legacy constants
// (0.5 → 0x7F, 1.0 → 0xFF), so untouched installs are byte-identical.
uint8_t CVarByte(CVar::Factory::Handle cvar, float fallback) {
    float v = CVar::Factory::GetFloat(cvar, fallback);
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    return static_cast<uint8_t>(v * 255.0f);
}

uint8_t DimByte() { return CVarByte(g_dimCVar, 0.5f); }
uint8_t SelectedByte() { return CVarByte(g_selCVar, 1.0f); }

// Gives `frame` its drawn alpha through its own vtable setter.
void ApplyDrawn(void *frame, uint8_t drawn) {
    if (*DrawnByte(frame) == drawn || g_depth >= kMaxDepth)
        return;
    const bool saved = g_propagating;
    g_propagating = true;
    ++g_depth;
    void **vtable = *static_cast<void ***>(frame);
    reinterpret_cast<SetFrameAlpha_t>(
        vtable[Offsets::VTBL_FRAME_SET_FRAME_ALPHA / sizeof(void *)])(frame, drawn);
    --g_depth;
    g_propagating = saved;
}

// The engine setter's body with `drawn` in place of the copied value: an
// unchanged byte returns early, otherwise the regions recolor and each child
// is redrawn from its own alpha. SetFrameAlpha_h runs it in place of the
// original, which would copy the value down.
void Draw(void *frame, uint8_t drawn) {
    uint8_t *byte = DrawnByte(frame);
    if (*byte == drawn)
        return;
    *byte = drawn;
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST, [](void *region) {
        void **vtable = *static_cast<void ***>(region);
        reinterpret_cast<Recolor_t>(
            vtable[Offsets::VTBL_REGION_RECOLOR / sizeof(void *)])(region, 0);
    });
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST,
                [drawn](void *child) {
                    if (IsNamePlate(child) || UnderNamePlate(child)) {
                        // Into the plate subtree: run the hook body so plates
                        // substitute and overlays stay verbatim. Flagged: this
                        // copy-down must not record.
                        const bool saved = g_inOriginal;
                        g_inOriginal = true;
                        SetFrameAlpha_h(child, nullptr, drawn);
                        g_inOriginal = saved;
                        return;
                    }
                    ApplyDrawn(child, Mul(Own(child), drawn));
                });
}

// The original setter's body for a plate: byte, recolor, then copy-down —
// except children with an explicitly-set alpha keep it. Vanilla would
// overwrite them, which stomps addon overlays every engine fade while the
// addon's write-once cache never repairs them. The engine's own plate parts
// never set one, so they still follow the fade exactly as vanilla.
void DrawPlate(void *frame, uint8_t drawn) {
    uint8_t *byte = DrawnByte(frame);
    if (*byte == drawn)
        return;
    *byte = drawn;
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_REGION_LIST, [](void *region) {
        void **vtable = *static_cast<void ***>(region);
        reinterpret_cast<Recolor_t>(
            vtable[Offsets::VTBL_REGION_RECOLOR / sizeof(void *)])(region, 0);
    });
    Game::ForEachFrameListObject(frame, Offsets::OFF_FRAME_CHILD_LIST,
                [drawn](void *child) {
                    if (g_own.find(child) != g_own.end())
                        return; // explicitly managed: never inherits the fade
                    const bool saved = g_inOriginal;
                    g_inOriginal = true;
                    g_origSetAlpha(child, drawn);
                    g_inOriginal = saved;
                });
}

void __fastcall SetFrameAlpha_h(void *frame, void * /*edx*/, uint32_t alpha) {
    const uint8_t value = static_cast<uint8_t>(alpha);
    if (IsNamePlate(frame)) {
        // Plate fade, with one substitution per legacy constant: dim writes
        // resolve through `nameplateNotSelectedAlpha`, full writes through
        // `nameplateSelectedAlpha`. Nothing is recorded for the plate itself
        // and nothing multiplies.
        uint8_t out = value;
        if (value == kLegacyDim)
            out = DimByte();
        else if (value == kLegacyFull)
            out = SelectedByte();
        DrawPlate(frame, out);
        return;
    }
    if (UnderNamePlate(frame)) {
        // Pure vanilla, plus a record: the overlay's own SetAlpha is
        // absolute, and the record is what later plate fades consult. (A
        // write of exactly 0x7F/0xFF here is the addon's own choice and
        // passes through untouched — only the plate's own writes resolve the
        // CVars.) Propagated copy-down writes arrive with g_inOriginal set
        // and must not record.
        if (!g_inOriginal)
            SetOwn(frame, value);
        const bool saved = g_inOriginal;
        g_inOriginal = true;
        g_origSetAlpha(frame, value);
        g_inOriginal = saved;
        return;
    }
    if (g_propagating) {
        // Reached from ApplyDrawn: `value` is already the drawn alpha.
        Draw(frame, value);
        return;
    }
    SetOwn(frame, value);
    Draw(frame, DrawnFor(frame, value));
}

void __fastcall SetParent_h(void *frame, void * /*edx*/, void *parent) {
    g_origSetParent(frame, parent);
    if (IsNamePlate(frame) || UnderNamePlate(frame))
        return; // vanilla: a new parent contributes no alpha
    ApplyDrawn(frame, DrawnFor(frame, Own(frame)));
}

void OnFrameDestroyed(void *frame) { g_own.erase(frame); }

const Frame::Destruction::AutoSubscribe _destructionSub{&OnFrameDestroyed};

// frame:GetAlpha() - the own alpha, not the drawn one.
int __fastcall GetAlpha_h(void *L) {
    const int n = g_origGetAlpha(L);
    if (n != 1)
        return n;
    const void *frame = Game::Lua::ResolveObject(L, 1);
    if (frame == nullptr || IsNamePlate(frame) || UnderNamePlate(frame))
        return n; // the plate subtree reports the byte, as vanilla does
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushNumber(L, Own(frame) / 255.0);
    return 1;
}

const Game::HookAutoRegister _setAlphaHook{Offsets::FUN_FRAME_SET_FRAME_ALPHA,
                                           reinterpret_cast<void *>(&SetFrameAlpha_h),
                                           reinterpret_cast<void **>(&g_origSetAlpha)};
const Game::HookAutoRegister _setParentHook{Offsets::FUN_FRAME_SET_PARENT,
                                            reinterpret_cast<void *>(&SetParent_h),
                                            reinterpret_cast<void **>(&g_origSetParent)};
const Game::HookAutoRegister _getAlphaHook{Offsets::FUN_SCRIPT_FRAME_GETALPHA,
                                           reinterpret_cast<void *>(&GetAlpha_h),
                                           reinterpret_cast<void **>(&g_origGetAlpha)};

void RegisterCVar() {
    g_dimCVar = CVar::Factory::Register(
        "nameplateNotSelectedAlpha",
        "The alpha of a nameplate whose unit is not the target, from 0 to 1",
        Offsets::CVAR_CATEGORY_GAME, "0.5");
    g_selCVar = CVar::Factory::Register(
        "nameplateSelectedAlpha",
        "The alpha of a nameplate whose unit is the target, from 0 to 1",
        Offsets::CVAR_CATEGORY_GAME, "1");
}

const Game::ModuleAutoRegister _autoreg{&RegisterCVar};

} // namespace

uint8_t OwnAlpha(const void *frame) { return Own(frame); }

} // namespace Frame::Alpha
