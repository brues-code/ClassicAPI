-- Broodwatch brood: spawning, hit feedback, damage and death.

local _, BW = ...
local CENTER = BW.CENTER
local ROOM_RADIUS = BW.ROOM_RADIUS
local SPAWN_MIN_DISTANCE = BW.SPAWN_MIN_DISTANCE
local MAX_ALIVE = BW.MAX_ALIVE
local DEATH_SECONDS = BW.DEATH_SECONDS
local ANIM_STAND = BW.ANIM_STAND
local ANIM_DEATH = BW.ANIM_DEATH
local KINDS = BW.KINDS
local ELITE = BW.ELITE
local TUNING = BW.TUNING
local VAMPIRIC_SHARE = BW.VAMPIRIC_SHARE
local pools = BW.pools
local enemies = BW.enemies
local FloorAt = BW.FloorAt
local Dist2 = BW.Dist2
local DropGem = BW.DropGem
local Mastered = BW.Mastered
local SchoolColor = BW.SchoolColor
local SPELLS = BW.SPELLS

------------------------------------------------------------------------------
-- The brood

-- The brood toughens as the run goes on, so late builds keep meeting
-- resistance: with m the run's minutes over 5, health grows (1 + m)^3 (x8 at
-- 5:00, x27 at 10:00) and bites (1 + m)^2 (x4 at 5:00, x9 at 10:00).
local function KindStats(kind)
	local m = 1 + BW.run.time / 300;
	return math.floor(kind.hp * m * m * m + 0.5), kind.dps * m * m;
end

-- Every aura on the enemy goes (its death, or its return from the pool).
local function ClearAuras(e)
	if next(e.auras) then
		for spell in pairs(e.auras) do
			e.auras[spell] = nil;
		end
		e.actor:RemoveAuraVisual();
	end
	e.fearAura = nil;
end

local function NewEnemy(kind)
	-- auras: the spell of each aura shown -> the time it ends (ShowAura).
	local e = { kind = kind, actor = BW.scene:AcquireActor(), auras = {} };
	if kind.path then
		e.actor:SetModelByPath(kind.path);
	else
		e.actor:SetModelByCreatureDisplayID(kind.display);
	end
	-- A whelp casts the next leap of Chain Lightning: its impact comes here.
	e.actor:HookScript("OnSpellVisualImpact", function(_, _, _, castID) BW.OnImpact(nil, nil, nil, castID); end);
	return e;
end

-- A point in the room at least SPAWN_MIN_DISTANCE from the hero (ten tries).
local function SpawnPoint()
	local hx, hy = BW.hero:GetPosition();
	local x, y;
	for _ = 1, 10 do
		local a = math.random() * 2 * math.pi;
		local r = ROOM_RADIUS * math.sqrt(math.random());
		x, y = CENTER[1] + r * math.cos(a), CENTER[2] + r * math.sin(a);
		if Dist2(x, y, hx, hy) >= SPAWN_MIN_DISTANCE * SPAWN_MIN_DISTANCE then
			break;
		end
	end
	return x, y;
end

-- A whelp drops in a little above the floor and lands; an egg stands on it.
-- (x, y): where, or a SpawnPoint.
local function Spawn(kind, x, y)
	if not x then
		x, y = SpawnPoint();
	end
	local pool = pools[kind.key];
	local e = pool and table.remove(pool) or NewEnemy(kind);
	if e.sheepUntil then -- it died a sheep
		e.sheepUntil = nil;
		e.actor:SetModelByCreatureDisplayID(kind.display);
	end
	e.hp, e.dps = KindStats(kind);
	e.maxHp = e.hp;
	e.alive = true;
	e.dying = nil;
	e.fading = nil;
	e.dotLeft = nil;
	e.burnLeft = nil;
	e.slowUntil, e.slowFactor = nil, nil;
	e.stunUntil = nil;
	e.frozenAura = nil;
	e.fearUntil, e.fearTick = nil, nil;
	e.frozenUntil = nil;
	e.weakUntil, e.weakShare = nil, nil;
	e.taunter, e.tauntUntil = nil, nil;
	e.actState, e.actNext = nil, nil;
	ClearAuras(e);
	e.angle = math.random() * 2 * math.pi;
	local actor = e.actor;
	actor:SetAnimation(ANIM_STAND);
	actor:SetPaused(false, false); -- a crack held its last frame (the corpses, Step)
	if kind.speed == 0 then
		actor:SetPosition(x, y, FloorAt(x, y, CENTER[3]));
		actor:SetYaw(math.random() * 2 * math.pi);
		actor:Show();
	else
		actor:SetPosition(x, y, FloorAt(x, y, CENTER[3]) + 0.5);
		actor:Show();
		actor:SetMovementEnabled(true);
		actor:SetMovementSpeed("run", kind.speed);
	end
	tinsert(enemies, e);
	BW.run.alive = BW.run.alive + 1;
	BW.run.kindAlive[kind.key] = (BW.run.kindAlive[kind.key] or 0) + 1;
	return e;
