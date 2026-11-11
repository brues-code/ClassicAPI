-- Broodwatch items: drops and what each one does.

local _, BW = ...
local Blast = BW.Blast
local ELITE = BW.ELITE
local enemies = BW.enemies
local gems = BW.gems
local FloorAt = BW.FloorAt
local Dist2 = BW.Dist2
local Kill = BW.Kill
local ShowNumber = BW.ShowNumber
local Damage = BW.Damage
local PackCenter = BW.PackCenter
local markerPool = BW.markerPool
local ReleaseMarker = BW.ReleaseMarker
local Icon = BW.Icon
local TUNING = BW.TUNING
local ApplyHeroSpeed = BW.ApplyHeroSpeed
local MaxHp = BW.MaxHp
local UpdateHealth = BW.UpdateHealth
local ITEMS = BW.ITEMS

------------------------------------------------------------------------------
-- Items: a whelp drops one now and then, an elite always. A drop lies on the
-- floor (a reticle under it, its icon bobbing over it) and is used when you
-- walk over it. The icons are the items' use spells' (C_Spell.GetSpellTexture).

local ANIM_ATTACK_UNARMED = 16

local drops = {}
local dropPool = {}
local chickens = {}
local chickenPool = {}

local function RunGrowth()
	local m = BW.run.time / 300;
	return 1 + 2 * m + m * m;
end

local function PickItem()
	local total = 0;
	for _, item in ipairs(ITEMS) do
		total = total + item.weight;
	end
	local roll = math.random() * total;
	for _, item in ipairs(ITEMS) do
		if roll < item.weight then
			return item;
		end
		roll = roll - item.weight;
	end
	return ITEMS[1];
end

BW.DropItem = function(e, x, y, z)
	if e.kind ~= ELITE and math.random() >= TUNING.ITEM_CHANCE then
		return;
	end
	local drop = table.remove(dropPool);
	if not drop then
		drop = { actor = BW.scene:AcquireActor(), icon = BW.feedback:CreateTexture(nil, "ARTWORK") };
		drop.icon:SetWidth(TUNING.ITEM_ICON);
		drop.icon:SetHeight(TUNING.ITEM_ICON);
	end
	drop.item = PickItem();
	drop.x, drop.y, drop.z = x, y, z;
	drop.life = TUNING.ITEM_SECONDS;
	drop.actor:SetPosition(x, y, z);
	drop.actor:SetGroundReticle(0.9);
	drop.actor:Show();
	drop.icon:SetTexture(drop.item.icon or Icon(drop.item.spell));
	drop.icon:Show();
	tinsert(drops, drop);
end

local function ReleaseDrop(i)
	local drop = drops[i];
	drop.actor:ClearGroundReticle();
	drop.actor:Hide();
	drop.icon:Hide();
	tinsert(dropPool, drop);
	table.remove(drops, i);
end

local function Stun(e, seconds)
	e.stunUntil = BW.run.time + seconds;
	local x, y, z = e.actor:GetPosition();
	e.actor:MoveTo(x, y, z);
end

-- A grenade lands on the thickest pack: the reticle marks it while it flies.
local function ThrowGrenade(item)
	local x, y, z, target = PackCenter(24, TUNING.GRENADE_RADIUS);
	if not target then
		return;
	end
	local marker = table.remove(markerPool) or { actor = BW.scene:AcquireActor() };
	marker.actor:SetPosition(x, y, FloorAt(x, y, z));
	marker.actor:SetGroundReticle(TUNING.GRENADE_RADIUS);
	marker.actor:Show();
	local damage = TUNING.GRENADE_DAMAGE * RunGrowth();
	local function Explode(entry)
		ReleaseMarker(marker);
		local ex, ey = entry.target.actor:GetPosition();
		local r2 = TUNING.GRENADE_RADIUS * TUNING.GRENADE_RADIUS;
		for _, o in ipairs(enemies) do
			if o.alive then
				local ox, oy = o.actor:GetPosition();
				if Dist2(ox, oy, ex, ey) <= r2 then
					Stun(o, TUNING.GRENADE_STUN);
					Damage(o, damage, TUNING.ITEM_COLOR);
					Blast(o, ex, ey, TUNING.GRENADE_KNOCK);
				end
			end
		end
	end
	local castID = BW.hero:PlaySpellVisual(item.spell, target.actor, 0);
	local entry = { target = target, onImpact = Explode };
	if castID then
		BW.run.inFlight[castID] = entry;
	else
		Explode(entry);
	end
