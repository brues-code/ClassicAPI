-- Broodwatch spells: targets, totems, ground areas, casts and leaps.

local _, BW = ...
local CAST_SPACING = BW.CAST_SPACING
local SPELLS = BW.SPELLS
local PASSIVES = BW.PASSIVES
local HOLY_NOVA_HEAL_TARGETS = BW.HOLY_NOVA_HEAL_TARGETS
local enemies = BW.enemies
local FloorAt = BW.FloorAt
local Dist2 = BW.Dist2
local Flash = BW.Flash
local Damage = BW.Damage

------------------------------------------------------------------------------
-- The hero's spells

local function Stat(def, field, level)
	local v = def[field];
	v = v and v[level];
	if v and BW.run then
		if field == "radius" and BW.run.passives.amplify then
			v = v * (1 + PASSIVES.amplify.per * BW.run.passives.amplify / 100);
		elseif field == "count" and BW.run.passives.multishot then
			v = v + BW.run.passives.multishot;
		end
	end
	return v;
end

-- Missile targets: up to `count` live enemies in range, by the spell's rule.
local picked = {}
local function PickTargets(def, count, hx, hy)
	for i = table.getn(picked), 1, -1 do
		picked[i] = nil;
	end
	local range2 = def.range * def.range;
	for _ = 1, count do
		local best, bestScore;
		for _, e in ipairs(enemies) do
			if e.alive and not e.marked and not (def.kind == "dot" and e.dotLeft) then
				local x, y = e.actor:GetPosition();
				local d2 = Dist2(x, y, hx, hy);
				if d2 <= range2 then
					local score;
					if def.pick == "nearest" then
						score = -d2;
					elseif def.pick == "strongest" then
						score = e.hp;
					else
						score = math.random();
					end
					if not bestScore or score > bestScore then
						best, bestScore = e, score;
					end
				end
			end
		end
		if not best then
			break;
		end
		best.marked = true;
		tinsert(picked, best);
	end
	for _, e in ipairs(picked) do
		e.marked = nil;
	end
	return picked;
end

-- The live enemy nearest to (x, y) within `range` that `skip` allows.
local function Nearest(x, y, range, skip)
	local best, bestD2 = nil, range * range;
	for _, e in ipairs(enemies) do
		if e.alive and not skip(e) then
			local ex, ey = e.actor:GetPosition();
			local d2 = Dist2(ex, ey, x, y);
			if d2 <= bestD2 then
				best, bestD2 = e, d2;
			end
		end
	end
	return best;
end

------------------------------------------------------------------------------
-- Magma Totems: each stands where it was planted and pulses until its time
-- runs out. A pulse burns every enemy in its radius and shows the impact on
-- up to MAGMA_IMPACTS of them.

local totems = {}
local totemPool = {}
local MAGMA_IMPACTS = 6

local function PlantTotems(def, level)
	local hx, hy, hz = BW.hero:GetPosition();
	local n = Stat(def, "totems", level);
	for k = 1, n do
		local totem = table.remove(totemPool);
		if not totem then
			totem = { actor = BW.scene:AcquireActor() };
			totem.actor:SetModelByCreatureDisplayID(def.display);
			totem.actor:HookScript("OnSpellVisualImpact", function(_, _, _, castID) BW.OnImpact(nil, nil, nil, castID); end);
		end
		local a = (k - 1) * 2 * math.pi / n + math.random() * 0.5;
		local x, y = hx + 2 * math.cos(a), hy + 2 * math.sin(a);
		totem.x, totem.y = x, y;
		totem.life = Stat(def, "duration", level);
		totem.damage = Stat(def, "damage", level);
		totem.radius = Stat(def, "radius", level);
		totem.pulseGap = 0.5;
		totem.actor:SetPosition(x, y, FloorAt(x, y, hz));
		totem.actor:Show();
		tinsert(totems, totem);
	end
end

local function UpdateTotems(dt)
	local def = SPELLS.totem;
	for i = table.getn(totems), 1, -1 do
		local totem = totems[i];
		totem.life = totem.life - dt;
		if totem.life <= 0 then
			totem.actor:Hide();
			tinsert(totemPool, totem);
			table.remove(totems, i);
		else
			totem.pulseGap = totem.pulseGap - dt;
			if totem.pulseGap <= 0 then
				totem.pulseGap = totem.pulseGap + def.pulseEvery;
				local r2 = totem.radius * totem.radius;
				local shown = 0;
				for _, e in ipairs(enemies) do
					if e.alive then
						local x, y = e.actor:GetPosition();
						if Dist2(x, y, totem.x, totem.y) <= r2 then
							if shown < MAGMA_IMPACTS then
								shown = shown + 1;
								totem.actor:PlaySpellVisual(def.pulse, e.actor, 0);
							end
							Damage(e, totem.damage, def.color);
						end
					end
				end
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

------------------------------------------------------------------------------
-- Ground areas: each stands where it landed for its duration and burns every
-- enemy inside it once a second.

local areas = {}
local areaPool = {}

