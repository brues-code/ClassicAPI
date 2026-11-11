-- Broodwatch mines: Goblin Land Mines in the lair. The brood sets them off;
-- you lead it over them.

local _, BW = ...
local TUNING = BW.TUNING
local enemies = BW.enemies
local Dist2 = BW.Dist2
local FloorAt = BW.FloorAt
local RunGrowth = BW.RunGrowth
local SpawnPoint = BW.SpawnPoint
local Damage = BW.Damage
local Blast = BW.Blast

local ANIM_STAND = 0
local ANIM_DESPAWN = 157

------------------------------------------------------------------------------
-- A mine is not an enemy: no spell aims at it, and the hero walks over it
-- safely. It lies armed under a reticle the size of its blast until a whelp
-- (any enemy that moves) comes within MINE_TRIGGER. Then it blows: every
-- enemy within MINE_RADIUS is hurt and thrown, the mine plays Despawn and
-- hides, and MINE_RESPAWN seconds later it lies armed somewhere else.

local mines = {}

local function Arm(mine)
	local x, y = SpawnPoint();
	local _, _, hz = BW.hero:GetPosition();
	mine.x, mine.y, mine.z = x, y, FloorAt(x, y, hz);
	mine.state = "armed";
	mine.actor:SetPosition(mine.x, mine.y, mine.z);
	mine.actor:SetYaw(math.random() * 2 * math.pi);
	mine.actor:SetAnimation(ANIM_STAND);
	mine.actor:SetGroundReticle(TUNING.MINE_RADIUS);
	mine.actor:Show();
end

-- Lays MINE_COUNT armed mines (a new run).
local function LayMines()
	for i = 1, TUNING.MINE_COUNT do
		local mine = mines[i];
		if not mine then
			mine = { actor = BW.scene:AcquireActor() };
			mine.actor:SetModelByCreatureDisplayID(TUNING.MINE_DISPLAY);
			mines[i] = mine;
		end
		Arm(mine);
	end
end

local function Blow(mine)
	mine.state = "blowing";
	mine.left = TUNING.MINE_DESPAWN;
	mine.actor:ClearGroundReticle();
	mine.actor:PlaySpellVisualAtPoint(TUNING.MINE_SPELL, mine.x, mine.y, mine.z, 0);
	mine.actor:SetAnimation(ANIM_DESPAWN);
	local r2 = TUNING.MINE_RADIUS * TUNING.MINE_RADIUS;
	local damage = TUNING.MINE_DAMAGE * RunGrowth();
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, mine.x, mine.y) <= r2 then
				Damage(e, damage, TUNING.MINE_COLOR);
				Blast(e, mine.x, mine.y, TUNING.MINE_KNOCK);
			end
		end
	end
end

-- Whether a whelp stands on the mine.
local function Stepped(mine)
	local t2 = TUNING.MINE_TRIGGER * TUNING.MINE_TRIGGER;
	for _, e in ipairs(enemies) do
		if e.alive and e.kind.speed > 0 then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, mine.x, mine.y) <= t2 then
				return true;
			end
		end
	end
	return false;
end

local function UpdateMines(dt)
	for _, mine in ipairs(mines) do
		if mine.state == "armed" then
			if Stepped(mine) then
				Blow(mine);
			end
		else
			mine.left = mine.left - dt;
			if mine.left <= 0 then
				if mine.state == "blowing" then
					mine.state = "spent";
					mine.left = TUNING.MINE_RESPAWN;
					mine.actor:Hide();
				else
					Arm(mine);
				end
			end
		end
	end
end

local function ClearMines()
	for _, mine in ipairs(mines) do
		mine.state = "spent";
		mine.left = 0;
		mine.actor:ClearGroundReticle();
		mine.actor:Hide();
	end
end

-- For the files that load after this one.
BW.LayMines = LayMines
BW.UpdateMines = UpdateMines
BW.ClearMines = ClearMines
