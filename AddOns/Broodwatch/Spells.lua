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
local Blast = BW.Blast
local Hold = BW.Hold
local Fear = BW.Fear
local Weaken = BW.Weaken
local Polymorph = BW.Polymorph
local ApplySpeed = BW.ApplySpeed
local TUNING = BW.TUNING
local Mastered = BW.Mastered

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
			if e.alive and not e.marked and not (def.kind == "dot" and e.dotLeft)
				and not (def.kind == "polymorph" and e.sheepUntil) then
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
			if Mastered("totem") then
				-- The eruption: every enemy in reach, at the mastery's multiple.
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
							Damage(e, totem.damage * def.mastery.burst, def.color);
						end
					end
				end
			end
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
local function Slow(e, factor, seconds)
	e.slowUntil = math.max(e.slowUntil or 0, BW.run.time + (seconds or SLOW_SECONDS));
	if e.slowFactor ~= factor then
		e.slowFactor = factor;
		ApplySpeed(e);
	end
end

local function Unslow(e)
	e.slowUntil = nil;
	e.slowFactor = nil;
	ApplySpeed(e);
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
	local follow = Mastered("consecration");
	for i = table.getn(areas), 1, -1 do
		local area = areas[i];
		if follow and area.key == "consecration" then
			local hx, hy, hz = BW.hero:GetPosition();
			area.x, area.y = hx, hy;
			area.actor:SetPosition(hx, hy, hz);
		end
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
			if area.key == "blizzard" and Mastered("blizzard") then
				local def = SPELLS.blizzard;
				local r2 = area.radius * area.radius;
				for _, e in ipairs(enemies) do
					if e.alive then
						local x, y = e.actor:GetPosition();
						if Dist2(x, y, area.x, area.y) <= r2 then
							Hold(e, def.mastery.freeze, SPELLS.frostnova.spell, true);
						end
					end
				end
			end
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

