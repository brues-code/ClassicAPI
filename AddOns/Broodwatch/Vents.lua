-- Broodwatch vents: the lair's lava cracks erupt in waves, and burn
-- whoever stands on them, you or the brood.

local _, BW = ...
local VENTS = BW.VENTS
local VENT_PATH = BW.VENT_PATH
local VENT_MIRROR_PATH = BW.VENT_MIRROR_PATH
local TUNING = BW.TUNING
local enemies = BW.enemies
local Dist2 = BW.Dist2
local RunGrowth = BW.RunGrowth
local Damage = BW.Damage
local Blast = BW.Blast
local MaxHp = BW.MaxHp

local ANIM_STAND = 0
local ANIM_ERUPT = 153 -- the crack's Custom0

------------------------------------------------------------------------------
-- Each crack is an actor at its gameobject's spawn, built once. A crack is
-- idle, marked (a red ring, VENT_WARN seconds) or erupting (VENT_ERUPT
-- seconds). Only idle cracks are picked for a wave. The cracks of a wave
-- erupt together, and one of them plays the sound: a sound per crack at
-- once roars.

local vents = {}

local function Build()
	for _, row in ipairs(VENTS) do
		local vent = { actor = BW.scene:AcquireActor() };
		vent.x, vent.y, vent.z = row[1], row[2], row[3];
		vent.radius = TUNING.VENT_RADIUS * row[5];
		vent.actor:SetModelByPath(row[6] and VENT_MIRROR_PATH or VENT_PATH);
		vent.actor:SetPosition(vent.x, vent.y, vent.z);
		vent.actor:SetYaw(row[4]);
		vent.actor:SetScale(row[5]);
		vent.actor:Show();
		tinsert(vents, vent);
	end
end

local function Idle(vent)
	vent.state = "idle";
	vent.actor:ClearGroundReticle();
	vent.actor:SetAnimation(ANIM_STAND);
end

-- A new run: every crack idle, the first wave at VENT_FIRST.
local function ResetVents()
	if not vents[1] then
		Build();
	end
	for _, vent in ipairs(vents) do
		Idle(vent);
	end
	BW.run.nextVents = TUNING.VENT_FIRST;
end

local function MarkWave()
	local size = 1 + math.floor((BW.run.time - TUNING.VENT_FIRST) / TUNING.VENT_WAVE_GROWTH);
	size = math.max(1, math.min(size, TUNING.VENT_WAVE_MAX)); -- at least one (a test's early wave)
	for _ = 1, size do
		-- A random idle crack (a few tries; a busy lair skips the rest).
		for _ = 1, 5 do
			local vent = vents[math.random(table.getn(vents))];
			if vent.state == "idle" then
				vent.state = "marked";
				vent.left = TUNING.VENT_WARN;
				vent.actor:SetGroundReticle(vent.radius, false);
				break;
			end
		end
	end
end

local function Erupt(vent, hx, hy, sound)
	vent.state = "erupting";
	vent.left = TUNING.VENT_ERUPT;
	vent.actor:ClearGroundReticle();
	vent.actor:SetAnimation(ANIM_ERUPT);
	if sound then
		vent.actor:PlaySound(TUNING.VENT_SOUND);
	end
	local r2 = vent.radius * vent.radius;
	if Dist2(hx, hy, vent.x, vent.y) <= r2 then
		BW.run.pendingHurt = BW.run.pendingHurt + MaxHp() * TUNING.VENT_HERO;
	end
	local damage = TUNING.VENT_DAMAGE * RunGrowth();
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, vent.x, vent.y) <= r2 then
				Damage(e, damage, TUNING.VENT_COLOR);
				Blast(e, vent.x, vent.y, TUNING.VENT_KNOCK);
			end
		end
	end
end

local function UpdateVents(dt, hx, hy)
	if BW.run.time >= BW.run.nextVents then
		BW.run.nextVents = BW.run.nextVents + TUNING.VENT_EVERY;
		MarkWave();
	end
	local sounded = false;
	for _, vent in ipairs(vents) do
		if vent.state ~= "idle" then
			vent.left = vent.left - dt;
			if vent.left <= 0 then
				if vent.state == "marked" then
					Erupt(vent, hx, hy, not sounded);
					sounded = true;
				else
					Idle(vent);
				end
			end
		end
	end
end

-- For the files that load after this one.
BW.ResetVents = ResetVents
BW.UpdateVents = UpdateVents
