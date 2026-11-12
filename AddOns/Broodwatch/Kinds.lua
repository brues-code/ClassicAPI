-- Broodwatch spell kinds beyond the first ones (Spells.lua): cones, turrets,
-- zones, traps and charges. Each registers its Fire and Cast in
-- BW.kindHandlers; the step runs UpdateKinds, a new run ClearKinds.

local _, BW = ...
local SPELLS = BW.SPELLS
local TUNING = BW.TUNING
local enemies = BW.enemies
local Dist2 = BW.Dist2
local FloorAt = BW.FloorAt
local Stat = BW.Stat
local Mastered = BW.Mastered
local SpellDamage = BW.SpellDamage
local Burn = BW.Burn
local Hold = BW.Hold
local Blast = BW.Blast
local Slow = BW.Slow
local ShowAura = BW.ShowAura
local Nearest = BW.Nearest
local PackCenter = BW.PackCenter
local QueueCast = BW.QueueCast

local handlers = BW.kindHandlers
local FROZEN_AURA = 122 -- Frost Nova's ice, on what a cone freezes
local IMPACTS = 8       -- enemies a hit shows its impact kit on, so a pack does not roar
local ANIM_SPAWN, ANIM_CLOSED, ANIM_CUSTOM0 = 145, 147, 153

-- The usual Fire of a kind with no aim: one cast in the queue.
local function QueueOne(key, state, def, level)
	QueueCast({ key = key, state = state, level = level });
	state.pending = state.pending + 1;
	return true;
end

-- One cast of the spell's visual from `caster` at the first IMPACTS of
-- `hit` (PlaySpellVisual with a table: the cast kit once, an impact kit on
-- each), or at the caster itself when nothing was hit.
local function ShowHits(caster, spell, hit)
	local actors = {};
	for i = 1, math.min(table.getn(hit), IMPACTS) do
		actors[i] = hit[i].actor;
	end
	if actors[1] then
		caster:PlaySpellVisual(spell, actors, 0);
	else
		caster:PlaySpellVisual(spell, caster, 0);
	end
end

-- What a hit does besides its damage, by the spell's fields at `level`.
local function Afflict(e, key, def, level, fromX, fromY)
	if not e.alive then
		return;
	end
	local slow = Stat(def, "slow", level);
	if slow then
		Slow(e, slow, 2);
	end
	local freeze = Stat(def, "freeze", level) or (Mastered(key) and def.mastery.freeze);
	if freeze then
		Hold(e, freeze, def.effect or FROZEN_AURA, true);
	end
	local ignite = Stat(def, "ignite", level);
	if ignite then
		Burn(e, ignite, def.igniteSeconds, def.burnSchool or def.school, def.color);
	end
	if def.knock then
		Blast(e, fromX, fromY, def.knock);
	end
end

------------------------------------------------------------------------------
-- Cones: everything within `length` of you, inside CONE_ARC (times `wide`)
-- of the cone's aim. The aim is the direction that catches the most of the
-- brood; the cast turns you to it for CAST_POSE (the cone hangs at your hand
-- or breath), and you strafe or backpedal the way you hold meanwhile
-- (Steer).

-- Counts the enemies in the cone aimed at `yaw`, or with `list` gathers them.
local function InCone(def, level, hx, hy, yaw, list)
	local length = Stat(def, "length", level);
	local half = TUNING.CONE_ARC * (def.wide or 1) / 2;
	local l2 = length * length;
	local count = 0;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			local dx, dy = x - hx, y - hy;
			local d2 = dx * dx + dy * dy;
			if d2 <= l2 then
				local off = math.mod(math.atan2(dy, dx) - yaw + 3 * math.pi, 2 * math.pi) - math.pi;
				if d2 < 1 or math.abs(off) <= half then
					count = count + 1;
					if list then
						tinsert(list, e);
					end
				end
			end
		end
	end
	return count;
end

