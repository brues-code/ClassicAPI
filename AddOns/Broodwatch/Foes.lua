-- Broodwatch foes: what the lair's brood does besides bite. A kind with
-- `acts` (Data.lua) stops, shows on the floor where it will strike, and
-- strikes: a Warder throws fire at a spot, a drake charges along a line,
-- a red whelp breathes a cone. You see it coming; you step out of it.
-- Onyxia (Onyxia.lua) uses the same acts, and adds her own.

local _, BW = ...
local PASSIVES = BW.PASSIVES
local CENTER = BW.CENTER
local ROOM_RADIUS = BW.ROOM_RADIUS
local enemies = BW.enemies
local Dist2 = BW.Dist2
local FloorAt = BW.FloorAt
local ApplySpeed = BW.ApplySpeed
local BiteShare = BW.BiteShare
local Taunted = BW.Taunted
local MaxHp = BW.MaxHp

------------------------------------------------------------------------------
-- Marks: empty actors that show a red reticle (an actor with no model shows
-- its reticle). A strike's marks go when it lands, or when it is broken off.

local markPool = {}
local marks = {} -- the marks shown, for the end of a run

local function Mark(x, y, z, radius)
	local mark = table.remove(markPool) or { actor = BW.scene:AcquireActor() };
	mark.actor:SetPosition(x, y, FloorAt(x, y, z));
	mark.actor:SetGroundReticle(radius, false);
	mark.actor:Show();
	marks[mark] = true;
	return mark;
end

local function Unmark(list)
	for i = table.getn(list), 1, -1 do
		local mark = list[i];
		list[i] = nil;
		if marks[mark] then
			marks[mark] = nil;
			mark.actor:ClearGroundReticle();
			mark.actor:Hide();
			tinsert(markPool, mark);
		end
	end
end

------------------------------------------------------------------------------
-- The hero is struck by an act, of the act's school (Core.lua's HurtHero):
-- `share` of your maximum health (Onyxia's blows), or `hit` seconds of the
-- striker's bite, softened as a bite is (Demoralizing Shout's weakness).
-- Devotion softens both. `breath`: the act is a dragon's breath.

local function Strike(e, act)
	local devotion = 1 - PASSIVES.devotion.per * (BW.run.passives.devotion or 0) / 100;
	local amount;
	if act.share then
		amount = MaxHp() * act.share;
	else
		amount = e.dps * act.hit * BiteShare(e);
	end
	BW.HurtHero(amount * devotion, act.school, act.breath);
end

-- Throws the hero, as a knockback spell does (not in the grace after the
-- cards, nor while a Limited Invulnerability Potion lasts). The hero stops
-- when it lands, so Steer's next move is sent again (a move given in the air
-- takes effect on landing).
local function KnockHero(fromX, fromY, knock)
	if BW.run.graceUntil or BW.run.invulnUntil then
		return;
	end
	if BW.hero:KnockBack(fromX, fromY, knock[1], knock[2]) then
		BW.moveFwd, BW.moveSide = 0, 0;
	end
end

-- Whether the enemy can't act: held, fleeing, a sheep, drawn off by a
-- taunt, or thrown (a knockback stops what it prepares).
local function Unable(e)
	local t = BW.run.time;
	return (e.stunUntil and t < e.stunUntil) or e.fearUntil or e.sheepUntil or Taunted(e) or e.actor:IsFalling();
end

------------------------------------------------------------------------------
-- The acts. Each has Start(e, act, hx, hy, hz) (the wind-up: it shows its
-- marks), and may have Go(e, act) and Charge(e, act, dt, hx, hy) (a move
-- after the wind-up, until Charge returns true; Go returns the most it
-- takes), and Stop(e, act) (clears what Start or Go left, when the act ends
-- or is broken off). A strike that comes with a spell's visual lands at the
-- visual's impact (BW.OnImpact runs the in-flight entry's onImpact). The
-- visuals are cast at a point: no actor is hit, so no actor plays an impact
-- animation.

local acts = {}

local function Face(e, x, y)
	local ex, ey = e.actor:GetPosition();
	local yaw = math.atan2(y - ey, x - ex);
	e.actor:StopMoving();
	e.actor:SetYaw(yaw);
	return yaw, ex, ey;
end

-- Registers a visual's flight: `entry.onImpact(entry)` runs at its impact,
-- and its marks clear by `entry.expires` if the impact is lost.
local function Track(e, castID, entry)
	entry.expires = BW.run.time + entry.act.cast + 3;
	entry.e = e;
	BW.run.inFlight[castID] = entry;
	e.actCast = castID;
