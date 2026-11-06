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

// `C_MountJournal.GetMountFromSpell(spellID)` -> mountID | nil
// `C_MountJournal.GetMountInfoExtraByID(mountID)` -> creatureDisplayInfoID,
//     description, source, isSelfMount, mountTypeID, uiModelSceneID, animID,
//     spellVisualKitID, disablePlayerMountPreview
//
// Retail's mount ID indexes its Mount table. 1.12 has no such table, so here
// a mount's ID IS its spell ID: GetMountFromSpell(458) returns 458. That
// keeps every mount the client knows addressable (Turtle's own mounts are
// in no retail table), and retail's numbering means nothing to a 1.12 addon.
//
// A spell is a mount when one of its effects applies SPELL_AURA_MOUNTED
// (78). Spell.dbc is read directly, so a mount not yet learned resolves too.
// On Turtle a mount ITEM does not carry its mount spell: every one carries
// 46499 "Add Mount to Collection", which teaches a spell from a server-only
// item table. So only the mount spell (spellbook, spell link) resolves; 46499
// is not a mount. See docs/notes/model-scene.md.
//
// creatureDisplayInfoID is the display the server mounts the caster on: the
// aura's misc value is a creature entry, and the server takes a display from
// that creature's template (tortoise-wow Aura::HandleAuraMounted ->
// Creature::ChooseDisplayId). The client's creature cache holds the
// template's first display (SMSG_CREATURE_QUERY_RESPONSE sends display_id[0])
// and every mount creature has exactly one, so the cached display is the
// mounted one (23219 Swift Mistsaber -> creature 14555 -> 14332). An
// uncached creature returns nil and starts the cache load;
// CREATURE_DATA_LOAD_RESULT fires when it lands, as for
// C_CreatureInfo.RequestLoadCreatureByID.
//
// The rest follows the retail Mount table where a 1.12 mount spell has a
// row (kRetailRows), else retail's common values: mount type 230 (ground),
// model scene 4, seat animation 91 (Mount — what a retail probe returned for
// every mount, and what 1.12's world mount seats the rider with), no visual
// kit. 1.12 has no self-mounts and no shop source text. The description is
// the spell's own, tokens filled in.

#include "Game.h"
#include "Offsets.h"
#include "creature/Info.h"
#include "spell/Arg.h"
#include "spell/Description.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Mount::Journal {