end

-- The enemy leaves the fight. A whelp's corpse keeps its movement until it
-- is released: it stops where it is, plays Death, then lies Dead, as a dead
-- unit does. A kind with a crack (an egg) plays it, with its sound.
local function Retire(e)
	e.alive = false;
	BW.run.alive = BW.run.alive - 1;
	BW.run.kindAlive[e.kind.key] = BW.run.kindAlive[e.kind.key] - 1;
	e.actor:StopMoving();
	if e.kind.crackAnim then
		e.dying = e.kind.crackSeconds;
		e.actor:SetAnimation(e.kind.crackAnim);
		e.actor:PlaySound(e.kind.crackSound);
	else
		e.dying = DEATH_SECONDS;
		e.actor:SetAnimation(ANIM_DEATH);
	end
	ClearAuras(e);
	e.fearUntil, e.fearTick = nil, nil;
end

local Wildfire -- a combo, below with the lasting effects

local function Kill(e)
	Retire(e);
	BW.run.kills = BW.run.kills + 1;
	local x, y, z = e.actor:GetPosition();
	DropGem(x, y, z, e.kind.xp, e.kind.gemScale);
	BW.DropItem(e, x, y, z);
	if e.kind == ELITE then
		BW.DropChest(x, y, z);
	end
	-- A corrupted enemy passes its Corruption on (in the step, after the loop).
	if e.dotLeft and e.dotSpread > 0 then
		tinsert(BW.run.spreads, e);
	end
	e.dotLeft = nil;
	if e.burnLeft and e.burnSchool == "Fire" then
		Wildfire(e, x, y);
	end
	e.burnLeft = nil;
end

------------------------------------------------------------------------------
-- Hit feedback: a short flash on the model, and a number that rises from the
-- hit point and fades.

local FLASH_SECONDS = 0.12
local NUMBER_SECONDS = 0.8
local NUMBER_RISE = 34       -- pixels over the number's life
local NUMBER_FONT = "Fonts\\ARIALN.TTF"
local BIG_HIT = 40           -- hits this large use the large size
local MAX_NUMBERS = 80

local numbers = {}           -- live numbers, oldest first
local numberPool = {}

local function TakeNumber()
	local n = table.remove(numberPool);
	if n then
		return n;
	end
	if table.getn(numbers) >= MAX_NUMBERS then
		-- Too many at once: the oldest gives up its string.
		return table.remove(numbers, 1);
	end
	n = { text = BW.feedback:CreateFontString(nil, "OVERLAY") };
	n.text:SetFontObject(NumberFontNormal);
	return n;
end

local function ShowNumber(e, amount, color, text)
	local x, y, z = e.actor:GetNamePosition();
	if not x then
		x, y, z = e.actor:GetPosition();
		z = z + 1.5;
	end
	local n = TakeNumber();
	n.x, n.y, n.z = x, y, z;
	n.age = 0;
	n.dx = math.random(-12, 12);
	local size = amount >= BIG_HIT and 22 or 16;
	if n.size ~= size then
		n.size = size;
		n.text:SetFont(NUMBER_FONT, size, "OUTLINE");
	end
	n.text:SetTextColor(color[1], color[2], color[3]);
	n.text:SetText(text or tostring(amount));
	n.text:SetAlpha(1);
	n.text:Show();
	tinsert(numbers, n);
end

local function UpdateNumbers(dt)
	local count = table.getn(numbers);
	if count == 0 then
		return;
	end
	local scale = BW.scene:GetEffectiveScale();
	for i = count, 1, -1 do
		local n = numbers[i];
		n.age = n.age + dt;
		local sx, sy = BW.scene:Project3DPointTo2D(n.x, n.y, n.z);
		if n.age >= NUMBER_SECONDS or not sx then
			n.text:Hide();
			tinsert(numberPool, n);
			table.remove(numbers, i);
		else
			local t = n.age / NUMBER_SECONDS;
			n.text:ClearAllPoints();
			n.text:SetPoint("BOTTOM", BW.scene, "BOTTOMLEFT", sx / scale + n.dx, sy / scale + NUMBER_RISE * t);
			if t > 0.5 then
				n.text:SetAlpha(2 - 2 * t);
			end
		end
	end
