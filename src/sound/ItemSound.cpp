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

// `C_Sound.PlayItemSound(soundType, item)` — play the noise an item makes
// when it is picked up, dropped, used or closed, without performing the
// action. Plus `Enum.ItemSoundType`, the values it takes.
//
// The engine already owns the whole lookup: `FUN_SOUND_PLAY_ITEM` takes a
// sound type and an ItemDisplayInfo id, walks ItemDisplayInfo -> its
// ItemGroupSounds row -> the row's four SoundEntries ids, and plays the one
// the type selects. The four columns line up with the modern enum in order
// (Pickup, Drop, Use, Close), which is what makes this a backport rather
// than a reconstruction — so all this module does is turn an item argument
// into a display id and call it.
//
// Sound comes from the item's DISPLAY info, not the item itself, so items
// sharing a display share a sound: every cloth chest piece that looks alike
// sounds alike. That is the engine's own grouping, and the reason a sound
// exists for items nobody ever handed a bespoke recording.
//
// Only Pickup and Drop play on a stock client, and that is the DATA, not
// this code: ItemGroupSounds has 24 rows, and the 18 that any
// ItemDisplayInfo row actually points at (groups 7..24, ~29,000 displays
// between them) fill in columns 1 and 2 only. The six that reference a Use
// sound are unreachable or broken — groups 1 and 2 have no displays at all,
// and every Use id they name (273/274/275) matches no SoundEntries row. The
// Close column is zero in all 24. So Use and Close fall through the
// engine's own bounds checks in silence, which is why no item can test them.
//
// Deliberately still a pass-through: the type indexes the row rather than
// selecting a branch here, so a server that fills those columns in (and
// adds the SoundEntries rows they name) gets Use and Close working with no
// change on this side. Do not "simplify" this by rejecting types 2 and 3.

#include "Game.h"
#include "Offsets.h"
#include "item/ID.h"
#include "item/Location.h"
#include "item/Record.h"

#include <cstdint>

namespace Sound::ItemSound {

namespace {

// `void __fastcall(int soundType /*ecx*/, int displayInfoID /*edx*/)`.
// Silently does nothing for an unknown display id or a row with no sounds,
// which is the behavior we want at the Lua edge too.
using PlayItemSound_t = void(__fastcall *)(int soundType, int displayInfoID);

// Modern `Enum.ItemSoundType`. These are the ItemGroupSounds column order,
// so the numbers are the engine's own, not a mapping we invented.
const Game::Lua::EnumIntegerEntry kItemSoundTypeEntries[] = {
    {"Pickup", 0},
    {"Drop", 1},
    {"Use", 2},
    {"Close", 3},
};

constexpr int kSoundTypeMax = 3;

// C_Sound.PlayItemSound(soundType, item)
//
// `item` is an ItemLocation table, and also anything else this project's
// item APIs accept — a GUID string, an itemID, an item link, or the name of
// an item the player carries — since `ResolveItemArgOrLocation` is the
// shared resolver for "which item did the caller mean".
//
// Plays nothing when the item is not resolvable or its record is not cached
// yet, matching the engine's own silence for an unknown display id. Returns
// nothing, as the engine's player reports nothing.
int __fastcall Script_PlayItemSound(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Sound.PlayItemSound(soundType, item)");
        return 0;
    }
    // Read the sound type BEFORE resolving: the location resolver stomps
    // the Lua stack (it drives PackBagSlot), so argument 1 must be off the
    // stack first.
    const int soundType = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (soundType < 0 || soundType > kSoundTypeMax)
        return 0;

    const uint8_t *cgItem = Item::Location::ResolveItemArgOrLocation(L, 2);
    if (cgItem == nullptr)
        return 0;

    const int itemID = Item::ID::FromCGItem(cgItem);
    if (itemID <= 0)
        return 0;

    const uint8_t *record = Item::PeekRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return 0; // not cached — nothing to look a display id up with

    const uint32_t displayInfoID =
        Game::Read<uint32_t>(record, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    if (displayInfoID == 0)
        return 0;

    reinterpret_cast<PlayItemSound_t>(Offsets::FUN_SOUND_PLAY_ITEM)(
        soundType, static_cast<int>(displayInfoID));
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Sound", "PlayItemSound", &Script_PlayItemSound);
    Game::Lua::RegisterIntegerEnum("Enum", "ItemSoundType", kItemSoundTypeEntries,
                                   sizeof(kItemSoundTypeEntries) /
                                       sizeof(kItemSoundTypeEntries[0]));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::ItemSound