------------------------------------------------------------------------------
-- Stoneclaw Totems: each pulls the brood around it onto itself (the Battle
-- Chicken's taunt, BW.Taunted) until it crumbles.

local wards = {}
local wardPool = {}
local WARD_PULSE = 0.5 -- seconds between two taunts

local function PlantWard(def, level)
	local hx, hy, hz = BW.hero:GetPosition();
	local ward = table.remove(wardPool);
	if not ward then
		ward = { actor = BW.scene:AcquireActor() };
		ward.actor:SetModelByCreatureDisplayID(def.display);
	end
	local a = math.random() * 2 * math.pi;
	local x, y = hx + 2.5 * math.cos(a), hy + 2.5 * math.sin(a);
	ward.x, ward.y = x, y;
	ward.life = Stat(def, "duration", level);
	ward.radius = Stat(def, "radius", level);
	ward.pulse = 0;
	ward.gone = nil;
	ward.actor:SetPosition(x, y, FloorAt(x, y, hz));
	ward.actor:Show();
	tinsert(wards, ward);
end

-- `crumbled`: its time ran out (the mastery's stun), not a run's end.
local function DropWard(i, crumbled)
	local ward = wards[i];
	if crumbled and Mastered("stoneclaw") then
		local r2 = ward.radius * ward.radius;
		for _, e in ipairs(enemies) do
			if e.alive then
				local x, y = e.actor:GetPosition();
				if Dist2(x, y, ward.x, ward.y) <= r2 then
					Hold(e, SPELLS.stoneclaw.mastery.stun, SPELLS.stoneclaw.mastery.aura);
				end
			end
		end
	end
	ward.gone = true;
	ward.actor:Hide();
	tinsert(wardPool, ward);
	table.remove(wards, i);
end

local function UpdateWards(dt)
	for i = table.getn(wards), 1, -1 do
		local ward = wards[i];
		ward.life = ward.life - dt;
		if ward.life <= 0 then
			DropWard(i, true);
		else
			ward.pulse = ward.pulse - dt;
			if ward.pulse <= 0 then
				ward.pulse = ward.pulse + WARD_PULSE;
				local r2 = ward.radius * ward.radius;
				for _, e in ipairs(enemies) do
					if e.alive then
						local x, y = e.actor:GetPosition();
						if Dist2(x, y, ward.x, ward.y) <= r2 then
							e.taunter = ward;
							e.tauntUntil = BW.run.time + WARD_PULSE * 2;
						end
					end
				end
			end
		end
	end
end

local function ClearWards()
	for i = table.getn(wards), 1, -1 do
		DropWard(i);
	end
end

------------------------------------------------------------------------------
-- The imp: summoned the first time Summon Imp casts, it stays for the run,
-- follows you and casts Firebolt (the spell's `missile`) at the nearest enemy.

local pet

local function SummonPet(def)
	if pet and pet.out then
		return pet;
	end
	if not pet then
		pet = { actor = BW.scene:AcquireActor() };
		pet.actor:SetModelByCreatureDisplayID(def.display);
		pet.actor:HookScript("OnSpellVisualImpact", function(_, _, _, castID) BW.OnImpact(nil, nil, nil, castID); end);
	end
	local hx, hy, hz = BW.hero:GetPosition();
	BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
	pet.actor:SetPosition(hx + 1.5, hy + 1.5, hz + 0.5);
	pet.actor:Show();
	pet.actor:SetMovementEnabled(true);
	pet.actor:SetMovementSpeed("run", TUNING.IMP_SPEED);
	pet.out = true;
	pet.move = 0;
	return pet;
end

local function UpdatePet(dt, hx, hy, hz)
	if not (pet and pet.out) then
		return;
	end
	pet.move = pet.move - dt;
	if pet.move > 0 then
		return;
	end
	pet.move = 0.4;
	local px, py = pet.actor:GetPosition();
	if Dist2(px, py, hx, hy) > TUNING.IMP_FOLLOW * TUNING.IMP_FOLLOW then
		-- To your side and behind you, as a pet follows.
		local a = BW.run.heroYaw + 2.4;
		pet.actor:MoveTo(hx + 2 * math.cos(a), hy + 2 * math.sin(a), hz);
	end
end

local function ClearPet()
	if pet and pet.out then
		pet.out = nil;
		pet.actor:SetMovementEnabled(false);
		pet.actor:Hide();
	end
end

local petHit = {}
local function NotPetHit(e)
	return petHit[e];
end

local function PetCast(key, def, level)
	local imp = SummonPet(def);
	local px, py = imp.actor:GetPosition();
	local bolts = Mastered(key) and def.mastery.bolts or 1;
	local cast = false;
	for _ = 1, bolts do
		local target = Nearest(px, py, def.range, NotPetHit);
		if not target then
			break;
		end
		petHit[target] = true;
		local castID = imp.actor:PlaySpellVisual(def.missile, target.actor, 0);
		if castID then
			BW.run.inFlight[castID] = { key = key, target = target, damage = Stat(def, "damage", level) };
		end
		cast = true;
	end
	for e in pairs(petHit) do
		petHit[e] = nil;
	end
	return cast;
end

------------------------------------------------------------------------------
-- Inferno: the meteor (PlaySpellVisualAtPoint) strikes the ground
-- INFERNO_FALL after the cast. Where it lands it burns and stuns the brood,
-- and an Infernal stands in the crater: rooted while it lands, then it hunts
-- the nearest whelp and swings at it, and its Immolation burns the whelps
-- around it every second, which then chase it instead of you.

local meteors = {}   -- falling, until they land
local infernals = {} -- fighting, or fading out
local infernalPool = {}

local function NoSkip()
	return false;
end

local function DropMeteor(level, x, y, z)
	z = FloorAt(x, y, z);
	BW.hero:PlaySpellVisualAtPoint(SPELLS.inferno.spell, x, y, z, 0);
	tinsert(meteors, { x = x, y = y, z = z, left = TUNING.INFERNO_FALL, level = level });
end

local function Land(meteor)
	local def = SPELLS.inferno;
	local radius = Stat(def, "radius", meteor.level);
	local damage = Stat(def, "damage", meteor.level);
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, meteor.x, meteor.y) <= radius * radius then
				Damage(e, damage, def.color);
				if e.alive then
					Hold(e, TUNING.INFERNO_STUN, def.stunAura);
				end
			end
		end
	end
	local infernal = table.remove(infernalPool);
	if not infernal then
		infernal = { actor = BW.scene:AcquireActor() };
		infernal.actor:SetModelByCreatureDisplayID(def.display);
	end
	infernal.gone = nil;
	infernal.prey = nil;
	infernal.life = Stat(def, "duration", meteor.level);
	infernal.hit = Stat(def, "hit", meteor.level);
	infernal.burnDamage = Stat(def, "burn", meteor.level);
	infernal.rooted = TUNING.INFERNAL_ROOT;
	infernal.swing, infernal.burn, infernal.move = 0, 1, 0;
	infernal.actor:SetPosition(meteor.x, meteor.y, meteor.z);
	infernal.actor:SetYaw(BW.run.heroYaw);
	infernal.actor:Show();
	infernal.actor:SetAnimation(BW.ANIM_STAND);
	infernal.actor:SetMovementEnabled(true);
	infernal.actor:SetMovementSpeed("run", TUNING.INFERNAL_SPEED);
	tinsert(infernals, infernal);
end

-- Immolation: burns the whelps around the Infernal, and they turn on it.
local function Immolate(infernal, x, y)
	local r2 = TUNING.INFERNAL_BURN_RADIUS * TUNING.INFERNAL_BURN_RADIUS;
	for _, e in ipairs(enemies) do
		if e.alive then
			local ex, ey = e.actor:GetPosition();
			if Dist2(ex, ey, x, y) <= r2 then
				Damage(e, infernal.burnDamage, SPELLS.inferno.color);
				e.taunter = infernal;
				e.tauntUntil = BW.run.time + TUNING.INFERNAL_TAUNT;
			end
		end
	end
end

local function Fight(infernal, dt)
	local x, y = infernal.actor:GetPosition();
	infernal.burn = infernal.burn - dt;
	if infernal.burn <= 0 then
		infernal.burn = infernal.burn + 1;
		Immolate(infernal, x, y);
	end
	local prey = infernal.prey;
	if not (prey and prey.alive) then
		prey = Nearest(x, y, BW.ROOM_RADIUS * 2, NoSkip);
		infernal.prey = prey;
	end
	if not prey then
		return;
	end
	local tx, ty, tz = prey.actor:GetPosition();
	infernal.swing = infernal.swing - dt;
	if Dist2(tx, ty, x, y) <= TUNING.INFERNAL_REACH * TUNING.INFERNAL_REACH then
		if infernal.swing <= 0 then
			infernal.swing = TUNING.INFERNAL_SWING;
			infernal.actor:StopMoving();
			infernal.actor:SetYaw(math.atan2(ty - y, tx - x));
			infernal.actor:SetAnimation(BW.ANIM_ATTACK_UNARMED);
			Damage(prey, infernal.hit, SPELLS.inferno.color);
		end
	else
		infernal.move = infernal.move - dt;
		if infernal.move <= 0 then
			infernal.move = 0.25;
			infernal.actor:MoveTo(tx, ty, tz);
		end
	end
end

local function UpdateInfernals(dt)
	for i = table.getn(meteors), 1, -1 do
		local meteor = meteors[i];
		meteor.left = meteor.left - dt;
		if meteor.left <= 0 then
			table.remove(meteors, i);
			Land(meteor);
		end
	end
	for i = table.getn(infernals), 1, -1 do
		local infernal = infernals[i];
		if infernal.gone then
			-- The fade hid it.
			if not infernal.actor:IsShown() then
				table.remove(infernals, i);
				tinsert(infernalPool, infernal);
			end
		else
			infernal.life = infernal.life - dt;
			if infernal.life <= 0 then
				-- Gone: the whelps it held come back for you (Taunted).
				infernal.gone = true;
				infernal.actor:SetMovementEnabled(false);
				infernal.actor:FadeOut();
			elseif infernal.rooted > 0 then
				infernal.rooted = infernal.rooted - dt;
			else
				Fight(infernal, dt);
			end
		end
	end
end

local function ClearInfernals()
	for i = table.getn(meteors), 1, -1 do
		meteors[i] = nil;
	end
	for i = table.getn(infernals), 1, -1 do
		local infernal = infernals[i];
		infernal.gone = true;
		infernal.actor:SetMovementEnabled(false);
		infernal.actor:Hide();
		tinsert(infernalPool, infernal);
		infernals[i] = nil;
	end
end

------------------------------------------------------------------------------
-- Lightning Shield: its orbs circle you as the aura's state kit
-- (AddAuraVisual). Every ZAP_GAP seconds an orb strikes a whelp that bites
-- you (the step calls Zap), and the shield goes with its last orb. A recast
-- fills the orbs again.

local function RaiseLightningShield(def, level)
	BW.run.orbs = Stat(def, "orbs", level);
	BW.run.orbDamage = Stat(def, "damage", level);
	BW.run.nextZap = BW.run.nextZap or 0;
	BW.hero:AddAuraVisual(def.spell);
end

local function Strike(e)
	local def = SPELLS.lshield;
	Damage(e, BW.run.orbDamage, def.color);
	e.actor:ApplySpellVisualKit(def.zapKit, true);
end

local function Zap(biter)
	local run = BW.run;
	if not run.orbs or run.time < run.nextZap then
		return;
	end
	run.nextZap = run.time + TUNING.ZAP_GAP;
	Strike(biter);
	if Mastered("lshield") then
		local x, y = biter.actor:GetPosition();
		local other = Nearest(x, y, SPELLS.lshield.mastery.jump, function(o) return o == biter; end);
		if other then
			Strike(other);
		end
	end
	run.orbs = run.orbs - 1;
	if run.orbs <= 0 then
		run.orbs = nil;
		BW.hero:RemoveAuraVisual(SPELLS.lshield.spell);
	end
end

------------------------------------------------------------------------------
-- Channels: the hero's beam on one enemy (PlaySpellVisual with a channel
-- time). Each second it burns the target: Drain Life heals you, Mind Flay
-- slows it. The beam ends (StopSpellVisual with its cast) when its target
-- dies or slips out of reach, or with its time.

local channels = {} -- spell key -> the channel under way
local CHANNEL_SLACK = 4 -- yards past the spell's range before the beam breaks

local function Channel(key, def, level, target)
	local castID = BW.hero:PlaySpellVisual(def.spell, target.actor, 0, Stat(def, "duration", level) * 1000);
	if not castID then
		return false;
	end
	-- A tick each second, the last at the channel's end, as a periodic
	-- aura's last tick comes at its expiry.
	channels[key] = { castID = castID, target = target, level = level, tick = 1, ticks = Stat(def, "duration", level) };
	return true;
end

local function EndChannel(key)
	local channel = channels[key];
	if channel then
		channels[key] = nil;
		BW.hero:StopSpellVisual(channel.castID);
	end
end

local channelHit = {}
local function NotChannelHit(e)
	return channelHit[e];
end

-- Arcane Missiles' tick: the server's triggered missile (def.missile) from
-- the hero at the target, its damage at its impact. The mastery splits it
-- among the enemies nearest the target.
local function ChannelMissiles(key, def, level, target, damage)
	local count = Mastered(key) and def.mastery.split or 1;
	local x, y = target.actor:GetPosition();
	local e = target;
	for _ = 1, count do
		channelHit[e] = true;
		local castID = BW.hero:PlaySpellVisual(def.missile, e.actor, 0);
		if castID then
			BW.run.inFlight[castID] = { key = key, target = e, damage = damage, level = level };
		end
		e = Nearest(x, y, def.range, NotChannelHit);
		if not e then
			break;
		end
	end
	for hit in pairs(channelHit) do
		channelHit[hit] = nil;
	end
end

-- A channel's tick on its target at (x, y): the damage (or Arcane
-- Missiles' missiles), Drain Life's heal, Mind Flay's slow and its mastery's
-- flay around the target.
local function ChannelTick(key, def, channel, target, x, y)
	local damage = Stat(def, "damage", channel.level);
	if def.missile then
		ChannelMissiles(key, def, channel.level, target, damage);
	else
		Damage(target, damage, def.color);
	end
	local heal = Stat(def, "heal", channel.level);
	if heal then
		BW.run.pendingHeal = BW.run.pendingHeal + heal;
	end
	local slow = Stat(def, "slow", channel.level);
	if slow and target.alive then
		Slow(target, slow);
	end
	if Mastered(key) and def.mastery.radius then
		local r2 = def.mastery.radius * def.mastery.radius;
		for _, e in ipairs(enemies) do
			if e.alive and e ~= target then
				local ex, ey = e.actor:GetPosition();
				if Dist2(ex, ey, x, y) <= r2 then
					Damage(e, damage, def.color);
				end
			end
		end
	end
end

-- Removing a key during `pairs` is allowed; adding one is not, so a leap
-- reuses its entry.
local function UpdateChannels(dt)
	local hx, hy = BW.hero:GetPosition();
	for key, channel in pairs(channels) do
		local def = SPELLS[key];
		local target = channel.target;
		local reach = def.range + CHANNEL_SLACK;
		local x, y = target.actor:GetPosition();
		channel.tick = channel.tick - dt;
		if not target.alive or Dist2(x, y, hx, hy) > reach * reach then
			-- Drain Life's mastery leaps the beam on for the ticks left.
			local nextTarget;
			if Mastered(key) and def.mastery.leap then
				channelHit[target] = true;
				nextTarget = Nearest(x, y, def.mastery.leap, NotChannelHit);
				channelHit[target] = nil;
			end
			BW.hero:StopSpellVisual(channel.castID);
			local leftMs = (channel.tick + channel.ticks - 1) * 1000;
			local castID = nextTarget and BW.hero:PlaySpellVisual(def.spell, nextTarget.actor, 0, leftMs);
			if castID then
				channel.castID, channel.target = castID, nextTarget;
			else
				channels[key] = nil;
			end
		elseif channel.tick <= 0 then
			channel.tick = channel.tick + 1;
			channel.ticks = channel.ticks - 1;
			if channel.ticks <= 0 then
				channels[key] = nil; -- the last tick; the beam's own time ends it
			end
			ChannelTick(key, def, channel, target, x, y);
		end
	end
end

local function ClearChannels()
	for key in pairs(channels) do
		EndChannel(key);
	end
end

------------------------------------------------------------------------------
-- Power Word: Shield: the bites soak into it first (the step), and its bubble
-- shows until it breaks or its time ends.

-- `broken`: the bites used it up (the mastery's burst), not its time.
local function EndShield(broken)
	if broken and Mastered("shield") then
		local m = SPELLS.shield.mastery;
		local hx, hy = BW.hero:GetPosition();
		local r2 = m.radius * m.radius;
		for _, e in ipairs(enemies) do
			if e.alive then
				local x, y = e.actor:GetPosition();
				if Dist2(x, y, hx, hy) <= r2 then
					Damage(e, m.damage, SPELLS.shield.color);
					Blast(e, hx, hy, m.knock);
				end
			end
		end
	end
	if BW.run.shield then
		BW.run.shield = nil;
		BW.run.shieldUntil = nil;
		BW.hero:RemoveAuraVisual(SPELLS.shield.spell);
		BW.UpdateHealth();
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
	if def.kind == "missile" or def.kind == "dot" or def.kind == "chain" or def.kind == "polymorph" then
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
	elseif def.kind == "pet" then
		return PetCast(key, def, level);
	elseif def.kind == "channel" then
		if channels[key] then
			return false;
		end
		local target = PickTargets(def, 1, hx, hy)[1];
		return target ~= nil and Channel(key, def, level, target);
	elseif def.kind == "totem" or def.kind == "ward" or def.kind == "shield" or def.kind == "thorns"
		or def.kind == "lshield" then
		QueueCast({ key = key, state = state, level = level });
		state.pending = state.pending + 1;
	elseif def.kind == "inferno" then
		local x, y, z = PackCenter(def.range, Stat(def, "radius", level));
		if not x then
			return false;
		end
		QueueCast({ key = key, state = state, level = level, x = x, y = y, z = z });
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
			key = key, state = state, level = level, damage = Stat(def, "damage", level),
			radius = Stat(def, "radius", level), heal = Stat(def, "heal", level),
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
	elseif def.kind == "ward" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		PlantWard(def, entry.level);
	elseif def.kind == "shield" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		BW.run.shield = BW.MaxHp() * Stat(def, "absorb", entry.level) / 100;
		BW.run.shieldUntil = BW.run.time + Stat(def, "duration", entry.level);
		BW.hero:AddAuraVisual(def.spell);
		BW.UpdateHealth();
	elseif def.kind == "thorns" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		BW.run.thornsUntil = BW.run.time + Stat(def, "duration", entry.level);
		BW.run.thornsDamage = Stat(def, "damage", entry.level);
		BW.run.thornsTick = 1;
	elseif def.kind == "lshield" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		RaiseLightningShield(def, entry.level);
	elseif def.kind == "inferno" then
		DropMeteor(entry.level, entry.x, entry.y, entry.z);
		-- The mastery's second meteor, at another enemy in range.
		if Mastered(entry.key) then
			local hx, hy = BW.hero:GetPosition();
			local other = PickTargets(def, 1, hx, hy)[1];
			if other then
				local x, y, z = other.actor:GetPosition();
				DropMeteor(entry.level, x, y, z);
			end
		end
	elseif def.kind == "area" then
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		if def.place == "pack" then
			QueueLanding(entry.key, entry.level, entry.x, entry.y, entry.z);
			if entry.key == "rainoffire" and Mastered("rainoffire") then
				local hx, hy = BW.hero:GetPosition();
				local other = PickTargets(def, 1, hx, hy)[1];
				if other then
					local x, y, z = other.actor:GetPosition();
					QueueLanding(entry.key, entry.level, x, y, z);
				end
			end
		else
			PlantArea(entry.key, entry.level, entry.x, entry.y, entry.z);
		end
	else
		BW.hero:PlaySpellVisual(def.spell, BW.hero, 0);
		local hx, hy = BW.hero:GetPosition();
		local r2 = entry.radius * entry.radius;
		local root = Stat(def, "root", entry.level);
		local fear = Stat(def, "fear", entry.level);
		local weaken = Stat(def, "weaken", entry.level);
		local mastery = Mastered(entry.key) and def.mastery;
		local hits = 0;
		for _, e in ipairs(enemies) do
			if e.alive then
				local x, y = e.actor:GetPosition();
				if Dist2(x, y, hx, hy) <= r2 then
					if entry.damage then
						Damage(e, entry.damage, def.color);
					end
					if e.alive then
						if root then
							Hold(e, root, def.spell);
						end
						if fear then
							Fear(e, fear, def.spell);
						end
						if weaken then
							Weaken(e, weaken, Stat(def, "duration", entry.level));
						end
					end
					if def.knock then
						Blast(e, hx, hy, def.knock);
					end
					if mastery then
						if mastery.slow and e.alive then
							Slow(e, mastery.slow, mastery.seconds);
						end
						if mastery.knock then
							Blast(e, hx, hy, mastery.knock);
						end
					end
					hits = hits + 1;
				end
			end
		end
		if entry.heal and hits > 0 then
			BW.run.pendingHeal = BW.run.pendingHeal + entry.heal * math.min(hits, HOLY_NOVA_HEAL_TARGETS);
		end
		-- Arcane Explosion's Echo bursts again (once); the shout's battle cry
		-- speeds you while it lasts.
		if mastery and entry.key == "nova" and not entry.echo then
			QueueCast({
				key = entry.key, state = entry.state, level = entry.level, damage = entry.damage,
				radius = entry.radius, echo = true,
			});
			entry.state.pending = entry.state.pending + 1;
		end
		if mastery and entry.key == "demoshout" then
			BW.run.shoutUntil = BW.run.time + Stat(def, "duration", entry.level);
			BW.ApplyHeroSpeed();
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
			key = entry.key, target = nextTarget, jumps = entry.jumps - 1, hit = entry.hit,
			damage = Mastered(entry.key) and entry.damage * SPELLS[entry.key].mastery.grow or entry.damage,
		};
	end
end

-- Polymorph's impact: the target, and at rank V every whelp near it, turn
-- into sheep.
local function Sheep(entry, def, target)
	local seconds = Stat(def, "duration", entry.level);
	if target.alive then
		Polymorph(target, seconds);
	end
	if Mastered(entry.key) then
		local tx, ty = target.actor:GetPosition();
		local r2 = def.mastery.radius * def.mastery.radius;
		for _, e in ipairs(enemies) do
			if e.alive and not e.sheepUntil then
				local x, y = e.actor:GetPosition();
				if Dist2(x, y, tx, ty) <= r2 then
					Polymorph(e, seconds);
				end
			end
		end
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
	local target = entry.target;
	if def.kind == "polymorph" then
		Sheep(entry, def, target);
		return;
	end
	local wasAlive = target.alive;
	Damage(target, entry.damage, def.color);
	-- Death Coil: you heal, and the target flees (at rank V, all around it).
	local heal = def.kind == "missile" and Stat(def, "heal", entry.level);
	if heal then
		BW.run.pendingHeal = BW.run.pendingHeal + heal;
	end
	local fear = Stat(def, "fear", entry.level);
	if fear and def.kind == "missile" then
		if target.alive then
			Fear(target, fear, def.spell);
		end
		if Mastered(entry.key) and def.mastery.radius then
			local tx, ty = target.actor:GetPosition();
			local r2 = def.mastery.radius * def.mastery.radius;
			for _, e in ipairs(enemies) do
				if e.alive then
					local x, y = e.actor:GetPosition();
					if Dist2(x, y, tx, ty) <= r2 then
						Fear(e, fear, def.spell);
					end
				end
			end
		end
	end
	if not Mastered(entry.key) or not def.mastery then
		return;
	end
	local m = def.mastery;
	if entry.key == "frostbolt" and target.alive then
		Hold(target, m.freeze, SPELLS.frostnova.spell, true);
	elseif entry.key == "shadowbolt" and wasAlive and not target.alive then
		local tx, ty = target.actor:GetPosition();
		local r2 = m.radius * m.radius;
		for _, e in ipairs(enemies) do
			if e.alive then
				local x, y = e.actor:GetPosition();
				if Dist2(x, y, tx, ty) <= r2 then
					Damage(e, entry.damage * m.share, def.color);
				end
			end
		end
	elseif entry.key == "lightning" and not entry.overload and math.random() < m.chance then
		local hx, hy = BW.hero:GetPosition();
		local other = PickTargets(def, 1, hx, hy)[1];
		local state = BW.run.spells[entry.key];
		if other and state then
			QueueCast({ key = entry.key, state = state, target = other, damage = entry.damage, level = entry.level, overload = true });
			state.pending = state.pending + 1;
		end
	end
end

-- For the files that load after this one.
BW.Zap = Zap
BW.UpdateInfernals = UpdateInfernals
BW.ClearInfernals = ClearInfernals
BW.UpdateChannels = UpdateChannels
BW.ClearChannels = ClearChannels
BW.UpdateWards = UpdateWards
BW.ClearWards = ClearWards
BW.UpdatePet = UpdatePet
BW.ClearPet = ClearPet
BW.EndShield = EndShield
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
