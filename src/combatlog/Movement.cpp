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

// The movement file: every movement message the client receives, and the
// player's own, for replay tools. A ClassicAPI extension; no Blizzard client
// writes one. The combat log's advanced block has positions only on the lines
// that carry it (about one per unit every 2.6 s in Era's raid log), which is
// too coarse for a replay, so this file adds the messages themselves. It is
// not sampled: a sample between messages records only the client's own
// prediction.
//
// It is written while the combat log file is open, advanced logging is on and
// the advancedCombatLoggingMovement cvar is on, to
// Logs\WoWMovementLog-<the combat log's MMDDYY_HHMMSS>.txt, with the combat
// log's stamp. A separate file keeps the combat log readable by Era parsers.
//
// Lines (positions with two decimals, angles and speeds with four):
//   MOVEMENT_LOG_VERSION,1,BUILD_VERSION,<version>,MAP_ID,<map>
//     on open and after each loading screen;
//   UNIT_MOVE,unitGUID,event,flags,time,x,y,z,facing,pitch,fallTime,
//     jumpVelocity,jumpCos,jumpSin,jumpSpeed
//     a player's move: the 30 relayed MSG_MOVE_* the server forwards (event
//     is the opcode's name without MSG_MOVE_: START_FORWARD, HEARTBEAT, ...),
//     the player's own sends of the same opcodes, and TELEPORT_ACK, the
//     server moving the player. flags are the mover's movement flags; time
//     is the status's clock in milliseconds: on a relayed move the server's
//     when it got the mover's message (tortoise MovementHandler.cpp
//     UpdateTime(GetPacketTime())), on the player's own the client's. The
//     server sends relayed moves in batches, so two sent 500 ms apart can
//     arrive 1 ms apart: the clock, not the stamp, spaces them. pitch is 0
//     unless swimming, the jump four 0 unless the 0x2000 flag is set;
//   UNIT_SPEED,unitGUID,type,speed
//     type WALK, RUN, RUN_BACK, SWIM, SWIM_BACK or TURN_RATE, in yards (or
//     radians) a second;
//   MONSTER_MOVE,unitGUID,transportGUID,splineID,flags,durationMs,
//     facingType,faceX,faceY,faceZ,faceGUID,faceAngle,pointCount,x,y,z,...
//     a server-driven path (creatures, and players on a taxi): the points
//     start at the unit's start point and end at the destination, all
//     absolute, transport-relative when transportGUID is not 0. facingType
//     is the server's 0 (none), 2 (a point), 3 (a unit) or 4 (an angle);
//   MONSTER_STOP,unitGUID,transportGUID,splineID,x,y,z;
//   UNIT_INFO,unitGUID,x,y,z,facing,walkSpeed,runSpeed,runBackSpeed,
//     swimSpeed,swimBackSpeed,displayID,nativeDisplayID,mountDisplayID,scale,
//     bytes1,bytes2,heldDisplay1..3,heldInfo1..6
//     what a unit looks like, and where it is, when it comes into view and
//     each time its look changes (UnitInfo.cpp): the speeds are the ones
//     the client moves it at, which UNIT_SPEED changes; bytes1 and bytes2 are
//     UNIT_FIELD_BYTES_1 (stand state, shapeshift form, stealth) and
//     UNIT_FIELD_BYTES_2 (sheath state), the held fields the hands a
//     creature holds (UNIT_VIRTUAL_ITEM_SLOT_DISPLAY, and two
//     UNIT_VIRTUAL_ITEM_INFO dwords per hand);
//   PLAYER_INFO,unitGUID,race,class,sex,skin,face,hairStyle,hairColor,
//     facialHair,playerFlags,item1,enchant1,...,item19,enchant19,
//     emblemStyle,emblemColor,borderStyle,borderColor,backgroundColor
//     after a player's UNIT_INFO, and each time it changes: what the client
//     dresses a player in its own display with. Each inventory slot gives
//     its item and the enchant whose glow it shows, 0 for none; the five
//     emblem values are the guild's tabard emblem from the guild cache
//     (GetGuildEmblem), all -1 while the client has no record of the guild
//     or the player has none (version 5);
//   UNIT_NAME,unitGUID,"name"
//     the name the client has for a player (name cache), a pet (pet name
//     cache) or a creature (its creature record), when it first has one and
//     when it changes, so a log
//     read elsewhere names units the combat log never does;
//   UNIT_STATE,unitGUID,targetGUID,ownerGUID,flags,level,factionTemplate,
//       attackTargetGUID
//     when the unit comes into view and each time one of these changes: its
//     target (UNIT_FIELD_TARGET), its charmer or creator (a pet's, guardian's
//     or totem's owner, a mind-controlled unit's controller), UNIT_FIELD_FLAGS
//     (stunned 0x40000, confused 0x400000, fleeing 0x800000, in combat
//     0x80000, ...), its level, its FactionTemplate id, and the unit it
//     swings at (SMSG_ATTACKSTART to SMSG_ATTACKSTOP, the unit's +0xC48);
//   UNIT_POWER,unitGUID,health,maxHealth,powerType,power,maxPower
//     when the unit comes into view and each time one of these changes: its
//     health, and the power of its own type in stored units (rage in tenths);
//   UNIT_AURAS,unitGUID,spellID,harmful,stacks,...
//     the auras in its 48 slots, in slot order, when it comes into view with
//     any, and each time they change in a way the combat log does not
//     write (the arrival or loss of a hidden or passive aura, a mount's for
//     one, or any loss on a dead unit): each one's spell, 1 when harmful (as
//     this server encodes it) else 0, and its stacks. Between two lines the
//     log's SPELL_AURA_ lines give the changes. A line with no auras is the
//     last one gone;
//   UNIT_CAST,unitGUID,castSpellID,castTimeMs,channelSpellID
//     a unit's cast state each time it changes (UnitInfo.cpp), and when a
//     unit that casts or channels comes into view: the spell it casts with
//     a cast time and that time in milliseconds, then the spell it
//     channels, each 0 for none. A cast that stops without a
//     SPELL_CAST_SUCCESS was interrupted; a channel ends when its spell
//     goes back to 0;
//   UNIT_GONE,unitGUID
//     the unit left view;
//   GAMEOBJECT_INFO,objectGUID,ownerGUID,displayID,type,state,flags,x,y,z,
//       facing,rotationX,rotationY,rotationZ,rotationW,scale,"name"
//     a game object when it comes into view and each time a field but its
//     place changes (ObjectInfo.cpp): its creator, display, GAMEOBJECT_TYPE_ID,
//     GAMEOBJECT_STATE, GAMEOBJECT_FLAGS, place, the GAMEOBJECT_ROTATION
//     quaternion the client turns its model by, scale, and the name from its
//     record ("" until the record arrives);
//   DYNAMICOBJECT_INFO,objectGUID,casterGUID,spellID,bytes,radius,x,y,z,facing
//     a spell's ground area when it comes into view: its caster, spell,
//     DYNAMICOBJECT_BYTES, radius in yards and place;
//   OBJECT_GONE,objectGUID
//     the game object or ground area left view.
// GUIDs are written as the combat log writes them. After a header the unit
// and object lines start over: everything in view is written again.
//
// The wire formats are the client's own readers:
//   - a movement status (0x0047EBA0): flags, time, x, y, z, facing; with
//     0x2000000 a transport GUID and offset; with 0x200000 the pitch; the fall
//     time; with 0x2000 four jump floats; with 0x4000000 a spline elevation
//     (not read: it is last);
//   - relayed moves (0x00603BB0): a packed GUID and a status. The player's
//     own sends of those opcodes carry the status alone (0x00600860 writes a
//     GUID only for acknowledgements), for the active mover
//     (VAR_ACTIVE_MOVER_GUID), which is the player unless they control
//     another unit;
//   - MSG_MOVE_TELEPORT_ACK and the forced speed changes (0x00603F90): a
//     packed GUID and a u32 counter, then a status or a float;
//   - relayed speed changes (0x00603AE0): a packed GUID, a status, a float;
//     spline speed changes (0x00603C10): a packed GUID and a float;
//   - monster moves (0x00603F00, 0x006018F0): a packed GUID, for 0x2AE a
//     second packed GUID (the transport), the start point, the spline id, a
//     type byte; type 1 stops there; type 2 adds a point, 3 a full u64 GUID,
//     4 an angle; then the spline flags, the duration and a point count. With
//     spline flag 0x200 that many points follow whole; otherwise the
//     destination comes whole and the count - 1 points before it as packed
//     offsets subtracted from it (0x00602130: 11, 11 and 10 signed bits,
//     each times 0.25).
// SMSG_COMPRESSED_MOVES re-dispatches each message it holds through the same
// funnel Net::PacketDispatch observes, so those arrive here one by one.
//
// Not logged: the path a unit is on when it comes into view (the object
// update's spline; its next move is logged), knockback parameters (the fall
// that follows is logged), and the spline mode toggles (0x304 to 0x31A).

