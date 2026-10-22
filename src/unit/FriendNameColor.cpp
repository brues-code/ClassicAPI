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

// Friends' names in light blue — 4.3.4's s_friendColor.
//
// 4.3.4's CGGameUI::GetSelectionHighlightColor colours a player-controlled
// unit that neither side can attack: party colour first, then the friend
// colour (FriendList::GetFriend), then green when PvP flagged or blue. The
// name above the head and the selection circle both take it.
//
// 1.12's CGUnit_C::GetSelectionHighlightColor (FUN_UNIT_SELECTION_COLOR) has
// the same ladder without the friend rung. Its two not-in-party exits are
// where 4.3.4 checks the friends list, so each exit's colour load becomes a
// call that returns the friend colour for a friend and the engine's own
// colour otherwise. The friends list changing re-asks every name through the
// engine's mark-all (4.3.4's PlayerNameTriggerColorUpdateAll); without it a
// name keeps the colour it fetched until something else dirties it.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "friendlist/FriendList.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::FriendNameColor {

namespace {

// ARGB, as the engine stores its colours: CImVector(0xFF, 0xC8, 0x53) is
// B,G,R, so #53C8FF.
constexpr uint32_t kFriendColor = 0xFF53C8FF;

uint32_t ExitColor(const void *unit, uintptr_t engineColor) {
    if (FriendList::IsGuidFriend(Unit::Identity::GuidForObject(unit)))
        return kFriendColor;
    return Game::Read<uint32_t>(engineColor);
}

uint32_t __fastcall PvpExitColor(const void *unit) {
    return ExitColor(unit, Offsets::VAR_SELECTION_COLOR_PVP);
}

uint32_t __fastcall UnflaggedExitColor(const void *unit) {
    return ExitColor(unit, Offsets::VAR_SELECTION_COLOR_UNFLAGGED);
}

// Stands in for `mov eax,[VAR_SELECTION_COLOR_PVP]`; ECX and EDX are free.
__declspec(naked) void PvpExitThunk() {
    __asm {
        mov ecx, edi
        call PvpExitColor
        ret
    }
}

// Stands in for `mov ecx,[VAR_SELECTION_COLOR_UNFLAGGED]`; EAX holds the
// out pointer the next instruction stores through, EDX is free.
__declspec(naked) void UnflaggedExitThunk() {
    __asm {
        push eax
        mov ecx, edi
        call UnflaggedExitColor
        mov ecx, eax
        pop eax
        ret
    }
}

int g_friendListUpdateId = -1;

bool OnSignal(int eventID) {
    if (g_friendListUpdateId < 0)
        g_friendListUpdateId = Event::Custom::LookupByName("FRIENDLIST_UPDATE");
    if (eventID == g_friendListUpdateId)
        reinterpret_cast<void(__cdecl *)()>(
            static_cast<uintptr_t>(Offsets::FUN_PLAYER_NAME_MARK_ALL_COLOR_DIRTY))();
    return false;
}

const Event::SignalHook::AutoSubscribe _friendListUpdate{&OnSignal};

void Register() {
    static const uint8_t kPvpLoad[5] = {0xA1, 0xB4, 0xD8, 0xC4, 0x00};
    static const uint8_t kUnflaggedLoad[6] = {0x8B, 0x0D, 0x4C, 0xDA, 0xC4, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_SELECTION_COLOR_PVP_EXIT, kPvpLoad, sizeof kPvpLoad,
                            reinterpret_cast<const void *>(&PvpExitThunk));
    Common::ReplaceWithCall(Offsets::SITE_SELECTION_COLOR_UNFLAGGED_EXIT, kUnflaggedLoad,
                            sizeof kUnflaggedLoad,
                            reinterpret_cast<const void *>(&UnflaggedExitThunk));
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Unit::FriendNameColor