end

local function ClearNumbers()
	for i = table.getn(numbers), 1, -1 do
		numbers[i].text:Hide();
		tinsert(numberPool, numbers[i]);
		numbers[i] = nil;
	end
end

local function Flash(e)
	e.flash = FLASH_SECONDS;
	if not e.lit then
		e.lit = true;
		e.actor:SetHighlight("mouseover", true);
	end
end

local function Unflash(e)
	if e.lit then
		e.lit = false;
		e.actor:SetHighlight("mouseover", false);
	end
end

local function Damage(e, amount, color)
	if not e.alive then
		return;
	end
	-- Masteries: Corruption and Frost Nova's Shatter make their marks take more.
	if e.dotLeft and Mastered("corruption") then
		amount = amount * SPELLS.corruption.mastery.amplify;
	end
	if e.frozenUntil and BW.run.time < e.frozenUntil and Mastered("frostnova") then
		amount = amount * SPELLS.frostnova.mastery.amplify;
	end
	local dealt = math.floor(amount * BW.run.damageScale + 0.5);
	e.hp = e.hp - dealt;
	if BW.run.passives.vampiric then
		BW.run.leech = BW.run.leech + dealt * VAMPIRIC_SHARE * BW.run.passives.vampiric;
	end
	Flash(e);
	ShowNumber(e, dealt, color);
	if e.hp <= 0 then
		Kill(e);
	end
end

-- Throws an enemy away from (x, y) at `knock` (out, up). A corpse flies too,
-- in its death pose.
local function Blast(e, x, y, knock)
	local share = e.kind.knockShare or 1;
	if share == 0 then
		return;
	end
	e.actor:KnockBack(x, y, knock[1] * share, knock[2] * share);
end

------------------------------------------------------------------------------
-- Lasting effects: a hold (a root or a stun) keeps an enemy where it is, a
-- fear sends it away from you, a weakness softens its bites. A hold or a fear
-- shows the aura of the spell that caused it (ShowAura) for its own time,
-- and the auras stack: a frozen whelp that is feared keeps its ice. A stun
-- aura's kit pins Stun while the whelp stands. A kind takes its holdShare
-- of a hold's, a fear's or a polymorph's time: elites shrug off part, and a
-- kind with none (an egg) is immune.

local function ShowAura(e, spell, untilTime)
	if not spell then
		return;
	end
	if not e.auras[spell] then
		e.actor:AddAuraVisual(spell);
	end
	e.auras[spell] = math.max(e.auras[spell] or 0, untilTime);
end

local function EndAura(e, spell)
	if spell and e.auras[spell] then
		e.auras[spell] = nil;
		e.actor:RemoveAuraVisual(spell);
	end
end

-- A hold or a polymorph breaks a fear, its aura with it.
local function EndFear(e)
	EndAura(e, e.fearAura);
	e.fearUntil, e.fearTick, e.fearAura = nil, nil, nil;
end

local function HoldShare(e)
	return e.kind.holdShare or 1;
end