end

-- Fire thrown at the spot where you stand: the spot is marked through the
-- cast and the missile's flight, and burns whoever is on it at the impact.
acts.bolt = {
	Start = function(e, act, hx, hy, hz)
		Face(e, hx, hy);
		local z = FloorAt(hx, hy, hz);
		local castID = e.actor:PlaySpellVisualAtPoint(act.spell, hx, hy, z, act.cast * 1000);
		if not castID then
			return false;
		end
		local entry = { marks = { Mark(hx, hy, z, act.radius) }, x = hx, y = hy, act = act };
		entry.onImpact = function(landed)
			Unmark(landed.marks);
			local px, py = BW.hero:GetPosition();
			if Dist2(px, py, landed.x, landed.y) <= landed.act.radius * landed.act.radius then
				Strike(landed.e, landed.act);
			end
		end;
		Track(e, castID, entry);
		return true;
	end,
}

-- Whether (px, py) is within `length` of (x, y) and `arc` of `yaw`.
local function InCone(px, py, x, y, yaw, length, arc)
	local dx, dy = px - x, py - y;
	if dx * dx + dy * dy > length * length then
		return false;
	end
	local off = math.mod(math.atan2(dy, dx) - yaw + 3 * math.pi, 2 * math.pi) - math.pi;
	return math.abs(off) <= arc / 2;
end

