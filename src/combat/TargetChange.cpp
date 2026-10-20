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

// The `stopAutoAttackOnTargetChange` cvar — 4.3.4's CGGameUI::Target tail:
//
//   FrameScript_SignalEvent(PLAYER_TARGET_CHANGED);
//   if (cvar) { if (Spell_C_GetAutoRepeatingSpell()) Spell_C_CancelAutoRepeat(true); }
//   else if (inMelee) player->CombatModeEnter(newTarget);
//
// 1.12's target setter (FUN_SET_SELECTION_BY_GUID) already has the cvar-0
// branch: releasing the old target stops melee, and after the event it
// restarts melee on the new target when the player was attacking. Both are
// direct calls, so the cvar is two call redirects in that function only:
// the event fire also stops auto-repeat, and the melee restart is skipped.
// Defaults to 0 as in 4.3.4, which is 1.12's behavior.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "cvar/Factory.h"

#include <cstdint>

namespace Combat::TargetChange {

namespace {

using FireEvent_t = void(__fastcall *)(int eventID);
using AttackStart_t = void(__fastcall *)(void *player, void *edx, const uint64_t *guid);
using StopAutoRepeat_t = void(__cdecl *)();

CVar::Factory::Handle g_stopOnChange = nullptr;

bool StopOnChange() { return CVar::Factory::GetInt(g_stopOnChange) != 0; }

void __fastcall FireTargetChanged(int eventID) {
    reinterpret_cast<FireEvent_t>(Offsets::FUN_FIRE_EVENT_NO_ARGS)(eventID);
    if (StopOnChange())
        reinterpret_cast<StopAutoRepeat_t>(Offsets::FUN_STOP_AUTO_REPEAT_SPELL)();
}

void __fastcall RestartMelee(void *player, void *, const uint64_t *guid) {
    if (!StopOnChange())
        reinterpret_cast<AttackStart_t>(Offsets::FUN_ATTACK_START)(player, nullptr, guid);
}

void Register() {
    g_stopOnChange = CVar::Factory::Register("stopAutoAttackOnTargetChange", "0");
    Common::RedirectCall(Offsets::CALL_SET_SELECTION_FIRE_TARGET_CHANGED,
                         Offsets::FUN_FIRE_EVENT_NO_ARGS,
                         reinterpret_cast<const void *>(&FireTargetChanged));
    Common::RedirectCall(Offsets::CALL_SET_SELECTION_ATTACK_START, Offsets::FUN_ATTACK_START,
                         reinterpret_cast<const void *>(&RestartMelee));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Combat::TargetChange
