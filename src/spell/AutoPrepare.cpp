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
//   - the cast request's one call of the caster check keeps the pending
//     actions when it passes, and the send of the pending cast runs them.
//     1.12's request checks the caster before it arms the targets, so the
//     actions wait for the send, as in 4.3.4: a cast with no target, or a
//     targeting cursor the player cancels, does not stand or dismount.
// The actions are the engine's own: the stand setter, and CMSG_CANCEL_AURA
// for the mount / form aura (1.12 has no CMSG_CANCEL_MOUNT_AURA; cancelling
// the mount aura is how its own UI dismounts). The server applies the
// cancel / stand before the cast packet that follows it.
//
// autoUnshift and autoDismount also cover interaction, as in 4.3.4:
//   - NPC right-click: 4.3.4's CGPlayer_C::HandleOnRightClick cancels a form
//     without SHAPESHIFT_FORM_FLAG_CAN_INTERACT in place of
//     ERR_CANT_INTERACT_SHAPESHIFTED, before its range check, so we do the
//     same at 1.12's gate. Neither client gates an NPC on the mount. The
//     hover cursor asks CanInteractNow, whose form test 4.3.4 also passes
//     on CanAutoUnshift, so the NPC's service cursor shows while shifted.
//   - game objects: for an object with no Lock row, 4.3.4's CanUseNow lets
//     the mount pass when CanAutoDismount holds and the form when
//     CanAutoUnshift holds. The 1.12 use check is also the hover cursor's
//     query, so it only passes. Then TypeBase::Use, on its no-spell branch
//     only, dismounts (unless the object is usable mounted, the mailbox) and
//     cancels the form before CMSG_GAMEOBJ_USE. 4.3.4 does that with no
//     cvar test, the gate having run; we keep the cvars, so with them off
//     an object with a Lock row is still used as 1.12 uses it. 1.12's
//     mailbox and text types replace that use slot, which 4.3.4 does not,
//     so they get the same step. A locked object (a chest, a vein) opens by
//     a spell whose cast passes the caster check above.
//
// 4.3.4's mount-in-form step (the cast path's and CGItem_C::Use's
// CancelShapeshiftForMount, for a SPELL_AURA_MOUNTED spell when
// !CanCurrentFormMount) needs no site of its own: 482 of 1.12's 484 mount
// spells carry SPELL_ATTR_NOT_SHAPESHIFT, so the form check refuses them
// with SPELL_FAILED_NOT_SHAPESHIFT and the form rung above unshifts; an item
// reaches the same rung through its cast.
//
// The remaining 4.3.4 sites:
//   - loot (autoStand): CGUnit_C / CGCorpse_C::OnRightClick stand up in
//     place of ERR_LOOT_NOTSTANDING. 1.12's two loot steps read the stand
//     state the same way, so the read stands the player and reports 0.
//   - loot (autoDismount): 4.3.4's unit right-click has no mount test on
//     its loot step, and LootUnit dismounts before CMSG_LOOT. 1.12's loot
//     step skips a mounted player, so its read passes, and 1.12's LootUnit
//     dismounts at the same point. A corpse is not looted mounted in either.
//   - item use (autoUnshift): CGItem_C::Use leaves the form in place of
//     ERR_NO_ITEMS_WHILE_SHAPESHIFTED, after its toggle test (an item
//     whose aura is up only turns it off). 1.12 tests the toggle after the
//     form, so its refusal passes and the form is left at the next test,
//     the bind-on-use one, where 4.3.4 has it.
//   - attack (autoDismount): CanAttackNow (the attack cursor, the right-click
//     attack) and VerifyAttackIconPressed (ERR_ATTACK_MOUNTED) pass a mount
//     when CanAutoDismount holds; CombatModeEnter then dismounts in melee
//     reach, else shows ERR_OUT_OF_RANGE and does not attack. 1.12 inlines
//     the CanAttackNow mount test in four places and has the resolver's
//     refusal; each read passes, and StartAttack, past all its tests, runs
//     the CombatModeEnter step. The reach is Combat::Swing::InMeleeRange,
//     4.3.4's GetMeleeRange formula on the engine's own constants.
//   - taxi: the taxi map's open stands, dismounts and leaves the form when
//     it has more than one node. 4.3.4 does that with no cvar; here each
//     action needs its cvar, so with them off the map opens as in 1.12.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "combat/Swing.h"
#include "cvar/Factory.h"
#include "dbc/Lookup.h"
#include "spell/Cancel.h"
#include "unit/Identity.h"