-- A cone `length` long and `arc` wide: ahead, at you (a breath), or with
-- `back`, behind as the striker faces (a tail's sweep, which does not turn).
-- Three marks along it, each as wide as the cone where it lies. A strike
-- with `knock` throws you away from the striker.
acts.cone = {
	Start = function(e, act, hx, hy, hz)
		local yaw, ex, ey;
		if act.back then
			ex, ey = e.actor:GetPosition();
			e.actor:StopMoving();
			yaw = e.actor:GetYaw() + math.pi;
		else
			yaw, ex, ey = Face(e, hx, hy);
		end
		local fx, fy = ex + act.length * math.cos(yaw), ey + act.length * math.sin(yaw);
		local castID = e.actor:PlaySpellVisualAtPoint(act.spell, fx, fy, hz, act.cast * 1000);
		if not castID then
			return false;
		end
		local entry = { marks = {}, x = ex, y = ey, yaw = yaw, act = act };
		local half = math.tan(math.min(act.arc, 2.6) / 2); -- a wide arc's marks stay on the floor near it
		for k = 1, 3 do
			local d = act.length * (k - 0.5) / 3;
			tinsert(entry.marks, Mark(ex + d * math.cos(yaw), ey + d * math.sin(yaw), hz, d * half));
		end
		entry.onImpact = function(landed)
			Unmark(landed.marks);
			local px, py = BW.hero:GetPosition();
			local a = landed.act;
			if InCone(px, py, landed.x, landed.y, landed.yaw, a.length, a.arc) then
				Strike(landed.e, a);
				if a.knock then
					KnockHero(landed.x, landed.y, a.knock);
				end
			end
		end;
		Track(e, castID, entry);
		return true;
	end,
}

-- A blow all around the striker, `radius` wide (a wing's buffet): one mark
-- under it. A strike throws you away by `knock`.
acts.nova = {
	Start = function(e, act, hx, hy, hz)
		local _, ex, ey = Face(e, hx, hy);
		local _, _, ez = e.actor:GetPosition();
		local castID = e.actor:PlaySpellVisualAtPoint(act.spell, ex, ey, ez, act.cast * 1000);
		if not castID then
			return false;
		end
		local entry = { marks = { Mark(ex, ey, ez, act.radius) }, x = ex, y = ey, act = act };
		entry.onImpact = function(landed)
			Unmark(landed.marks);
			local px, py = BW.hero:GetPosition();
			if Dist2(px, py, landed.x, landed.y) <= landed.act.radius * landed.act.radius then
				Strike(landed.e, landed.act);
				KnockHero(landed.x, landed.y, landed.act.knock);
			end
		end;
		Track(e, castID, entry);
		return true;
	end,
}

-- A charge along a line through where you stand, `past` yards beyond you
-- (short of the lair's wall: a move goes through walls): marks every
-- MARK_STEP yards show it. After the wind-up it runs the line at
-- `speed`, strikes and throws you once if it passes within `width`, and
-- stands dazed for `recover` at the end.
local MARK_STEP = 2.5
local CHARGE_ROOM = ROOM_RADIUS + 4 -- yards from CENTER a charge may end

local function InRoom(x, y)
	return Dist2(x, y, CENTER[1], CENTER[2]) <= CHARGE_ROOM * CHARGE_ROOM;
end

acts.charge = {
	Start = function(e, act, hx, hy, hz)
		local yaw, ex, ey = Face(e, hx, hy);
		local length = math.min(math.sqrt(Dist2(ex, ey, hx, hy)) + act.past, act.range + act.past);
		while length > MARK_STEP and not InRoom(ex + length * math.cos(yaw), ey + length * math.sin(yaw)) do
			length = length - 1;
		end
		e.actX, e.actY, e.actZ = ex + length * math.cos(yaw), ey + length * math.sin(yaw), hz;
		e.actRun = length / act.speed + 0.5; -- the most the run takes (a wall can hold it)
		e.actMarks = e.actMarks or {};
		for d = MARK_STEP, length, MARK_STEP do
			tinsert(e.actMarks, Mark(ex + d * math.cos(yaw), ey + d * math.sin(yaw), hz, act.width));
		end
		return true;
	end,
	Go = function(e, act)
		e.actor:SetMovementSpeed("run", act.speed);
		e.actor:MoveTo(e.actX, e.actY, e.actZ);
		e.actor:PlaySpellVisual(act.spell, BW.hero, 0);
		e.actHit = nil;
		return e.actRun;
	end,
	Charge = function(e, act, dt, hx, hy)
		local x, y = e.actor:GetPosition();
		if not e.actHit and Dist2(x, y, hx, hy) <= act.width * act.width then
			e.actHit = true;
			Strike(e, act);
			KnockHero(x, y, act.knock);
		end
		return Dist2(x, y, e.actX, e.actY) <= 1 or e.actLeft <= 0;
	end,
	-- A charge broken off stops where it is, at its own speed again.
	Stop = function(e)
		ApplySpeed(e);
		e.actor:StopMoving();
	end,
}

------------------------------------------------------------------------------
-- An act's states on the enemy: e.actState is "wind" (it stands and shows
-- its marks, `cast` seconds), "go" (a move after it), then "rest"
-- (`recover` seconds; a thrown spell is out of its hands by then) and nil.
-- e.act is the act under way. Each act has its own timer, e.actReady[act]:
-- the run time it may start again, `every` seconds after it ends (plus up to
-- `spread` more at random). A hold, a fear, a sheep, a taunt or a throw
-- breaks a wind-up or a move off; what is already thrown still lands.
--
-- The acts an enemy picks from are e.acts (Onyxia's change with her phase),
-- else its kind's; the first that is ready, and that you are in range of,
-- starts.

local function Next(e, act, share)
	e.actReady[act] = BW.run.time + (act.every + math.random() * (act.spread or 0)) * share;
end

local function EndMove(e)
	local stop = acts[e.act.kind].Stop;
	if stop then
		stop(e, e.act);
	end
end

local function Break(e)
	local act = e.act;
	if e.actState == "wind" and e.actCast then
		e.actor:StopSpellVisual(e.actCast);
		local entry = BW.run.inFlight[e.actCast];
		BW.run.inFlight[e.actCast] = nil;
		if entry then
			Unmark(entry.marks);
		end
	end
	if e.actState ~= "rest" then
		EndMove(e); -- what the act's Start or Go left (a charge's speed, a Deep Breath's lane)
	end
	if e.actMarks then
		Unmark(e.actMarks);
	end
	e.actState, e.actCast, e.act = nil, nil, nil;
	Next(e, act, 0.5);
end

-- Whether the act may start: you are within its `range` (no closer than
-- `near`), and for an act with a `side`, on that side of the striker as it
-- faces ("front" or "back", half the circle each).
local function InRange(e, act, hx, hy)
	local x, y = e.actor:GetPosition();
	local d2 = Dist2(x, y, hx, hy);
	if d2 > act.range * act.range or (act.near and d2 < act.near * act.near) then
		return false;
	end
	if act.side then
		local off = math.mod(math.atan2(hy - y, hx - x) - e.actor:GetYaw() + 3 * math.pi, 2 * math.pi) - math.pi;
		return (math.abs(off) <= math.pi / 2) == (act.side == "front");
	end
	return true;
end

-- Starts an act now, whatever its timer (Onyxia's Deep Breath, from her
-- flight). Returns whether it started.
local function StartAct(e, act, hx, hy, hz)
	if not acts[act.kind].Start(e, act, hx, hy, hz) then
		return false;
	end
	e.act, e.actState, e.actLeft = act, "wind", act.cast;
	return true;
end

-- The first ready act in range starts. A fresh enemy waits a while for
-- each act (`first` seconds, else up to 1.5 times its `every`), not in step
-- with the rest.
local function TryStart(e, list, hx, hy, hz)
	local t = BW.run.time;
	for _, act in ipairs(list) do
		local ready = e.actReady[act];
		if not ready then
			if act.first then
				e.actReady[act] = t + act.first;
			else
				Next(e, act, 0.5 + math.random());
			end
		elseif t >= ready and InRange(e, act, hx, hy) and StartAct(e, act, hx, hy, hz) then
			return;
		end
	end
end

local function UpdateFoe(e, dt, hx, hy, hz)
	local state = e.actState;
	if not e.alive or Unable(e) then
		if state then
			Break(e);
		end
		return;
	end
	if not state then
		local list = e.acts or e.kind.acts;
		if list then
			e.actReady = e.actReady or {};
			TryStart(e, list, hx, hy, hz);
		end
		return;
	end
	local act = e.act;
	e.actLeft = e.actLeft - dt;
	if state == "wind" then
		if e.actLeft <= 0 then
			if acts[act.kind].Go then
				e.actState, e.actLeft = "go", acts[act.kind].Go(e, act);
			else
				e.actState, e.actLeft, e.actCast = "rest", act.recover, nil;
			end
		end
	elseif state == "go" then
		if acts[act.kind].Charge(e, act, dt, hx, hy) then
			EndMove(e);
			if e.actMarks then
				Unmark(e.actMarks);
			end
			e.actState, e.actLeft = "rest", act.recover;
		end
	elseif e.actLeft <= 0 then
		e.actState, e.act = nil, nil;
		Next(e, act, 1);
	end
end

-- Thrown fire and breath that never landed (its impact lost) clear their
-- marks in time.
local function Expire()
	for castID, entry in pairs(BW.run.inFlight) do
		if entry.expires and BW.run.time >= entry.expires then
			BW.run.inFlight[castID] = nil; -- clearing a key during `pairs` is allowed
			Unmark(entry.marks);
		end
	end
end

local function UpdateFoes(dt, hx, hy, hz)
	for _, e in ipairs(enemies) do
		if (e.alive and (e.acts or e.kind.acts)) or e.actState then
			UpdateFoe(e, dt, hx, hy, hz);
		end
	end
	Expire();
end

-- Whether the enemy is busy with an act (Chase leaves it be). Onyxia always
-- is: she moves herself (Onyxia.lua).
local function Acting(e)
	return e.actState ~= nil or e.kind.boss;
end

-- A fresh enemy from the pool: no act under way, every act's timer fresh.
local function ResetActs(e)
	e.actState, e.act, e.acts = nil, nil, nil;
	if e.actReady then
		for act in pairs(e.actReady) do
			e.actReady[act] = nil;
		end
	end
end

-- Where a kind that keeps its distance (`keep`) goes: the point `keep` yards
-- from the hero on its side, kept in the room.
local function KeepPoint(e, hx, hy)
	local x, y = e.actor:GetPosition();
	local dx, dy = x - hx, y - hy;
	local d = math.sqrt(dx * dx + dy * dy);
	if d < 0.1 then
		dx, dy, d = math.cos(e.angle), math.sin(e.angle), 1;
	end
	local keep = e.kind.keep;
	local px, py = hx + dx / d * keep, hy + dy / d * keep;
	local cx, cy = px - CENTER[1], py - CENTER[2];
	local c = math.sqrt(cx * cx + cy * cy);
	if c > ROOM_RADIUS then
		px, py = CENTER[1] + cx / c * ROOM_RADIUS, CENTER[2] + cy / c * ROOM_RADIUS;
	end
	return px, py;
end

local function ClearFoes()
	for mark in pairs(marks) do
		marks[mark] = nil;
		mark.actor:ClearGroundReticle();
		mark.actor:Hide();
		tinsert(markPool, mark);
	end
end

-- For the files that load after this one.
BW.ACTS = acts
BW.Mark = Mark
BW.Unmark = Unmark
BW.Strike = Strike
BW.KnockHero = KnockHero
BW.Face = Face
BW.Track = Track
BW.Break = Break
BW.StartAct = StartAct
BW.UpdateFoes = UpdateFoes
BW.Acting = Acting
BW.ResetActs = ResetActs
BW.KeepPoint = KeepPoint
BW.ClearFoes = ClearFoes
