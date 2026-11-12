-- Broodwatch gear: the weapon you hold and the gear you wear, kept for the
-- run (Data.lua's GEAR). Chests offer it (Hud.lua).

local _, BW = ...
local GEAR = BW.GEAR
local enemies = BW.enemies
local Dist2 = BW.Dist2
local RunGrowth = BW.RunGrowth
local SpellDamage = BW.SpellDamage
local Burn = BW.Burn
local Blast = BW.Blast
local Slow = BW.Slow
local Nearest = BW.Nearest
local SchoolColor = BW.SchoolColor

local HAND_SLOTS = { 16, 17, 18 } -- main hand, off hand, ranged

-- What a weapon's own hits read in SpellDamage: their school (the combos
-- and the colour) and that a gear proc does not proc again.
local function ProcDef(gear)
	return { school = gear.school, hitColor = SchoolColor(gear.school), gear = true };
end
local THUNDERFURY = ProcDef(GEAR.thunderfury)
local SULFURAS = ProcDef(GEAR.sulfuras)
local RHOKDELAR = ProcDef(GEAR.rhokdelar)

------------------------------------------------------------------------------
-- Taking it

local function Equip(key)
	local gear = GEAR[key];
	if gear.slot == "weapon" then
		BW.run.weapon = key;
		BW.run.procReady = 0;
		BW.run.shotLeft = 0;
		BW.hero:TryOn(gear.itemID);
		if key == "benediction" then
			BW.run.blessed = true;
			BW.run.turnAt = BW.run.time + gear.every;
		end
	else
		BW.run.gear[key] = true;
		if gear.every then
			BW.run.gearReady[key] = BW.run.time; -- ready at once
		end
	end
end

-- A new run: you hold your own weapons again.
local function ResetGear()
	for _, slot in ipairs(HAND_SLOTS) do
		BW.hero:DressPlayerSlot(slot);
	end
end

-- The gear a chest may offer: a weapon while you hold none (`weapon`), else
-- worn gear you lack while you wear fewer than MAX_WORN.
local function GearOffers(weapon)
	local list = {};
	local worn = 0;
	for _ in pairs(BW.run.gear) do
		worn = worn + 1;
	end
	for _, key in ipairs(BW.GEAR_ORDER) do
		local gear = GEAR[key];
		if weapon then
			if gear.slot == "weapon" and not BW.run.weapon then
				tinsert(list, key);
			end
		elseif gear.slot == "worn" and not BW.run.gear[key] and worn < BW.MAX_WORN then
			tinsert(list, key);
		end
	end
	return list;
end

------------------------------------------------------------------------------
-- What it does to your spells

-- The share of a spell's damage it deals: Benediction's or Anathema's
-- school, and the on-use trinkets.
local function GearScale(def)
	local run = BW.run;
	local scale = 1;
	if run.weapon == "benediction" then
		if (run.blessed and def.school == "Holy") or (not run.blessed and def.school == "Shadow") then
			scale = scale * (1 + GEAR.benediction.bonus);
		end
	end
	if run.tepUntil then
		scale = scale * (1 + GEAR.tep.bonus);
	end
	if run.zhcUntil then
		scale = scale * (1 + run.zhcBonus);
	end
	return scale;
end

-- A spell has just been cast, to recharge in `cooldown` seconds: Hand of
-- Justice may make it ready at once; the Hero Charm's power fades a step.
local function AfterCast(cooldown)
	local run = BW.run;
	if run.zhcUntil then
		run.zhcBonus = math.max(0, run.zhcBonus - GEAR.zhc.fade);
	end
	if run.gear.hoj and math.random() < GEAR.hoj.chance then
		return 0;
	end
	return cooldown;
end

------------------------------------------------------------------------------
-- The weapons' procs: a spell hit (SpellDamage) may set one off.

-- Thunderfury's bolt: it hurts and slows its target, then leaps on to the
-- nearest of the brood it has not hit, as Chain Lightning does.
local leapHit
local function WasHit(e)
	return leapHit[e];
end

local function Thunder(entry)
	local gear = GEAR.thunderfury;
	local target = entry.target;
	entry.hit[target] = true;
	if target.alive then
		SpellDamage(target, entry.damage, THUNDERFURY);
		if target.alive then
			Slow(target, gear.slow, gear.slowSeconds);
		end
	end
	if entry.jumps <= 0 then
		return;
	end
	local x, y = target.actor:GetPosition();
	leapHit = entry.hit;
	local nextTarget = Nearest(x, y, gear.hop, WasHit);
	leapHit = nil;
	if nextTarget then
		local castID = target.actor:PlaySpellVisual(gear.spell, nextTarget.actor, 0);
		if castID then
			BW.run.inFlight[castID] = { target = nextTarget, damage = entry.damage, jumps = entry.jumps - 1,
				hit = entry.hit, onImpact = Thunder };
		end
	end
end

