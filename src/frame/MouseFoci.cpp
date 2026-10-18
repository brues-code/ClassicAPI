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

// `GetMouseFoci()` -> table of every frame under the cursor, top-first.
//
// Modern WoW exposes GetMouseFoci() alongside GetMouseFocus(), with the
// invariant `GetMouseFocus() == GetMouseFoci()[1]`. Vanilla stores only the
// single topmost focus (Script_GetMouseFocus 0x0048DF40 reads
// `*(*VAR_UI_CONTEXT_PTR + OFF_UI_CONTEXT_MOUSE_FOCUS)`), so this replays the
// engine's own mouse-focus pass, FUN_007660D0, without stopping at the first
// hit:
//
//   - candidates are the mouse input queues (OFF_UI_CONTEXT_EVENT_QUEUES,
//     type SIMPLE_EVENT_MOUSE): exactly the shown, mouse-enabled frames, per
//     strata, already in the engine's priority order;
//   - strata are walked from the top (8) down, as the pass does;
//   - each frame is tested with FUN_FRAME_HIT_TEST against the cursor
//     converted by NDCToDDC — the pass's own test, so hit-rect insets and
//     scroll clipping count. (The pass calls it through the frame's mouse
//     handler, which also starts drags; we call the test directly.)
//
// The engine's stored focus is still forced to index 1: it is the value
// GetMouseFocus returns, and it can lag the cursor until the next mouse move.
//
// Pure query — no hooks, no Lua calls, registered like GetMouseFocus.

#include "Game.h"
#include "Offsets.h"
#include "ui/FrameObject.h"

#include <cstdint>
#include <vector>

namespace Frame::MouseFoci {

namespace {

using NDCToDDC_t = void(__fastcall *)(float *outX, float *outY, float x, float y);
using HitTest_t = int(__thiscall *)(void *frame, const float *point);

int __fastcall Script_GetMouseFoci(void *L) {
    auto *ctx = Game::Read<uint8_t *>(Offsets::VAR_UI_CONTEXT_PTR);
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L); // result at idx 1
    if (ctx == nullptr)
        return 1; // pre-world → empty table

    void *focus = Game::Read<void *>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_FOCUS);

    const float *mouse = Game::Ptr<float>(ctx, Offsets::OFF_UI_CONTEXT_MOUSE_POSITION);
    float point[2] = {0.0f, 0.0f};
    reinterpret_cast<NDCToDDC_t>(Offsets::FUN_COORD_NDC_TO_DDC)(&point[0], &point[1], mouse[0],
                                                                 mouse[1]);

    std::vector<void *> ordered;
    if (focus != nullptr)
        ordered.push_back(focus);
    auto hitTest = reinterpret_cast<HitTest_t>(Offsets::FUN_FRAME_HIT_TEST);
    for (int strata = Offsets::FRAME_STRATA_COUNT - 1; strata >= 0; --strata) {
        const uint8_t *queue =
            ctx + Offsets::OFF_UI_CONTEXT_EVENT_QUEUES +
            (strata + Offsets::FRAME_STRATA_COUNT * Offsets::SIMPLE_EVENT_MOUSE) *
                Offsets::EVENT_QUEUE_STRIDE;
        auto *const *nodes = Game::Read<void *const *>(queue, Offsets::OFF_EVENT_QUEUE_DATA);
        const uint32_t count = Game::Read<uint32_t>(queue, Offsets::OFF_EVENT_QUEUE_COUNT);
        for (uint32_t i = 0; i < count; ++i) {
            void *frame = Game::Read<void *>(nodes[i], 0);
            if (frame != focus && hitTest(frame, point) != 0)
                ordered.push_back(frame);
        }
    }

    for (size_t i = 0; i < ordered.size(); ++i) {
        Game::Lua::PushNumber(L, static_cast<double>(i + 1));
        UI::FrameObject::Push(L, ordered[i]);
        Game::Lua::RawSet(L, 1); // result[i+1] = frame
    }
    return 1;
}

void Register() {
    Game::Lua::RegisterGlobalFunction("GetMouseFoci", &Script_GetMouseFoci);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Frame::MouseFoci
