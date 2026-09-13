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
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Description {

using FormatSpellDescription_t = void(__fastcall *)(const void *spellRecord, char *outBuf,
                                                    int bufLen, int contextFlag, int reserved3,
                                                    int useToolTipText, int reserved5,
                                                    int reserved6);

static int __fastcall Script_GetSpellDescription(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    const uint8_t *record = Spell::Lookup::RecordForID(spellID);
    if (record == nullptr)
        return 0; // nil for invalid / out-of-range spell IDs

    // 1024 bytes is what every engine caller uses (`push 0x400`). Spell
    // descriptions in vanilla don't exceed this in any locale; the helper
    // null-terminates and stops writing on overflow, so the upper bound is
    // self-enforced.
    char buffer[0x400];
    buffer[0] = '\0';

    auto fn = reinterpret_cast<FormatSpellDescription_t>(
        Offsets::FUN_FORMAT_SPELL_DESCRIPTION);
    fn(record, buffer, sizeof(buffer),
       /*contextFlag*/ 0,
       /*reserved3*/ 0,
       /*useToolTipText*/ 0, // 0 = read description (+0x228), non-zero = ToolTip (+0x24C)
       /*reserved5*/ 1,
       /*reserved6*/ 0);

    if (buffer[0] == '\0')
        return 0; // empty / no description in current locale → nil
    Game::Lua::PushString(L, buffer);
    return 1;
}

// --- Documentation ----------------------------------------------------------

static const Game::Doc::Field kGetSpellDescriptionArgs[] = {
    Game::Doc::Req("spell", "SpellIdentifier", "A spell ID, spell link, or spell name."),
};
static const Game::Doc::Field kGetSpellDescriptionRets[] = {
    Game::Doc::Opt("description", "string", nullptr,
                   "Nil for an unknown spell and for a spell with no description in "
                   "your language."),
};
static const Game::Doc::Function kGetSpellDescription{
    "The spell's description text, with its placeholders filled in at the base rank.",
    kGetSpellDescriptionArgs, kGetSpellDescriptionRets};

static void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellDescription",
                                     &Script_GetSpellDescription, &kGetSpellDescription);
}

static const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace Spell::Description
