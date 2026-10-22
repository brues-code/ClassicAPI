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

// The `autoStand`, `autoDismount` and `autoUnshift` cvars — 4.3.4's
// stand / dismount / leave-form before a cast.
//
// 4.3.4 splits it in two. The caster check (CGUnit_C::CheckCasterState-
// ForSpell's mount, stand and form rungs) refuses only when the matching
// `CGUnit_C::CanAutoStand/CanAutoDismount/CanAutoUnshift` is false; then,
// with every rung passed, the cast path (Spell_C.cpp, before the send)
// stands, dismounts and cancels the form in that order. A form refusal
// for SPELL_FAILED_ONLY_SHAPESHIFT is never auto-handled. Spell_C_Is-
// UsableAction honors CanAutoUnshift the same way, so the button stays lit.
//
// 1.12's caster check (FUN_CHECK_CASTER_STATE_FOR_SPELL) has the same three
// rungs and refuses unconditionally. We mirror the split on them:
//   - the mount rung's descriptor read and the stand rung's GetStandState
//     call become calls that report "not mounted" / "standing" when the
//     auto action is allowed, noting it as pending;
//   - both calls to the form check (caster check and FUN_SPELL_IS_USABLE)
//     pass a SPELL_FAILED_NOT_SHAPESHIFT refusal when CanAutoUnshift holds;
//   - the cast request's one call of the caster check runs the pending
//     actions when it passes, before the request sends the cast.
// The actions are the engine's own: the stand setter, and CMSG_CANCEL_AURA
// for the mount / form aura (1.12 has no CMSG_CANCEL_MOUNT_AURA; cancelling
// the mount aura is how its own UI dismounts). The server applies the
// cancel / stand before the cast packet that follows it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"
#include "dbc/Lookup.h"
#include "spell/Cancel.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Spell::AutoPrepare {

namespace {

using CheckCasterState_t = char(__fastcall *)(void *caster, void *edx,
                                              const uint8_t *record, void *item);
using CheckForm_t = char(__fastcall *)(void *unit, void *edx,
                                       const uint8_t *record, int *reason);
using GetStandState_t = int(__fastcall *)(const void *unit, void *edx);
using SendStandState_t = void(__fastcall *)(const void *player, void *edx, uint32_t state);

constexpr int kGetStandStateSlot = 0xA4 / 4;

CVar::Factory::Handle g_autoStand = nullptr;
CVar::Factory::Handle g_autoDismount = nullptr;
CVar::Factory::Handle g_autoUnshift = nullptr;

// Set by the rungs during one caster check, run when it passes.
struct Pending {
    bool stand;
    bool dismount;
    bool unshift;
};
Pending g_pending{};

const uint8_t *Fields(const void *unit) {
    return *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(unit) + Offsets::OFF_UNIT_DESCRIPTOR);
}

bool IsActivePlayer(const void *unit) {
    return unit != nullptr && unit == Unit::Identity::PlayerObject();
}

bool OnTaxi(const void *unit) {
    return (Game::Read<uint32_t>(Fields(unit), Offsets::OFF_UNIT_FIELD_FLAGS) &
            Offsets::UNIT_FLAG_TAXI_FLIGHT) != 0;
}

int Form(const void *unit) {
    return (Game::Read<uint32_t>(Fields(unit), Offsets::OFF_UNIT_FIELD_BYTES_1) >> 16) & 0xFF;
}

bool Enabled(CVar::Factory::Handle cvar) { return CVar::Factory::GetInt(cvar) != 0; }

// 4.3.4 CGUnit_C::CanAutoStand.
bool CanAutoStand(const void *unit) { return IsActivePlayer(unit) && Enabled(g_autoStand); }

// 4.3.4 CGUnit_C::CanAutoDismount (no flying in 1.12, so no
// autoDismountFlying leg).
bool CanAutoDismount(const void *unit) {
    return IsActivePlayer(unit) && !OnTaxi(unit) && Enabled(g_autoDismount);
}

// 4.3.4 CGUnit_C::CanAutoUnshift.
bool CanAutoUnshift(const void *unit) {
    if (!IsActivePlayer(unit) || OnTaxi(unit) || !Enabled(g_autoUnshift))
        return false;
    const uint8_t *form = DBC::Record(Offsets::VAR_SPELL_SHAPESHIFT_FORM_RECORDS,
                                      Offsets::VAR_SPELL_SHAPESHIFT_FORM_COUNT,
                                      static_cast<uint32_t>(Form(unit)));
    return form == nullptr ||
           (Game::Read<uint32_t>(form, Offsets::OFF_SPELL_SHAPESHIFT_FORM_FLAGS) &
            Offsets::SHAPESHIFT_FORM_FLAG_NO_AUTO_UNSHIFT) == 0;
}

// The mount rung's read: the mount display, or 0 when the cast will
// dismount first.
int __fastcall MountDisplayForCast(const void *caster) {
    const int display = Game::Read<int>(Fields(caster), Offsets::OFF_UNIT_FIELD_MOUNTDISPLAYID);
    if (display > 0 && CanAutoDismount(caster)) {
        g_pending.dismount = true;
        return 0;
    }
    return display;
}