-- Sulfuras's fireball bursts where it lands: it hurts, burns and throws the
-- brood around.
local function Inferno(entry)
	local gear = GEAR.sulfuras;
	local x, y = entry.target.actor:GetPosition();
	local r2 = gear.radius * gear.radius;
	local growth = RunGrowth();
	for _, e in ipairs(enemies) do
		if e.alive then
			local ex, ey = e.actor:GetPosition();
			if Dist2(ex, ey, x, y) <= r2 then
				SpellDamage(e, gear.damage * growth, SULFURAS);
				if e.alive then
					Burn(e, gear.burn * growth, gear.burnSeconds, gear.school);
				end
				Blast(e, x, y, gear.knock);
			end
		end
	end
end

local PROCS = {
	thunderfury = function(e, gear)
		local castID = BW.hero:PlaySpellVisual(gear.spell, e.actor, 0);
		local entry = { target = e, damage = gear.damage * RunGrowth(), jumps = gear.jumps, hit = {}, onImpact = Thunder };
		if castID then
			BW.run.inFlight[castID] = entry;
		else
			Thunder(entry);
		end
	end,
	sulfuras = function(e, gear)
		local castID = BW.hero:PlaySpellVisual(gear.spell, e.actor, 0);
		local entry = { target = e, onImpact = Inferno };
		if castID then
			BW.run.inFlight[castID] = entry;
		else
			Inferno(entry);
		end
	end,
}

local function OnSpellHit(e, def)
	local run = BW.run;
	local proc = run.weapon and PROCS[run.weapon];
	if not proc or def.gear or run.time < run.procReady or not e.alive then
		return;
	end
	local gear = GEAR[run.weapon];
	if math.random() < gear.chance then
		run.procReady = run.time + gear.every;
		proc(e, gear);
	end
end

------------------------------------------------------------------------------
-- The step

local function Shoot(entry)
	if entry.target.alive then
		SpellDamage(entry.target, entry.damage, RHOKDELAR);
	end
end

local function NotAlive(e)
	return not e.alive;
end

local function UpdateWeapon(dt, hx, hy)
	local run = BW.run;
	if run.weapon == "benediction" then
		local gear = GEAR.benediction;
		if run.time >= run.turnAt then
			run.turnAt = run.time + gear.every;
			run.blessed = not run.blessed;
			BW.hero:PlaySpellVisual(run.blessed and gear.toBenediction or gear.toAnathema, BW.hero, 0);
			BW.hero:TryOn(run.blessed and gear.itemID or gear.other);
			BW.UpdateBuild();
		end
		if run.blessed then
			run.pendingHeal = run.pendingHeal + gear.renew * dt;
		end
	elseif run.weapon == "rhokdelar" then
		local gear = GEAR.rhokdelar;
		run.shotLeft = run.shotLeft - dt;
		if run.shotLeft <= 0 then
			local target = Nearest(hx, hy, gear.range, NotAlive);
			if target then
				run.shotLeft = gear.every;
				local entry = { target = target, damage = gear.damage * RunGrowth(), onImpact = Shoot };
				local castID = BW.hero:PlaySpellVisual(gear.spell, target.actor, 0);
				if castID then
					run.inFlight[castID] = entry;
				else
					Shoot(entry);
				end
			end
		end
	end
end

-- Whether any of the brood is within `range` of the hero.
local function BroodNear(hx, hy, range)
	local r2 = range * range;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, hx, hy) <= r2 then
				return true;
			end
		end
	end
	return false;
end

-- The on-use trinkets go off when they are ready and the brood is near.
local function UpdateTrinkets(hx, hy)
	local run = BW.run;
	local t = run.time;
	if run.tepUntil and t >= run.tepUntil then
		run.tepUntil = nil;
	end
	if run.zhcUntil and t >= run.zhcUntil then
		run.zhcUntil, run.zhcBonus = nil, nil;
	end
	for key, ready in pairs(run.gearReady) do
		local gear = GEAR[key];
		if t >= ready and BroodNear(hx, hy, gear.near) then
			run.gearReady[key] = t + gear.every;
			run[key .. "Until"] = t + gear.seconds;
			if key == "zhc" then
				run.zhcBonus = gear.bonus;
			end
			BW.hero:PlaySpellVisual(gear.spell, BW.hero, 0);
			local slot = run.gearSlots[key];
			if slot then
				slot.cd:SetCooldown(GetTime(), gear.every);
			end
		end
	end
end

-- The potions' times.
local function UpdatePotions()
	local run = BW.run;
	local changed = false;
	if run.invulnUntil and run.time >= run.invulnUntil then
		run.invulnUntil = nil;
		changed = true;
	end
	if run.fireWard and run.time >= run.fireWardUntil then
		run.fireWard = nil;
		changed = true;
	end
	if changed then
		BW.UpdateHealth();
	end
end

local function UpdateGear(dt, hx, hy)
	UpdateWeapon(dt, hx, hy);
	UpdateTrinkets(hx, hy);
	UpdatePotions();
end

-- For the files that load after this one.
BW.Equip = Equip
BW.ResetGear = ResetGear
BW.GearOffers = GearOffers
BW.GearScale = GearScale
BW.AfterCast = AfterCast
BW.OnSpellHit = OnSpellHit
BW.UpdateGear = UpdateGear