-- The aim that catches the most: tried toward each of up to AIM_TRIES
-- enemies in reach. Nil when none is in reach.
local AIM_TRIES = 16
local function BestAim(def, level, hx, hy)
	local length = Stat(def, "length", level);
	local l2 = length * length;
	local best, bestCount, tried = nil, 0, 0;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, hx, hy) <= l2 then
				local yaw = math.atan2(y - hy, x - hx);
				local count = InCone(def, level, hx, hy, yaw);
				if count > bestCount then
					best, bestCount = yaw, count;
				end
				tried = tried + 1;
				if tried >= AIM_TRIES then
					break;
				end
			end
		end
	end
	return best;
end

handlers.cone = {
	Fire = function(key, state, def, level)
		local hx, hy = BW.hero:GetPosition();
		return BestAim(def, level, hx, hy) ~= nil and QueueOne(key, state, def, level);
	end,
	Cast = function(entry, def)
		local hx, hy = BW.hero:GetPosition();
		local yaw = BestAim(def, entry.level, hx, hy);
		if not yaw then
			return;
		end
		-- The pose: you face the cone (Steer turns you, next frame).
		BW.run.pose = TUNING.CAST_POSE;
		BW.run.poseYaw = yaw;
		local hit = {};
		InCone(def, entry.level, hx, hy, yaw, hit);
		ShowHits(BW.hero, def.spell, hit);
		local damage = Stat(def, "damage", entry.level);
		for _, e in ipairs(hit) do
			SpellDamage(e, damage, def);
			Afflict(e, entry.key, def, entry.level, hx, hy);
		end
	end,
}

------------------------------------------------------------------------------
-- Totems: a turret shoots its `bolt` at the nearest enemies in range every
-- `every` seconds (Fire Nova Totem also pulses as Magma Totem does); a zone
-- slows every enemy near it, with its aura's visual on each.

local totems = {}
local totemPool = {}
local MAGMA_PULSE = 8187 -- Magma Totem's pulse: its visual puts an Immolate impact on each enemy

local function PlantTotem(key, def, level)
	local hx, hy, hz = BW.hero:GetPosition();
	local totem = table.remove(totemPool);
	if not totem then
		totem = { actor = BW.scene:AcquireActor() };
		totem.actor:HookScript("OnSpellVisualImpact", function(_, _, _, castID) BW.OnImpact(nil, nil, nil, castID); end);
	end
	if totem.display ~= def.display then
		totem.display = def.display;
		totem.actor:SetModelByCreatureDisplayID(def.display);
	end
	local a = math.random() * 2 * math.pi;
	totem.x, totem.y = hx + 2 * math.cos(a), hy + 2 * math.sin(a);
	totem.key, totem.level = key, level;
	totem.life = Stat(def, "duration", level);
	totem.shot, totem.pulse, totem.root = 0, def.pulseEvery or 0.5, 0;
	totem.actor:SetPosition(totem.x, totem.y, FloorAt(totem.x, totem.y, hz));
	totem.actor:Show();
	tinsert(totems, totem);
end

local totemHit = {}
local function NotTotemHit(e)
	return totemHit[e];
end

local function Shoot(totem, def)
	local bolts = def.bolts or (Mastered(totem.key) and def.mastery.bolts) or 1;
	local damage = Stat(def, "damage", totem.level);
	for _ = 1, bolts do
		local target = Nearest(totem.x, totem.y, def.range, NotTotemHit);
		if not target then
			break;
		end
		totemHit[target] = true;
		local castID = totem.actor:PlaySpellVisual(def.bolt, target.actor, 0);
		if castID then
			BW.run.inFlight[castID] = { key = totem.key, target = target, damage = damage, level = totem.level };
		end
	end
	for e in pairs(totemHit) do
		totemHit[e] = nil;
	end
end

-- Every enemy within the radius: `each(e)`; the first IMPACTS get `spell`'s
-- impact from the totem.
local function AroundTotem(totem, radius, spell, each)
	local r2 = radius * radius;
	local shown = 0;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, totem.x, totem.y) <= r2 then
				if spell and shown < IMPACTS then
					shown = shown + 1;
					totem.actor:PlaySpellVisual(spell, e.actor, 0);
				end
				each(e);
			end
		end
	end
end

