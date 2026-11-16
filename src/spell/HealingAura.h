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

#include "AutoList.h"

#include <cstdint>

// Extension point for non-stock healing-aura semantics feeding
// `GetSpellBonusHealing`. The stock vanilla healing auras — 135
// (MOD_HEALING_DONE, flat) and 175 (MOD_SPELL_HEALING_OF_STAT_PERCENT,
// ×Spirit) — are decoded inline in `spell/BonusDamage.cpp`. Server-custom
// aura effects (e.g. Turtle's Ironclad, aura 199 = ×Armor) register a
// resolver here so their meaning lives outside the stock decoder.
namespace Spell::HealingAura {

// Flat healing contribution for a single Spell.dbc aura effect: the aura
// index plus its computed amount (base + dice), and the player's Spirit /
// Armor for stat-scaled auras. Returns 0 for aura indices no resolver
// recognizes. Consulted by the stock decoder for any effect that isn't a
// stock healing aura.
long Resolve(int auraIndex, long amount, int32_t spirit, int32_t armor);

// A resolver maps one aura effect to its flat healing contribution, or
// returns 0 if it doesn't recognize the aura index. Declare a file-scope
// `static const Spell::HealingAura::AutoResolver` in a platform module to
// register one at static-init time.
using Resolver = long (*)(int auraIndex, long amount, int32_t spirit,
                          int32_t armor);
using AutoResolver = AutoList<struct AutoResolverTag, Resolver>;

} // namespace Spell::HealingAura