#include "Game.h"
#include "Offsets.h"
#include "combatlog/File.h"
#include "combatlog/LogFile.h"
#include "combatlog/Movement.h"
#include "cvar/Factory.h"
#include "event/EnteringWorld.h"
#include "net/PacketDispatch.h"
#include "net/SendObserver.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace CombatLog::Movement {

namespace {

constexpr uint32_t MSG_MOVE_TELEPORT_ACK = 0xC7;
constexpr uint32_t SMSG_MONSTER_MOVE = 0xDD;
constexpr uint32_t SMSG_MONSTER_MOVE_TRANSPORT = 0x2AE;

// Monster move types: the byte after the spline id.
constexpr uint8_t kMoveStop = 1;
constexpr uint8_t kFacePoint = 2;
constexpr uint8_t kFaceTarget = 3;
constexpr uint8_t kFaceAngle = 4;
// Spline flag: every point comes whole (0x006018F0 tests it).
constexpr uint32_t kSplineWholePoints = 0x200;

struct Named {
    uint32_t opcode;
    const char *name;
};

// The 30 relayed moves (0x00603BB0's set), by their MSG_MOVE_ names.
constexpr Named kMoves[] = {
    {0xB5, "START_FORWARD"},
    {0xB6, "START_BACKWARD"},
    {0xB7, "STOP"},
    {0xB8, "START_STRAFE_LEFT"},
    {0xB9, "START_STRAFE_RIGHT"},
    {0xBA, "STOP_STRAFE"},
    {0xBB, "JUMP"},
    {0xBC, "START_TURN_LEFT"},
    {0xBD, "START_TURN_RIGHT"},
    {0xBE, "STOP_TURN"},
    {0xBF, "START_PITCH_UP"},
    {0xC0, "START_PITCH_DOWN"},
    {0xC1, "STOP_PITCH"},
    {0xC2, "SET_RUN_MODE"},
    {0xC3, "SET_WALK_MODE"},
    {0xC5, "TELEPORT"},
    {0xC9, "FALL_LAND"},
    {0xCA, "START_SWIM"},
    {0xCB, "STOP_SWIM"},
    {0xD9, "TOGGLE_COLLISION_CHEAT"},
    {0xDA, "SET_FACING"},
    {0xDB, "SET_PITCH"},
    {0xEC, "ROOT"},
    {0xED, "UNROOT"},
    {0xEE, "HEARTBEAT"},
    {0xF1, "KNOCK_BACK"},
    {0xF7, "HOVER"},
    {0x2AD, "TOGGLE_GRAVITY_CHEAT"},
    {0x2B0, "FEATHER_FALL"},
    {0x2B1, "WATER_WALK"},
};

// MSG_MOVE_SET_*_SPEED: another player's speed (0x00603AE0's set).
constexpr Named kRelayedSpeeds[] = {
    {0xCD, "RUN"}, {0xCF, "RUN_BACK"}, {0xD1, "WALK"},
    {0xD3, "SWIM"}, {0xD5, "SWIM_BACK"}, {0xD8, "TURN_RATE"},
};

// SMSG_SPLINE_SET_*_SPEED: a server-driven unit's speed (0x00603C10's set).
constexpr Named kSplineSpeeds[] = {
    {0x2FE, "RUN"},  {0x2FF, "RUN_BACK"},  {0x300, "SWIM"},
    {0x301, "WALK"}, {0x302, "SWIM_BACK"}, {0x303, "TURN_RATE"},
};

// SMSG_FORCE_*_SPEED_CHANGE: the player's own speed (in 0x00603F90's set).
constexpr Named kForcedSpeeds[] = {
    {0xE2, "RUN"},   {0xE4, "RUN_BACK"},  {0xE6, "SWIM"},
    {0x2DA, "WALK"}, {0x2DC, "SWIM_BACK"}, {0x2DE, "TURN_RATE"},
};

template <std::size_t N>
const char *NameOf(const Named (&table)[N], uint32_t opcode) {
    for (const Named &entry : table) {
        if (entry.opcode == opcode)
            return entry.name;
    }
    return nullptr;
}

LogFile g_log;
uint32_t g_session = 0; // the combat log session the open file belongs to
uint32_t g_generation = 0; // headers written
CVar::Factory::Handle g_cvar = nullptr;

void RegisterCVars() {
    g_cvar = CVar::Factory::Register(
        "advancedCombatLoggingMovement",
        "Whether to write the movement the client receives to a second combat log file",
        Offsets::CVAR_CATEGORY_GAME, "0");
}

const Game::ModuleAutoRegister _autoreg{&RegisterCVars};

void Header() {
    const char *version = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(Offsets::VAR_BUILD_VERSION_STRING));
    char text[128];
    std::snprintf(text, sizeof text, "MOVEMENT_LOG_VERSION,%d,BUILD_VERSION,%s,MAP_ID,%d",
                  kVersion, version, Game::Read<int>(Offsets::VAR_CURRENT_MAP_ID));
    g_log.Emit(text);
    ++g_generation;
}