local function UpdateTotems(dt)
	for i = table.getn(totems), 1, -1 do
		local totem = totems[i];
		local def = SPELLS[totem.key];
		totem.life = totem.life - dt;
		if totem.life <= 0 then
			totem.actor:Hide();
			tinsert(totemPool, totem);
			table.remove(totems, i);
		elseif def.kind == "turret" then
			totem.shot = totem.shot - dt;
			if totem.shot <= 0 then
				totem.shot = totem.shot + def.every;
				Shoot(totem, def);
			end
			if def.pulse then
				totem.pulse = totem.pulse - dt;
				if totem.pulse <= 0 then
					totem.pulse = totem.pulse + def.pulseEvery;
					local damage = Stat(def, "pulseDamage", totem.level);
					AroundTotem(totem, Stat(def, "radius", totem.level), MAGMA_PULSE, function(e)
						SpellDamage(e, damage, def);
					end);
				end
			end
		else -- a zone
			totem.pulse = totem.pulse - dt;
			if totem.pulse <= 0 then
				totem.pulse = totem.pulse + 0.5;
				local slow = Stat(def, "slow", totem.level);
				local auraEnds = BW.run.time + 1;
				local rooting = false;
				if Mastered(totem.key) then
					totem.root = totem.root - 0.5;
					if totem.root <= 0 then
						totem.root = totem.root + def.mastery.every;
						rooting = true;
					end
				end
				AroundTotem(totem, Stat(def, "radius", totem.level), nil, function(e)
					Slow(e, slow, 1);
					ShowAura(e, def.aura, auraEnds);
					if rooting and e.alive then
						Hold(e, def.mastery.root);
					end
				end);
			end
		end
	end
end

local function ClearTotems()
	for i = table.getn(totems), 1, -1 do
		totems[i].actor:Hide();
		tinsert(totemPool, totems[i]);
		totems[i] = nil;
	end
end

local totemKind = {
	Fire = QueueOne,
	Cast = function(entry, def)
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		PlantTotem(entry.key, def, entry.level);
	end,
}
handlers.turret = totemKind
handlers.zone = totemKind

------------------------------------------------------------------------------
-- Traps: each lands at your feet (Spawn 145), arms after TRAP_ARM (Closed
-- 147) and springs on the first whelp within TRAP_TRIGGER: its Custom0
-- (153) and that slot's sound, then what the trap does to everything in
-- its radius. A trap whose spell has an `area` (or Explosive Trap's
-- mastery) leaves its ground burning or icy for a while.

local traps = {}
local trapPool = {}
local grounds = {}
local groundPool = {}

local function DropTrap(i)
	local trap = traps[i];
	trap.actor:ClearGroundReticle();
	trap.actor:Hide();
	tinsert(trapPool, trap);
	table.remove(traps, i);
end

local function LayTrap(key, def, level, x, y, z)
	if table.getn(traps) >= TUNING.TRAP_MAX then
		DropTrap(1);
	end
	local trap = table.remove(trapPool) or { actor = BW.scene:AcquireActor() };
	if trap.path ~= def.path then
		trap.path = def.path;
		trap.actor:SetModelByPath(def.path);
		trap.actor:SetScale(TUNING.TRAP_SCALE);
	end
	trap.key, trap.level = key, level;
	trap.x, trap.y, trap.z = x, y, FloorAt(x, y, z);
	trap.state = "arming";
	trap.left = TUNING.TRAP_ARM;
	trap.life = TUNING.TRAP_LIFE;
	trap.springs = Mastered(key) and def.mastery.springs or 1;
	trap.actor:SetPosition(trap.x, trap.y, trap.z);
	trap.actor:SetYaw(math.random() * 2 * math.pi);
	trap.actor:SetAnimation(ANIM_SPAWN);
	trap.actor:SetGroundReticle(0); -- small while it arms
	trap.actor:Show();
	tinsert(traps, trap);
end

local function LayGround(spell, x, y, z, radius, seconds, def, level)
	local ground = table.remove(groundPool) or { actor = BW.scene:AcquireActor() };
	ground.def, ground.level = def, level;
	ground.x, ground.y, ground.radius = x, y, radius;
	ground.life, ground.tick = seconds, 0;
	ground.actor:SetModelBySpellArea(spell, radius);
	ground.actor:SetPosition(x, y, z);
	ground.actor:Show();
	tinsert(grounds, ground);