end

-- The Sapper Charge goes off on you: it clears the brood close by, and hurts.
local function Sapper(item, hx, hy)
	BW.hero:PlaySpellVisual(item.spell, BW.hero, 0);
	local r2 = TUNING.SAPPER_RADIUS * TUNING.SAPPER_RADIUS;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			if Dist2(x, y, hx, hy) <= r2 then
				if e.kind == ELITE then
					Damage(e, e.maxHp * TUNING.SAPPER_ELITE / BW.run.damageScale, TUNING.ITEM_COLOR);
				else
					Kill(e);
				end
			end
		end
	end
	BW.run.hp = math.max(1, BW.run.hp - MaxHp() * TUNING.SAPPER_SELF);
	UpdateHealth();
end

local function SummonChicken(item, hx, hy, hz)
	BW.hero:PlaySpellVisual(item.spell, BW.hero, 0);
	local chicken = table.remove(chickenPool);
	if not chicken then
		chicken = { actor = BW.scene:AcquireActor() };
		chicken.actor:SetModelByCreatureDisplayID(TUNING.CHICKEN_DISPLAY);
	end
	chicken.life = TUNING.CHICKEN_SECONDS;
	chicken.peck = 0;
	chicken.move = 0;
	chicken.prey = nil;
	chicken.gone = nil;
	chicken.actor:SetPosition(hx + 1.5, hy, hz + 0.5);
	chicken.actor:Show();
	chicken.actor:SetMovementEnabled(true);
	chicken.actor:SetMovementSpeed("run", TUNING.CHICKEN_SPEED);
	tinsert(chickens, chicken);
end

-- The chicken's prey: the whelp nearest to it among those at the edge of
-- the pack (past CHICKEN_HUNT_MIN from the hero, within the leash), else the
-- nearest within the leash. It keeps its prey until the prey dies or strays.
local function ChickenPrey(cx, cy, hx, hy)
	local leash2 = TUNING.CHICKEN_LEASH * TUNING.CHICKEN_LEASH;
	local min2 = TUNING.CHICKEN_HUNT_MIN * TUNING.CHICKEN_HUNT_MIN;
	local edge, edgeD2, any, anyD2;
	for _, e in ipairs(enemies) do
		if e.alive then
			local x, y = e.actor:GetPosition();
			local fromHero = Dist2(x, y, hx, hy);
			if fromHero <= leash2 then
				local d2 = Dist2(x, y, cx, cy);
				if fromHero >= min2 and (not edge or d2 < edgeD2) then
					edge, edgeD2 = e, d2;
				end
				if not any or d2 < anyD2 then
					any, anyD2 = e, d2;
				end
			end
		end
	end
	return edge or any;
end

local function UpdateChickens(dt, hx, hy, hz)
	local leash2 = TUNING.CHICKEN_LEASH * TUNING.CHICKEN_LEASH;
	local reach2 = TUNING.CHICKEN_REACH * TUNING.CHICKEN_REACH;
	local taunt2 = TUNING.CHICKEN_TAUNT_RADIUS * TUNING.CHICKEN_TAUNT_RADIUS;
	for i = table.getn(chickens), 1, -1 do
		local chicken = chickens[i];
		chicken.life = chicken.life - dt;
		if chicken.life <= 0 then
			chicken.gone = true;
			chicken.prey = nil;
			chicken.actor:SetMovementEnabled(false);
			chicken.actor:Hide();
			tinsert(chickenPool, chicken);
			table.remove(chickens, i);
		else
			local cx, cy, cz = chicken.actor:GetPosition();
			chicken.peck = chicken.peck - dt;
			chicken.move = chicken.move - dt;
			local prey = chicken.prey;
			if prey then
				local px, py = prey.actor:GetPosition();
				if not prey.alive or Dist2(px, py, hx, hy) > leash2 then
					prey = nil;
				end
			end
			if not prey then
				prey = ChickenPrey(cx, cy, hx, hy);
				chicken.prey = prey;
			end
			if prey then
				local tx, ty, tz = prey.actor:GetPosition();
				if Dist2(tx, ty, cx, cy) <= reach2 then
					if chicken.peck <= 0 then
						chicken.peck = TUNING.CHICKEN_PECK;
						chicken.actor:SetYaw(math.atan2(ty - cy, tx - cx));
						chicken.actor:SetAnimation(ANIM_ATTACK_UNARMED);
						Damage(prey, TUNING.CHICKEN_DAMAGE * RunGrowth(), TUNING.ITEM_COLOR);
						-- The peck taunts the whelps around the chicken: they
						-- chase it, and leave the hero be (Chase, Step).
						for _, e in ipairs(enemies) do
							if e.alive then
								local x, y = e.actor:GetPosition();
								if Dist2(x, y, cx, cy) <= taunt2 then
									e.taunter = chicken;
									e.tauntUntil = BW.run.time + TUNING.CHICKEN_TAUNT;
								end
							end
						end
					end
				elseif chicken.move <= 0 then
					chicken.move = 0.25;
					chicken.actor:MoveTo(tx, ty, tz);
				end
			elseif chicken.move <= 0 then
				-- Nothing to hunt: it struts about the hero.
				chicken.move = 1.5 + math.random();
				local angle = math.random() * 2 * math.pi;
				local r = TUNING.CHICKEN_ROAM * (0.4 + 0.6 * math.random());
				chicken.actor:MoveTo(hx + r * math.cos(angle), hy + r * math.sin(angle), hz);
			end
		end
	end
