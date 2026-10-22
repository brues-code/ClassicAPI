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

// The selection colour — the name above the head, the selection circle, and
// `UnitSelectionColor` / `UnitSelectionType` for Lua.
//
// 4.3.4's CGGameUI::GetSelectionHighlightColor(guid, color, extended) is one
// ladder for both: CGUnit_C's virtual returns the target flash or calls it
// with extended = 1, Script_UnitSelectionColor calls it with 0. Retail
// exposes that flag as `useExtendedColors` and names each rung
// (UnitSelectionType, 0 Hostile .. 9 Dead).
//
// 1.12's CGUnit_C::GetSelectionHighlightColor (FUN_UNIT_SELECTION_COLOR) is
// the extended ladder inlined into the virtual, without the friend rung.
// Friends' names: its two not-in-party exits are where 4.3.4 checks the
// friends list, so each exit's colour load becomes a call that returns the
// friend colour for a friend and the engine's own colour otherwise. The
// friends list changing re-asks every name through the engine's mark-all
// (4.3.4's PlayerNameTriggerColorUpdateAll); without it a name keeps the
// colour it fetched until something else dirties it.
//
// Lua: `Type` is that ladder rung for rung, with the same engine predicates,
// so the colour Lua reads is the one the circle draws. The non-extended
// variant drops the party, friend and dead rungs and gives a plain player
// pure blue, as 4.3.4's does. A GUID with no unit object (a party or raid
// member out of range) takes 4.3.4's no-object branch, with the PvP bit
// from the member's stats block — Script_UnitIsPVP's source for one.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "event/SignalHook.h"
#include "friendlist/FriendList.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Unit::SelectionColor {

namespace {

// Retail's UnitSelectionType values this client has a rung for.
enum Type : int {
    Hostile = 0,
    Unfriendly = 1,
    Neutral = 2,
    Friendly = 3,
    Player = 4,
    PlayerExtended = 5,
    Party = 6,
    PartyPvp = 7,
    Friend = 8,
    Dead = 9,
};

// ARGB by Type. The engine's builder (FUN_UNIT_SELECTION_COLOR's guarded
// prologue) and 0x005FA160 (dead) write these from immediates; they are not
// read live because the builder runs lazily on the first name or circle
// fetch, which an addon can beat. Player (#0000FF) and Friend (#53C8FF) are
// 4.3.4's s_playerColors[0] and s_friendColor, which 1.12 never builds.
constexpr uint32_t kColors[] = {
    0xFFFF0000, // Hostile — s_reactionColors[0..1]
    0xFFFF8000, // Unfriendly — [2]
    0xFFFFFF00, // Neutral — [3]
    0xFF00FF00, // Friendly — [4..7]
    0xFF0000FF, // Player
    0xFF6060FF, // PlayerExtended — VAR_SELECTION_COLOR_UNFLAGGED
    0xFFAAAAFF, // Party
    0xFFAAFFAA, // PartyPvp
    0xFF53C8FF, // Friend
    0xFF7F7F7F, // Dead
};

// UnitReaction 0..7 → the rung its s_reactionColors entry belongs to.
constexpr Type kReactionTypes[8] = {Hostile, Hostile,  Unfriendly, Neutral,
                                    Friendly, Friendly, Friendly,   Friendly};

// --- the ladder -------------------------------------------------------------

bool CanAttack(void *attacker, void *target) {
    using Fn = bool(__thiscall *)(void *attacker, void *target);
    return reinterpret_cast<Fn>(static_cast<uintptr_t>(Offsets::FUN_UNIT_CAN_ATTACK))(attacker,
                                                                                      target);
}

bool IsPartyMember(uint64_t guid) {
    using Fn = int(__fastcall *)(const uint64_t *guid);
    return reinterpret_cast<Fn>(static_cast<uintptr_t>(Offsets::FUN_PARTY_IS_MEMBER))(&guid) != 0;
}

Type PlayerType(bool extended) { return extended ? PlayerExtended : Player; }

// The party and friend rungs, which only the extended ladder has.
bool GroupType(uint64_t guid, bool pvp, Type *out) {
    if (IsPartyMember(guid)) {
        *out = pvp ? PartyPvp : Party;
        return true;
    }
    if (FriendList::IsGuidFriend(guid)) {
        *out = Friend;
        return true;
    }
    return false;
}

// 4.3.4's no-object branch.
Type RemoteType(uint64_t guid, bool extended) {
    bool pvp = false;
    if (guid != 0) {
        using Lookup = const uint8_t *(__fastcall *)(const uint64_t *guid);
        const uint8_t *stats = reinterpret_cast<Lookup>(
            static_cast<uintptr_t>(Offsets::FUN_GROUP_MEMBER_STATS_LOOKUP))(&guid);
        if (stats != nullptr)
            pvp = (stats[Offsets::OFF_GROUP_MEMBER_STATUS_FLAGS] &
                   Offsets::GROUP_MEMBER_STATUS_PVP) != 0;
    }
    Type type;
    if (extended && GroupType(guid, pvp, &type))
        return type;
    return pvp ? Friendly : PlayerType(extended);
}

Type ForGuid(uint64_t guid, bool extended) {
    void *unit = guid != 0 ? Object::ByGuid(Offsets::TYPEMASK_UNIT, guid) : nullptr;
    void *viewer = const_cast<uint8_t *>(Unit::Identity::PlayerObject());
    if (unit == nullptr || viewer == nullptr)
        return RemoteType(guid, extended);

    const uint8_t *fields = Game::Read<const uint8_t *>(unit, Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return RemoteType(guid, extended);
    const uint32_t flags = Game::Read<uint32_t>(fields, Offsets::OFF_UNIT_FIELD_FLAGS);

    if ((flags & Offsets::UNIT_FLAG_PLAYER_CONTROLLED) == 0) {
        if (extended && Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) < 1)
            return Dead;
        using Reaction = int(__thiscall *)(void *unit, void *other);
        const int reaction = reinterpret_cast<Reaction>(
            static_cast<uintptr_t>(Offsets::FUN_UNIT_REACTION))(unit, viewer);
        return kReactionTypes[reaction & 7];
    }

    if (CanAttack(unit, viewer))
        return CanAttack(viewer, unit) ? Hostile : PlayerType(extended);
    if (CanAttack(viewer, unit))
        return Neutral;

    using IsPvp = bool(__thiscall *)(void *unit);
    const bool pvp =
        reinterpret_cast<IsPvp>(static_cast<uintptr_t>(Offsets::FUN_UNIT_IS_PVP_UI))(unit);
    // The engine skips the party test for the player itself; IsMember counts it.
    Type type;
    if (extended && guid != Unit::Identity::GuidForObject(viewer) && GroupType(guid, pvp, &type))
        return type;
    return pvp ? Friendly : PlayerType(extended);
}

// --- the engine's ladder: the friend rung -----------------------------------

uint32_t ExitColor(const void *unit, uintptr_t engineColor) {
    if (FriendList::IsGuidFriend(Unit::Identity::GuidForObject(unit)))
        return kColors[Friend];
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

// --- Lua ----------------------------------------------------------------------

// Reads the arguments; false (after raising) for a non-string unit.
bool ReadArgs(void *L, const char *usage, Type *out) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, usage);
        return false;
    }
    const uint64_t guid = Unit::Identity::GuidForToken(Game::Lua::ToString(L, 1));
    *out = ForGuid(guid, Game::Lua::ToBoolean(L, 2) != 0);
    return true;
}

