-- Broodwatch eggs: a clutch laid in the lair hatches into whelps unless you
-- break it first.

local _, BW = ...
local EGG = BW.EGG
local TUNING = BW.TUNING
local Spawn = BW.Spawn
local SpawnPoint = BW.SpawnPoint
local Retire = BW.Retire
local PickKind = BW.PickKind
local FloorAt = BW.FloorAt
local CENTER = BW.CENTER

------------------------------------------------------------------------------
-- An egg is an enemy (BW.enemies), so every spell can break it, and a broken
-- one drops its gem as a whelp does. Here each egg counts down to its hatch:
-- EGG_WARN seconds before, a red ring marks it, and at the hatch it cracks
-- and EGG_WHELPS whelps (of the kinds spawning by then) come out where it stood.

local eggs = {} -- the eggs laid and not yet broken or hatched

-- Each egg knows its clutch: where it was laid and when it hatches (the
-- off-screen markers show a clutch once).
local function LayClutch()
	local cx, cy = SpawnPoint();
	local clutch = { x = cx, y = cy, z = FloorAt(cx, cy, CENTER[3]), hatchAt = BW.run.time + TUNING.EGG_HATCH };
	local n = TUNING.EGG_CLUTCH;
	for k = 1, n do
		local a = (k - 1) * 2 * math.pi / n + math.random() * 0.6;
		local e = Spawn(EGG, cx + TUNING.EGG_SPREAD * math.cos(a), cy + TUNING.EGG_SPREAD * math.sin(a));
		e.hatchAt = clutch.hatchAt;
		e.clutch = clutch;
		e.warned = nil;
		tinsert(eggs, e);
	end
end

local function Hatch(e)
	e.actor:ClearGroundReticle();
	local x, y = e.actor:GetPosition();
	-- A dot on the egg ends with it: Corruption spreads only from a kill.
	e.dotLeft = nil;
	Retire(e);
	for _ = 1, TUNING.EGG_WHELPS do
		local a = math.random() * 2 * math.pi;
		Spawn(PickKind(), x + 0.6 * math.cos(a), y + 0.6 * math.sin(a));
	end
end

local function UpdateEggs()
	if BW.run.time >= BW.run.nextClutch then
		BW.run.nextClutch = BW.run.nextClutch + TUNING.EGG_EVERY;
		LayClutch();
	end
	for i = table.getn(eggs), 1, -1 do
		local e = eggs[i];
		if not e.alive then
			-- Broken.
			e.actor:ClearGroundReticle();
			table.remove(eggs, i);
		elseif BW.run.time >= e.hatchAt then
			Hatch(e);
			table.remove(eggs, i);
		elseif not e.warned and BW.run.time >= e.hatchAt - TUNING.EGG_WARN then
			e.warned = true;
			e.actor:SetGroundReticle(0, false);
		end
	end
end

-- The eggs themselves are enemies: ClearEnemies releases them.
local function ClearEggs()
	for i = table.getn(eggs), 1, -1 do
		eggs[i].actor:ClearGroundReticle();
		eggs[i] = nil;
	end
end

-- For the files that load after this one.
BW.eggs = eggs
BW.LayClutch = LayClutch
BW.UpdateEggs = UpdateEggs
BW.ClearEggs = ClearEggs