end

local function Spring(trap)
	local def = SPELLS[trap.key];
	trap.springs = trap.springs - 1;
	trap.state = trap.springs > 0 and "arming" or "sprung";
	trap.left = trap.springs > 0 and TUNING.TRAP_ARM or TUNING.TRAP_SPRUNG;
	trap.actor:SetAnimation(ANIM_CUSTOM0);
	trap.actor:PlaySound(def.sound);
	trap.actor:SetGroundReticle(0);
	local radius = Stat(def, "radius", trap.level);
	local r2 = radius * radius;
	local hit = {};
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, trap.x, trap.y) <= r2 then
				tinsert(hit, e);
			end
		end
	end
	-- The effect's impact on what it hits (Explosive Trap Effect's molten
	-- blast); the freeze shows its own aura.
	if def.effect ~= 3355 then
		ShowHits(trap.actor, def.effect, hit);
	end
	local damage = Stat(def, "damage", trap.level);
	for _, e in ipairs(hit) do
		if damage then
			SpellDamage(e, damage, def);
		end
		Afflict(e, trap.key, def, trap.level, trap.x, trap.y);
	end
	if def.area then
		LayGround(def.area, trap.x, trap.y, trap.z, radius, Stat(def, "duration", trap.level), def, trap.level);
	elseif Mastered(trap.key) and def.mastery.area then
		LayGround(def.effect, trap.x, trap.y, trap.z, radius, def.mastery.area, def, trap.level);
	end
end

-- A whelp (an enemy that moves) on the trap.
local function Stepped(trap)
	local t2 = TUNING.TRAP_TRIGGER * TUNING.TRAP_TRIGGER;
	for _, e in ipairs(enemies) do
		if e.alive and e.kind.speed > 0 then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, trap.x, trap.y) <= t2 then
				return true;
			end
		end
	end
	return false;
end

local function UpdateTraps(dt)
	for i = table.getn(traps), 1, -1 do
		local trap = traps[i];
		trap.life = trap.life - dt;
		if trap.state == "armed" then
			if Stepped(trap) then
				Spring(trap);
			elseif trap.life <= 0 then
				DropTrap(i);
			end
		else
			trap.left = trap.left - dt;
			if trap.left <= 0 then
				if trap.state == "arming" then
					trap.state = "armed";
					trap.actor:SetAnimation(ANIM_CLOSED);
					-- Armed: the ring shows what it will catch.
					trap.actor:SetGroundReticle(Stat(SPELLS[trap.key], "radius", trap.level));
				else
					DropTrap(i);
				end
			end
		end
	end
	-- The grounds: each second they burn (a burning ground) or chill (an
	-- icy one) everything on them.
	for i = table.getn(grounds), 1, -1 do
		local ground = grounds[i];
		ground.life = ground.life - dt;
		ground.tick = ground.tick - dt;
		if ground.tick <= 0 then
			ground.tick = ground.tick + 1;
			local def = ground.def;
			local ignite = Stat(def, "ignite", ground.level);
			local slow = Stat(def, "slow", ground.level);
			local r2 = ground.radius * ground.radius;
			for _, e in ipairs(enemies) do
				if e.alive then
					local x, y = e.actor:GetPosition();
					if Dist2(x, y, ground.x, ground.y) <= r2 then
						if ignite then
							Burn(e, ignite, def.igniteSeconds, def.school, def.color);
						end
						if slow then
							Slow(e, slow, 1.2);
						end
					end
				end
			end
		end
		if ground.life <= 0 then
			ground.actor:Hide();
			tinsert(groundPool, ground);
			table.remove(grounds, i);
		end
	end
end

local function ClearTraps()
	for i = table.getn(traps), 1, -1 do
		DropTrap(i);
	end
	for i = table.getn(grounds), 1, -1 do
		grounds[i].actor:Hide();
		tinsert(groundPool, grounds[i]);
		grounds[i] = nil;
	end
end