#include <cstdint>
#include <initializer_list>

namespace Spell::AutoPrepare {

namespace {

using CheckCasterState_t = char(__fastcall *)(void *caster, void *edx,
                                              const uint8_t *record, void *item);
using CheckForm_t = char(__fastcall *)(void *unit, void *edx,
                                       const uint8_t *record, int *reason);
using GetStandState_t = int(__fastcall *)(const void *unit, void *edx);
using SendStandState_t = void(__fastcall *)(const void *player, void *edx, uint32_t state);
using IsShapeShifted_t = char(__fastcall *)(const void *unit);
using UsableMounted_t = bool(__fastcall *)(const void *descriptorSlot);
using ObjectUse_t = char(__fastcall *)(void *handler);
using DisplayError_t = void(__cdecl *)(int id, ...);
using TriggerTutorial_t = void(__fastcall *)(int id, int arg);
using CanAutoInteract_t = bool(__fastcall *)(const void *player);

// Continuation addresses the naked thunks load from memory.
uintptr_t g_itemUseFormPassed = Offsets::ITEM_USE_FORM_PASSED;
uintptr_t g_startAttackReturn = Offsets::START_ATTACK_RETURN;

constexpr int kGetStandStateSlot = 0xA4 / 4;

// The instructions the sites replace, by name: the mount reads
// (`mov dst,[base+0x1FC]`) and the GetStandState calls (`call [base+0xA4]`).
constexpr uint8_t kMovEaxEcxMount[6] = {0x8B, 0x81, 0xFC, 0x01, 0x00, 0x00};
constexpr uint8_t kMovEaxEdxMount[6] = {0x8B, 0x82, 0xFC, 0x01, 0x00, 0x00};
constexpr uint8_t kMovEcxEaxMount[6] = {0x8B, 0x88, 0xFC, 0x01, 0x00, 0x00};
constexpr uint8_t kMovEdxEaxMount[6] = {0x8B, 0x90, 0xFC, 0x01, 0x00, 0x00};
constexpr uint8_t kMovEdxEcxMount[6] = {0x8B, 0x91, 0xFC, 0x01, 0x00, 0x00};
constexpr uint8_t kCallEaxStandState[6] = {0xFF, 0x90, 0xA4, 0x00, 0x00, 0x00};
constexpr uint8_t kCallEdxStandState[6] = {0xFF, 0x92, 0xA4, 0x00, 0x00, 0x00};

// Common::ReplaceWithCall / RedirectCall / RedirectSlot, taking the hook
// as a typed function.
template <size_t N, typename Fn>
bool Replace(uintptr_t site, const uint8_t (&original)[N], Fn *target) {
    return Common::ReplaceWithCall(site, original, N, reinterpret_cast<const void *>(target));
}

template <typename Fn> bool Redirect(uintptr_t site, uintptr_t expected, Fn *target) {
    return Common::RedirectCall(site, expected, reinterpret_cast<const void *>(target));
}

template <typename Fn> bool RedirectSlot(uintptr_t slot, uintptr_t expected, Fn *target) {
    return Common::RedirectSlot(slot, expected, reinterpret_cast<const void *>(target));
}

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

// The unit's SpellShapeshiftForm row's flags; a form with no row reads as 0.
uint32_t FormFlags(const void *unit) {
    const uint8_t *form = DBC::Record(Offsets::VAR_SPELL_SHAPESHIFT_FORM_RECORDS,
                                      Offsets::VAR_SPELL_SHAPESHIFT_FORM_COUNT,
                                      static_cast<uint32_t>(Form(unit)));
    return form != nullptr ? Game::Read<uint32_t>(form, Offsets::OFF_SPELL_SHAPESHIFT_FORM_FLAGS)
                           : 0;
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
    return IsActivePlayer(unit) && !OnTaxi(unit) && Enabled(g_autoUnshift) &&
           (FormFlags(unit) & Offsets::SHAPESHIFT_FORM_FLAG_NO_AUTO_UNSHIFT) == 0;
}

int StandState(const void *unit) {
    auto getStandState =
        (*reinterpret_cast<const GetStandState_t *const *>(unit))[kGetStandStateSlot];
    return getStandState(unit, nullptr);
}

int MountDisplay(const void *unit) {
    return Game::Read<int>(Fields(unit), Offsets::OFF_UNIT_FIELD_MOUNTDISPLAYID);
}

// 4.3.4's pairing at every site: the state holds and its CanAuto* allows
// the action.
bool AutoStands(const void *unit) { return StandState(unit) != 0 && CanAutoStand(unit); }

bool AutoDismounts(const void *unit) { return MountDisplay(unit) > 0 && CanAutoDismount(unit); }

// The engine's actions: the stand setter, and CMSG_CANCEL_AURA for the
// mount / form aura.
void Stand(const void *player) {
    reinterpret_cast<SendStandState_t>(Offsets::FUN_PLAYER_SEND_STAND_STATE)(player, nullptr, 0);
}

void Dismount() { Spell::Cancel::CancelPlayerAura(Offsets::SPELL_AURA_MOUNTED); }

void CancelForm(const void *unit) {
    Spell::Cancel::CancelPlayerAura(Offsets::SPELL_AURA_MOD_SHAPESHIFT, Form(unit));
}

char IsShapeShifted(const void *unit) {
    return reinterpret_cast<IsShapeShifted_t>(Offsets::FUN_UNIT_IS_SHAPESHIFTED)(unit);
}

// The gates' test after IsShapeShifted: a form with no row, or one
// without the interact flag.
bool FormBlocksInteraction(const void *unit) {
    return (FormFlags(unit) & Offsets::SHAPESHIFT_FORM_FLAG_CAN_INTERACT) == 0;
}

// True when the gate would refuse and autoUnshift takes the form off instead.
bool UnshiftsForInteraction(const void *player) {
    return IsShapeShifted(player) && FormBlocksInteraction(player) && CanAutoUnshift(player);
}

// The NPC right-click's IsShapeShifted: leaves the form and reports "not
// shifted", as 4.3.4's HandleOnRightClick does.
char __fastcall IsShapeShiftedForTalk(const void *player) {
    if (UnshiftsForInteraction(player)) {
        CancelForm(player);
        return 0;
    }
    return IsShapeShifted(player);
}

// The game-object use check's and CanInteractNow's IsShapeShifted (4.3.4's
// CanUseNow / CanInteractNow): the same verdict, nothing done. Both are
// hover-cursor queries, so the cursor shows the service while shifted.
char __fastcall IsShapeShiftedForQuery(const void *player) {
    return UnshiftsForInteraction(player) ? 0 : IsShapeShifted(player);
}

// The mount reads of the gates 4.3.4 passes on CanAutoDismount (an object
// use, a loot, an attack): the mount display, or 0 when the action that
// follows dismounts first.
int __fastcall MountDisplayForAction(const void *player) {
    return AutoDismounts(player) ? 0 : MountDisplay(player);
}

// Stands in for `mov ecx,[eax+0x1FC]` at SITE_GAMEOBJECT_CAN_USE_MOUNT_READ,
// player in EDI. EAX and EDX are dead there; ECX takes the display.
__declspec(naked) void UseMountReadThunk() {
    __asm {
        mov ecx, edi
        call MountDisplayForAction
        mov ecx, eax
        ret
    }
}

// Stand in for a `mov eax,[reg+0x1FC]` mount read, the player in ESI or EDI;
// EAX takes the display, every other register is kept.
__declspec(naked) void MountReadEsiThunk() {
    __asm {
        push ecx
        push edx
        mov ecx, esi
        call MountDisplayForAction
        pop edx
        pop ecx
        ret
    }
}

__declspec(naked) void MountReadEdiThunk() {
    __asm {
        push ecx
        push edx
        mov ecx, edi
        call MountDisplayForAction
        pop edx
        pop ecx
        ret
    }
}

// Stands in for `mov edx,[eax+0x1FC]` at SITE_ATTACK_RESOLVER_CAN_ATTACK_-
// MOUNT_READ, player in ECX, which the can-attack call after it still uses.
__declspec(naked) void ResolverMountReadThunk() {
    __asm {
        push ecx
        call MountDisplayForAction
        mov edx, eax
        pop ecx
        ret
    }
}

// The loot steps' GetStandState call (ECX = the player): stands the player
// up and reports 0 when autoStand allows, as 4.3.4's OnRightClick does.
int __fastcall StandStateForLoot(const void *player) {
    if (AutoStands(player)) {
        Stand(player);
        return 0;
    }
    return StandState(player);
}

// LootUnit before CMSG_LOOT: 4.3.4's dismount.
void __fastcall DismountForLoot(const void *player) {
    if (AutoDismounts(player))
        Dismount();
}

// Stands in for `push 0; xor dl,dl; mov cl,1` at SITE_LOOT_UNIT_SEND, player
// in ESI: the dismount, then the replaced code with its push kept under the
// return address.
__declspec(naked) void LootSendThunk() {
    __asm {
        mov ecx, esi
        call DismountForLoot
        pop eax
        push 0
        push eax
        xor dl, dl
        mov cl, 1
        ret
    }
}

// Set at the item use's form refusal when autoUnshift lets the use go on;
// the bind-on-use test runs the unshift, the toggle cancel drops it. Every
// path past the form test reaches one of the two.
bool g_itemUnshift = false;

// The item use's form refusal: lets the use go on when autoUnshift allows.
// The form is left later, at 4.3.4's point: past the toggle test, so an
// item whose aura is up is only turned off, as 4.3.4 does it.
bool __fastcall UnshiftForItem(const void *player) {
    g_itemUnshift = CanAutoUnshift(player);
    return g_itemUnshift;
}

// Stands in for `push 0x15E` at SITE_ITEM_USE_FORM_REFUSAL, player in ESI:
// continues at ITEM_USE_FORM_PASSED, or pushes the error for the `jmp` to
// the error tail that follows.
__declspec(naked) void ItemFormRefusalThunk() {
    __asm {
        mov ecx, esi
        call UnshiftForItem
        test al, al
        jz refuse
        mov eax, g_itemUseFormPassed
        mov [esp], eax
        ret
    refuse:
        pop eax
        push 0x15E
        jmp eax
    }
}

// The toggle test's cancel: the use turns the aura off and casts nothing,
// so the form stays.
void __fastcall ItemToggleCancel(int spellId) {
    g_itemUnshift = false;
    reinterpret_cast<void(__fastcall *)(int)>(Offsets::FUN_CANCEL_AURA_SEND)(spellId);
}

// Before the bind-on-use test: 4.3.4 CGItem_C::Use's form cancel.
void UnshiftBeforeItemBind() {
    if (!g_itemUnshift)
        return;
    g_itemUnshift = false;
    if (const void *player = Unit::Identity::PlayerObject())
        CancelForm(player);
}

// Stands in for `mov ecx,[ebp-8]; cmp dword [ecx+0x194],3` at
// SITE_ITEM_USE_BIND_TEST: the unshift, then the replaced code, whose flags
// the `jne` after the return reads. The call keeps EBX, ESI, EDI and EBP.
__declspec(naked) void ItemBindTestThunk() {
    __asm {
        call UnshiftBeforeItemBind
        mov ecx, [ebp - 8]
        cmp dword ptr [ecx + 0x194], 3
        ret
    }
}

// StartAttack past its tests: 4.3.4 CombatModeEnter's dismount, in melee
// reach only; out of reach it does not attack, and shows ERR_OUT_OF_RANGE
// unless click-to-move is walking the player there (4.3.4's `silent`).
bool __fastcall PrepareAttack(const void *player, void *target) {
    if (!AutoDismounts(player))
        return true;
    if (!Combat::Swing::InMeleeRange(const_cast<void *>(player), target)) {
        if (!reinterpret_cast<CanAutoInteract_t>(Offsets::FUN_PLAYER_CAN_AUTO_INTERACT)(player))
            reinterpret_cast<DisplayError_t>(Offsets::FUN_GAMEUI_DISPLAY_ERROR)(
                Offsets::GAME_ERROR_OUT_OF_RANGE);
        return false;
    }
    Dismount();
    return true;
}

// Stands in for `mov eax,[esi+0xC8C]` at SITE_START_ATTACK_PASSED, player in
// ESI, target in EDI: on a refusal, returns from StartAttack.
__declspec(naked) void StartAttackPassedThunk() {
    __asm {
        mov ecx, esi
        mov edx, edi
        call PrepareAttack
        test al, al
        jz abort
        mov eax, [esi + 0xC8C]
        ret
    abort:
        mov eax, g_startAttackReturn
        mov [esp], eax
        ret
    }
}

// The taxi map opener's tutorial call, its last step: then 4.3.4's
// stand, dismount and form cancel for a map with more than one node.
void __fastcall TaxiMapOpenedTutorial(int id, int arg) {
    reinterpret_cast<TriggerTutorial_t>(Offsets::FUN_TRIGGER_TUTORIAL)(id, arg);
    if (Game::Read<int>(static_cast<uintptr_t>(Offsets::VAR_TAXI_MAP_NODE_COUNT)) <= 1)
        return;
    const void *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return;
    if (AutoStands(player))
        Stand(player);
    if (AutoDismounts(player))
        Dismount();
    // 4.3.4's IsCancelShapeshiftAtFlightMaster adds forms with flag 0x800,
    // which no 1.12 SpellShapeshiftForm row carries.
    if (IsShapeShifted(player) && CanAutoUnshift(player))
        CancelForm(player);
}

// 4.3.4 TypeBase::Use's step before CMSG_GAMEOBJ_USE: dismount unless the
// object is usable mounted, then leave a form that cannot interact.
void __fastcall PrepareObjectUse(const void *handler) {
    const void *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return;
    const uint8_t *object =
        Game::Read<const uint8_t *>(handler, Offsets::OFF_GAMEOBJECT_HANDLER_OBJECT);
    if (AutoDismounts(player) &&
        !reinterpret_cast<UsableMounted_t>(Offsets::FUN_GAMEOBJECT_USABLE_MOUNTED)(
            object + Offsets::OFF_GAMEOBJECT_DESCRIPTOR))
        Dismount();
    if (UnshiftsForInteraction(player))
        CancelForm(player);
}

// Stands in for `mov edi,0x7FF9E4` at SITE_GAMEOBJECT_USE_SEND, handler at
// [EBP-4]. EAX, ECX and EDX are dead there.
__declspec(naked) void UseSendThunk() {
    __asm {
        mov ecx, [ebp - 4]
        call PrepareObjectUse
        mov edi, 0x7FF9E4
        ret
    }
}

// The mailbox and text use slots: the same step, then the type's own use.
char __fastcall MailboxUse(void *handler) {
    PrepareObjectUse(handler);
    return reinterpret_cast<ObjectUse_t>(Offsets::FUN_GAMEOBJECT_MAILBOX_USE)(handler);
}

char __fastcall TextUse(void *handler) {
    PrepareObjectUse(handler);
    return reinterpret_cast<ObjectUse_t>(Offsets::FUN_GAMEOBJECT_TEXT_USE)(handler);
}

// The mount rung's read: the mount display, or 0 when the cast will
// dismount first.
int __fastcall MountDisplayForCast(const void *caster) {
    if (AutoDismounts(caster)) {
        g_pending.dismount = true;
        return 0;
    }
    return MountDisplay(caster);
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
    if (AutoStands(caster)) {
        g_pending.stand = true;
        return 0;
    }
    return StandState(caster);
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

// The cast request's call of the caster check: a passed check keeps what its
// rungs noted for the send; a refused one drops it.
char __fastcall CheckCasterState(void *caster, void *, const uint8_t *record, void *item) {
    g_pending = {};
    const char passed = reinterpret_cast<CheckCasterState_t>(
        Offsets::FUN_CHECK_CASTER_STATE_FOR_SPELL)(caster, nullptr, record, item);
    if (!passed)
        g_pending = {};
    return passed;
}

// The pending cast's send (4.3.4's Spell_C_SendPendingCast): stand, dismount,
// then leave the form, in that order, before the packet. Here, past the
// target arm, a cast that finds no target ("You have no target") or whose
// targeting cursor is cancelled has not dismounted. The rungs note only for
// the active player; each action runs only while its state still holds, as
// the cursor can be up while the player stands, dismounts or leaves the form.
void __fastcall SendCast(void *pendingCast, uint8_t flag) {
    const Pending pending = g_pending;
    g_pending = {};
    if (const void *player = Unit::Identity::PlayerObject()) {
        if (pending.stand && StandState(player) != 0)
            Stand(player);
        if (pending.dismount && MountDisplay(player) > 0)
            Dismount();
        if (pending.unshift && Form(player) != 0)
            CancelForm(player);
    }
    reinterpret_cast<void(__fastcall *)(void *, uint8_t)>(Offsets::FUN_SPELL_SEND_CAST)(
        pendingCast, flag);
}

void Register() {
    // 4.3.4 defaults all three to 1. Here they default to 0, 1.12's own
    // behavior: autoUnshift drops a form the player may want to keep
    // (Shadowform on a Renew, Bear Form on a heal), so it is opted into.
    g_autoStand = CVar::Factory::Register("autoStand", "Automatically stand when needed", Offsets::CVAR_CATEGORY_GAME, "0");
    g_autoDismount = CVar::Factory::Register("autoDismount", "Automatically dismount when needed", Offsets::CVAR_CATEGORY_GAME, "0");
    g_autoUnshift = CVar::Factory::Register("autoUnshift", "Automatically leave shapeshift form when needed", Offsets::CVAR_CATEGORY_GAME, "0");

    // Interaction. The use check passes the mount and form only together
    // with the step at every use; without it the object would be used still
    // mounted or shifted.
    Redirect(Offsets::CALL_NPC_INTERACT_IS_SHAPESHIFTED, Offsets::FUN_UNIT_IS_SHAPESHIFTED,
             &IsShapeShiftedForTalk);
    static const uint8_t kUseSend[5] = {0xBF, 0xE4, 0xF9, 0x7F, 0x00};
    if (Replace(Offsets::SITE_GAMEOBJECT_USE_SEND, kUseSend, &UseSendThunk) &&
        RedirectSlot(Offsets::SLOT_GAMEOBJECT_MAILBOX_USE, Offsets::FUN_GAMEOBJECT_MAILBOX_USE,
                     &MailboxUse) &&
        RedirectSlot(Offsets::SLOT_GAMEOBJECT_TEXT_USE, Offsets::FUN_GAMEOBJECT_TEXT_USE,
                     &TextUse)) {
        Replace(Offsets::SITE_GAMEOBJECT_CAN_USE_MOUNT_READ, kMovEcxEaxMount, &UseMountReadThunk);
        Redirect(Offsets::CALL_GAMEOBJECT_CAN_USE_IS_SHAPESHIFTED,
                 Offsets::FUN_UNIT_IS_SHAPESHIFTED, &IsShapeShiftedForQuery);
    }
    Redirect(Offsets::CALL_UNIT_CAN_INTERACT_NOW_IS_SHAPESHIFTED,
             Offsets::FUN_UNIT_IS_SHAPESHIFTED, &IsShapeShiftedForQuery);

    // Loot. The loot step passes the mount only together with LootUnit's
    // dismount.
    Replace(Offsets::SITE_UNIT_LOOT_STAND_CALL, kCallEaxStandState, &StandStateForLoot);
    Replace(Offsets::SITE_CORPSE_LOOT_STAND_CALL, kCallEdxStandState, &StandStateForLoot);
    static const uint8_t kLootSend[6] = {0x6A, 0x00, 0x32, 0xD2, 0xB1, 0x01};
    if (Replace(Offsets::SITE_LOOT_UNIT_SEND, kLootSend, &LootSendThunk))
        Replace(Offsets::SITE_UNIT_LOOT_MOUNT_READ, kMovEaxEcxMount, &MountReadEsiThunk);

    // Item use. The refusal passes only together with the unshift after the
    // toggle test; without it the item would be used still shifted.
    static const uint8_t kItemBindTest[10] = {0x8B, 0x4D, 0xF8, 0x83, 0xB9,
                                              0x94, 0x01, 0x00, 0x00, 0x03};
    static const uint8_t kItemFormRefusal[5] = {0x68, 0x5E, 0x01, 0x00, 0x00};
    if (Redirect(Offsets::CALL_ITEM_USE_TOGGLE_CANCEL, Offsets::FUN_CANCEL_AURA_SEND,
                 &ItemToggleCancel) &&
        Replace(Offsets::SITE_ITEM_USE_BIND_TEST, kItemBindTest, &ItemBindTestThunk))
        Replace(Offsets::SITE_ITEM_USE_FORM_REFUSAL, kItemFormRefusal, &ItemFormRefusalThunk);

    // Attack. The mount reads pass only together with StartAttack's
    // dismount; without it the attack would start mounted.
    static const uint8_t kStartAttackPassed[6] = {0x8B, 0x86, 0x8C, 0x0C, 0x00, 0x00};
    if (Replace(Offsets::SITE_START_ATTACK_PASSED, kStartAttackPassed, &StartAttackPassedThunk)) {
        Replace(Offsets::SITE_START_ATTACK_MOUNT_READ, kMovEaxEcxMount, &MountReadEsiThunk);
        Replace(Offsets::SITE_RIGHT_CLICK_ATTACK_MOUNT_READ, kMovEaxEdxMount, &MountReadEsiThunk);
        Replace(Offsets::SITE_CURSOR_ATTACK_MOUNT_READ, kMovEaxEdxMount, &MountReadEdiThunk);
        Replace(Offsets::SITE_ATTACK_RESOLVER_MOUNT_READ, kMovEaxEcxMount, &MountReadEdiThunk);
        Replace(Offsets::SITE_ATTACK_RESOLVER_CAN_ATTACK_MOUNT_READ, kMovEdxEaxMount,
                &ResolverMountReadThunk);
    }

    // Taxi.
    Redirect(Offsets::CALL_TAXI_MAP_OPENED_TUTORIAL, Offsets::FUN_TRIGGER_TUTORIAL,
             &TaxiMapOpenedTutorial);

    // The rungs only note what to do; the caster-check call keeps it and
    // every send runs it. Without all of them a passed rung would send the
    // cast still mounted or seated. A send with nothing noted is the
    // original send, so sites redirected before a miss are harmless.
    for (const uintptr_t site :
         {Offsets::CALL_TARGET_ARM_SEND_CAST, Offsets::CALL_TARGET_ARM_SEND_CAST_GUID,
          Offsets::CALL_BIND_TARGET_OBJECT_SEND_CAST, Offsets::CALL_BIND_TARGET_LOCATION_SEND_CAST,
          Offsets::CALL_BIND_TARGET_TRADE_SLOT_SEND_CAST}) {
        if (!Redirect(site, Offsets::FUN_SPELL_SEND_CAST, &SendCast))
            return;
    }
    if (!Redirect(Offsets::CALL_TRY_CAST_CHECK_CASTER_STATE,
                  Offsets::FUN_CHECK_CASTER_STATE_FOR_SPELL, &CheckCasterState))
        return;
    Replace(Offsets::SITE_CASTER_STATE_MOUNT_READ, kMovEdxEcxMount, &MountReadThunk);
    Replace(Offsets::SITE_CASTER_STATE_STAND_CALL, kCallEdxStandState, &StandStateForCast);
    if (Redirect(Offsets::CALL_CASTER_STATE_SHAPESHIFT_FORM,
                 Offsets::FUN_CHECK_SPELL_SHAPESHIFT_FORM, &CheckFormForCast))
        Redirect(Offsets::CALL_SPELL_USABLE_SHAPESHIFT_FORM,
                 Offsets::FUN_CHECK_SPELL_SHAPESHIFT_FORM, &CheckFormForUsable);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Spell::AutoPrepare