namespace {

// Retail Mount rows (12.1.7.70740 export) for 1.12 mount spells whose scene
// or mount type differs from the defaults below. Only rows whose spell has
// the same name in Turtle's Spell.dbc: retail's 3363 (Nether-Swept Drake) is
// Turtle's "Summon Riding Gryphon", so it is left out. The other 92 of the
// 100 matching spells are scene 4 / type 230.
struct RetailRow {
    uint32_t spellID;
    uint32_t mountTypeID;
    uint32_t uiModelSceneID;
};
constexpr RetailRow kRetailRows[] = {
    {25863, 230, 44}, // Black Qiraji Battle Tank
    {25953, 241, 44}, // Blue Qiraji Battle Tank
    {26054, 241, 44}, // Red Qiraji Battle Tank
    {26055, 241, 44}, // Yellow Qiraji Battle Tank
    {26056, 241, 44}, // Green Qiraji Battle Tank
    {26655, 230, 44}, // Black Qiraji Battle Tank
    {30174, 231, 4},  // Riding Turtle
};
constexpr uint32_t kDefaultMountTypeID = 230;
constexpr uint32_t kDefaultUiModelSceneID = 4;
constexpr uint32_t kSeatAnimation = 91; // Mount

// The mount spell's record for `mountID`, or null when it is not a mount.
const uint8_t *MountRecord(int mountID) {
    const uint8_t *record = Spell::Lookup::RecordForID(mountID);
    return Spell::Lookup::HasAuraEffect(record, Offsets::SPELL_AURA_MOUNTED) ? record : nullptr;
}

// The display the mount aura's creature is cached with, 0 when the creature
// is not cached yet (and then its load is started).
uint32_t MountDisplayID(const uint8_t *record) {
    const int effect = Spell::Lookup::FindAuraEffect(record, Offsets::SPELL_AURA_MOUNTED);
    const int creatureID =
        Game::Read<int>(record, Offsets::OFF_SPELL_RECORD_EFFECT_MISC_VALUE + effect * 4);
    if (creatureID <= 0)
        return 0;
    const uint8_t *creature = Creature::Info::CacheRecord(static_cast<uint32_t>(creatureID));
    if (creature == nullptr) {
        Creature::Info::RequestLoad(static_cast<uint32_t>(creatureID));
        return 0;
    }
    return Game::Read<uint32_t>(creature, Offsets::OFF_CREATURE_DISPLAYID);
}

int __fastcall Script_GetMountFromSpell(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (MountRecord(spellID) == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 1;
}

int __fastcall Script_GetMountInfoExtraByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    const int mountID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const uint8_t *record = MountRecord(mountID);
    if (record == nullptr)
        return 0;

    uint32_t mountTypeID = kDefaultMountTypeID;
    uint32_t uiModelSceneID = kDefaultUiModelSceneID;
    for (const RetailRow &row : kRetailRows) {
        if (row.spellID == static_cast<uint32_t>(mountID)) {
            mountTypeID = row.mountTypeID;
            uiModelSceneID = row.uiModelSceneID;
            break;
        }
    }

    if (const uint32_t displayID = MountDisplayID(record))
        Game::Lua::PushNumber(L, static_cast<double>(displayID));
    else
        Game::Lua::PushNil(L);
    char description[1024];
    Spell::Description::Expand(record, description, sizeof description);
    Game::Lua::PushString(L, description);
    Game::Lua::PushString(L, "");   // source
    Game::Lua::PushBool(L, false);  // isSelfMount
    Game::Lua::PushNumber(L, static_cast<double>(mountTypeID));
    Game::Lua::PushNumber(L, static_cast<double>(uiModelSceneID));
    Game::Lua::PushNumber(L, static_cast<double>(kSeatAnimation));
    Game::Lua::PushNumber(L, 0);    // spellVisualKitID
    Game::Lua::PushBool(L, false);  // disablePlayerMountPreview
    return 9;
}

// --- Documentation ----------------------------------------------------------

const Game::Doc::Field kGetMountFromSpellArgs[] = {
    Game::Doc::Req("spellID", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
const Game::Doc::Field kGetMountFromSpellRets[] = {
    Game::Doc::Opt("mountID", "number", nullptr,
                   "The mount's ID, which is its spell ID. Nil when the spell is not a mount."),
};
const Game::Doc::Function kGetMountFromSpell{
    "The mount a spell summons. A mount's ID is the ID of the spell that summons it.",
    kGetMountFromSpellArgs, kGetMountFromSpellRets};

const Game::Doc::Field kGetMountInfoExtraByIDArgs[] = {
    Game::Doc::Req("mountID", "number", "A mount ID, which is the mount's spell ID."),
};
const Game::Doc::Field kGetMountInfoExtraByIDRets[] = {
    Game::Doc::Opt("creatureDisplayInfoID", "number", nullptr,
                   "The display the player is mounted on. Nil until the mount's creature "
                   "is cached; the call starts loading it, and CREATURE_DATA_LOAD_RESULT "
                   "fires when it arrives."),
    Game::Doc::Req("description", "cstring", "The mount spell's description."),
    Game::Doc::Req("source", "cstring", "Always empty."),
    Game::Doc::Req("isSelfMount", "bool", "Always false."),
    Game::Doc::Req("mountTypeID", "number", "The mount type; 230 for most mounts."),
    Game::Doc::Req("uiModelSceneID", "number", "The model scene that shows the mount."),
    Game::Doc::Req("animID", "number", "The animation the rider plays while mounted."),
    Game::Doc::Req("spellVisualKitID", "number", "Always 0."),
    Game::Doc::Req("disablePlayerMountPreview", "bool", "Always false."),
};
const Game::Doc::Function kGetMountInfoExtraByID{
    "Display and preview details of a mount. Returns nothing when the ID is not a mount.",
    kGetMountInfoExtraByIDArgs, kGetMountInfoExtraByIDRets};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_MountJournal", "GetMountFromSpell",
                                     &Script_GetMountFromSpell, &kGetMountFromSpell);
    Game::Lua::RegisterTableFunction("C_MountJournal", "GetMountInfoExtraByID",
                                     &Script_GetMountInfoExtraByID, &kGetMountInfoExtraByID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Mount::Journal
