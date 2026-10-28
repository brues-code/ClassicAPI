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

// `IsPetAttackAction(index)` — true when the 1-based pet bar slot holds the
// Attack command.
//
// 4.3.4's Script_IsPetAttackAction (0x00952EB0) pushes CGPetInfo::
// IsAttackAction: a pet exists, the index is below 10, and the slot word is
// command 2. That is 1.12's Script_IsPetAttackActive (0x004BE0E0) without
// its attack-latch test, so the slot test below is the 1.12 one: type in
// byte 3 & 0x3F, action in the low 16 bits.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Pet::ActionBar {

namespace {

bool IsAttackAction(uint32_t index) {
    if (Game::Read<uint64_t>(Offsets::VAR_PET_GUID) == 0 ||
        index >= Offsets::PET_ACTION_SLOT_COUNT)
        return false;
    const uint32_t word = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PET_ACTION_SLOTS))[index];
    return ((word >> 24) & 0x3F) == Offsets::PET_ACTION_TYPE_COMMAND &&
           (word & 0xFFFF) == Offsets::PET_ACTION_COMMAND_ATTACK;
}

int __fastcall Script_IsPetAttackAction(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: IsPetAttackAction(index)");
        return 0;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, IsAttackAction(static_cast<uint32_t>(index - 1)));
    return 1;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("index", "number", "The pet bar slot, 1 to 10."),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isAttack", "boolean", "True if the slot holds the Attack command."),
};
const Game::Doc::Function kIsPetAttackAction{
    "Returns true if a pet bar slot holds the Attack command.",
    kArgs, kRets, "PetGlobals"};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsPetAttackAction", &Script_IsPetAttackAction,
                                      &kIsPetAttackAction);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Pet::ActionBar
