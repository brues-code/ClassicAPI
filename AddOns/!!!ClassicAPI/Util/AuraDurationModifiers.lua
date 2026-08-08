-- Server-side DoT-duration adjustments the 1.12 client is never told about.
--
-- ClassicAPI derives C_UnitAuras expirationTime for a debuff on another unit
-- from the cast packet (SMSG_SPELL_GO). The vanilla server never retransmits a
-- LATER server-side duration change on a non-self unit -- verified in the
-- server source: the change runs through SetAuraDuration / RefreshHolder, and
-- the only duration packet (SMSG_UPDATE_AURA_DURATION) is self-scoped to the
-- aura-bearer, so an observing caster hears nothing (and on a mob the packet
-- isn't even built). We can't hear the change, but we DO see the *triggering*
-- cast, so C_UnitAuras.RegisterAuraDurationModifier mirrors the server's edit
-- off that trigger.
--
-- A trigger is matched by exact spellID (stable across ranks -- these are the
-- IDs the server binds its spell script to); the affected aura by
-- SpellFamilyName + a family-flag overlap (rank-proof, exactly how the server
-- scripts locate it). Flags below are from the client's Spell.dbc. When the
-- trigger is a whole class of spells rather than a named ability,
-- RegisterAuraDurationModifierByTrigger matches it by SpellFamilyName + school
-- index instead (see Shadow Weaving below).
--
-- Carnage (druid) is the case a rule cannot express, and is handled at the
-- bottom of this file instead: its refresh fires behind a per-combo-point
-- probability roll, so keying it on the Ferocious Bite cast alone would
-- over-refresh at every rank but 2/2 spending five points. The roll's outcome
-- IS observable -- it returns a combo point -- but that arrives as a combo
-- point change rather than a packet, which is not something the trigger
-- matching here can see.
-- Conflagrate's *full*-consume variant (other servers) removes Immolate
-- outright; that clears the aura slot and ClassicAPI evicts it via the normal
-- removal path, so the reduce rule below is harmless there and correct on
-- Turtle (where Immolate keeps ticking with 3s shaved off).

if TURTLE_WOW_VERSION then
    local WARLOCK, SHAMAN = 5, 11
    -- Lua 5.0 has no 0x hex literals; tonumber(hex, 16) keeps the flags readable.
    local IMMOLATE_FLAG   = tonumber("4", 16)        -- CF_WARLOCK_IMMOLATE  (bit 2)
    local FLAMESHOCK_FLAG = tonumber("10000000", 16) -- CF_SHAMAN_FLAME_SHOCK (bit 28)

    -- Conflagrate (all ranks) -> shave 3s off the caster's Immolate.
    local CONFLAGRATE = { 17962, 18930, 18931, 18932 }
    for i = 1, table.getn(CONFLAGRATE) do
        C_UnitAuras.RegisterAuraDurationModifier(CONFLAGRATE[i], WARLOCK, IMMOLATE_FLAG, 0, "reduce", 3)
    end

    -- Molten Blast (all ranks) -> refresh the caster's Flame Shock to full.
    local MOLTEN_BLAST = { 36916, 36917, 36918, 36919, 36920, 36921 }
    for i = 1, table.getn(MOLTEN_BLAST) do
        C_UnitAuras.RegisterAuraDurationModifier(MOLTEN_BLAST[i], SHAMAN, FLAMESHOCK_FLAG, 0, "refresh")
    end
end
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

-- Carnage (druid): Ferocious Bite gains a chance PER COMBO POINT SPENT (10% at
-- rank 1, 20% at rank 2) to refresh the caster's Rake and Rip and to grant an
-- additional combo point. The refresh emits no packet and the proc applies no
-- aura, so no trigger rule can see it -- but the granted point can be:
-- Ferocious Bite spends every point, so a gain right after one is the proc.
-- Only 2/2 spending five is certain, so confirming beats assuming; refreshing
-- on every Bite would show a full timer on a DoT about to fall off.
--
-- Rip and Rake are matched by family flags (every rank, server additions
-- included); Ferocious Bite is an ID list like the triggers above. An unlisted
-- rank under-triggers, leaving the timer as it is today.
local DRUID = 7
local RIP_FLAG,  RIP_ICON  = tonumber("800000", 16), 108
local RAKE_FLAG, RAKE_ICON = tonumber("1000", 16),   494
local FEROCIOUS_BITE = {
    [22568] = 1, [22827] = 1, [22828] = 1, [22829] = 1, [31018] = 1,
}

-- Long enough to absorb latency, short enough that the GCD the Bite just
-- started excludes any other source of a combo point.
local CARNAGE_WINDOW = 0.5

local carnageGuid, carnageUntil

-- At file load UnitClass has no player to answer for and returns nil, so the
-- class gate would silently never register.
EventUtil.ContinueOnPlayerLogin(function()
    local _, class = UnitClass("player")
    if class ~= "DRUID" then return end

    local f = CreateFrame("Frame")
    f:RegisterEvent("UNIT_SPELLCAST_SUCCEEDED")
    f:RegisterEvent("PLAYER_COMBO_POINTS")
    f:SetScript("OnEvent", function()
        if event == "UNIT_SPELLCAST_SUCCEEDED" then
            if arg1 ~= "player" or not FEROCIOUS_BITE[arg3] then return end
            -- Captured now, used later: the refresh belongs to the unit the
            -- Bite hit, which need not still be the target.
            carnageGuid, carnageUntil = UnitGUID("target"), GetTime() + CARNAGE_WINDOW
            return
        end

        if not carnageGuid then return end
        if GetTime() > carnageUntil then carnageGuid = nil; return end

        -- The Bite spent every point, so the first change reported after it
        -- already reflects that: 0 is a failed roll, anything above it is the
        -- refund. Waiting to see the 0 would miss the proc whenever the server
        -- coalesces spend and refund into one update (5 -> 1).
        if GetComboPoints() > 0 then
            C_UnitAuras.RefreshAuraDurationByFamily(carnageGuid, DRUID, RIP_FLAG, RIP_ICON)
            C_UnitAuras.RefreshAuraDurationByFamily(carnageGuid, DRUID, RAKE_FLAG, RAKE_ICON)
            carnageGuid = nil
        end
    end)
end)
