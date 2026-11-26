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

// Auto-loot as 3.80.2 decides it: the autoLootDefault cvar, the
// AUTOLOOTTOGGLE modified click, and camelot's (_forever_ 1.60.1)
// C_AutoLoot.SetUseAutoLootToggle(useAutoLootToggle).
//
// 1.12 decides at two places, each by asking EventIsKeyDown for Shift: the
// world right-click (OnSpriteRightClick, which InteractUnit and
// C_PlayerInteractionManager.InteractUnit also run) passes the answer to the
// object's interact, and OpenItem (a container item with loot) passes it to
// CGPlayer_C::SetAutoLoot. The interact hands it on to SetAutoLoot itself, or
// through the gathering cast, and the loot response then loots everything
// while the player's auto-loot byte is set. Both reads are redirected here
// (Offsets::CALL_*_AUTOLOOT_KEY); the rest of the chain is the engine's.
//
// The answer is 3.80.2's (0x14169F330): autoLootDefault XOR the toggle. The
// toggle is IsModifiedClick("AUTOLOOTTOGGLE") (Shift by default, as 1.12's
// fixed key is), so a rebound modifier now counts. With no modifier set,
// 3.80.2 asks a binding lookup instead (0x141252750 / 0x1412487A0), which
// this port does not follow: the toggle then reads as not held.
//
// Two other DLLs change the same chain, and this works with both:
//   - vanilla-tweaks' quickloot patch, which Turtle's WoW.exe ships with,
//     flips both readers of the byte, so a CLEAR byte sweeps. The decision
//     is written in whichever polarity the readers have, read live
//     (Offsets::SITE_LOOT_OPEN_AUTOLOOT_JCC), and autoLootDefault defaults
//     to 1 on a flipped exe: out of the box, plain loots and the modifier
//     opens the window, as the patch alone does.
//   - SuperWoW's SetAutoloot ORs its flag into the byte when the loot
//     window opens, after this decision. Its flag then acts as it does
//     without ClassicAPI on either exe.
//
// SetUseAutoLootToggle is camelot's only (3.80.2 and retail have no
// C_AutoLoot). Its client's code is protected (no plain reference even to
// GetSpellCooldown's name), so its effect is read from its Lua, and it is
// NOT verified against the engine:
//   - Blizzard_GamepadActionBars (MakeInteractAction) sets it to
//     `autoLootOnTap ~= isHoldAction` before each interact, with
//     autoLootOnTap = autoLootDefault, and after a hold sets it back to
//     autoLootOnTap. A tap then follows the cvar and a hold the opposite,
//     and between interacts the value equals the cvar.
//   - ContainerFrame's held open sets true, opens, sets false.
// The reading that fits both: the value is whether to auto-loot, in place
// of autoLootDefault, and the modifier still flips it. ("Counts as the
// modifier held" would make a tap never auto-loot and a hold always, at
// either cvar value, and would invert every mouse click after a pad
// interact while autoLootDefault is 1.) It stays until the next call;
// before the first, and after a reload, autoLootDefault decides.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"
#include "bindings/ModifiedClick.h"
#include "cvar/Factory.h"
#include "lua/Arg.h"

#include <cstdint>

namespace Loot::AutoLoot {

namespace {

CVar::Factory::Handle g_default = nullptr;
// SetUseAutoLootToggle's value: -1 before any call, else 0 / 1.
int g_use = -1;

// True when the readers sweep on a clear byte (the quickloot patch).
bool ReadersFlipped() {
    return Game::Read<uint8_t>(Offsets::SITE_LOOT_OPEN_AUTOLOOT_JCC) == Offsets::JCC_OPCODE_JNZ;
}

// Stands in for `EventIsKeyDown(0)` at both sites: __fastcall(key), 0 or 1
// (OpenItem tests the result `== 1`). Returns the byte that makes the
// readers do what was decided.
uint32_t __fastcall AutoLootKey_h(uint32_t /*key*/) {
    const bool use = g_use >= 0 ? g_use != 0 : CVar::Factory::GetInt(g_default, 0) != 0;
    const bool toggle = Bindings::ModifiedClick::IsModifiedClick("AUTOLOOTTOGGLE");
    const bool autoLoot = use != toggle;
    return autoLoot != ReadersFlipped() ? 1u : 0u;
}

constexpr const char *kUsage = "Usage: C_AutoLoot.SetUseAutoLootToggle(useAutoLootToggle)";

int __fastcall Script_SetUseAutoLootToggle(void *L) {
    bool use = false;
    if (!Game::Lua::Arg::Bool(L, 1, kUsage, use))
        return 0;
    g_use = use ? 1 : 0;
    return 0;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("useAutoLootToggle", "bool",
                   "True to auto-loot, false to open the loot window. The auto-loot modifier "
                   "still gives the opposite."),
};
const Game::Doc::Function kDoc{
    "Sets whether interactions and opened items auto-loot, in place of the autoLootDefault "
    "setting, until the next call.",
    kArgs, {}, "AutoLoot"};

// Cvars are process-global; the first glue boot registers it before any world.
void Register() {
    g_default = CVar::Factory::Register("autoLootDefault",
                                        "Automatically loot items when the loot window opens",
                                        Offsets::CVAR_CATEGORY_GAME,
                                        ReadersFlipped() ? "1" : "0");
}

const Game::GlueModuleAutoRegister _glue{&Register};

void RegisterLuaFunctions() {
    Common::RedirectCall(Offsets::CALL_SPRITE_RIGHT_CLICK_AUTOLOOT_KEY,
                         Offsets::FUN_GAMEUI_IS_KEY_DOWN,
                         reinterpret_cast<const void *>(&AutoLootKey_h));
    Common::RedirectCall(Offsets::CALL_OPEN_ITEM_AUTOLOOT_KEY, Offsets::FUN_GAMEUI_IS_KEY_DOWN,
                         reinterpret_cast<const void *>(&AutoLootKey_h));
    Game::Lua::RegisterTableFunction("C_AutoLoot", "SetUseAutoLootToggle",
                                     &Script_SetUseAutoLootToggle, &kDoc);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

void PrepareForReload() { g_use = -1; }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace Loot::AutoLoot