-- Slows an enemy to `factor` of its run speed until a little after the next
-- tick; the step lets it go when that time passes.
local SLOW_SECONDS = 1.2
local function Slow(e, factor)
	e.slowUntil = BW.run.time + SLOW_SECONDS;
	if e.slowFactor ~= factor then
		e.slowFactor = factor;
		e.actor:SetMovementSpeed("run", e.kind.speed * factor);
	end
end

local function Unslow(e)
	e.slowUntil = nil;
	e.slowFactor = nil;
	e.actor:SetMovementSpeed("run", e.kind.speed);
end

-- The point of the thickest pack in range: of up to 24 enemies in range,
-- the one with the most others within `radius` of it.
local function PackCenter(range, radius)
	local hx, hy = BW.hero:GetPosition();
	local range2, r2 = range * range, radius * radius;
	local best, bestCount = nil, -1;
	local tried = 0;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, hx, hy) <= range2 then
				local count = 0;
				for _, o in ipairs(enemies) do
					if o.alive then
						local ox, oy = o.actor:GetPosition();
						if Dist2(ox, oy, x, y) <= r2 then
							count = count + 1;
						end
					end
				end
				if count > bestCount then
					best, bestCount = e, count;
				end
				tried = tried + 1;
				if tried >= 24 then
					break;
				end
			end
		end
	end
	if not best then
		return nil;
	end
	local x, y, z = best.actor:GetPosition();
	return x, y, z, best;
end

local function PlantArea(key, level, x, y, z)
	local def = SPELLS[key];
	local area = table.remove(areaPool);
	if not area then
		area = { actor = BW.scene:AcquireActor() };
	end
	area.key = key;
	area.x, area.y = x, y;
	area.radius = Stat(def, "radius", level);
	area.damage = Stat(def, "damage", level);
	area.life = Stat(def, "duration", level);
	area.slow = Stat(def, "slow", level);
	area.tick = 1;
	area.actor:SetModelBySpellArea(def.spell, area.radius);
	area.actor:SetPosition(x, y, FloorAt(x, y, z));
	area.actor:Show();
	tinsert(areas, area);
end

local function UpdateAreas(dt)
	for i = table.getn(areas), 1, -1 do
		local area = areas[i];
		area.life = area.life - dt;
		area.tick = area.tick - dt;
		if area.tick <= 0 then
			area.tick = area.tick + 1;
			local r2 = area.radius * area.radius;
			local color = SPELLS[area.key].color;
			for _, e in ipairs(enemies) do
				if e.alive then
					local x, y = e.actor:GetPosition();
					if Dist2(x, y, area.x, area.y) <= r2 then
						if area.slow then
							Slow(e, area.slow);
						end
						Damage(e, area.damage, color);
					end
				end
			end
		end
		if area.life <= 0 then
			area.actor:Hide();
			tinsert(areaPool, area);
			table.remove(areas, i);
		end
	end
end

-- A storm aimed at a pack marks the ground with the reticle first
-- (actor:SetGroundReticle on an actor with no model), then lands.
local LANDING_SECONDS = 0.6
local markerPool = {}

local function QueueLanding(key, level, x, y, z)
	local marker = table.remove(markerPool);
	if not marker then
		marker = { actor = BW.scene:AcquireActor() };
	end
	local floorZ = FloorAt(x, y, z);
	marker.actor:SetPosition(x, y, floorZ);
	marker.actor:SetGroundReticle(Stat(SPELLS[key], "radius", level));
	marker.actor:Show();
	tinsert(BW.run.landings, { marker = marker, key = key, level = level, x = x, y = y, z = floorZ, left = LANDING_SECONDS });
end

local function ReleaseMarker(marker)
	marker.actor:ClearGroundReticle();
	marker.actor:Hide();
	tinsert(markerPool, marker);
end

local function UpdateLandings(dt)
	for i = table.getn(BW.run.landings), 1, -1 do
		local landing = BW.run.landings[i];
		landing.left = landing.left - dt;
		if landing.left <= 0 then
			ReleaseMarker(landing.marker);
			PlantArea(landing.key, landing.level, landing.x, landing.y, landing.z);
			table.remove(BW.run.landings, i);
		end
	end
end

local function ClearLandings()
	for i = table.getn(BW.run.landings), 1, -1 do
		ReleaseMarker(BW.run.landings[i].marker);
		BW.run.landings[i] = nil;
	end
end

local function ClearAreas()
	for i = table.getn(areas), 1, -1 do
		areas[i].actor:Hide();
		tinsert(areaPool, areas[i]);
		areas[i] = nil;
	end
end

-- The hero casts one visual at a time; casts wait in a queue.
local function QueueCast(entry)
	tinsert(BW.run.castQueue, entry);
end

