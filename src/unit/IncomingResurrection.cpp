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

// `UnitHasIncomingResurrection(unit)` and `INCOMING_RESURRECT_CHANGED` —
// 4.3.4's CGIncomingResurrection (Ui/IncomingResurrection.cpp) and the two
// calls its spell code makes into it:
//   - SMSG_SPELL_START: a spell with a resurrect effect and an object target
//     adds {caster, spell} under that target
//     (PendingSpellCast::OnSpellStartPacket, 0x008ACD80);
//   - the cast's end removes it (FinishPendingSpellCast, 0x008AF840, which
//     SPELL_GO and every failure run).
// Each change signals INCOMING_RESURRECT_CHANGED to every token naming the
// target, or the corpse's owner when the target is a corpse
// (Script_SendUnitSignal). The query answers for the unit itself, then for
// its corpse (HasIncomingResurrection, 0x009FCF80).
//
// Where 1.12 differs:
//   - Resurrect effects. 4.3.4 tests effect 18 alone. 1.12's class
//     resurrections (Resurrection, Redemption, Ancestral Spirit, Rebirth,
//     every rank) carry 113, RESURRECT_NEW, and 18 only item and NPC spells
//     (Spell.dbc), so both count.
//   - The cast's end. 1.12 keeps no pending cast. Its end is the caster's
//     SMSG_SPELL_GO, or CGUnit_C::ClearCastingSpell and the failure packets,
//     which `Spell::Cast` owns and reports through OnCastEnded. Turtle sends
//     observers no failure packet on an interrupt; SuperWoW calls
//     ClearCastingSpell instead (docs/notes/cast-channel.md).
//   - Lifetime. 4.3.4 empties the table on entering and leaving the world. A
//     cast whose caster leaves view never reports its end, so an entry whose
//     caster is no longer visible ends on the next world tick, which covers
//     a world change as well.
//   - One cast per caster, as the unit's single precast slot (+0xC8C) holds:
//     a caster's new resurrection replaces its last.

#include "unit/IncomingResurrection.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/PacketDispatch.h"
#include "object/Corpse.h"
#include "object/Resolve.h"
#include "spell/Lookup.h"
#include "tick/WorldTick.h"
#include "unit/Identity.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Unit::IncomingResurrection {

namespace {

constexpr int SPELL_EFFECT_RESURRECT = 18;
constexpr int SPELL_EFFECT_RESURRECT_NEW = 113;

const Game::Doc::Field kChangedPayload[] = {
    Game::Doc::Req("unitTarget", "UnitToken", "The unit, or the player whose corpse it is."),
};
const Game::Doc::Event kChangedDoc{
    "Unit", "A resurrection cast at the unit began or ended. Read UnitHasIncomingResurrection.",
    kChangedPayload};
const Event::Custom::AutoReserve kChanged{"INCOMING_RESURRECT_CHANGED", &kChangedDoc};

struct Entry {
    uint64_t target; // the cast's object target: a unit or a corpse
    uint64_t unit;   // who hears the change: the corpse's owner, else target
    uint64_t caster;
    int spellID;
};

std::vector<Entry> g_entries;

void Signal(uint64_t unit) {
    const int slot = kChanged.Slot();
    if (!Event::Custom::HasListeners(slot))
        return;
    char tokens[16][32];
    const int n = Unit::Identity::TokensForGUID(unit, tokens, 16);
    for (int i = 0; i < n; ++i)
        Event::Custom::Fire(slot, "%s", tokens[i]);
}

// Ends `caster`'s resurrection: of `spellID`, or any for 0.
void End(uint64_t caster, int spellID) {
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
        if (it->caster == caster && (spellID == 0 || it->spellID == spellID)) {
            const uint64_t unit = it->unit;
            g_entries.erase(it);
            Signal(unit);
            return;
        }
    }
}

