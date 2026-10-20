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

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "spell/Cancel.h"

#include <cstdint>

namespace Unit::ShapeshiftForm {

namespace {

// Read the local player's current shapeshift form byte. Returns
// `-1` if the player CGUnit / descriptor isn't resolvable (yet) —
// distinct from `0` (resolved but not shapeshifted) so the hook
// can tell "transient unresolved" from "left a form" and not fire
// a bogus change event during early-login windows.
int ReadCurrentForm() {
    auto *player = static_cast<const uint8_t *>(Game::ResolveUnitToken("player"));
    if (player == nullptr)
        return -1;
    auto *fields = *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_UNIT_DESCRIPTOR);
    if (fields == nullptr)
        return -1;
    const uint32_t bytes1 = *reinterpret_cast<const uint32_t *>(
        fields + Offsets::OFF_UNIT_FIELD_BYTES_1);
    return static_cast<int>((bytes1 >> 16) & 0xFF);
}

// `GetShapeshiftFormID()` — returns the player's current shapeshift
// form ID from vanilla 1.12.1's `SpellShapeshiftForm.dbc`:
//   Druid:   1 Cat, 2 Tree, 3 Travel, 4 Aquatic, 5 Bear,
//            8 Dire Bear, 31 Moonkin
//   Warrior: 17 Battle, 18 Defensive, 19 Berserker
//   Shaman:  16 Ghost Wolf
//   Rogue:   30 Stealth
//   Priest:  28 Shadowform, 32 Spirit of Redemption
// Turtle WoW extends the DBC with custom rows: 9 Tree of Life Form,
// 11 Swift Travel Form. Both slots are empty on Blizzard 1.12.1.
// Returns `0` when the player isn't shapeshifted. **These are the
// 1.12 values; modern WoW renumbered the table** (e.g. modern
// Travel=17, Tree=35) — addons backporting modern code shouldn't
// reuse the constants verbatim.
//
// Reads byte 2 of `UNIT_BYTES_1` (descriptor `+0x212`) on the local
// player. Derived from `Script_GetShapeshiftFormInfo` at `0x004B45C0`,
// which uses the same byte to compare against each form-spell's
// effect-encoded form ID when answering "is this form active". The
// engine sets it from `SMSG_UPDATE_OBJECT` aura/form updates, so it
// tracks live state without us needing to listen on any event.
//
// Vanilla 1.12 has no native `GetShapeshiftFormID` — only the
// 1-based bar-index `GetShapeshiftFormInfo`. Modern API lets callers
// key behavior on the DBC ID directly (`if formID == CAT_FORM then`)
// without needing to iterate the bar.
int __fastcall Script_GetShapeshiftFormID(void *L) {
    const int form = ReadCurrentForm();
    Game::Lua::PushNumber(L, form < 0 ? 0.0 : static_cast<double>(form));
    return 1;
}

// `CancelShapeshiftForm()` — drops whatever shapeshift the player is
// currently in. Walks the player's buff slots, scans each spell's
// `EffectApplyAuraName[3]` for `SPELL_AURA_MOD_SHAPESHIFT` paired with
// an `EffectMiscValue` matching the current form byte, and sends a
// `CMSG_CANCEL_AURA` for the spellID that owns that aura.
//
// Mirrors 3.3.5's `Script_CancelShapeshiftForm` inner (`FUN_00726CE0`),
// which does the same effect-array scan + form-id match.
//
// **Warrior stances are not cancelable.** Vanilla treats warriors as
// always-in-a-stance — the engine doesn't expose a "no stance" state
// and right-clicking the active stance in the stance bar does
// nothing. The server rejects the cancel even when the request is
// well-formed, so this function is a no-op for stances regardless of
// approach. All other shapeshift forms (druid, shaman, priest, rogue)
// are real visible buffs on the aura array and cancel cleanly.
//
// No-op if the player isn't in a form, can't be resolved, or no
// matching aura is found.
int __fastcall Script_CancelShapeshiftForm(void *L) {
    const int form = ReadCurrentForm();
    if (form > 0)
        Spell::Cancel::CancelPlayerAura(Offsets::SPELL_AURA_MOD_SHAPESHIFT, form);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetShapeshiftFormID", &Script_GetShapeshiftFormID);
    Game::Lua::RegisterGlobalFunction("CancelShapeshiftForm", &Script_CancelShapeshiftForm);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

// `UPDATE_SHAPESHIFT_FORM` — synthesized payload-less event that
// fires whenever the local player's form byte changes, matching
// modern WoW's semantics. Vanilla 1.12 has only the plural
// `UPDATE_SHAPESHIFT_FORMS` (which modern uses for "available forms
// list changed"); the singular per-form-change variant is added by
// later expansions and was not present here.
//
// Hook target: `FUN_BONUS_ACTIONBAR_UPDATE` (0x004E4FC0). The engine
// calls this every time it refreshes the bonus action bar — once
// post-login from `FUN_004908C0`, and per-update from the
// local-player UpdateField dispatch at `0x005DE01B`. It runs AFTER
// the descriptor's form byte is written, so reading the byte from
// the post-hook gives the new value.
//
// The byte-comparison gate matters: this engine helper also fires
// `UPDATE_BONUS_ACTIONBAR` when other UNIT_BYTES_1 bytes change
// (standstate, etc.), and we don't want to spam the event for
// non-form updates. We also distinguish "unresolved" (-1) from "0
// no-form" so transient resolve failures during login don't look
// like leaving a form.
constexpr const char *kEventName = "UPDATE_SHAPESHIFT_FORM";
const Event::Custom::AutoReserve _reserve{kEventName};

int s_lastForm = -1;

using BonusActionBarUpdate_t = void(__fastcall *)();
BonusActionBarUpdate_t BonusActionBarUpdate_o = nullptr;

void __fastcall BonusActionBarUpdate_h() {
    BonusActionBarUpdate_o();
    const int current = ReadCurrentForm();
    if (current < 0 || current == s_lastForm)
        return;
    s_lastForm = current;
    Event::Custom::Fire(_reserve.Slot(), "");
}

} // namespace

static const Game::HookAutoRegister _hookreg{
    Offsets::FUN_BONUS_ACTIONBAR_UPDATE,
    reinterpret_cast<void *>(&BonusActionBarUpdate_h),
    reinterpret_cast<void **>(&BonusActionBarUpdate_o)};

} // namespace Unit::ShapeshiftForm
