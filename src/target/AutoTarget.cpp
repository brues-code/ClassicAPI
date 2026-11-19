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

// TargetEnemyAttacker — "Auto-Target Enemy when they attack you", as the
// 3.80.2 Classic client does it (docs/notes/soft-targeting-3802.md, the
// reason table): a unit whose melee swing (SMSG_ATTACKERSTATEUPDATE,
// FUN_1415a04d0) or harmful spell (SMSG_SPELL_GO, FUN_1413d7a80) lands on the
// player becomes the target with the Auto reason. Target::Reason decides
// whether it takes: it fills an empty target, never replaces a chosen one,
// and is stored locked with TargetAutoLock on.
//
// Defaults to 0: stock 1.12 never targets an attacker, so the change is
// opt-in (Classic Era and retail ship 1).

#include "Game.h"
#include "Offsets.h"
#include "cvar/Character.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "spell/CastPacket.h"
#include "spell/Lookup.h"
#include "target/Reason.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Target::AutoTarget {

namespace {

using Hostility_t = int(__fastcall *)(const uint8_t *record, void *edx);

constexpr int kHarmful = 2;

CVar::Factory::Handle g_enemyAttacker = nullptr;

bool On() { return CVar::Factory::GetInt(g_enemyAttacker) != 0; }

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_ATTACKERSTATEUPDATE || packet == nullptr || !On())
        return;
    const uint64_t player = Unit::Identity::PlayerGuid();
    Net::Read<uint32_t>(packet); // hitInfo
    const uint64_t attacker = Net::ReadPackedGuid(packet);
    const uint64_t victim = Net::ReadPackedGuid(packet);
    if (player != 0 && victim == player && attacker != 0 && attacker != player)
        Reason::Set(attacker, Reason::Auto);
}

const Net::PacketDispatch::AutoSubscribe _packet{&OnPacket};

void OnCastGo(const Spell::CastPacket::Go &g) {
    const uint64_t player = Unit::Identity::PlayerGuid();
    if (!On() || player == 0 || g.header.caster == player || g.header.caster == 0)
        return;
    bool hitsPlayer = false;
    for (int i = 0; i < g.hitCount && !hitsPlayer; ++i)
        hitsPlayer = g.hits[i] == player;
    if (!hitsPlayer)
        return;
    const uint8_t *record = Spell::Lookup::RecordForID(static_cast<int>(g.header.spellId));
    if (record != nullptr &&
        reinterpret_cast<Hostility_t>(Offsets::FUN_SPELL_HOSTILITY)(record, nullptr) == kHarmful)
        Reason::Set(g.header.caster, Reason::Auto);
}

const Spell::CastPacket::AutoGo _go{&OnCastGo};

void Register() {
    g_enemyAttacker = CVar::Character::Register("TargetEnemyAttacker", "Auto-Target Enemy when they attack you", Offsets::CVAR_CATEGORY_GAME, "0");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::AutoTarget