end

local function UseItem(item, hx, hy, hz)
	BW.heroNumber.actor = BW.hero;
	ShowNumber(BW.heroNumber, 0, TUNING.ITEM_COLOR, item.name);
	if item.key == "potion" then
		BW.hero:PlaySpellVisual(item.spell, BW.hero, 0);
		local heal = math.floor(MaxHp() * TUNING.POTION_HEAL + 0.5);
		BW.run.hp = math.min(MaxHp(), BW.run.hp + heal);
		UpdateHealth();
	elseif item.key == "grenade" then
		ThrowGrenade(item);
	elseif item.key == "swift" then
		BW.hero:PlaySpellVisual(item.spell, BW.hero, 0);
		BW.run.swiftUntil = BW.run.time + TUNING.SWIFTNESS_SECONDS;
		ApplyHeroSpeed();
	elseif item.key == "hearth" then
		BW.hero:PlaySpellVisual(item.spell, BW.hero, 0);
		for _, gem in ipairs(gems) do
			gem.pulled = true;
		end
	elseif item.key == "sapper" then
		Sapper(item, hx, hy);
	elseif item.key == "chicken" then
		SummonChicken(item, hx, hy, hz);
	end
end

-- Whether a Battle Chicken holds this whelp: taunted, and the chicken out.
local function Taunted(e)
	return e.taunter and not e.taunter.gone and BW.run.time < e.tauntUntil;
end

local function UpdateItems(dt, hx, hy, hz)
	if BW.run.swiftUntil and BW.run.time >= BW.run.swiftUntil then
		BW.run.swiftUntil = nil;
		ApplyHeroSpeed();
	end
	local reach2 = TUNING.ITEM_REACH * TUNING.ITEM_REACH;
	local scale = BW.scene:GetEffectiveScale();
	local bob = math.sin(BW.run.time * 4) * 4;
	for i = table.getn(drops), 1, -1 do
		local drop = drops[i];
		drop.life = drop.life - dt;
		if Dist2(drop.x, drop.y, hx, hy) <= reach2 then
			local item = drop.item;
			ReleaseDrop(i);
			UseItem(item, hx, hy, hz);
		elseif drop.life <= 0 then
			ReleaseDrop(i);
		else
			local sx, sy = BW.scene:Project3DPointTo2D(drop.x, drop.y, drop.z + 1.2);
			if sx then
				drop.icon:ClearAllPoints();
				drop.icon:SetPoint("BOTTOM", BW.scene, "BOTTOMLEFT", sx / scale, sy / scale + bob);
			end
		end
	end
	UpdateChickens(dt, hx, hy, hz);
end

local function ClearItems()
	for i = table.getn(drops), 1, -1 do
		ReleaseDrop(i);
	end
	for i = table.getn(chickens), 1, -1 do
		chickens[i].gone = true;
		chickens[i].prey = nil;
		chickens[i].actor:SetMovementEnabled(false);
		chickens[i].actor:Hide();
		tinsert(chickenPool, chickens[i]);
		chickens[i] = nil;
	end
end

-- For the files that load after this one.
BW.Taunted = Taunted
BW.UpdateItems = UpdateItems
BW.ClearItems = ClearItems