// Stands in for `mov edx,[ecx+0x1FC]` at SITE_CASTER_STATE_MOUNT_READ. The
// caster is in EDI; EAX holds the spell's Attributes, which the stand rung
// reads next, so only EDX may change.
__declspec(naked) void MountReadThunk() {
    __asm {
        push eax
        mov ecx, edi
        call MountDisplayForCast
        mov edx, eax
        pop eax
        ret
    }
}

// Stands in for `call [edx+0xA4]` (GetStandState) at
// SITE_CASTER_STATE_STAND_CALL, caster in ECX: the stand state, or 0 when
// the cast will stand first.
int __fastcall StandStateForCast(const void *caster) {
    auto getStandState =
        (*reinterpret_cast<const GetStandState_t *const *>(caster))[kGetStandStateSlot];
    const int state = getStandState(caster, nullptr);
    if (state != 0 && CanAutoStand(caster)) {
        g_pending.stand = true;
        return 0;
    }
    return state;
}

char CheckForm(void *unit, const uint8_t *record, int *reason) {
    return reinterpret_cast<CheckForm_t>(Offsets::FUN_CHECK_SPELL_SHAPESHIFT_FORM)(
        unit, nullptr, record, reason);
}

// The caster check's form call.
char __fastcall CheckFormForCast(void *unit, void *, const uint8_t *record, int *reason) {
    if (CheckForm(unit, record, reason))
        return 1;
    if (*reason != Offsets::SPELL_FAILED_NOT_SHAPESHIFT || !CanAutoUnshift(unit))
        return 0;
    g_pending.unshift = true;
    return 1;
}

// FUN_SPELL_IS_USABLE's form call: the same verdict, nothing pending.
char __fastcall CheckFormForUsable(void *unit, void *, const uint8_t *record, int *reason) {
    if (CheckForm(unit, record, reason))
        return 1;
    return *reason == Offsets::SPELL_FAILED_NOT_SHAPESHIFT && CanAutoUnshift(unit);
}

// The cast request's call of the caster check. 4.3.4's order: stand,
// dismount, then leave the form.
char __fastcall CheckCasterState(void *caster, void *, const uint8_t *record, void *item) {
    g_pending = {};
    const char passed = reinterpret_cast<CheckCasterState_t>(
        Offsets::FUN_CHECK_CASTER_STATE_FOR_SPELL)(caster, nullptr, record, item);
    const Pending pending = g_pending;
    g_pending = {};
    if (!passed)
        return 0;
    if (pending.stand)
        reinterpret_cast<SendStandState_t>(Offsets::FUN_PLAYER_SEND_STAND_STATE)(
            caster, nullptr, 0);
    if (pending.dismount)
        Spell::Cancel::CancelPlayerAura(Offsets::SPELL_AURA_MOUNTED);
    if (pending.unshift)
        Spell::Cancel::CancelPlayerAura(Offsets::SPELL_AURA_MOD_SHAPESHIFT, Form(caster));
    return 1;
}

void Register() {
    // 4.3.4 defaults all three to 1. Here they default to 0, 1.12's own
    // behavior: autoUnshift drops a form the player may want to keep
    // (Shadowform on a Renew, Bear Form on a heal), so it is opted into.
    g_autoStand = CVar::Factory::Register("autoStand", "0");
    g_autoDismount = CVar::Factory::Register("autoDismount", "0");
    g_autoUnshift = CVar::Factory::Register("autoUnshift", "0");

    // The rungs only note what to do; the caster-check call runs it. Without
    // that call a passed rung would send the cast still mounted or seated.
    if (!Common::RedirectCall(Offsets::CALL_TRY_CAST_CHECK_CASTER_STATE,
                              Offsets::FUN_CHECK_CASTER_STATE_FOR_SPELL,
                              reinterpret_cast<const void *>(&CheckCasterState)))
        return;
    static const uint8_t kMountRead[6] = {0x8B, 0x91, 0xFC, 0x01, 0x00, 0x00};
    static const uint8_t kStandCall[6] = {0xFF, 0x92, 0xA4, 0x00, 0x00, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_CASTER_STATE_MOUNT_READ, kMountRead,
                            sizeof kMountRead, reinterpret_cast<const void *>(&MountReadThunk));
    Common::ReplaceWithCall(Offsets::SITE_CASTER_STATE_STAND_CALL, kStandCall,
                            sizeof kStandCall, reinterpret_cast<const void *>(&StandStateForCast));
    if (Common::RedirectCall(Offsets::CALL_CASTER_STATE_SHAPESHIFT_FORM,
                             Offsets::FUN_CHECK_SPELL_SHAPESHIFT_FORM,
                             reinterpret_cast<const void *>(&CheckFormForCast)))
        Common::RedirectCall(Offsets::CALL_SPELL_USABLE_SHAPESHIFT_FORM,
                             Offsets::FUN_CHECK_SPELL_SHAPESHIFT_FORM,
                             reinterpret_cast<const void *>(&CheckFormForUsable));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Spell::AutoPrepare
