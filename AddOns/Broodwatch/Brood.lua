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

local function NewEnemy(kind)
	local e = { kind = kind, actor = BW.scene:AcquireActor() };
	e.actor:SetModelByCreatureDisplayID(kind.display);
	-- A whelp casts the next leap of Chain Lightning: its impact comes here.
	e.actor:HookScript("OnSpellVisualImpact", function(_, _, _, castID) BW.OnImpact(nil, nil, nil, castID); end);
	return e;
end

local function Spawn(kind)
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
	local pool = pools[kind.key];
	local e = pool and table.remove(pool) or NewEnemy(kind);
	e.hp, e.dps = KindStats(kind);
	e.maxHp = e.hp;
	e.alive = true;
	e.dying = nil;
	e.fading = nil;
	e.dotLeft = nil;
	e.slowUntil, e.slowFactor = nil, nil;
	e.stunUntil = nil;
	e.fearUntil, e.fearTick = nil, nil;
	e.frozenUntil = nil;
	e.weakUntil, e.weakShare = nil, nil;
	e.taunter, e.tauntUntil = nil, nil;
	if e.kit then
		e.kit, e.kitUntil = nil, nil;
		e.actor:SetSpellVisualKit();
	end
	e.angle = math.random() * 2 * math.pi;
	local actor = e.actor;
	actor:SetPosition(x, y, FloorAt(x, y, CENTER[3]) + 0.5);
	actor:SetAnimation(ANIM_STAND);
	actor:Show();
	actor:SetMovementEnabled(true);
	actor:SetMovementSpeed("run", kind.speed);
	tinsert(enemies, e);
	BW.run.alive = BW.run.alive + 1;
	return e;
end

local function Kill(e)
	e.alive = false;
	e.dying = DEATH_SECONDS;
	BW.run.alive = BW.run.alive - 1;
	BW.run.kills = BW.run.kills + 1;
	local x, y, z = e.actor:GetPosition();
	-- The corpse keeps its movement until it is released: it stops where
	-- it is, plays Death, then lies Dead, as a dead unit does.
	e.actor:StopMoving();
	e.actor:SetAnimation(ANIM_DEATH);
	if e.kit then
		e.kit, e.kitUntil = nil, nil;
		e.actor:SetSpellVisualKit();
	end
	e.fearUntil = nil;
	DropGem(x, y, z, e.kind.xp, e.kind.gemScale);
	BW.DropItem(e, x, y, z);
	-- A corrupted enemy passes its Corruption on (in the step, after the loop).
	if e.dotLeft and e.dotSpread > 0 then
		tinsert(BW.run.spreads, e);
	end
	e.dotLeft = nil;
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
	local share = e.kind == ELITE and TUNING.ELITE_KNOCK_SHARE or 1;
	e.actor:KnockBack(x, y, knock[1] * share, knock[2] * share);
end

------------------------------------------------------------------------------
-- Lasting effects: a hold (a root or a stun) keeps an enemy where it is, a
-- fear sends it away from you, a weakness softens its bites. A hold or a fear
-- can show a SpellVisualKit on the enemy while it lasts. Elites shrug off
-- part of a hold or a fear.

local function ShowKit(e, kit, untilTime)
	if not kit then
		return;
	end
	if e.kit ~= kit then
		e.kit = kit;
		e.actor:SetSpellVisualKit(kit);
	end
	e.kitUntil = math.max(e.kitUntil or 0, untilTime);
end

local function HoldShare(e)
	return e.kind == ELITE and TUNING.ELITE_HOLD_SHARE or 1;
end

-- `frozen`: the hold is ice (Frost Nova's Shatter counts it).
local function Hold(e, seconds, kit, frozen)
	e.stunUntil = math.max(e.stunUntil or 0, BW.run.time + seconds * HoldShare(e));
	if frozen then
		e.frozenUntil = e.stunUntil;
	end
	e.fearUntil = nil;
	e.actor:StopMoving();
	ShowKit(e, kit, e.stunUntil);
end

local function Fear(e, seconds, kit)
	e.fearUntil = math.max(e.fearUntil or 0, BW.run.time + seconds * HoldShare(e));
	ShowKit(e, kit, e.fearUntil);
end

local function Weaken(e, share, seconds)
	e.weakShare = math.max(e.weakShare or 0, share);
	e.weakUntil = math.max(e.weakUntil or 0, BW.run.time + seconds);
end

-- Ends the effects whose time has run out (the step, for each live enemy),
-- and burns the fleeing with Psychic Scream's Terror.
local function UpdateStates(e, dt)
	local t = BW.run.time;
	if e.fearUntil and Mastered("scream") then
		e.fearTick = (e.fearTick or 1) - dt;
		if e.fearTick <= 0 then
			e.fearTick = e.fearTick + 1;
			Damage(e, SPELLS.scream.mastery.damage, SPELLS.scream.color);
			if not e.alive then
				return;
			end
		end
	end
	if e.kitUntil and t >= e.kitUntil then
		e.kit, e.kitUntil = nil, nil;
		e.actor:SetSpellVisualKit();
	end
	if e.fearUntil and t >= e.fearUntil then
		e.fearUntil, e.fearTick = nil, nil;
	end
	if e.weakUntil and t >= e.weakUntil then
		e.weakUntil, e.weakShare = nil, nil;
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
	-- Newer kinds are rarer: the first kind open gets the most weight.
	local roll = math.random() * (open * (open + 1) / 2);
	for i = 1, open do
		local w = open - i + 1;
		if roll < w then
			return KINDS[i];
		end
		roll = roll - w;
	end
	return KINDS[1];
end

-- For the files that load after this one.
BW.Blast = Blast
BW.Hold = Hold
BW.Fear = Fear
BW.Weaken = Weaken
BW.UpdateStates = UpdateStates
BW.BiteShare = BiteShare
BW.Spawn = Spawn
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