-- `aura`: the spell whose aura shows; `frozen`: the hold is ice (Frost
-- Nova's Shatter counts it).
local function Hold(e, seconds, aura, frozen)
	if HoldShare(e) == 0 then
		return;
	end
	local untilTime = BW.run.time + seconds * HoldShare(e);
	e.stunUntil = math.max(e.stunUntil or 0, untilTime);
	if frozen then
		e.frozenUntil = e.stunUntil;
		e.frozenAura = aura;
	end
	EndFear(e);
	e.actor:StopMoving();
	ShowAura(e, aura, untilTime);
end

local function Fear(e, seconds, aura)
	if HoldShare(e) == 0 then
		return;
	end
	local untilTime = BW.run.time + seconds * HoldShare(e);
	e.fearUntil = math.max(e.fearUntil or 0, untilTime);
	if aura then
		if e.fearAura ~= aura then
			EndAura(e, e.fearAura);
		end
		e.fearAura = aura;
		ShowAura(e, aura, untilTime);
	end
end

-- An enemy's run speed: a sheep's amble, else its kind's, slowed.
local function ApplySpeed(e)
	if e.kind.speed == 0 then
		return;
	end
	local speed = e.sheepUntil and TUNING.SHEEP_SPEED or e.kind.speed * (e.slowFactor or 1);
	e.actor:SetMovementSpeed("run", speed);
end

-- Polymorph: the enemy is a sheep (SPELLS.polymorph.display) that wanders,
-- harmless, until its time ends. Its own model comes back after.
local function Polymorph(e, seconds)
	if HoldShare(e) == 0 then
		return;
	end
	if not e.sheepUntil then
		e.sheepUntil = 0;
		e.actor:SetModelByCreatureDisplayID(SPELLS.polymorph.display);
		e.actor:StopMoving();
		ApplySpeed(e);
	end
	EndFear(e);
	e.sheepUntil = math.max(e.sheepUntil, BW.run.time + seconds * HoldShare(e));
end

local function Unpolymorph(e)
	e.sheepUntil = nil;
	e.actor:SetModelByCreatureDisplayID(e.kind.display);
	ApplySpeed(e);
end

local function Weaken(e, share, seconds)
	e.weakShare = math.max(e.weakShare or 0, share);
	e.weakUntil = math.max(e.weakUntil or 0, BW.run.time + seconds);
end

------------------------------------------------------------------------------
-- Burning: `dps` each second for `seconds`, of a spell's school (Pyroblast's
-- fire, Moonfire's arcane), its ticks in the school's colour. A stronger burn takes over, a longer one lasts.
-- A fire burn shows Immolate's aura (348: state kit 235, flames at the base).

local function Burn(e, dps, seconds, school)
	if not e.alive then
		return;
	end
	e.burnDps = math.max(e.burnLeft and e.burnDps or 0, dps);
	e.burnLeft = math.max(e.burnLeft or 0, seconds);
	e.burnSeconds = seconds;
	e.burnTick = e.burnTick or 1;
	e.burnSchool = school;
	e.burnColor = SchoolColor(school);
	if school == "Fire" then
		ShowAura(e, TUNING.BURN_AURA, BW.run.time + e.burnLeft);
	end
end

local function EndBurn(e)
	if e.burnSchool == "Fire" then
		EndAura(e, TUNING.BURN_AURA);
	end
	e.burnLeft, e.burnTick = nil, nil;
end

-- A frozen enemy's ice breaks (Shatter): its hold and its aura end.
local function Thaw(e)
	EndAura(e, e.frozenAura);
	e.frozenUntil, e.stunUntil, e.frozenAura = nil, nil, nil;
end

local function Announce(e, combo)
	ShowNumber(e, BIG_HIT, TUNING.COMBO_COLOR, combo .. "!");
end

-- Wildfire: a burning enemy that dies sets the enemies around it alight
-- with its burn (called from Kill).
Wildfire = function(e, x, y)
	local r2 = TUNING.WILDFIRE_RADIUS * TUNING.WILDFIRE_RADIUS;
	local caught = false;
	for _, o in ipairs(enemies) do
		if o.alive and not o.burnLeft then
			local ox, oy = o.actor:GetPosition();
			if Dist2(ox, oy, x, y) <= r2 then
				Burn(o, e.burnDps, e.burnSeconds, "Fire");
				caught = true;
			end
		end
	end
	if caught then
		Announce(e, "Wildfire");
	end
end

-- A spell's hit, by its school (def.school, the spell's own) against what
-- the enemy suffers. The combos:
-- - Shatter: any hit but frost on a frozen enemy deals SHATTER times its
--   damage and breaks the ice.
-- - Dread: shadow hurts a fleeing enemy DREAD times as much.
-- - Detonate: nature sets off a burn: what is left of it lands at once,
--   and half of that on every enemy within DETONATE_RADIUS.
-- - Steam: frost puts out a fire burn and stuns the enemy for STEAM_STUN.
-- - Wildfire (Kill): a burning enemy that dies sets its neighbours alight.
local function SpellDamage(e, amount, def)
	if not e.alive then
		return;
	end
	local school = def.school;
	local combo, detonated, steamed;
	if school ~= "Frost" and e.frozenUntil and BW.run.time < e.frozenUntil then
		amount = amount * TUNING.SHATTER;
		Thaw(e);
		combo = "Shatter";
	elseif school == "Shadow" and e.fearUntil then
		amount = amount * TUNING.DREAD;
	end
	if e.burnLeft then
		if school == "Nature" then
			detonated = e.burnDps * e.burnLeft;
			EndBurn(e);
			amount = amount + detonated;
			combo = "Detonate";
		elseif school == "Frost" and e.burnSchool == "Fire" then
			EndBurn(e);
			steamed = true;
			combo = "Steam";
		end
	end
	local x, y = e.actor:GetPosition();
	Damage(e, amount, def.hitColor);
	if combo then
		Announce(e, combo);
	end
	if steamed and e.alive then
		Hold(e, TUNING.STEAM_STUN, TUNING.STUN_AURA);
	end
	if detonated then
		local r2 = TUNING.DETONATE_RADIUS * TUNING.DETONATE_RADIUS;
		for _, o in ipairs(enemies) do
			if o.alive and o ~= e then
				local ox, oy = o.actor:GetPosition();
				if Dist2(ox, oy, x, y) <= r2 then
					Damage(o, detonated * 0.5, def.hitColor);
				end
			end
		end
	end
