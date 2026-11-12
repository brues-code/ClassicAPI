-- Broodwatch core: the shared state, small helpers and the gems.
--
-- The files share the addon table BW (`local _, BW = ...`). A file keeps
-- its own helpers local. What another file calls, and state that changes,
-- lives on BW; constants are copied into a local at the top of each file
-- that reads them. The TOC loads Data first and Broodwatch.lua last.

local _, BW = ...
local GEM_PATH = BW.GEM_PATH
local MAX_GEMS = BW.MAX_GEMS
local MAX_LEVEL = BW.MAX_LEVEL

-- Whether the hero has the spell at rank V (its mastery).
local function Mastered(key)
	local state = BW.run and BW.run.spells[key];
	return state ~= nil and state.level >= MAX_LEVEL;
end

------------------------------------------------------------------------------
-- State

-- BW.OnImpact (the impact of any cast in flight) is defined with the spells,
-- BW.DropItem (a kill's chance of an item) with the items.

BW.held = {}
BW.moving = false

local pools = {}   -- kind key -> list of free enemy records
local enemies = {} -- live and dying enemy records
local gemPool = {}
local gems = {}

-- BW.run is the current run, or nil.
BW.paused = false
local offers = {}  -- the three level-up choices

------------------------------------------------------------------------------
-- Helpers

local function FloorAt(x, y, z)
	local _, _, hz = BW.scene:TraceSegment(x, y, z + 10, x, y, z - 50);
	return hz or z;
end

local function Dist2(ax, ay, bx, by)
	local dx, dy = ax - bx, ay - by;
	return dx * dx + dy * dy;
end

local function Clock(seconds)
	local s = math.floor(seconds);
	return string.format("%d:%02d", math.floor(s / 60), math.mod(s, 60));
end

-- Damage that is not a spell's (items, mines) grows as the brood's health
-- does (KindStats: (1 + m)^3, m the run's minutes over 5).
local function RunGrowth()
	local m = 1 + BW.run.time / 300;
	return m * m * m;
end

local function Shuffle(list)
	for i = table.getn(list), 2, -1 do
		local j = math.random(i);
		list[i], list[j] = list[j], list[i];
	end
end

------------------------------------------------------------------------------
-- The hero's movement: forward or back (1, -1 or 0), to the side (1 left,
-- -1 right, 0) and the facing (nil keeps it), sent to the actor only when
-- they change, since each command re-anchors the move. Back is the client's
-- backpedal, slower than a run; the side is its strafe.

BW.moveFwd, BW.moveSide, BW.moveFacing = 0, 0, nil

local function Drive(fwd, side, facing)
	local hero = BW.hero;
	if facing and facing ~= BW.moveFacing then
		BW.moveFacing = facing;
		hero:SetYaw(facing);
	end
	if fwd ~= BW.moveFwd then
		BW.moveFwd = fwd;
		if fwd > 0 then
			hero:StartMovingForward();
		elseif fwd < 0 then
			hero:StartMovingBackward();
		else
			hero:StopMoving();
		end
	end
	if side ~= BW.moveSide then
		BW.moveSide = side;
		if side > 0 then
			hero:StartStrafingLeft();
		elseif side < 0 then
			hero:StartStrafingRight();
		else
			hero:StopStrafing();
		end
	end
	BW.moving = fwd ~= 0 or side ~= 0;
end

------------------------------------------------------------------------------
-- Gems

local function DropGem(x, y, z, xp, scale)
	if table.getn(gems) >= MAX_GEMS then
		-- Too many on the floor: the experience goes straight to the oldest gem.
		gems[1].xp = gems[1].xp + xp;
		return;
	end
	local gem = table.remove(gemPool);
	if not gem then
		gem = { actor = BW.scene:AcquireActor() };
		gem.actor:SetModelByPath(GEM_PATH);
	end
	gem.xp = xp;
	gem.x, gem.y, gem.z = x, y, z;
	gem.pulled = false;
	gem.actor:SetScale(scale or 0.3);
	gem.actor:SetPosition(x, y, z);
	gem.actor:Show();
	tinsert(gems, gem);
end

local function ClearGems()
	for i = table.getn(gems), 1, -1 do
		gems[i].actor:Hide();
		tinsert(gemPool, gems[i]);
		gems[i] = nil;
	end
end

-- For the files that load after this one.
BW.Mastered = Mastered
BW.pools = pools
BW.enemies = enemies
BW.gemPool = gemPool
BW.gems = gems
BW.offers = offers
BW.FloorAt = FloorAt
BW.Dist2 = Dist2
BW.Clock = Clock
BW.Shuffle = Shuffle
BW.Drive = Drive
BW.RunGrowth = RunGrowth
BW.DropGem = DropGem
BW.ClearGems = ClearGems