int __fastcall Script_UnitSelectionColor(void *L) {
    Type type;
    if (!ReadArgs(L, "Usage: UnitSelectionColor(\"unit\")", &type))
        return 0;
    const uint32_t argb = kColors[type];
    Game::Lua::PushNumber(L, ((argb >> 16) & 0xFF) / 255.0);
    Game::Lua::PushNumber(L, ((argb >> 8) & 0xFF) / 255.0);
    Game::Lua::PushNumber(L, (argb & 0xFF) / 255.0);
    Game::Lua::PushNumber(L, (argb >> 24) / 255.0);
    return 4;
}

int __fastcall Script_UnitSelectionType(void *L) {
    Type type;
    if (!ReadArgs(L, "Usage: UnitSelectionType(\"unit\")", &type))
        return 0;
    Game::Lua::PushNumber(L, type);
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken", "The unit to read."),
    Game::Doc::Opt("useExtendedColors", "bool", "false",
                   "True adds the party, friend and dead colours the name and circle use."),
};
const Game::Doc::Field kColorRets[] = {
    Game::Doc::Req("red", "number", "Red, 0 to 1."),
    Game::Doc::Req("green", "number", "Green, 0 to 1."),
    Game::Doc::Req("blue", "number", "Blue, 0 to 1."),
    Game::Doc::Req("alpha", "number", "Alpha, 0 to 1."),
};
const Game::Doc::Function kUnitSelectionColor{
    "The colour of the unit's selection circle.", kArgs, kColorRets, "UnitGlobals"};

const Game::Doc::Field kTypeRets[] = {
    Game::Doc::Req("selectionType", "number",
                   "0 hostile, 1 unfriendly, 2 neutral, 3 friendly, 4 player, 5 player "
                   "(extended), 6 party, 7 party PvP, 8 friend, 9 dead."),
};
const Game::Doc::Function kUnitSelectionType{
    "Which selection colour the unit takes.", kArgs, kTypeRets, "UnitGlobals"};

void Register() {
    static const uint8_t kPvpLoad[5] = {0xA1, 0xB4, 0xD8, 0xC4, 0x00};
    static const uint8_t kUnflaggedLoad[6] = {0x8B, 0x0D, 0x4C, 0xDA, 0xC4, 0x00};
    Common::ReplaceWithCall(Offsets::SITE_SELECTION_COLOR_PVP_EXIT, kPvpLoad, sizeof kPvpLoad,
                            reinterpret_cast<const void *>(&PvpExitThunk));
    Common::ReplaceWithCall(Offsets::SITE_SELECTION_COLOR_UNFLAGGED_EXIT, kUnflaggedLoad,
                            sizeof kUnflaggedLoad,
                            reinterpret_cast<const void *>(&UnflaggedExitThunk));

    Game::Lua::RegisterGlobalFunction("UnitSelectionColor", &Script_UnitSelectionColor,
                                      &kUnitSelectionColor);
    Game::Lua::RegisterGlobalFunction("UnitSelectionType", &Script_UnitSelectionType,
                                      &kUnitSelectionType);
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Unit::SelectionColor
