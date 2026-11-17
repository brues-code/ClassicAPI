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

// The combat log file's EMOTE line: a raid boss emote, as MoP Classic writes
// it with the same COMBAT_LOG_VERSION 9 (Era and 4.3.4 write none):
//   EMOTE,sourceGUID,"sourceName",targetGUID,"targetName",text
// with 0000000000000000,nil for no target, the target's name without its
// realm, and the text last and unquoted, as the chat frame shows it.
//
// Which messages: CHAT_MSG_RAID_BOSS_EMOTE (0x5A) only. Checked on 22,495
// EMOTE lines of MoP Classic logs: DBM catches Ji-Kun's Caws on
// CHAT_MSG_RAID_BOSS_EMOTE (42 lines) and her nests hatching on
// CHAT_MSG_MONSTER_EMOTE (none, though they hatch many times a fight), and
// none of Durumu's monster emotes are there; of the lines AzerothCore's
// creature_text types, 247 are raid boss emotes (41) and 3 are monster
// emotes (16, one Nadox line), and none of the 346 yells of those creatures
// appear.
//
// The message (SMSG_MESSAGECHAT, read at 0x0049D560 for type 90): u8 type,
// u32 language, u32 length and that many bytes of the speaker's name, the
// target's GUID, u32 length and the text, u8 tag. It names its speaker but
// carries no speaker GUID, so the source is the one creature in view with
// that name, 0 when none or several have it. The chat frame shows the text
// as format(text, name) (ChatFrame.lua), so its first %s is the speaker's
// name, as the logged text has it.

#include "Game.h"
#include "Offsets.h"
#include "combatlog/File.h"
#include "guid/Guid.h"
#include "net/PacketDispatch.h"
#include "net/PacketReader.h"
#include "object/Resolve.h"
#include "unit/Identity.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace CombatLog::Emote {

namespace {

constexpr uint8_t CHAT_MSG_RAID_BOSS_EMOTE = 0x5A;
// The longest name and text the client takes (its chat message buffer).
constexpr uint32_t kMaxText = 0x800;

// `length` bytes of the packet as a string, without the NULs the server ends
// it with; false when the packet is shorter.
bool ReadString(Net::CDataStore *packet, std::string &out) {
    const uint32_t length = Net::Read<uint32_t>(packet);
    if (Net::Overrun(packet) || length > kMaxText)
        return false;
    const auto *bytes = reinterpret_cast<const char *>(Net::ReadBytes(packet, length));
    if (bytes == nullptr)
        return false;
    out.assign(bytes, strnlen(bytes, length));
    return true;
}

// The one creature in view named `name`, else 0.
uint64_t Speaker(const std::string &name) {
    uint64_t found = 0;
    int count = 0;
    ::Object::ForEachVisible([&](uint64_t guid) {
        if (!Guid::IsCreature(guid))
            return true;
        const void *unit = ::Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0);
        const char *unitName = unit != nullptr ? Unit::Identity::NameForUnit(unit) : nullptr;
        if (unitName != nullptr && name == unitName) {
            found = guid;
            ++count;
        }
        return count < 2;
    });
    return count == 1 ? found : 0;
}

// The target's name: the unit's own when it is in view, else a player's from
// the name cache; null for none.
const char *TargetName(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    if (const void *unit = ::Object::ByGuid(Offsets::TYPEMASK_UNIT, guid, nullptr, 0))
        return Unit::Identity::NameForUnit(unit);
    return Guid::IsPlayer(guid) ? Unit::Identity::NameForGuid(guid) : nullptr;
}

void OnPacket(uint32_t opcode, Net::CDataStore *packet) {
    if (opcode != Offsets::SMSG_MESSAGECHAT || packet == nullptr || File::Session() == 0)
        return;
    if (Net::Read<uint8_t>(packet) != CHAT_MSG_RAID_BOSS_EMOTE)
        return;
    Net::Read<uint32_t>(packet); // language
    std::string name, text;
    if (!ReadString(packet, name))
        return;
    const uint64_t target = Net::Read<uint64_t>(packet);
    if (!ReadString(packet, text) || Net::Overrun(packet))
        return;
    if (const std::size_t at = text.find("%s"); at != std::string::npos)
        text.replace(at, 2, name);
    // One line in the file.
    for (char &c : text)
        if (c == '\r' || c == '\n')
            c = ' ';

    std::string line = "EMOTE";
    File::AppendGuid(line, Speaker(name));
    File::AppendQuoted(line, name.c_str());
    File::AppendGuid(line, target);
    const char *targetName = TargetName(target);
    File::AppendQuoted(line, targetName != nullptr && *targetName != '\0' ? targetName : nullptr);
    line += ',';
    line += text;
    File::WriteLine(line);
}

const Net::PacketDispatch::AutoSubscribe _sub{&OnPacket};

} // namespace

} // namespace CombatLog::Emote
