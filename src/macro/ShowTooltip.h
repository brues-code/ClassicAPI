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

#pragma once

#include <cstddef>
#include <cstdint>

// `#showtooltip` / `#show` for action-bar macros.
//
// The engine caches one "primary spell" per macro (`MacroEntry +
// OFF_MACRO_PRIMARY_SPELL`, pet flag at `+IS_PET`) and its slot→spell helper
// (`FUN_ACTION_SLOT_TO_SPELL`) returns it for every macro slot — so cooldown,
// usable, range, current and auto-repeat state for a macro button all follow
// that one field. Vanilla only ever fills it from the first `/cast` line.
//
// This module resolves a macro's `#showtooltip [conditions] value` (or `#show`,
// or the bare form that reads the first `/cast` / `/use` line) on the world
// tick — conditionals evaluated by the Lua `SecureCmdOptionParse`, the value
// resolved to a spell (the engine's own name resolver) or an item (inventory
// walk) — writes the spell into that same engine field, and repaints the
// affected slots through the engine's own slot-changed notifier. The icon
// (`Action::Texture`), tooltip (`Tooltip::SetAction`) and item count /
// cooldown / consumable overrides (`Action::ItemState`) read the resolution
// through `Lookup` / `ForSlot`.
//
// Yields entirely when SuperCleveRoidMacros is loaded (it owns macro display
// with its own conditional dialect): `Active()` is false and every lookup
// fails, so the overrides pass straight through to the engine.

namespace Macro::ShowTooltip {

enum class Kind : uint8_t { None, Show, ShowTooltip };
enum class Target : uint8_t { None, Spell, Item };

struct Info {
    Kind kind;
    Target target;
    uint32_t spellID; // Target::Spell
    uint32_t isPet;   // Target::Spell — 1 when it resolved from the pet book
    int itemID;       // Target::Item
};

// False while yielding to SuperCleveRoidMacros (or before login).
bool Active();

// The directive resolution for a macro (by macroID — what the action table
// stores) or for an action slot (0-based). False when the macro has no
// directive, nothing resolved, or the module is yielding. A pending rescan
// / dirty entry is processed on the spot (the engine repaints buttons and
// the Macro UI in the same frame it re-parses an edited macro, before the
// next tick), so readers never see a stale resolution.
bool Lookup(uint32_t macroID, Info *out);
bool ForSlot(int slot0, Info *out);

// True when the macro's own icon is the question mark
// (`INV_Misc_QuestionMark`) — the one icon the directive replaces on the
// action bar and in the Macro UI; any other chosen icon stays.
bool HasQuestionMarkIcon(uint32_t macroID);

// Texture path of the resolved target: the spell's icon (its active icon
// when `activeIcon`, exactly the engine's toggle-up choice) or the item's
// icon. False (out empty) when nothing resolved or no icon record exists.
bool ResolvedIconPath(const Info &info, bool activeIcon, char *out, size_t outSize);

} // namespace Macro::ShowTooltip
