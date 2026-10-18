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

// `FocusUnit(unit)` / `ClearFocus()` / `focus` unit token, following 4.3.4's
// CGGameUI focus (decomp: GameUI.cpp SetFocusTarget, Unit_C.cpp Disable /
// OnDeath / OnFlagChanged). The `focus` token itself resolves through the
// hook in `unit/TokenExtensions.cpp`.
//
// Unit events: every unit event the engine fires reaches "focus", as in
// 4.3.4 — see `unit/TokenEvents.cpp`.
//
// Dropping the focus — 4.3.4's rules, read where 1.12 has no event for them:
//   - The unit leaves the object table: dropped unless it is you, your pet, a
//     party or raid member, or their pet — the engine's own rule for the target
//     there (CGUnit_C::Disable FUN_UNIT_DISABLE → FUN_UNIT_IS_GROUP_MEMBER_OR_PET).
//     This also covers 4.3.4's pet-change rule: a dismissed pet is no longer
//     anyone's pet when its object goes. 1.12's rule has no duel exception.
//   - It dies while not friendly to you (OnDeath → DisengageFromActivePlayer),
//     or comes into range already dead and not friendly (InitAnims).
//   - It becomes not selectable (OnFlagChanged, UNIT_FLAG 0x2000000).
//   4.3.4 acts on the death and the flag CHANGE, so a dead or unselectable
//   unit can still be focused; the per-tick watcher below compares against the
//   state it saw last. SMSG_BREAK_TARGET has no 1.12 opcode.
// The focus survives /reload (ClassicAPI's choice; 4.3.4's CGGameUI::Shutdown
// clears it) and is dropped on logout.
//
// `focustarget` unit events are a ClassicAPI extension (4.3.4 fires none):
// `Unit::TokenObserver` watches the focus's current target and fires its
// field events with "focustarget". That GUID changes with no event to signal
// it (UNIT_FIELD_TARGET has no event-table slot), so the world tick re-reads it
// and re-points the observer when it changes.

#include "Focus.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "object/Resolve.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"
#include "unit/TokenEvents.h"
#include "unit/TokenObserver.h"

#include <cstdint>

namespace Unit::Focus {

namespace {

constexpr const char *kEventName = "PLAYER_FOCUS_CHANGED";
const Event::Custom::AutoReserve _reserve{kEventName};

uint64_t g_focusGUID = 0;

// What the world tick last saw of the focus unit. `Unseen` right after a
// FocusUnit (its current state is accepted as is); `Gone` while a kept
// groupmate is out of range (its return is a fresh arrival, as in 4.3.4's
// InitAnims); `Seen` with the dead / unselectable state of the last tick.
enum class Watch { Unseen, Seen, Gone };
Watch g_watch = Watch::Unseen;
bool g_wasDead = false;
bool g_wasUnselectable = false;

using Fire_t = void(__cdecl *)(int eventId, const char *fmt, ...);

void FireUnitEvent(int eventId, const char *token) {
    reinterpret_cast<Fire_t>(static_cast<uintptr_t>(Offsets::FUN_FIRE_EVENT))(eventId, "%s",
                                                                                token);
}

// GUID currently watched as `focustarget` (the focus unit's target), or 0.
uint64_t g_focusTargetGUID = 0;

int __fastcall FocusTargetFieldCb(uint32_t fieldOffset, uint32_t /*size*/, uint32_t guidLo,
                                  uint32_t guidHi, const uint32_t * /*oldValue*/,
                                  void * /*userArg*/) {
    if (guidLo != static_cast<uint32_t>(g_focusTargetGUID) ||
        guidHi != static_cast<uint32_t>(g_focusTargetGUID >> 32))
        return 1;
    FireUnitEvent(static_cast<int>(fieldOffset >> 2), "focustarget");
    return 1;
}

using TokenToGUID_t = uint64_t(__fastcall *)(const char *token);

uint64_t ResolveTokenGUID(const char *token) {
    if (token == nullptr || *token == '\0')
        return 0;
    return reinterpret_cast<TokenToGUID_t>(static_cast<uintptr_t>(Offsets::FUN_TOKEN_TO_GUID))(
        token);
}

// The live CGUnit for `guid`, or null when it isn't in the object table.
// Non-throwing — safe from any context.
void *ResolveObject(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    return Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, "Focus", 0x172);
}

// --- dropping the focus -----------------------------------------------------

bool IsGroupMemberOrPet(uint64_t guid) {
    using Fn_t = int(__fastcall *)(const uint64_t *guid);
    return reinterpret_cast<Fn_t>(
               static_cast<uintptr_t>(Offsets::FUN_UNIT_IS_GROUP_MEMBER_OR_PET))(&guid) != 0;
}

// The player's reaction to `unit` is friendly (Script_UnitIsFriend's > 3).
bool IsFriendlyToPlayer(void *unit) {
    void *player = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (player == nullptr)
        return false;
    using Reaction_t = int(__thiscall *)(void *unit, void *other);
    return reinterpret_cast<Reaction_t>(static_cast<uintptr_t>(Offsets::FUN_UNIT_REACTION))(
               player, unit) > 3;
}

void OnWorldTick() {
    uint64_t desiredFocusTarget = 0;
    if (g_focusGUID != 0) {
        void *unit = ResolveObject(g_focusGUID);
        if (unit == nullptr) {
            if (!IsGroupMemberOrPet(g_focusGUID)) {
                Set(0);
                return;
            }
            g_watch = Watch::Gone;
        } else {
            const auto *fields =
                Game::Read<const uint8_t *>(unit, Offsets::OFF_CGUNIT_OBJECT_FIELDS);
            if (fields != nullptr) {
                // Script_UnitIsDead's test.
                const bool dead =
                    Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) < 1 ||
                    (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_DYNAMIC_FLAGS) &
                     Offsets::UNIT_DYNFLAG_DEAD) != 0;
                const bool unselectable =
                    (Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS) &
                     Offsets::UNIT_FLAG_NOT_SELECTABLE) != 0;
                // A fresh arrival counts as a change from alive and selectable.
                const bool arrived = (g_watch == Watch::Gone);
                const bool wasDead = arrived ? false : g_wasDead;
                const bool wasUnselectable = arrived ? false : g_wasUnselectable;
                if (g_watch != Watch::Unseen &&
                    ((dead && !wasDead && !IsFriendlyToPlayer(unit)) ||
                     (unselectable && !wasUnselectable))) {
                    Set(0);
                    return;
                }
                g_watch = Watch::Seen;
                g_wasDead = dead;
                g_wasUnselectable = unselectable;
                desiredFocusTarget = Game::Read<uint64_t>(fields, Offsets::OFF_UNIT_FIELD_TARGET);
            }
        }
    }

    // Re-point the focustarget observer only when the target identity
    // changes (focus swapped targets, focus cleared, or focus has no target).
    if (desiredFocusTarget != g_focusTargetGUID) {
        if (g_focusTargetGUID != 0)
            TokenObserver::Unregister(g_focusTargetGUID, &FocusTargetFieldCb);
        g_focusTargetGUID = desiredFocusTarget;
        if (g_focusTargetGUID != 0)
            TokenObserver::Register(g_focusTargetGUID, &FocusTargetFieldCb);
    }
}

