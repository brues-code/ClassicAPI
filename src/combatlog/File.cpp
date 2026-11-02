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

// The combat log file — 4.3.4's CombatLogEntry::PackEvent line, written as
// SendCombatLogEvents writes it while LoggingCombat is on.
//
// The engine pieces:
//   - the switch is LoggingCombat's own flag, so /combatlog and
//     LoggingCombat() turn both files on and off, and logging out (the chat
//     teardown in CGGameUI::ShutdownGame, 0x00498700, clears the flag) stops
//     both;
//   - each time the flag turns on, a new file starts, named as retail names
//     it: Logs\WoWCombatLog-MMDDYY_HHMMSS.txt, the local time logging started.
//     1.12's own Logs\WoWCombatLog.txt keeps its chat-text lines, which 1.12
//     log readers parse;
//   - the file is a Storm log, opened as LoggingCombat opens its own (append)
//     and written with SLogWrite (which stamps "M/D HH:MM:SS.mmm  " on each
//     line, as in 4.3.4's file). When the flag turns off it is closed, which
//     writes its buffered lines. Storm closes what is still open at exit;
//   - the line is PackEvent's: no hideCaster, flags as hex, the spell school
//     as a hex mask (the extra spell's in decimal), damage values as numbers,
//     flags as 1 or nil, no isOffHand and no alternate power type.
//
// The flag is read once a frame (FrameTick, which also runs on the glue
// screen, so a logout's cleared flag is seen there) and before each line.
// LoggingCombat itself is not hooked: other DLLs add their own logs to it.

#include "combatlog/File.h"

#include "Game.h"
#include "Offsets.h"
#include "guid/Guid.h"
#include "tick/FrameTick.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace CombatLog::File {

namespace {

constexpr uint32_t kAppend = 4; // SLogCreate: open at the end of the file

uint32_t g_log = 0;

bool Logging() { return Game::Read<int>(Offsets::VAR_COMBAT_LOGGING) != 0; }

// A new log named for the local time now. True when it is open.
bool Open() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0)
        return false;
    char path[64];
    std::strftime(path, sizeof path, "Logs\\WoWCombatLog-%m%d%y_%H%M%S.txt", &local);
    using Create_t = int(__stdcall *)(const char *path, uint32_t flags, uint32_t *log);
    const auto create = reinterpret_cast<Create_t>(static_cast<uintptr_t>(Offsets::FUN_SLOG_CREATE));
    if (create(path, kAppend, &g_log) == 0)
        g_log = 0;
    return g_log != 0;
}

void Close() {
    using Close_t = void(__stdcall *)(uint32_t log);
    reinterpret_cast<Close_t>(static_cast<uintptr_t>(Offsets::FUN_SLOG_CLOSE))(g_log);
    g_log = 0;
}

// Follows the flag: a new file when it turns on, the file closed when it
// turns off.
void OnFrame() {
    const bool logging = Logging();
    if (logging && g_log == 0)
        Open();
    else if (!logging && g_log != 0)
        Close();
}

const Tick::FrameTick::AutoSubscribe _frame{&OnFrame};

// PackString
void Plain(std::string &line, const char *text) {
    line += ',';
    line += text;
}

// PackQuotedString, or PackString "nil" when there is no text.
void Quoted(std::string &line, const char *text) {
    if (text == nullptr) {
        Plain(line, "nil");
        return;
    }
    line += ",\"";
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c == '"' || *c == '\\')
            line += '\\';
        line += *c;
    }
    line += '"';
}

// PackHex
void Hex(std::string &line, uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, ",0x%x", value);
    line += text;
}

void Number(std::string &line, int value) {
    char text[16];
    std::snprintf(text, sizeof text, ",%d", value);
    line += text;
}

// PackBool
void Flag(std::string &line, bool value) { line += value ? ",1" : ",nil"; }

void Unit(std::string &line, uint64_t guid, bool named, const std::string &name, uint32_t flags,
          uint32_t raidFlags) {
    char text[Guid::STRING_SIZE];
    line += ',';
    line += Guid::FormatAsString(guid, text, sizeof text);
    Quoted(line, named ? name.c_str() : nullptr);
    Hex(line, flags);
    Hex(line, raidFlags);
}

// CombatLogEntry::PackEvent, the blocks 1.12 fills.
std::string Pack(const Entry &e) {
    std::string line = Name(e.event);
    Unit(line, e.source, e.sourceNamed, e.sourceName, e.sourceFlags, e.sourceRaidFlags);
    Unit(line, e.dest, e.destNamed, e.destName, e.destFlags, e.destRaidFlags);
    if (e.spellPrefix) {
        const uint8_t *spell = SpellRecord(e.spellId);
        Number(line, e.spellId);
        Quoted(line, SpellName(spell));
        Hex(line, spell != nullptr ? SpellSchoolMask(spell) : e.spellSchool);
    }
    if (e.info & INFO_COUNT)
        Number(line, e.v[0]);
    if (e.info & INFO_TEXT)
        Quoted(line, e.text.c_str());
    if (e.info & INFO_MISS)
        Plain(line, MissName(e.v[0]));
    if (e.info & INFO_ENVIRONMENTAL)
        Plain(line, EnvironmentalName(e.v[0]));
    if (e.info & INFO_DAMAGE) {
        for (const int slot : {1, 2, 3, 5, 6, 4})
            Number(line, e.v[slot]);
        Flag(line, e.critical);
        Flag(line, e.glancing);
        Flag(line, e.crushing);
    }
    if (e.info & INFO_MISS_ABSORBED)
        Number(line, e.v[4]);
    if (e.info & INFO_MISS_RESISTED)
        Number(line, e.v[5]);
    if (e.info & INFO_MISS_BLOCKED)
        Number(line, e.v[6]);
    if (e.info & INFO_HEAL) {
        for (int i = 0; i < 3; ++i)
            Number(line, e.v[i]);
        Flag(line, e.critical);
    }
    if (e.info & INFO_ENERGIZE) {
        for (int i = 0; i < 2; ++i)
            Number(line, e.v[i]);
    }
    if (e.info & INFO_DRAIN) {
        for (int i = 0; i < 3; ++i)
            Number(line, e.v[i]);
    }
    if (e.info & INFO_EXTRA_SPELL) {
        const uint8_t *extra = SpellRecord(e.v[0]);
        Number(line, e.v[0]);
        Quoted(line, SpellName(extra));
        Number(line, static_cast<int>(SpellSchoolMask(extra)));
    }
    if (e.info & INFO_ITEM) {
        Number(line, e.v[0]);
        Quoted(line, e.itemNamed ? e.itemName.c_str() : nullptr);
    }
    if (e.info & INFO_AURA_TYPE)
        Plain(line, e.harmful ? "DEBUFF" : "BUFF");
    if (e.info & INFO_AURA_AMOUNT)
        Number(line, e.v[2]);
    return line;
}

} // namespace

void Write(const Entry &entry) {
    // A line in the frame logging turned on comes before the frame's check.
    if (!Logging() || (g_log == 0 && !Open()))
        return;
    using Write_t = void(__cdecl *)(uint32_t log, const char *format, ...);
    reinterpret_cast<Write_t>(static_cast<uintptr_t>(Offsets::FUN_SLOG_WRITE))(
        g_log, "%s", Pack(entry).c_str());
}

} // namespace CombatLog::File
