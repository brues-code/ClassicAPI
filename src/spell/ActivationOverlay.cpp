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

// `C_SpellActivationOverlay.IsSpellOverlayed(spellID)` / `IsSpellOverlayed`
// and the SPELL_ACTIVATION_OVERLAY_SHOW / _HIDE / _GLOW_SHOW / _GLOW_HIDE
// events — 4.3.4's `CGSpellActivationOverlay`, without its FrameXML: drawing
// the alerts and the button glows is the addon's job.
//
// 4.3.4 keys everything off SpellActivationOverlay.dbc rows, with two triggers:
//   - an aura on the player (triggerType 0): `OnAuraAdded` fires SHOW with the
//     row's art and glows every book spell the row's class mask selects;
//     `OnAuraRemoved` fires HIDE and drops the glows.
//   - a learned spell turning usable (triggerType 1): `UpdateUsable`, run next
//     to `CGSpellBook::UpdateUsable` on target, combo-point, aura-state, health
//     and power changes, retests each such spell and glows what its mask
//     selects while it is usable.
// GLOW_SHOW / GLOW_HIDE fire on a spell's glowing ↔ not-glowing transitions,
// and `IsSpellOverlayed` answers "glowing now". After PLAYER_ENTERING_WORLD,
// `EnterWorld` fires HIDE with no spell, fires SHOW again for every active
// aura row, and reconciles the glows (which survive a /reload — FrameXML's
// action buttons ask `IsSpellOverlayed` when they update).
//
// 1.12 has no SpellActivationOverlay.dbc. The triggers come from:
//   - Usable: the engine's reactive list (VAR_REACTIVE_SPELL_ENTRIES), the
//     AttributesEx4 0x200 spells `FireUsableEvents` retests from the same
//     triggers. It is the 1.12 form of the triggerType-1 rows: 4.3.4 lists
//     Overpower, Revenge, Execute and Hammer of Wrath; 1.12 flags those plus
//     Riposte, Counterattack and Mongoose Bite. Turtle's Spell.dbc clears the
//     bit on Mongoose Bite and sets it on Arcane Surge, warrior Counterattack,
//     Kill Command and Surprise Attack. We read the answers the engine has just stored, at its
//     SPELL_UPDATE_USABLE signal, rather than retest: the verdict is
//     FUN_SPELL_IS_USABLE's, aura states and combo points included (Overpower
//     is gated by the hidden combo point the server grants on a dodge). A
//     usable entry glows every book spell with its name — the list's own
//     identity rule — so a bar slot holding any rank glows. 4.3.4's
//     triggerType-1 rows have no art (ScreenLocation 0), so these fire no SHOW.
//   - Aura: `kAuraRows`, the Cata rows (Cata Classic 4.4.2's table, the
//     nearest Blizzard data to 4.3.4) whose spell 1.12 has under the same ID
//     and name. Of 83, two qualify — Shadow Trance and the druid's
//     Clearcasting — and Shadow Trance's mask selects Shadow Bolt in 1.12's
//     SpellFamilyFlags too (stock and Turtle Spell.dbc, parsed). The other
//     81 are spells 1.12 lacks; Era carries no such table at all.
//
// The active aura rows are read from the player's aura descriptor, not
// counted from add / remove calls: a map change clears the descriptor without
// per-aura removals (see Aura::Source), so a count would stick. The
// descriptor already holds the change when Aura::Source's observers run.
//
// The SHOW payload is retail's with one substitution: 1.12 has no
// FileDataIDs, so the art arrives as its texture path (as LossOfControl
// passes spell icons). `locationType` is Enum.ScreenLocationType.

#include "Game.h"
#include "Offsets.h"
#include "aura/Source.h"
#include "event/Custom.h"
#include "event/EnteringWorld.h"
#include "event/SignalHook.h"
#include "spell/Lookup.h"
#include "unit/Identity.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace Spell::ActivationOverlay {