local function Fire(key, state)
	local def = SPELLS[key];
	local level = state.level;
	local hx, hy = BW.hero:GetPosition();
	if def.kind == "missile" or def.kind == "dot" or def.kind == "chain" then
		local targets = PickTargets(def, Stat(def, "count", level) or 1, hx, hy);
		if table.getn(targets) == 0 then
			return false;
		end
		for _, e in ipairs(targets) do
			QueueCast({
				key = key, state = state, target = e, damage = Stat(def, "damage", level),
				jumps = Stat(def, "jumps", level), level = level,
			});
			state.pending = state.pending + 1;
		end
	elseif def.kind == "totem" then
		QueueCast({ key = key, state = state, level = level });
		state.pending = state.pending + 1;
	elseif def.kind == "area" then
		local x, y, z;
		if def.place == "pack" then
			x, y, z = PackCenter(def.range, Stat(def, "radius", level));
			if not x then
				return false;
			end
		else
			x, y, z = BW.hero:GetPosition();
		end
		QueueCast({ key = key, state = state, level = level, x = x, y = y, z = z });
		state.pending = state.pending + 1;
	else
		QueueCast({
			key = key, state = state, damage = Stat(def, "damage", level), radius = Stat(def, "radius", level),
			heal = Stat(def, "heal", level),
		});
		state.pending = state.pending + 1;
	end
	return true;
end

local function CastNext()
	local entry = table.remove(BW.run.castQueue, 1);
	if not entry then
		return;
	end
	entry.state.pending = entry.state.pending - 1;
	local def = SPELLS[entry.key];
	if entry.target then
		if not entry.target.alive then
			return CastNext();
		end
		local castID = BW.hero:PlaySpellVisual(def.spell, entry.target.actor, 0);
		if castID then
			BW.run.inFlight[castID] = entry;
		end
	elseif def.kind == "totem" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		PlantTotems(def, entry.level);
	elseif def.kind == "area" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		if def.place == "pack" then
			QueueLanding(entry.key, entry.level, entry.x, entry.y, entry.z);
		else
			PlantArea(entry.key, entry.level, entry.x, entry.y, entry.z);
		end
	else
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		local hx, hy = BW.hero:GetPosition();
		local r2 = entry.radius * entry.radius;
		local hits = 0;
		for _, e in ipairs(enemies) do
			if e.alive then
				local x, y = e.actor:GetPosition();
				if Dist2(x, y, hx, hy) <= r2 then
					Damage(e, entry.damage, def.color);
					hits = hits + 1;
				end
			end
		end
		if entry.heal and hits > 0 then
			BW.run.pendingHeal = BW.run.pendingHeal + entry.heal * math.min(hits, HOLY_NOVA_HEAL_TARGETS);
		end
	end
	BW.run.castGap = CAST_SPACING;
end

-- Puts Corruption on an enemy: `damage` each second for `seconds`, and it
-- spreads to `spread` neighbours when the enemy dies.
local function Corrupt(e, damage, seconds, spread)
	e.dotLeft = seconds;
	e.dotTick = 1;
	e.dotDamage = damage;
	e.dotSeconds = seconds;
	e.dotSpread = spread;
	Flash(e);
end

-- Chain Lightning leaps on from the enemy it hit: that enemy casts the next
-- leap at its nearest neighbour not yet hit.
local chainHit
local function NotChainHit(e)
	return chainHit[e];
end

local function Leap(entry)
	if not entry.jumps or entry.jumps <= 0 then
		return;
	end
	entry.hit = entry.hit or {};
	entry.hit[entry.target] = true;
	local from = entry.target;
	local fx, fy = from.actor:GetPosition();
	chainHit = entry.hit;
	local nextTarget = Nearest(fx, fy, SPELLS[entry.key].hop, NotChainHit);
	chainHit = nil;
	if not nextTarget then
		return;
	end
	local castID = from.actor:PlaySpellVisual(SPELLS[entry.key].spell, nextTarget.actor, 0);
	if castID then
		BW.run.inFlight[castID] = {
			key = entry.key, target = nextTarget, damage = entry.damage, jumps = entry.jumps - 1, hit = entry.hit,
		};
	end
end

BW.OnImpact = function(_, _, _, castID)
	if not BW.run then
		return;
	end
	local entry = BW.run.inFlight[castID];
	if not entry then
		return;
	end
	BW.run.inFlight[castID] = nil;
	if entry.onImpact then
		entry.onImpact(entry);
		return;
	end
	local def = SPELLS[entry.key];
	if def.kind == "dot" then
		if entry.target.alive then
			Corrupt(entry.target, entry.damage, Stat(def, "duration", entry.level), Stat(def, "spread", entry.level));
		end
		return;
	end
	if def.kind == "chain" then
		Leap(entry);
	end
	Damage(entry.target, entry.damage, def.color);
end

-- For the files that load after this one.
BW.Stat = Stat
BW.Nearest = Nearest
BW.UpdateTotems = UpdateTotems
BW.ClearTotems = ClearTotems
BW.Unslow = Unslow
BW.PackCenter = PackCenter
BW.UpdateAreas = UpdateAreas
BW.markerPool = markerPool
BW.ReleaseMarker = ReleaseMarker
BW.UpdateLandings = UpdateLandings
BW.ClearLandings = ClearLandings
BW.ClearAreas = ClearAreas
BW.Fire = Fire
BW.CastNext = CastNext
BW.Corrupt = Corrupt