handlers.trap = {
	Fire = QueueOne,
	Cast = function(entry, def)
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		local hx, hy, hz = BW.hero:GetPosition();
		local n = Stat(def, "traps", entry.level);
		for k = 1, n do
			-- The first at your feet, the others in a ring around it.
			local r = k == 1 and 0 or 2.5;
			local a = (k - 2) * 2 * math.pi / math.max(n - 1, 1) + BW.run.heroYaw;
			LayTrap(entry.key, def, entry.level, hx + r * math.cos(a), hy + r * math.sin(a), hz);
		end
	end,
}

------------------------------------------------------------------------------
-- Charges: you rush at CHARGE_SPEED into the thickest pack in range, past
-- your control until you reach it or CHARGE_MAX runs out; where you land,
-- the brood around you is hurt and stunned (Intercept spins first).

local CHARGE_MIN = 5 -- yards: a pack closer than this is not worth the rush

handlers.charge = {
	Fire = function(key, state, def, level)
		if BW.run.charge then
			return false;
		end
		local x, y, _, target = PackCenter(Stat(def, "range", level), Stat(def, "radius", level));
		local hx, hy = BW.hero:GetPosition();
		if not target or Dist2(x, y, hx, hy) < CHARGE_MIN * CHARGE_MIN then
			return false;
		end
		QueueCast({ key = key, state = state, level = level, goal = target });
		state.pending = state.pending + 1;
		return true;
	end,
	Cast = function(entry, def)
		if not entry.goal.alive then
			return;
		end
		BW.hero:PlaySpellVisual(def.spell, entry.goal.actor, 0);
		BW.run.charge = { key = entry.key, level = entry.level, goal = entry.goal, left = TUNING.CHARGE_MAX };
		BW.hero:SetMovementSpeed("run", TUNING.CHARGE_SPEED);
		local hx, hy = BW.hero:GetPosition();
		local gx, gy = entry.goal.actor:GetPosition();
		BW.Drive(1, 0, math.atan2(gy - hy, gx - hx));
	end,
}

local function Land(charge)
	local def = SPELLS[charge.key];
	BW.run.charge = nil;
	BW.Drive(0, 0);
	BW.ApplyHeroSpeed();
	if def.whirl then
		BW.hero:PlaySpellVisual(def.whirl, BW.hero, 0);
	end
	local hx, hy = BW.hero:GetPosition();
	local radius = Stat(def, "radius", charge.level);
	local r2 = radius * radius;
	local damage = Stat(def, "damage", charge.level);
	local stun = Stat(def, "stun", charge.level);
	local knock = Mastered(charge.key) and def.mastery.knock;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, hx, hy) <= r2 then
				SpellDamage(e, damage, def);
				if e.alive then
					Hold(e, stun, def.stunAura);
				end
				if knock then
					Blast(e, hx, hy, knock);
				end
			end
		end
	end
end

local function UpdateCharge(dt)
	local charge = BW.run.charge;
	if not charge then
		return;
	end
	charge.left = charge.left - dt;
	local hx, hy = BW.hero:GetPosition();
	local goal = charge.goal;
	local gx, gy = goal.actor:GetPosition();
	if charge.left <= 0 or not goal.alive or Dist2(gx, gy, hx, hy) <= 2.5 * 2.5 then
		Land(charge);
		return;
	end
	-- Steer at the goal as it moves.
	BW.Drive(1, 0, math.atan2(gy - hy, gx - hx));
end

------------------------------------------------------------------------------

local function UpdateKinds(dt)
	if BW.run.pose then
		BW.run.pose = BW.run.pose - dt;
		if BW.run.pose <= 0 then
			BW.run.pose, BW.run.poseYaw = nil, nil; -- Steer turns you back to the way you hold
		end
	end
	UpdateTotems(dt);
	UpdateTraps(dt);
	UpdateCharge(dt);
end

local function ClearKinds()
	ClearTotems();
	ClearTraps();
	if BW.run and BW.run.charge then
		BW.run.charge = nil;
		BW.ApplyHeroSpeed();
	end
end

-- For the files that load after this one.
BW.UpdateKinds = UpdateKinds
BW.ClearKinds = ClearKinds