namespace {

// Enum.ScreenLocationType.
enum ScreenLocation {
    CENTER = 0,
    LEFT = 1,
    RIGHT = 2,
    TOP = 3,
    BOTTOM = 4,
    TOP_LEFT = 5,
    TOP_RIGHT = 6,
    LEFT_OUTSIDE = 7,
    RIGHT_OUTSIDE = 8,
    LEFT_RIGHT = 9,
    TOP_BOTTOM = 10,
    LEFT_RIGHT_OUTSIDE = 11,
};

struct AuraRow {
    uint32_t spellID;    // the aura on the player
    const char *texture; // the row's FileData path
    int location;        // ScreenLocation
    double scale;
    uint32_t color;      // 0xRRGGBB
    uint64_t classMask;  // book spells of the aura's family with any of these flags glow
};

// Cata ScreenLocation 9 is "Left + Right (Flipped)", retail's LeftRight.
const AuraRow kAuraRows[] = {
    // Row 144: Shadow Trance (from Nightfall) glows Shadow Bolt (warlock 0x1).
    {17941, "Textures\\SpellActivationOverlays\\Nightfall", LEFT_RIGHT, 1.0, 0xFFFFFF, 0x1},
    // Row 148: Clearcasting (from Omen of Clarity); its mask is empty.
    {16870, "Textures\\SpellActivationOverlays\\Natures_Grace", LEFT_RIGHT, 1.0, 0xFFFFFF, 0},
};
constexpr int kAuraRowCount = sizeof(kAuraRows) / sizeof(kAuraRows[0]);

// --- Events -----------------------------------------------------------------

const Game::Doc::Field kShowPayload[] = {
    Game::Doc::Req("spellID", "number", "The aura that triggered the alert."),
    Game::Doc::Req("texture", "string", "Texture path of the alert art."),
    Game::Doc::Req("locationType", "ScreenLocationType", "Where the art goes around the screen center."),
    Game::Doc::Req("scale", "number"),
    Game::Doc::Req("r", "number", "Red tint, 0 to 255."),
    Game::Doc::Req("g", "number", "Green tint, 0 to 255."),
    Game::Doc::Req("b", "number", "Blue tint, 0 to 255."),
};
const Game::Doc::Event kShowDoc{
    "SpellActivationOverlay", "Fires when a proc aura lands on the player and its alert art should show.",
    kShowPayload};

const Game::Doc::Field kHidePayload[] = {
    Game::Doc::Opt("spellID", "number", nullptr, "The aura whose alert ends; nil ends every alert."),
};
const Game::Doc::Event kHideDoc{
    "SpellActivationOverlay", "Fires when a proc aura leaves the player and its alert art should hide.",
    kHidePayload};

const Game::Doc::Field kGlowPayload[] = {
    Game::Doc::Req("spellID", "number", "The spell whose action buttons glow."),
};
const Game::Doc::Event kGlowShowDoc{
    "SpellActivationOverlay", "Fires when a spell starts to glow on the action bar.", kGlowPayload};
const Game::Doc::Event kGlowHideDoc{
    "SpellActivationOverlay", "Fires when a spell stops glowing on the action bar.", kGlowPayload};

const Event::Custom::AutoReserve g_show{"SPELL_ACTIVATION_OVERLAY_SHOW", &kShowDoc};
const Event::Custom::AutoReserve g_hide{"SPELL_ACTIVATION_OVERLAY_HIDE", &kHideDoc};
const Event::Custom::AutoReserve g_glowShow{"SPELL_ACTIVATION_OVERLAY_GLOW_SHOW", &kGlowShowDoc};
const Event::Custom::AutoReserve g_glowHide{"SPELL_ACTIVATION_OVERLAY_GLOW_HIDE", &kGlowHideDoc};

// --- State ------------------------------------------------------------------

// Inputs at the last reconcile: bit i = kAuraRows[i]'s aura on the player;
// the reactive entries the engine called usable.
uint32_t g_activeRows = 0;
std::vector<uint32_t> g_usable;
// This pass's read of the reactive list; swapped with g_usable on a change,
// so a SPELL_UPDATE_USABLE that changes nothing (every power tick) allocates
// nothing.
std::vector<uint32_t> g_read;
// Published: bit i = SHOW sent for row i with no HIDE since; the sorted
// spell IDs with GLOW_SHOW sent and no GLOW_HIDE since.
uint32_t g_shownRows = 0;
std::vector<uint32_t> g_glowing;

bool g_reconciling = false;
bool g_again = false;

const char *SpellName(const uint8_t *rec) {
    const int locale = Game::Read<int>(Offsets::VAR_LOCALE_INDEX);
    return Game::Read<const char *>(rec, Offsets::OFF_SPELL_NAMES + locale * 4);
}

uint32_t ActiveRows() {
    const uint8_t *player = Unit::Identity::PlayerObject();
    if (player == nullptr)
        return 0;
    const auto *desc = Game::Read<const uint8_t *>(player, Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return 0;
    uint32_t rows = 0;
    for (int slot = 0; slot < Offsets::UNIT_AURA_TOTAL; ++slot) {
        const uint32_t id = Game::Read<uint32_t>(desc, Offsets::OFF_UNIT_FIELD_AURA + slot * 4);
        for (int i = 0; id != 0 && i < kAuraRowCount; ++i) {
            if (kAuraRows[i].spellID == id)
                rows |= 1u << i;
        }
    }
    return rows;
}

void UsableReactive(std::vector<uint32_t> &out) {
    out.clear();
    const auto *entries = Game::Read<const uint8_t *>(Offsets::VAR_REACTIVE_SPELL_ENTRIES);
    if (entries == nullptr)
        return;
    const uint32_t count = Game::Read<uint32_t>(Offsets::VAR_REACTIVE_SPELL_COUNT);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = entries + i * Offsets::REACTIVE_SPELL_ENTRY_STRIDE;
        if (Game::Read<uint32_t>(entry, Offsets::OFF_REACTIVE_SPELL_ENTRY_USABLE) != 0)
            out.push_back(Game::Read<uint32_t>(entry, 0));
    }
}

// The book spells the active sources glow, sorted. 4.3.4 matches each source
// against its glowable list — every spell added to the book — so this walks
// the book the same way.
std::vector<uint32_t> GlowTargets(uint32_t rows, const std::vector<uint32_t> &usable) {
    std::vector<uint32_t> out;
    if (rows == 0 && usable.empty())
        return out;

    struct Mask {
        uint32_t family;
        uint64_t flags;
    };
    std::vector<Mask> masks;
    for (int i = 0; i < kAuraRowCount; ++i) {
        const uint8_t *aura = Spell::Lookup::RecordForID(static_cast<int>(kAuraRows[i].spellID));
        if ((rows & (1u << i)) != 0 && kAuraRows[i].classMask != 0 && aura != nullptr)
            masks.push_back({Game::Read<uint32_t>(aura, Offsets::OFF_SPELL_RECORD_FAMILY_NAME),
                             kAuraRows[i].classMask});
    }
    std::vector<const char *> names;
    for (uint32_t id : usable) {
        const uint8_t *rec = Spell::Lookup::RecordForID(static_cast<int>(id));
        if (rec != nullptr && SpellName(rec) != nullptr)
            names.push_back(SpellName(rec));
    }

    const int count = std::min(Game::Read<int>(Offsets::VAR_PLAYER_SPELLBOOK_COUNT),
                               static_cast<int>(Offsets::SPELLBOOK_MAX_SLOTS));
    for (int slot = 0; slot < count; ++slot) {
        const int id = Game::Read<int>(Offsets::VAR_PLAYER_SPELLBOOK + slot * 4);
        const uint8_t *rec = Spell::Lookup::RecordForID(id);
        if (rec == nullptr)
            continue;
        bool glow = false;
        for (const Mask &m : masks)
            glow = glow || Spell::Lookup::IsFitToFamily(rec, m.family, m.flags);
        const char *name = SpellName(rec);
        for (const char *n : names)
            glow = glow || (name != nullptr && std::strcmp(name, n) == 0);
        if (glow)
            out.push_back(static_cast<uint32_t>(id));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Step(bool enterWorld) {
    const uint32_t rows = ActiveRows();
    UsableReactive(g_read);
    if (!enterWorld && rows == g_activeRows && g_read == g_usable)
        return;
    g_activeRows = rows;
    g_usable.swap(g_read);

    // Art first, then glows — 4.3.4's order on both triggers.
    if (enterWorld) {
        Event::Custom::Fire(g_hide.Slot(), "");
        g_shownRows = 0;
    }
    const uint32_t wasShown = g_shownRows;
    g_shownRows = rows;
    for (int i = 0; i < kAuraRowCount; ++i) {
        const uint32_t bit = 1u << i;
        const AuraRow &row = kAuraRows[i];
        if ((rows & bit) != 0 && (wasShown & bit) == 0)
            Event::Custom::Fire(g_show.Slot(), "%d%s%d%f%d%d%d", static_cast<int>(row.spellID),
                                row.texture, row.location, row.scale,
                                static_cast<int>((row.color >> 16) & 0xFF),
                                static_cast<int>((row.color >> 8) & 0xFF),
                                static_cast<int>(row.color & 0xFF));
        else if ((rows & bit) == 0 && (wasShown & bit) != 0)
            Event::Custom::Fire(g_hide.Slot(), "%d", static_cast<int>(row.spellID));
    }

    // Published before the events, so a handler's IsSpellOverlayed agrees.
    std::vector<uint32_t> was = std::move(g_glowing);
    g_glowing = GlowTargets(rows, g_usable);
    std::vector<uint32_t> stopped, started;
    std::set_difference(was.begin(), was.end(), g_glowing.begin(), g_glowing.end(),
                        std::back_inserter(stopped));
    std::set_difference(g_glowing.begin(), g_glowing.end(), was.begin(), was.end(),
                        std::back_inserter(started));
    for (uint32_t id : stopped)
        Event::Custom::Fire(g_glowHide.Slot(), "%d", static_cast<int>(id));
    for (uint32_t id : started)
        Event::Custom::Fire(g_glowShow.Slot(), "%d", static_cast<int>(id));
}

// A handler of one of our events can change what we read (a target change
// re-runs FireUsableEvents); that request runs once the outer pass is done.
void Reconcile(bool enterWorld) {
    if (g_reconciling) {
        g_again = true;
        return;
    }
    g_reconciling = true;
    Step(enterWorld);
    while (g_again) {
        g_again = false;
        Step(false);
    }
    g_reconciling = false;
}

bool OnSignal(int eventID) {
    if (eventID == Offsets::EVENT_SPELL_UPDATE_USABLE)
        Reconcile(false);
    return false;
}

const Event::SignalHook::AutoSubscribe _usable{&OnSignal};

void OnAuraChange(const Aura::Source::Change &c) {
    if (c.type != Aura::Source::ChangeType::Added && c.type != Aura::Source::ChangeType::Removed)
        return;
    bool ours = false;
    for (const AuraRow &row : kAuraRows)
        ours = ours || row.spellID == c.spellId;
    if (ours && c.unit == Unit::Identity::PlayerObject())
        Reconcile(false);
}

const Aura::Source::AutoObserve _auras{&OnAuraChange};

void OnEnterWorld() { Reconcile(true); }

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

// Logout (4.3.4's ShutdownGame): the Lua state that saw our events is gone.
void OnGlueInit() {
    g_activeRows = 0;
    g_usable.clear();
    g_shownRows = 0;
    g_glowing.clear();
}

const Game::GlueModuleAutoRegister _glue{&OnGlueInit};

// --- Lua --------------------------------------------------------------------

int __fastcall Script_IsSpellOverlayed(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: IsSpellOverlayed(spellID)");
        return 0;
    }
    const auto id = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, std::binary_search(g_glowing.begin(), g_glowing.end(), id));
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("spellID", "number"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("isSpellOverlayed", "bool", "True while the spell glows on the action bar."),
};
const Game::Doc::Function kIsSpellOverlayedGlobal{
    "Whether the spell glows on the action bar now.", kArgs, kRets, "SpellGlobals"};
const Game::Doc::Function kIsSpellOverlayed{
    "Whether the spell glows on the action bar now.", kArgs, kRets};

const Game::Lua::EnumIntegerEntry kScreenLocationEntries[] = {
    {"Center", CENTER},
    {"Left", LEFT},
    {"Right", RIGHT},
    {"Top", TOP},
    {"Bottom", BOTTOM},
    {"TopLeft", TOP_LEFT},
    {"TopRight", TOP_RIGHT},
    {"LeftOutside", LEFT_OUTSIDE},
    {"RightOutside", RIGHT_OUTSIDE},
    {"LeftRight", LEFT_RIGHT},
    {"TopBottom", TOP_BOTTOM},
    {"LeftRightOutside", LEFT_RIGHT_OUTSIDE},
};

} // namespace

static void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsSpellOverlayed", &Script_IsSpellOverlayed,
                                      &kIsSpellOverlayedGlobal);
    Game::Lua::RegisterTableFunction("C_SpellActivationOverlay", "IsSpellOverlayed",
                                     &Script_IsSpellOverlayed, &kIsSpellOverlayed);
    Game::Lua::RegisterIntegerEnum(
        "Enum", "ScreenLocationType", kScreenLocationEntries, "ScreenLocationConstants");
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::ActivationOverlay
