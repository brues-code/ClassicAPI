-- AuraUtil: iterate / find auras without allocating a table per aura.
--
-- Backport of FrameXML's AuraUtil, the helper library that sits on top of
-- C_UnitAuras. Its whole value on 1.12 is the zero-allocation path: modern
-- addons that scan many units' auras every frame call AuraUtil.ForEachAura (with
-- usePackedAura false) instead of C_UnitAuras.GetAuraDataByIndex, so no AuraData
-- table is built per aura -- which matters far more under Lua 5.0's mark-sweep
-- GC than it does on retail's generational GC. See ClassicAPI issue #30.
--
-- Ported from the anniversary source (Blizzard_FrameXMLUtil/AuraUtil.lua),
-- keeping the exact public shapes:
--   * ForEachAura(unit, filter, batchSize, func, usePackedAura)
--   * FindAura(predicate, unit, filter, arg1, arg2, arg3)
--   * FindAuraByName(auraName, unit, filter)
--   * UnpackAuraData(auraData)
--
-- Iteration batches through C_UnitAuras.GetAuraSlots + a by-slot fetch
-- (continuation tokens), exactly as retail's does: one enumeration per batch,
-- then a direct fetch per aura -- linear in the aura count. The by-index getters
-- (GetAuraDataByIndex / UnitAura) re-walk the aura array from slot 0 on every
-- call, so iterating by index is quadratic; don't go back to it here.
--
-- Two deliberate differences from that source:
--   1. Retail's helpers receive GetAuraSlots' return list as varargs
--      (ForEachAuraHelper / FindAuraRecurse). Lua 5.0 builds an `arg` table for
--      EVERY vararg call, so that shape allocates one table per batch here --
--      exactly the per-frame garbage this library exists to avoid. We use
--      GetAuraSlots' fill-a-table form instead (a table as its 5th argument,
--      a ClassicAPI extension): the slot ids land in a reusable buffer and the
--      call returns `continuationToken, n`. No vararg frame anywhere.
--   2. The non-packed ForEachAura and FindAura fetch each aura through the
--      positional C_UnitAuras.UnitAuraBySlot (no table), the way retail's
--      AuraUtil did before it retired the positional by-slot accessor. So both
--      are zero-allocation here -- FindAura in particular, where the current
--      retail version allocates a table per aura scanned.
--
-- The comparator / dispel-priority / border helpers (DefaultAuraCompare,
-- UnitFrameDebuffComparator, ShouldDisplayDebuff, SetAuraBorder*, IsPriorityDebuff)
-- are NOT ported: they depend on isPriorityAura / auraInstanceID / the aura
-- visualization pipeline / secure-call machinery, none of which exist on 1.12.

local GetAuraSlots = C_UnitAuras.GetAuraSlots
local GetAuraDataBySlot = C_UnitAuras.GetAuraDataBySlot
local UnitAuraBySlot = C_UnitAuras.UnitAuraBySlot

AuraUtil = AuraUtil or {};

-- Slot-id buffers for GetAuraSlots' fill form, one per nesting level: a `func`
-- or `predicate` that itself iterates another unit's auras gets its own buffer
-- instead of clobbering the outer scan's. Costs one table per nesting level for
-- the session (retail's varargs are re-entrant for free; ours must be explicit).
-- An error thrown out of `func` skips the release and leaves `depth` one higher
-- for the rest of the session -- harmless: later scans use the next buffer up,
-- allocated once.
local slotBufs = {};
local depth = 0;

local function AcquireSlotBuf()
    depth = depth + 1;
    local buf = slotBufs[depth];
    if not buf then
        buf = {};
        slotBufs[depth] = buf;
    end
    return buf;
end

local function ReleaseSlotBuf()
    depth = depth - 1;
end

-- Returns the AuraData table's fields in the classic `UnitAura` positional order,
-- for code written against that shape. Retail appends `unpack(auraData.points)`;
-- vanilla auras carry no points, so the tuple ends at timeMod (15 values). Field
-- #13 is `isFromPlayerOrPlayerPet` (the table's field), as retail's UnpackAuraData
-- does -- the positional C_UnitAuras.UnitAura reports `castByPlayer` there
-- instead, which is false for a pet's aura.
function AuraUtil.UnpackAuraData(auraData)
    if not auraData then
        return nil;
    end
    return auraData.name,
        auraData.icon,
        auraData.applications,
        auraData.dispelName,
        auraData.duration,
        auraData.expirationTime,
        auraData.sourceUnit,
        auraData.isStealable,
        auraData.nameplateShowPersonal,
        auraData.spellId,
        auraData.canApplyAura,
        auraData.isBossAura,
        auraData.isFromPlayerOrPlayerPet,
        auraData.nameplateShowAll,
        auraData.timeMod;
end

-- Calls `func` for each aura on `unit` matching `filter`, in order, until `func`
-- returns a truthy value or the auras run out. Non-packed (default) passes the 15
-- positional `UnitAura` values (no table); `usePackedAura` passes the AuraData
-- table. `batchSize` is the number of slots fetched per GetAuraSlots call (nil =
-- all at once); it never caps how many auras are visited. `batchSize <= 0` does
-- nothing.
function AuraUtil.ForEachAura(unit, filter, batchSize, func, usePackedAura)
    if batchSize and batchSize <= 0 then
        return;
    end
    local buf = AcquireSlotBuf();
    local continuationToken, n;
    repeat
        continuationToken, n = GetAuraSlots(unit, filter, batchSize, continuationToken, buf);
        for i = 1, n do
            local done;
            if usePackedAura then
                done = func(GetAuraDataBySlot(unit, buf[i]));
            else
                done = func(UnitAuraBySlot(unit, buf[i]));
            end
            if done then
                ReleaseSlotBuf();
                return;
            end
        end
    until continuationToken == nil;
    ReleaseSlotBuf();
end

-- Returns the positional values of the first aura for which `predicate` returns
-- truthy, or nil. `predicate` receives up to three caller args followed by the
-- aura's positional values: predicate(arg1, arg2, arg3, name, icon, ...). Sources
-- auras from the positional by-slot accessor, so no table is allocated while
-- scanning.
function AuraUtil.FindAura(predicate, unit, filter, arg1, arg2, arg3)
    local buf = AcquireSlotBuf();
    local continuationToken, n;
    repeat
        continuationToken, n = GetAuraSlots(unit, filter, nil, continuationToken, buf);
        for i = 1, n do
            local slot = buf[i];
            if predicate(arg1, arg2, arg3, UnitAuraBySlot(unit, slot)) then
                ReleaseSlotBuf();
                return UnitAuraBySlot(unit, slot);
            end
        end
    until continuationToken == nil;
    ReleaseSlotBuf();
    return nil;
end

-- Returns the positional values of the first aura whose name matches `auraName`,
-- or nil. Names are localized and not unique -- this finds the first match.
function AuraUtil.FindAuraByName(auraName, unit, filter)
    return AuraUtil.UnpackAuraData(
        C_UnitAuras.GetAuraDataBySpellName(unit, auraName, filter));
end
