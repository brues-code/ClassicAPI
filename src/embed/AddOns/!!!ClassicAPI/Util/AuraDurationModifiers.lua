-- Server-side DoT-duration adjustments the 1.12 client is never told about.
--
-- ClassicAPI derives C_UnitAuras expirationTime for a debuff on another unit
-- from the cast packet (SMSG_SPELL_GO). The vanilla server never retransmits a
-- LATER server-side duration change on a non-self unit -- verified in the
-- server source: the change runs through SetAuraDuration / RefreshHolder, and
-- the only duration packet (SMSG_UPDATE_AURA_DURATION) is self-scoped to the
-- aura-bearer, so an observing caster hears nothing (and on a mob the packet
-- isn't even built). We can't hear the change, but we DO see the *triggering*
-- cast, so a registered rule mirrors the server's edit off that trigger.
--
-- A trigger keyed to a whole class of spells (rather than a named ability) is
-- registered from Lua with RegisterAuraDurationModifierByTrigger, matched by
-- SpellFamilyName + school index (see Shadow Weaving below). Exact-spellID
-- triggers are registered from C++ (Aura::Source::AddDurationMod): Turtle's
-- Conflagrate -> Immolate and Molten Blast -> Flame Shock live in the DLL
-- (src/turtle/DurationMods.cpp), as does Carnage, whose per-combo-point roll no
-- rule can express (src/turtle/Carnage.cpp).

-- Shadow Weaving (priest): any shadow-school priest cast refreshes the caster's
-- Shadow Vulnerability (15258, 15s, stacks to 5). The 1->5 stack changes fire
-- OnAuraStacksChanged (already handled); the 5->5 refresh is the silent blind
-- spot this covers. Matched by family(6) + school(5) so it spans every priest
-- shadow spell/rank incl. Turtle additions, excluding holy/discipline casts
-- (Smite, heals) which are priest-family but not shadow-school.
--
-- Gated on the 5/5 talent (15334), where the proc is 100% -> deterministic;
-- below 5/5 it's chance-based and inferring on every cast would over-refresh.
-- Deferred to SPELLS_CHANGED because talent/spell state isn't ready at load,
-- and re-checked there so a respec INTO 5/5 registers without a reload.
-- Caveat: DoT *ticks* (SW:P / Devouring Plague) refresh it server-side too but
-- emit no cast packet, so a pure DoT-only phase (no direct casts) under-counts.
local PRIEST, SHADOW = 6, 5
local SW_FLAG, SW_ICON = tonumber("4000000", 16), 9 -- 15258 SpellFamilyFlags + icon

EventUtil.RegisterOnceFrameEventAndCallback("SPELLS_CHANGED", function()
    if IsPlayerSpell(15334) then
        C_UnitAuras.RegisterAuraDurationModifierByTrigger(PRIEST, SHADOW, PRIEST, SW_FLAG, SW_ICON, "refresh")
    end
end)
