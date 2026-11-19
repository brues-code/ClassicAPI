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

// The soft-target tooltip — retail's "Show Target Tooltip": the
// SoftTargetTooltipInteract / Locked / Enemy / Friend / DurationMs cvars, as
// the 3.80.2 Classic client runs them each frame (FUN_1405e1f00):
//   - only while the world mouseover is empty (a hovered object owns the
//     tooltip); with one, the soft tooltip is forgotten;
//   - the first of the soft interact object, the target, the soft enemy and
//     the soft friend whose cvar is on and which exists is the one;
//   - a new one gets its tooltip, as the mouseover would show it;
//   - SoftTargetTooltipDurationMs after that it fades (the tooltip's own
//     fade: at once at the cursor, else over 2 s); 0 keeps it;
//   - with none, a shown one is hidden.
// The tooltip is GameTooltip, shown the way 1.12's mouseover publisher
// (0x00492890) shows it: OnTooltipSetDefaultAnchor — or, for a game object
// whose floating-tooltip test holds, UIParent at the cursor — then the unit
// or game object builder.
// The fade and the hide touch the tooltip only while it still shows the
// object this set — GameTooltip is shared, and another frame may own it now.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "object/Resolve.h"
#include "target/Reason.h"
#include "target/SoftInteract.h"
#include "target/SoftUnit.h"
#include "tick/FrameTick.h"
#include "time/Clock.h"

#include <cstdint>

namespace Target::SoftTargetTooltip {

namespace {

using InvokeScript_t = void(__fastcall *)(int handler, void *self);
using Build_t = int(__thiscall *)(void *tooltip, uint32_t *guid);
using TooltipFn_t = void(__thiscall *)(void *tooltip);
using SetOwner_t = void(__thiscall *)(void *tooltip, void *ownerLayout, int anchor, float x,
                                      float y);
using Floating_t = int(__thiscall *)(void *object);

CVar::Factory::Handle g_interact = nullptr;
CVar::Factory::Handle g_locked = nullptr;
CVar::Factory::Handle g_enemy = nullptr;
CVar::Factory::Handle g_friend = nullptr;
CVar::Factory::Handle g_duration = nullptr;

uint64_t g_shown = 0;     // the object the tooltip was set for
bool g_fading = false;    // its fade is still to come
uint32_t g_shownAt = 0;   // when it was set

void *Tooltip() { return Game::Read<void *>(Offsets::VAR_GAMETOOLTIP_OBJECT_PTR); }

bool StillOurs(void *tooltip) {
    return Game::Read<uint64_t>(tooltip, Offsets::OFF_TOOLTIP_UNIT_GUID_LO) == g_shown ||
           Game::Read<uint64_t>(tooltip, Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_LO) == g_shown;
}

bool Floating(void *object) {
    const auto *vtable = Game::Read<const uintptr_t *>(object, 0);
    return reinterpret_cast<Floating_t>(
               vtable[Offsets::OFF_CGOBJECT_VTBL_FLOATING_TOOLTIP / sizeof(uintptr_t)])(object) != 0;
}

void Show(void *tooltip, uint64_t guid) {
    const bool unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) != nullptr;
    void *gameObject = unit ? nullptr : Object::ByGuid(Offsets::TYPEMASK_GAMEOBJECT, guid);
    if (!unit && gameObject == nullptr)
        return;
    if (gameObject != nullptr && Floating(gameObject)) {
        auto *uiParent = Game::Read<uint8_t *>(Offsets::VAR_UI_PARENT);
        reinterpret_cast<SetOwner_t>(Offsets::FUN_GAMETOOLTIP_SET_OWNER)(
            tooltip, uiParent ? uiParent + Offsets::OFF_FRAME_LAYOUT_SUBOBJECT : nullptr,
            Offsets::TOOLTIP_ANCHOR_CURSOR, 0.0f, 0.0f);
    } else {
        const int anchor =
            Game::Read<int>(tooltip, Offsets::OFF_TOOLTIP_SET_DEFAULT_ANCHOR_HANDLER);
        if (anchor != 0)
            reinterpret_cast<InvokeScript_t>(Offsets::FUN_FRAME_INVOKE_SCRIPT)(anchor, tooltip);
    }
    uint32_t packed[2] = {static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32)};
    reinterpret_cast<Build_t>(unit ? Offsets::FUN_GAMETOOLTIP_BUILD_UNIT
                                   : Offsets::FUN_GAMETOOLTIP_BUILD_GAMEOBJECT)(tooltip, packed);
}

void Hide(void *tooltip) {
    const auto *vtable = Game::Read<const uintptr_t *>(tooltip, 0);
    reinterpret_cast<TooltipFn_t>(vtable[Offsets::VTBL_FRAME_HIDE / sizeof(uintptr_t)])(tooltip);
}

void Forget() {
    g_shown = 0;
    g_fading = false;
}

void OnFrameTick() {
    void *tooltip = Tooltip();
    if (tooltip == nullptr || !Object::RegistryReady() ||
        Game::Read<uint64_t>(Offsets::VAR_MOUSEOVER_GUID_LO) != 0) {
        Forget();
        return;
    }
    const struct {
        CVar::Factory::Handle cvar;
        uint64_t guid;
    } sources[] = {
        {g_interact, SoftInteract::Get()},
        {g_locked, Reason::Selection()},
        {g_enemy, SoftUnit::Get(SoftUnit::Side::Enemy)},
        {g_friend, SoftUnit::Get(SoftUnit::Side::Friend)},
    };
    const uint32_t now = Time::Clock::NowMs();
    for (const auto &source : sources) {
        if (CVar::Factory::GetInt(source.cvar) == 0 || source.guid == 0)
            continue;
        if (source.guid != g_shown) {
            g_shown = source.guid;
            g_shownAt = now;
            g_fading = true;
            Show(tooltip, source.guid);
            return;
        }
        const int duration = CVar::Factory::GetInt(g_duration);
        if (!g_fading || duration <= 0 ||
            !Time::Clock::Reached(now, g_shownAt + static_cast<uint32_t>(duration)))
            return;
        g_fading = false;
        if (StillOurs(tooltip))
            reinterpret_cast<TooltipFn_t>(Offsets::FUN_GAMETOOLTIP_FADE_OUT)(tooltip);
        return;
    }
    if (g_shown != 0 && StillOurs(tooltip))
        Hide(tooltip);
    Forget();
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrameTick};

void Register() {
    const auto reg = [](const char *name, const char *value) {
        return CVar::Factory::Register(name, "", Offsets::CVAR_CATEGORY_GAME, value);
    };
    g_interact = reg("SoftTargetTooltipInteract", "0");
    g_locked = reg("SoftTargetTooltipLocked", "0");
    g_enemy = reg("SoftTargetTooltipEnemy", "0");
    g_friend = reg("SoftTargetTooltipFriend", "0");
    g_duration = reg("SoftTargetTooltipDurationMs", "2000");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::SoftTargetTooltip
