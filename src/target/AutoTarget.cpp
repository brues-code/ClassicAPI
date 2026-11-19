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
// 3.80.2 Classic client does it (docs/notes/soft-targeting-3802.md): with
// the cvar on, the target becomes, with the Auto reason,
//   - a unit whose melee swing lands on the player (SMSG_ATTACKERSTATEUPDATE,
//     FUN_1415a04d0 @0x1415a0cc0);
//   - a living unit that starts a cast with the player as its unit target and
//     cannot assist the player (SMSG_SPELL_START, FUN_1413d66b0's tail).
// Target::Reason decides whether it takes: it fills an empty target, never
// replaces a chosen one, and is stored locked with TargetAutoLock on.
//
// Stock 1.12 makes the same two selects itself (0x006259FE, 0x006E799C),
// ungated and only with no target; Target::Reason drops those, so these are
// the only attacker targets and the cvar governs them. Defaults to 1, as
// Classic Era and retail ship it and as stock 1.12 behaves; 0 keeps a struck
// character from picking up a target at all.

#include "Game.h"
#include "Offsets.h"
#include "cvar/Character.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "object/Resolve.h"
#include "spell/CastPacket.h"
#include "target/Reason.h"
#include "unit/Identity.h"

#include <cstdint>

namespace Target::AutoTarget {

namespace {

using CanAssist_t = uint8_t(__thiscall *)(void *unit, void *other);

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

void OnCastStart(const Spell::CastPacket::Start &s) {
    const uint64_t player = Unit::Identity::PlayerGuid();
    const uint64_t caster = s.header.caster;
    if (!On() || player == 0 || caster == 0 || caster == player ||
        (s.targets.mask & Offsets::SPELL_TARGET_FLAG_UNIT) == 0 || s.targets.object != player)
        return;
    void *unit = Object::ByGuid(Offsets::TYPEMASK_UNIT, caster);
    void *self = Object::ByGuid(Offsets::TYPEMASK_UNIT, player);
    const uint8_t *fields = unit != nullptr ? Object::Fields(unit) : nullptr;
    if (self == nullptr || fields == nullptr ||
        Game::Read<int32_t>(fields, Offsets::OFF_UNIT_FIELD_HEALTH) < 1 ||
        reinterpret_cast<CanAssist_t>(Offsets::FUN_UNIT_CAN_ASSIST)(unit, self) != 0)
        return;
    Reason::Set(caster, Reason::Auto);
}

const Spell::CastPacket::AutoStart _start{&OnCastStart};

void Register() {
    g_enemyAttacker = CVar::Character::Register("TargetEnemyAttacker", "Auto-Target Enemy when they attack you", Offsets::CVAR_CATEGORY_GAME, "1");
}

const Game::ModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Target::AutoTarget