// The combat log session to write for, 0 when the file should be closed.
uint32_t WantedSession() {
    const uint32_t session = File::Session();
    return session != 0 && File::Advanced() && CVar::Factory::GetInt(g_cvar) != 0 ? session : 0;
}

// Follows the combat log file and the settings: a new file with each combat
// log file, closed when either setting turns off.
void OnFrame() {
    const uint32_t session = WantedSession();
    if (session != g_session) {
        g_log.Close();
        g_session = 0;
        if (session != 0 && g_log.Open("WoWMovementLog", File::FileTime())) {
            g_session = session;
            Header();
        }
    }
    g_log.Tick();
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

void OnEnterWorld() {
    if (g_log.IsOpen())
        Header();
}

const Event::EnteringWorld::AutoObserve _enterWorld{&OnEnterWorld};

using File::AppendFixed;
using File::AppendHex;
using File::AppendUnsigned;

void Point(std::string &line, const float (&p)[3]) {
    for (const float v : p)
        AppendFixed(line, v, 2);
}

void ReadPoint(Net::CDataStore *packet, float (&p)[3]) {
    for (float &v : p)
        v = Net::Read<float>(packet);
}

// A movement status (0x0047EBA0), as a UNIT_MOVE line's fields from flags on.
// False when the packet ends early.
bool AppendStatus(std::string &line, Net::CDataStore *packet) {
    const auto flags = Net::Read<uint32_t>(packet);
    const auto time = Net::Read<uint32_t>(packet);
    float position[3];
    ReadPoint(packet, position);
    const auto facing = Net::Read<float>(packet);
    if (flags & Offsets::MOVEFLAG_ONTRANSPORT) {
        Net::Read<uint64_t>(packet);
        for (int i = 0; i < 4; ++i)
            Net::Read<float>(packet); // the offset on it; the position is the world's
    }
    const float pitch = (flags & Offsets::MOVEFLAG_SWIMMING) ? Net::Read<float>(packet) : 0.0f;
    const auto fallTime = Net::Read<uint32_t>(packet);
    float jump[4] = {};
    if (flags & Offsets::MOVEFLAG_FALLING) {
        for (float &v : jump)
            v = Net::Read<float>(packet);
    }
    if (Net::Overrun(packet))
        return false;
    AppendHex(line, flags);
    AppendUnsigned(line, time);
    Point(line, position);
    AppendFixed(line, facing, 4);
    AppendFixed(line, pitch, 4);
    AppendUnsigned(line, fallTime);
    for (const float v : jump)
        AppendFixed(line, v, 4);
    return true;
}

void Move(uint64_t guid, const char *event, Net::CDataStore *packet) {
    std::string line = "UNIT_MOVE";
    File::AppendGuid(line, guid);
    line += ',';
    line += event;
    if (AppendStatus(line, packet))
        g_log.Emit(line);
}

void Speed(uint64_t guid, const char *type, float speed) {
    std::string line = "UNIT_SPEED";
    File::AppendGuid(line, guid);
    line += ',';
    line += type;
    AppendFixed(line, speed, 4);
    g_log.Emit(line);
}

void MonsterMove(uint32_t opcode, Net::CDataStore *packet) {
    const uint64_t guid = Net::ReadPackedGuid(packet);
    const uint64_t transport =
        opcode == SMSG_MONSTER_MOVE_TRANSPORT ? Net::ReadPackedGuid(packet) : 0;
    float start[3];
    ReadPoint(packet, start);
    const auto splineId = Net::Read<uint32_t>(packet);
    const auto type = Net::Read<uint8_t>(packet);
    if (Net::Overrun(packet))
        return;
    std::string line = type == kMoveStop ? "MONSTER_STOP" : "MONSTER_MOVE";
    File::AppendGuid(line, guid);
    File::AppendGuid(line, transport);
    AppendUnsigned(line, splineId);
    if (type == kMoveStop) {
        Point(line, start);
        g_log.Emit(line);
        return;
    }
    float facePoint[3] = {};
    uint64_t faceGuid = 0;
    float faceAngle = 0.0f;
    if (type == kFacePoint)
        ReadPoint(packet, facePoint);
    else if (type == kFaceTarget)
        faceGuid = Net::Read<uint64_t>(packet);
    else if (type == kFaceAngle)
        faceAngle = Net::Read<float>(packet);
    const auto flags = Net::Read<uint32_t>(packet);
    const auto duration = Net::Read<uint32_t>(packet);
    auto count = Net::Read<uint32_t>(packet);
    // The client reads one point even for a count of 0. A count the packet
    // cannot hold is a broken packet (each point is at least 4 bytes).
    if (count == 0)
        count = 1;
    if (Net::Overrun(packet) || count > (packet->m_size - packet->m_read) / 4)
        return;
    AppendHex(line, flags);
    AppendUnsigned(line, duration);
    AppendUnsigned(line, type);
    Point(line, facePoint);
    File::AppendGuid(line, faceGuid);
    AppendFixed(line, faceAngle, 4);
    AppendUnsigned(line, count + 1);
    Point(line, start);
    if (flags & kSplineWholePoints) {
        for (uint32_t i = 0; i < count; ++i) {
            float p[3];
            ReadPoint(packet, p);
            Point(line, p);
        }
    } else {
        float destination[3];
        ReadPoint(packet, destination);
        for (uint32_t i = 1; i < count; ++i) {
            const auto packed = static_cast<int32_t>(Net::Read<uint32_t>(packet));
            const float p[3] = {
                destination[0] - static_cast<float>((packed << 21) >> 21) * 0.25f,
                destination[1] - static_cast<float>((packed << 10) >> 21) * 0.25f,
                destination[2] - static_cast<float>(packed >> 22) * 0.25f,
            };
            Point(line, p);
        }
        Point(line, destination);
    }
    if (!Net::Overrun(packet))
        g_log.Emit(line);
}

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (!g_log.IsOpen())
        return;
    if (opcode == SMSG_MONSTER_MOVE || opcode == SMSG_MONSTER_MOVE_TRANSPORT) {
        MonsterMove(opcode, packet);
    } else if (const char *event = NameOf(kMoves, opcode)) {
        const uint64_t guid = Net::ReadPackedGuid(packet);
        Move(guid, event, packet);
    } else if (opcode == MSG_MOVE_TELEPORT_ACK) {
        const uint64_t guid = Net::ReadPackedGuid(packet);
        Net::Read<uint32_t>(packet); // the counter the acknowledgement echoes
        Move(guid, "TELEPORT_ACK", packet);
    } else if (const char *type = NameOf(kSplineSpeeds, opcode)) {
        const uint64_t guid = Net::ReadPackedGuid(packet);
        const auto speed = Net::Read<float>(packet);
        if (!Net::Overrun(packet))
            Speed(guid, type, speed);
    } else if (const char *type = NameOf(kForcedSpeeds, opcode)) {
        const uint64_t guid = Net::ReadPackedGuid(packet);
        Net::Read<uint32_t>(packet); // the counter
        const auto speed = Net::Read<float>(packet);
        if (!Net::Overrun(packet))
            Speed(guid, type, speed);
    } else if (const char *type = NameOf(kRelayedSpeeds, opcode)) {
        const uint64_t guid = Net::ReadPackedGuid(packet);
        std::string ignored;
        if (AppendStatus(ignored, packet)) {
            const auto speed = Net::Read<float>(packet);
            if (!Net::Overrun(packet))
                Speed(guid, type, speed);
        }
    }
}

const Net::PacketDispatch::AutoSubscribe _received{&OnPacket};

// The player's own moves: the same opcodes, sent with the status alone.
void OnSend(uint32_t opcode, Net::CDataStore *packet) {
    if (!g_log.IsOpen())
        return;
    if (const char *event = NameOf(kMoves, opcode))
        Move(Game::Read<uint64_t>(Offsets::VAR_ACTIVE_MOVER_GUID), event, packet);
}

const Net::SendObserver::AutoSubscribe _sent{&OnSend};

} // namespace

bool IsOpen() { return g_log.IsOpen(); }

void Emit(const std::string &body) {
    if (g_log.IsOpen())
        g_log.Emit(body);
}

uint32_t Generation() { return g_generation; }

} // namespace CombatLog::Movement