// `FocusUnit([unit])` — focus whatever `unit` resolves to; no argument means
// "target", like `/focus`.
int __fastcall Script_FocusUnit(void *L) {
    if (Game::Lua::IsString(L, 1))
        Set(ResolveTokenGUID(Game::Lua::ToString(L, 1)));
    else
        Set(ResolveTokenGUID("target"));
    return 0;
}

// `ClearFocus()` — drops the focus, fires PLAYER_FOCUS_CHANGED.
int __fastcall Script_ClearFocus(void *L) {
    (void)L;
    Set(0);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("FocusUnit", &Script_FocusUnit);
    Game::Lua::RegisterGlobalFunction("ClearFocus", &Script_ClearFocus);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Tick::WorldTick::AutoSubscribe _tickSub{&OnWorldTick};

// Drop focus when the player logs out to the glue screen, so character A's
// focus doesn't carry into character B (the DLL isn't reloaded). The glue
// hook fires on the world→glue return but NOT on /reload, so the focus
// survives /reload. No event or observer work: every unit object, with its
// observers, was torn down with the world.
void ClearOnLogout() {
    g_focusGUID = 0;
    g_focusTargetGUID = 0;
    g_watch = Watch::Unseen;
}
const Game::GlueModuleAutoRegister _clearOnLogout{&ClearOnLogout};

} // namespace

uint64_t Get() { return g_focusGUID; }

void Set(uint64_t guid) {
    if (guid == g_focusGUID)
        return; // same unit → no event (4.3.4 SetFocusTarget)
    // The old focustarget is stale; the world tick points the observer at the
    // new focus's target.
    if (g_focusTargetGUID != 0) {
        TokenObserver::Unregister(g_focusTargetGUID, &FocusTargetFieldCb);
        g_focusTargetGUID = 0;
    }
    const uint64_t old = g_focusGUID;
    g_focusGUID = guid;
    g_watch = Watch::Unseen;
    TokenEvents::Refresh(old);
    TokenEvents::Refresh(guid);
    const int slot = _reserve.Slot();
    if (slot >= 0)
        Event::Custom::Fire(slot, "");
}

} // namespace Unit::Focus
