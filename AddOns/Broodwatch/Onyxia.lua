-- Broodwatch's Onyxia: at RUN_SECONDS the brood stops coming and its mother
-- descends. Her death wins the run.
--
-- She fights in the world's three phases (Data.lua's BOSS): on the ground,
-- in the air over the lair, and on the ground again. Her acts are Foes.lua's
-- (Data.lua's ONYXIA_ACTS); this file moves her, changes her phase, sends her
-- whelps and adds the two acts that are hers alone: Deep Breath and
-- Bellowing Roar.
--
-- In the air her movement is off (an actor's movement walks the floor): her
-- position eases along a straight line each step (FlyTo), with the model's
-- own LiftOff, Fly, Hover and Land animations.

local _, BW = ...
local ONYXIA = BW.ONYXIA
local BOSS = BW.BOSS
local CENTER = BW.CENTER
local ROOM_RADIUS = BW.ROOM_RADIUS
local MAX_ALIVE = BW.MAX_ALIVE
local EASE = BW.EASE
local AT_ONCE = BW.AT_ONCE
local enemies = BW.enemies
local Dist2 = BW.Dist2
local FloorAt = BW.FloorAt
local RunGrowth = BW.RunGrowth
local Spawn = BW.Spawn
local Kill = BW.Kill
local Damage = BW.Damage
local KindByKey = BW.KindByKey
local ApplySpeed = BW.ApplySpeed
local ACTS = BW.ACTS
local Mark = BW.Mark
local Unmark = BW.Unmark
local Strike = BW.Strike
local Track = BW.Track
local StartAct = BW.StartAct
local MarkVents = BW.MarkVents

-- DragonOnyxia.m2's sequences (AnimationData IDs).
local ANIM_FLY = 135
local ANIM_LIFTOFF = 192
local ANIM_HOVER = 193
local ANIM_LAND = 200
local LIFT_SECONDS = 3 -- LiftOff and Land

local DEEP = BOSS.DEEP
local ROAR = BOSS.ROAR

------------------------------------------------------------------------------
-- The bar over the scene: her name and health, with a tick at each phase's
-- threshold; and an announcement under it, which fades.

local ticks -- the bar's phase ticks, made the first time it shows

local function ShowBar(e)
	local bar = BW.bossBar;
	if not ticks then
		ticks = {};
		for _, share in ipairs({ BOSS.PHASE_TWO, BOSS.PHASE_THREE }) do
			local tick = bar:CreateTexture(nil, "OVERLAY");
			tick:SetTexture(1, 1, 1, 0.6);
			tick:SetWidth(2);
			tick:SetHeight(bar:GetHeight());
			tick:SetPoint("LEFT", bar, "LEFT", bar:GetWidth() * share - 1, 0);
			tinsert(ticks, tick);
		end
	end
	bar:SetMinMaxValues(0, e.maxHp, AT_ONCE);
	bar:SetValue(e.hp, AT_ONCE);
	bar:Show();
end

local shownHp

local function UpdateBar(e)
	local hp = math.max(e.hp, 0);
	if hp == shownHp then
		return;
	end
	shownHp = hp;
	BW.bossBar:SetValue(hp, EASE);
	BW.bossText:SetText(string.format("%s  %d%%", BOSS.NAME, math.ceil(100 * hp / e.maxHp)));
end

local alertLeft = 0

local function Alert(text)
	alertLeft = BOSS.ALERT_SECONDS;
	BW.alertText:SetText(text);
	BW.alertText:SetTextColor(BOSS.ALERT_COLOR[1], BOSS.ALERT_COLOR[2], BOSS.ALERT_COLOR[3]);
	BW.alertText:SetAlpha(1);
	BW.alertText:Show();
end

local function UpdateAlert(dt)
	if alertLeft <= 0 then
		return;
	end
	alertLeft = alertLeft - dt;
	if alertLeft <= 0 then
		BW.alertText:Hide();
	elseif alertLeft < 1 then
		BW.alertText:SetAlpha(alertLeft);
	end
end

------------------------------------------------------------------------------
-- Places. The ring's point i (0 to RING_POINTS - 1) is HEIGHT over the
-- floor at that height; she lands ARRIVE yards from you, toward CENTER.

local function RingPoint(i)
	local a = i * 2 * math.pi / BOSS.RING_POINTS;
	return CENTER[1] + BOSS.RING_RADIUS * math.cos(a), CENTER[2] + BOSS.RING_RADIUS * math.sin(a),
		CENTER[3] + BOSS.HEIGHT;
end

local function NearestRing(x, y)
	local best, bestD2;
	for i = 0, BOSS.RING_POINTS - 1 do
		local rx, ry = RingPoint(i);
		local d2 = Dist2(x, y, rx, ry);
		if not best or d2 < bestD2 then
			best, bestD2 = i, d2;
		end
	end
	return best;
end

local function LandingPoint()
	local hx, hy = BW.hero:GetPosition();
	local dx, dy = CENTER[1] - hx, CENTER[2] - hy;
	local d = math.sqrt(dx * dx + dy * dy);
	if d < 1 then
		dx, dy, d = 1, 0, 1;
	end
	local x, y = hx + dx / d * BOSS.ARRIVE, hy + dy / d * BOSS.ARRIVE;
	local cx, cy = x - CENTER[1], y - CENTER[2];
	local c = math.sqrt(cx * cx + cy * cy);
	if c > ROOM_RADIUS then
		x, y = CENTER[1] + cx / c * ROOM_RADIUS, CENTER[2] + cy / c * ROOM_RADIUS;
	end
	return x, y, FloorAt(x, y, CENTER[3]);
end

------------------------------------------------------------------------------
-- Flight: a straight line from where she is to (x, y, z) in `seconds`, with
-- `anim`, facing the way she goes; then done(boss).

local function FlyTo(boss, x, y, z, seconds, anim, done)
	local actor = boss.e.actor;
	local ax, ay, az = actor:GetPosition();
	boss.fly = { ax = ax, ay = ay, az = az, bx = x, by = y, bz = z, t = 0, seconds = math.max(seconds, 0.05), done = done };
	if Dist2(ax, ay, x, y) > 0.25 then
		actor:SetYaw(math.atan2(y - ay, x - ax));
	end
	actor:SetAnimation(anim);
end

local function Distance(boss, x, y, z)
	local ax, ay, az = boss.e.actor:GetPosition();
	local dz = z - az;
	return math.sqrt(Dist2(ax, ay, x, y) + dz * dz);
end

local function UpdateFlight(boss, dt)
	local fly = boss.fly;
	if not fly then
		return;
	end
	fly.t = math.min(fly.t + dt, fly.seconds);
	local k = fly.t / fly.seconds;
	boss.e.actor:SetPosition(fly.ax + (fly.bx - fly.ax) * k, fly.ay + (fly.by - fly.ay) * k, fly.az + (fly.bz - fly.az) * k);
	if fly.t >= fly.seconds then
		boss.fly = nil;
		fly.done(boss);
	end
end

------------------------------------------------------------------------------
-- Phases. While she changes phase (and while she flies to a new point) she
-- does nothing else (e.acts nil), and nothing kills her in the air
-- (e.deathless: Damage leaves her 1): she dies on the ground.

local Hover, Landed

local function Fight(boss, phase)
	local e = boss.e;
	boss.phase = phase;
	e.acts = BOSS.ACTS[phase];
	e.actReady = e.actReady or {};
end

-- She stands where she is (her ring point), facing you, and throws fire.
Hover = function(boss)
	local e = boss.e;
	e.actor:SetAnimation(ANIM_HOVER);
	Fight(boss, 2);
	e.actReady[BOSS.ACTS[2][1]] = BW.run.time + 2; -- a moment after she arrives, as the world's 5 s
	boss.nextMove = BW.run.time + BOSS.MOVE_EVERY + math.random() * BOSS.MOVE_SPREAD;
	if not boss.waved then
		boss.waved = true;
		boss.waveLeft = BOSS.WHELPS_FIRST;
		boss.nextWave = BW.run.time + BOSS.WAVE_EVERY;
	end
end

local function Touchdown(boss)
	local x, y = boss.e.actor:GetPosition();
	FlyTo(boss, x, y, FloorAt(x, y, CENTER[3]), LIFT_SECONDS, ANIM_LAND, Landed);
end

-- She comes down by you: Fly to just over the floor, then Land.
local function Descend(boss)
	boss.e.acts = nil;
	boss.ring = nil;
	local x, y, z = LandingPoint();
	z = z + BOSS.LIFT_RISE;
	FlyTo(boss, x, y, z, Distance(boss, x, y, z) / BOSS.FLY_SPEED, ANIM_FLY, Touchdown);
end

-- On the floor: her movement walks again. The first landing starts the
-- fight; the second the last phase, with a roar at once.
Landed = function(boss)
	local e = boss.e;
	e.actor:SetAnimation(BW.ANIM_STAND); -- as Spawn does: the walk takes over from Land
	e.actor:SetMovementEnabled(true);
	ApplySpeed(e);
	e.flying, e.deathless = nil, nil;
	if boss.phase == 2 then
		Fight(boss, 3);
		e.actReady[ROAR] = BW.run.time;
		boss.lateGap = BOSS.LATE_WHELP_EVERY;
		Alert(BOSS.NAME .. " lands!");
	else
		Fight(boss, 1);
	end
end

local function Climb(boss)
	local x, y = boss.e.actor:GetPosition();
	boss.ring = NearestRing(x, y);
	local rx, ry, rz = RingPoint(boss.ring);
	FlyTo(boss, rx, ry, rz, Distance(boss, rx, ry, rz) / BOSS.FLY_SPEED, ANIM_FLY, Hover);
end

local function LiftOff(boss)
	local e = boss.e;
	e.acts = nil;
	e.flying, e.deathless = true, true;
	e.actor:StopMoving();
	e.actor:SetMovementEnabled(false);
	local x, y, z = e.actor:GetPosition();
	FlyTo(boss, x, y, z + BOSS.LIFT_RISE, LIFT_SECONDS, ANIM_LIFTOFF, Climb);
	boss.phase = 2; -- on her way up
	Alert(BOSS.NAME .. " takes to the air!");
end

-- In the air, at her point: to a neighbour, or across with Deep Breath.
local function Move(boss, hx, hy, hz)
	local e = boss.e;
	e.acts = nil;
	if math.random() < BOSS.DEEP_CHANCE then
		boss.deepTo = math.mod(boss.ring + BOSS.RING_POINTS / 2, BOSS.RING_POINTS);
		if StartAct(e, DEEP, hx, hy, hz) then
			return;
		end
	end
	local step = math.random(2) == 1 and 1 or BOSS.RING_POINTS - 1;
	boss.ring = math.mod(boss.ring + step, BOSS.RING_POINTS);
	local rx, ry, rz = RingPoint(boss.ring);
	FlyTo(boss, rx, ry, rz, Distance(boss, rx, ry, rz) / BOSS.FLY_SPEED, ANIM_FLY, Hover);
end

------------------------------------------------------------------------------
-- Deep Breath: she turns to the opposite point and draws breath (its 5 s
-- cast) while the lane under her flight is marked, then flies across at the
-- breath speed. Each mark bursts into burning ground as she passes over it:
-- it burns you (once a breath) and the brood in it.

local areaPool = {}
local areas = {} -- burning ground, until its time ends

local function BurnGround(x, y, z)
	local area = table.remove(areaPool) or { actor = BW.scene:AcquireActor() };
	area.actor:SetModelBySpellArea(DEEP.area, DEEP.width);
	area.actor:SetPosition(x, y, z);
	area.actor:Show();
	area.left = DEEP.areaSeconds;
	tinsert(areas, area);
end

local function UpdateAreas(dt)
	for i = table.getn(areas), 1, -1 do
		local area = areas[i];
		area.left = area.left - dt;
		if area.left <= 0 then
			area.actor:Hide();
			tinsert(areaPool, area);
			table.remove(areas, i);
		end
	end
end

local lane = {} -- the marked points: { x, y, z, d (yards from the start), mark }

local function ClearLane()
	for i = table.getn(lane), 1, -1 do
		if lane[i].mark then
			Unmark({ lane[i].mark });
		end
		lane[i] = nil;
	end
end

local function Breathe(e, point, hx, hy)
	Unmark({ point.mark });
	point.mark = nil;
	BurnGround(point.x, point.y, point.z);
	local r2 = DEEP.width * DEEP.width;
	if not BW.run.boss.breathHit and Dist2(hx, hy, point.x, point.y) <= r2 then
		BW.run.boss.breathHit = true;
		Strike(e, DEEP);
	end
	local damage = BOSS.DEEP_BROOD * RunGrowth();
	for _, o in ipairs(enemies) do
		if o.alive and o ~= e then
			local x, y = o.actor:GetPosition();
			if Dist2(x, y, point.x, point.y) <= r2 then
				Damage(o, damage, BOSS.ALERT_COLOR);
			end
		end
	end
end

ACTS.deep = {
	Start = function(e, act)
		local boss = BW.run.boss;
		local ax, ay = e.actor:GetPosition();
		local bx, by = RingPoint(boss.deepTo);
		local dx, dy = bx - ax, by - ay;
		local length = math.sqrt(dx * dx + dy * dy);
		local bz = FloorAt(bx, by, CENTER[3]);
		local castID = e.actor:PlaySpellVisualAtPoint(act.spell, bx, by, bz, act.cast * 1000);
		if not castID then
			return false;
		end
		e.actor:SetYaw(math.atan2(dy, dx));
		e.actCast = castID;
		for d = act.step / 2, length, act.step do
			local x, y = ax + dx / length * d, ay + dy / length * d;
			local z = FloorAt(x, y, CENTER[3]);
			tinsert(lane, { x = x, y = y, z = z, d = d, mark = Mark(x, y, z, act.width) });
		end
		boss.laneX, boss.laneY, boss.laneNext, boss.breathHit = ax, ay, 1, nil;
		Alert(BOSS.NAME .. " takes in a deep breath...");
		return true;
	end,
	Go = function(e)
		local boss = BW.run.boss;
		boss.ring = boss.deepTo;
		local rx, ry, rz = RingPoint(boss.ring);
		local seconds = Distance(boss, rx, ry, rz) / BOSS.BREATH_SPEED;
		FlyTo(boss, rx, ry, rz, seconds, ANIM_FLY, Hover);
		e.acts = nil; -- Hover gives them back when she arrives
		return seconds + 1;
	end,
	Charge = function(e, _, _, hx, hy)
		local boss = BW.run.boss;
		local x, y = e.actor:GetPosition();
		local flown = math.sqrt(Dist2(x, y, boss.laneX, boss.laneY));
		while lane[boss.laneNext] and (lane[boss.laneNext].d <= flown or not boss.fly) do
			Breathe(e, lane[boss.laneNext], hx, hy);
			boss.laneNext = boss.laneNext + 1;
		end
		return not lane[boss.laneNext] or e.actLeft <= 0;
	end,
	Stop = function()
		ClearLane();
	end,
}

------------------------------------------------------------------------------
-- Bellowing Roar: you flee from her for `fear` seconds (its Fear state on
-- you; Steer runs you away), and `vents` of the lair's cracks are marked to
-- erupt `warn` seconds after it, time to get clear once the fear ends.

local function FearHero(e, act)
	local run = BW.run;
	if run.graceUntil then
		return;
	end
	local hx, hy = BW.hero:GetPosition();
	local x, y = e.actor:GetPosition();
	if not run.fearUntil then
		BW.hero:AddAuraVisual(act.spell);
	end
	run.fearUntil = run.time + act.fear;
	run.fearYaw = math.atan2(hy - y, hx - x);
	run.fearSpell = act.spell;
end

local function EndFear()
	local run = BW.run;
	if run.fearUntil then
		run.fearUntil = nil;
		BW.hero:RemoveAuraVisual(run.fearSpell);
		-- The hero still runs: Steer's next Drive stops it, or turns it the
		-- way the keys held say.
	end
end

ACTS.roar = {
	Start = function(e, act)
		local x, y, z = e.actor:GetPosition();
		e.actor:StopMoving();
		local castID = e.actor:PlaySpellVisualAtPoint(act.spell, x, y, z, act.cast * 1000);
		if not castID then
			return false;
		end
		local entry = { marks = {}, act = act };
		entry.onImpact = function(landed)
			FearHero(landed.e, landed.act);
			MarkVents(landed.act.vents, landed.act.warn);
		end;
		Track(e, castID, entry);
		return true;
	end,
}

------------------------------------------------------------------------------
-- Her whelps: from the two caves, a pair at a time, in waves while she is in
-- the air (the first larger); in the last phase one now and then. Black and
-- red, as the brood is.

local function SendWhelp(cave)
	if BW.run.alive >= MAX_ALIVE then
		return;
	end
	local kind = KindByKey(math.random(2) == 1 and "black" or "red");
	Spawn(kind, cave[1], cave[2]);
end

local function UpdateWhelps(boss, dt)
	local t = BW.run.time;
	if boss.phase == 2 and boss.nextWave and t >= boss.nextWave then
		boss.nextWave = boss.nextWave + BOSS.WAVE_EVERY;
		boss.waveLeft = boss.waveLeft + BOSS.WHELPS_WAVE + math.random(0, BOSS.WHELPS_SPREAD);
	end
	if boss.waveLeft > 0 then
		boss.waveGap = boss.waveGap - dt;
		if boss.waveGap <= 0 then
			boss.waveGap = BOSS.WHELP_GAP;
			for _, cave in ipairs(BOSS.CAVES) do
				SendWhelp(cave);
			end
			boss.waveLeft = boss.waveLeft - table.getn(BOSS.CAVES);
		end
	end
	if boss.phase == 3 then
		boss.lateGap = boss.lateGap - dt;
		if boss.lateGap <= 0 then
			boss.lateGap = BOSS.LATE_WHELP_EVERY + math.random() * BOSS.LATE_SPREAD;
			SendWhelp(BOSS.CAVES[math.random(table.getn(BOSS.CAVES))]);
		end
	end
end

------------------------------------------------------------------------------
-- On the ground she closes to her reach. Standing, she turns to you while
-- you are in front of her; behind her, she leaves you there, for her tail.

local function Chase(boss, dt, hx, hy, hz)
	local e = boss.e;
	boss.repath = boss.repath - dt;
	if boss.repath > 0 or e.actState then
		return;
	end
	boss.repath = BOSS.REPATH;
	local x, y = e.actor:GetPosition();
	local dx, dy = hx - x, hy - y;
	local d = math.sqrt(dx * dx + dy * dy);
	local stand = ONYXIA.reach - 1.5;
	if d > ONYXIA.reach - 1 then
		local k = (d - stand) / d;
		e.actor:MoveTo(x + dx * k, y + dy * k, hz);
	else
		e.actor:StopMoving();
		local yaw = math.atan2(dy, dx);
		local off = math.mod(yaw - e.actor:GetYaw() + 3 * math.pi, 2 * math.pi) - math.pi;
		if math.abs(off) <= math.pi / 2 then
			e.actor:SetYaw(yaw);
		end
	end
end

-- In the air, standing at her point, she faces you.
local function Watch(boss, dt, hx, hy)
	local e = boss.e;
	boss.repath = boss.repath - dt;
	if boss.repath > 0 or e.actState or boss.fly then
		return;
	end
	boss.repath = BOSS.REPATH;
	local x, y = e.actor:GetPosition();
	e.actor:SetYaw(math.atan2(hy - y, hx - x));
end

------------------------------------------------------------------------------
-- Her death: the brood dies with her, nothing hurts you any more, she holds
-- Death's last frame (her model has no Dead), and the run is won.

local VICTORY_AFTER = 1 -- seconds after she lies still

local function Slain(boss)
	local e = boss.e;
	boss.slainAt = BW.run.time;
	e.dying = math.huge; -- her corpse stays (Step would fade it)
	BW.run.graceUntil = math.huge;
	UpdateBar(e);
	EndFear();
	for _, o in ipairs(enemies) do
		if o.alive then
			Kill(o);
		end
	end
	Alert(BOSS.NAME .. " is slain!");
end

local function UpdateDeath(boss)
	local since = BW.run.time - boss.slainAt;
	if not boss.still and since >= ONYXIA.crackSeconds then
		boss.still = true;
		boss.e.actor:SetPaused(true, false);
	end
	if since >= ONYXIA.crackSeconds + VICTORY_AFTER then
		BW.EndRun(true);
	end
end

------------------------------------------------------------------------------
-- The fight's step (Broodwatch.lua's Step, after the brood's acts).

local function UpdateOnyxia(dt, hx, hy, hz)
	UpdateAlert(dt);
	UpdateAreas(dt);
	local boss = BW.run.boss;
	if not boss then
		return;
	end
	if BW.run.fearUntil and BW.run.time >= BW.run.fearUntil then
		EndFear();
	end
	local e = boss.e;
	if boss.slainAt then
		UpdateDeath(boss);
		return;
	end
	if not e.alive then
		Slain(boss);
		return;
	end
	UpdateBar(e);
	UpdateFlight(boss, dt);
	UpdateWhelps(boss, dt);
	local share = e.hp / e.maxHp;
	if boss.phase == 1 then
		if share < BOSS.PHASE_TWO and not e.actState then
			LiftOff(boss);
		else
			Chase(boss, dt, hx, hy, hz);
		end
	elseif boss.phase == 2 then
		if boss.fly or e.actState or not boss.ring then
			return; -- on the way, or busy
		end
		if share < BOSS.PHASE_THREE then
			Descend(boss);
		elseif BW.run.time >= boss.nextMove then
			Move(boss, hx, hy, hz);
		else
			Watch(boss, dt, hx, hy);
		end
	elseif boss.phase == 3 then
		Chase(boss, dt, hx, hy, hz);
	end
end

-- She comes: from the ring's point nearest where she will land, she flies
-- down to it and lands. `share` (a test's) starts her with that share of
-- her health.
local function SummonOnyxia(share)
	local x, y, z = LandingPoint();
	local e = Spawn(ONYXIA, x, y);
	e.actor:SetMovementEnabled(false);
	e.actor:SetNameText(BOSS.NAME, UnitReactionColor[2].r, UnitReactionColor[2].g, UnitReactionColor[2].b);
	e.flying, e.deathless = true, true;
	if share then
		e.hp = math.floor(e.maxHp * share);
	end
	local boss = { e = e, phase = 0, repath = 0, waveLeft = 0, waveGap = 0, lateGap = 0, startTime = BW.run.time };
	BW.run.boss = boss;
	local rx, ry, rz = RingPoint(NearestRing(x, y));
	e.actor:SetPosition(rx, ry, rz);
	z = z + BOSS.LIFT_RISE;
	FlyTo(boss, x, y, z, Distance(boss, x, y, z) / BOSS.FLY_SPEED, ANIM_FLY, Touchdown);
	shownHp = nil;
	ShowBar(e);
	UpdateBar(e);
	Alert(BOSS.NAME .. " descends!");
end

-- A new run: no bar, no burning ground, no lane (ClearFoes takes the marks,
-- ClearEnemies her).
local function ClearOnyxia()
	BW.bossBar:Hide();
	BW.alertText:Hide();
	alertLeft = 0;
	for i = table.getn(areas), 1, -1 do
		areas[i].actor:Hide();
		tinsert(areaPool, areas[i]);
		areas[i] = nil;
	end
	for i = table.getn(lane), 1, -1 do
		lane[i] = nil;
	end
end

-- For the files that load after this one.
BW.SummonOnyxia = SummonOnyxia
BW.UpdateOnyxia = UpdateOnyxia
BW.ClearOnyxia = ClearOnyxia