void Begin(uint64_t target, uint64_t caster, int spellID) {
    for (const Entry &e : g_entries) {
        if (e.caster == caster && e.target == target && e.spellID == spellID)
            return;
    }
    End(caster, 0);
    const uint64_t owner = Object::Corpse::OwnerOf(target);
    const uint64_t unit = owner != 0 ? owner : target;
    g_entries.push_back({target, unit, caster, spellID});
    Signal(unit);
}

bool Targeted(uint64_t target) {
    for (const Entry &e : g_entries) {
        if (e.target == target)
            return true;
    }
    return false;
}

bool HasIncomingResurrection(uint64_t guid) {
    if (guid == 0 || g_entries.empty())
        return false;
    if (Targeted(guid))
        return true;
    const void *corpse = Object::Corpse::ByOwner(guid);
    return corpse != nullptr && Targeted(Unit::Identity::GuidForObject(corpse));
}

bool IsResurrect(int spellID) {
    const uint8_t *rec = Spell::Lookup::RecordForID(spellID);
    return Spell::Lookup::HasEffect(rec, SPELL_EFFECT_RESURRECT) ||
           Spell::Lookup::HasEffect(rec, SPELL_EFFECT_RESURRECT_NEW);
}

// SMSG_SPELL_START / SMSG_SPELL_GO: packed item or caster, packed caster,
// spell, then for SPELL_START u16 flags, u32 cast time and the targets.
void OnPacket(uint32_t opcode, Net::CDataStore *p) {
    if (opcode != Offsets::SMSG_SPELL_START && opcode != Offsets::SMSG_SPELL_GO)
        return;
    if (opcode == Offsets::SMSG_SPELL_GO && g_entries.empty())
        return;
    Net::ReadPackedGuid(p);
    const uint64_t caster = Net::ReadPackedGuid(p);
    const int spellID = static_cast<int>(Net::Read<uint32_t>(p));
    if (opcode == Offsets::SMSG_SPELL_GO) {
        if (p->m_read <= p->m_size)
            End(caster, spellID);
        return;
    }
    Net::Read<uint16_t>(p);
    Net::Read<uint32_t>(p);
    const uint16_t targetMask = Net::Read<uint16_t>(p);
    const uint64_t target =
        (targetMask & Offsets::SPELL_TARGET_FLAGS_OBJECT) ? Net::ReadPackedGuid(p) : 0;
    if (p->m_read > p->m_size || caster == 0 || target == 0 || !IsResurrect(spellID))
        return;
    Begin(target, caster, spellID);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

void OnWorldTick() {
    for (size_t i = 0; i < g_entries.size();) {
        if (Object::ByGuid(Offsets::TYPEMASK_UNIT, g_entries[i].caster) != nullptr) {
            ++i;
            continue;
        }
        const uint64_t unit = g_entries[i].unit;
        g_entries.erase(g_entries.begin() + static_cast<std::ptrdiff_t>(i));
        Signal(unit);
    }
}

const Tick::WorldTick::AutoSubscribe _tick{&OnWorldTick};

int __fastcall Script_UnitHasIncomingResurrection(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitHasIncomingResurrection(\"unit\")");
        return 0;
    }
    Game::Lua::PushBool(L, HasIncomingResurrection(
                               Unit::Identity::GuidForToken(Game::Lua::ToString(L, 1))));
    return 1;
}

const Game::Doc::Field kArgs[] = {
    Game::Doc::Req("unit", "UnitToken"),
};
const Game::Doc::Field kRets[] = {
    Game::Doc::Req("hasIncomingResurrection", "bool"),
};
const Game::Doc::Function kUnitHasIncomingResurrection{
    "Whether a resurrection is being cast at the unit, or at the corpse of a released player.",
    kArgs, kRets, "UnitGlobals"};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitHasIncomingResurrection",
                                      &Script_UnitHasIncomingResurrection,
                                      &kUnitHasIncomingResurrection);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void OnCastEnded(uint64_t caster, int spellID) {
    if (caster != 0 && spellID != 0 && !g_entries.empty())
        End(caster, spellID);
}

} // namespace Unit::IncomingResurrection