end

local function TickBurn(e, dt)
	e.burnTick = e.burnTick - dt;
	if e.burnTick > 0 then
		return;
	end
	e.burnTick = e.burnTick + 1;
	e.burnLeft = e.burnLeft - 1;
	Damage(e, e.burnDps, e.burnColor);
	if e.alive and e.burnLeft <= 0 then
		EndBurn(e);
	end
end

-- Ends the effects whose time has run out (the step, for each live enemy),
-- and burns the fleeing with Psychic Scream's Terror.
local function UpdateStates(e, dt)
	local t = BW.run.time;
	if e.burnLeft then
		TickBurn(e, dt);
		if not e.alive then
			return;
		end
	end
	if e.fearUntil and Mastered("scream") then
		e.fearTick = (e.fearTick or 1) - dt;
		if e.fearTick <= 0 then
			e.fearTick = e.fearTick + 1;
			Damage(e, SPELLS.scream.mastery.damage, SPELLS.scream.hitColor);
			if not e.alive then
				return;
			end
		end
	end
	for spell, untilTime in pairs(e.auras) do
		if t >= untilTime then -- clearing a key during `pairs` is allowed
			e.auras[spell] = nil;
			e.actor:RemoveAuraVisual(spell);
		end
	end
	if e.fearUntil and t >= e.fearUntil then
		EndFear(e);
	end
	if e.weakUntil and t >= e.weakUntil then
		e.weakUntil, e.weakShare = nil, nil;
	end
	if e.sheepUntil and t >= e.sheepUntil then
		Unpolymorph(e);
	end
end

-- The share of an enemy's bite that lands.
local function BiteShare(e)
	return e.weakShare and 1 - e.weakShare or 1;
end

local function Release(i)
	local e = enemies[i];
	Unflash(e);
	e.actor:SetMovementEnabled(false);
	e.actor:Hide();
	local pool = pools[e.kind.key];
	if not pool then
		pool = {};
		pools[e.kind.key] = pool;
	end
	tinsert(pool, e);
	table.remove(enemies, i);
end

local function ClearEnemies()
	for i = table.getn(enemies), 1, -1 do
		if enemies[i].alive then
			BW.run.alive = BW.run.alive - 1;
		end
		Release(i);
	end
end

-- How many should be alive at this second of the run.
local function TargetAlive(t)
	return math.min(MAX_ALIVE, 8 + math.floor(t * 0.38));
end

local function PickKind()
	local open = 0;
	for _, kind in ipairs(KINDS) do
		if BW.run.time >= kind.from then
			open = open + 1;
		end
	end
	-- Newer kinds are rarer: the first kind open gets the most weight. A
	-- kind at its `max` gives way to the first.
	local roll = math.random() * (open * (open + 1) / 2);
	for i = 1, open do
		local w = open - i + 1;
		if roll < w then
			local kind = KINDS[i];
			if kind.max and (BW.run.kindAlive[kind.key] or 0) >= kind.max then
				return KINDS[1];
			end
			return kind;
		end
		roll = roll - w;
	end
	return KINDS[1];
end

-- The kind of a key (KINDS, or the elite's), for the slash command.
local function KindByKey(key)
	if key == ELITE.key then
		return ELITE;
	end
	for _, kind in ipairs(KINDS) do
		if kind.key == key then
			return kind;
		end
	end
end

-- For the files that load after this one.
BW.KindByKey = KindByKey
BW.Blast = Blast
BW.Hold = Hold
BW.ShowAura = ShowAura
BW.Fear = Fear
BW.HoldShare = HoldShare
BW.Weaken = Weaken
BW.Burn = Burn
BW.SpellDamage = SpellDamage
BW.Polymorph = Polymorph
BW.ApplySpeed = ApplySpeed
BW.UpdateStates = UpdateStates
BW.BiteShare = BiteShare
BW.SpawnPoint = SpawnPoint
BW.Spawn = Spawn
BW.Retire = Retire
BW.Kill = Kill
BW.ShowNumber = ShowNumber
BW.UpdateNumbers = UpdateNumbers
BW.ClearNumbers = ClearNumbers
BW.Flash = Flash
BW.Unflash = Unflash
BW.Damage = Damage
BW.Release = Release
BW.ClearEnemies = ClearEnemies
BW.TargetAlive = TargetAlive
BW.PickKind = PickKind
