-- Broodwatch foes: what the lair's brood does besides bite. A kind with an
-- `act` (Data.lua) stops, shows on the floor where it will strike, and
-- strikes: a Warder throws fire at a spot, a drake charges along a line,
-- a red whelp breathes a cone. You see it coming; you step out of it.

local _, BW = ...
local TUNING = BW.TUNING
local PASSIVES = BW.PASSIVES
local CENTER = BW.CENTER
local ROOM_RADIUS = BW.ROOM_RADIUS
local enemies = BW.enemies
local Dist2 = BW.Dist2
local FloorAt = BW.FloorAt
local ApplySpeed = BW.ApplySpeed
local BiteShare = BW.BiteShare
local Taunted = BW.Taunted

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
-- The hero is struck: `act.hit` seconds of the striker's bite, softened as a
-- bite is (Demoralizing Shout's weakness, Devotion). It goes with the bites
-- of the step (through a shield, and not at all in the grace).

local function Strike(e, act)
	local devotion = 1 - PASSIVES.devotion.per * (BW.run.passives.devotion or 0) / 100;
	BW.run.pendingHurt = BW.run.pendingHurt + e.dps * act.hit * BiteShare(e) * devotion;
end

-- Throws the hero, as a knockback spell does (not in the grace after the
-- cards). The hero stops when it lands, so Steer's next move is sent again
-- (a move given in the air takes effect on landing).
local function KnockHero(fromX, fromY, knock)
	if BW.run.graceUntil then
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
-- marks), and may have Charge(e, act, dt, hx, hy) (a move after the wind-up,
-- until it returns true). A strike that comes with a spell's visual lands at
-- the visual's impact (BW.OnImpact runs the in-flight entry's onImpact).

local acts = {}

local function Face(e, x, y)
	local ex, ey = e.actor:GetPosition();
	local yaw = math.atan2(y - ey, x - ex);
	e.actor:StopMoving();
	e.actor:SetYaw(yaw);
	return yaw, ex, ey;
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
		local entry = { marks = { Mark(hx, hy, z, act.radius) }, x = hx, y = hy, e = e, act = act,
			expires = BW.run.time + act.cast + 3 };
		entry.onImpact = function(landed)
			Unmark(landed.marks);
			local px, py = BW.hero:GetPosition();
			if Dist2(px, py, landed.x, landed.y) <= landed.act.radius * landed.act.radius then
				Strike(landed.e, landed.act);
			end
		end;
		BW.run.inFlight[castID] = entry;
		e.actCast = castID;
		return true;
	end,
}

-- A breath of fire in a cone ahead, `length` long and `arc` wide; three
-- marks along it, each as wide as the cone where it lies.
acts.breath = {
	Start = function(e, act, hx, hy, hz)
		local yaw, ex, ey = Face(e, hx, hy);
		local fx, fy = ex + act.length * math.cos(yaw), ey + act.length * math.sin(yaw);
		local castID = e.actor:PlaySpellVisualAtPoint(act.spell, fx, fy, hz, act.cast * 1000);
		if not castID then
			return false;
		end
		local entry = { marks = {}, x = ex, y = ey, yaw = yaw, e = e, act = act, expires = BW.run.time + act.cast + 3 };
		local half = math.tan(act.arc / 2);
		for k = 1, 3 do
			local d = act.length * (k - 0.5) / 3;
			tinsert(entry.marks, Mark(ex + d * math.cos(yaw), ey + d * math.sin(yaw), hz, d * half));
		end
		entry.onImpact = function(landed)
			Unmark(landed.marks);
			local px, py = BW.hero:GetPosition();
			local dx, dy = px - landed.x, py - landed.y;
			if dx * dx + dy * dy <= landed.act.length * landed.act.length then
				local off = math.mod(math.atan2(dy, dx) - landed.yaw + 3 * math.pi, 2 * math.pi) - math.pi;
				if math.abs(off) <= landed.act.arc / 2 then
					Strike(landed.e, landed.act);
				end
			end
		end;
		BW.run.inFlight[castID] = entry;
		e.actCast = castID;
		return true;
	end,
}

-- A charge along a line through where you stand, `past` yards beyond you
-- (short of the lair's wall: a move goes through walls): marks every
-- MARK_STEP yards show it. After the wind-up it runs the line at
-- `speed` (Go returns the most the run takes), strikes and throws you once
-- if it passes within `width`, and stands dazed for `recover` at the end.
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
}

------------------------------------------------------------------------------
-- An act's states on the enemy: e.actState is "wind" (it stands and shows
-- its marks, `cast` seconds), "go" (a charge's run), then "rest" (`recover`
-- seconds; a thrown spell is out of its hands by then) and nil. e.actNext:
-- the run time it may start again. A hold, a fear, a sheep, a taunt or a
-- throw breaks a wind-up or a run off; what is already thrown still lands.

local function Next(e, act, share)
	e.actNext = BW.run.time + act.every * share;
end

local function EndRun(e)
	ApplySpeed(e);
	e.actor:StopMoving();
end

local function Break(e)
	local act = e.kind.act;
	if e.actState == "wind" and e.actCast then
		e.actor:StopSpellVisual(e.actCast);
		local entry = BW.run.inFlight[e.actCast];
		BW.run.inFlight[e.actCast] = nil;
		if entry then
			Unmark(entry.marks);
		end
	elseif e.actState == "go" then
		EndRun(e);
	end
	if e.actMarks then
		Unmark(e.actMarks);
	end
	e.actState, e.actCast = nil, nil;
	Next(e, act, 0.5);
end

-- Whether a fresh enemy of this kind may start: in range of the hero (and,
-- with `near`, not closer than it).
local function InRange(e, act, hx, hy)
	local x, y = e.actor:GetPosition();
	local d2 = Dist2(x, y, hx, hy);
	return d2 <= act.range * act.range and (not act.near or d2 >= act.near * act.near);
end

local function UpdateFoe(e, act, dt, hx, hy, hz)
	local state = e.actState;
	if not e.alive or Unable(e) then
		if state then
			Break(e);
		end
		return;
	end
	if not state then
		if not e.actNext then
			Next(e, act, 0.5 + math.random()); -- a fresh one waits a while, not in step with the rest
		elseif BW.run.time >= e.actNext and InRange(e, act, hx, hy)
			and acts[act.kind].Start(e, act, hx, hy, hz) then
			e.actState, e.actLeft = "wind", act.cast;
		end
		return;
	end
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
			EndRun(e);
			Unmark(e.actMarks);
			e.actState, e.actLeft = "rest", act.recover;
		end
	elseif e.actLeft <= 0 then
		e.actState = nil;
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
		local act = e.kind.act;
		if act and (e.alive or e.actState) then
			UpdateFoe(e, act, dt, hx, hy, hz);
		end
	end
	Expire();
end

-- Whether the enemy is busy with an act (Chase leaves it be).
local function Acting(e)
	return e.actState ~= nil;
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
BW.UpdateFoes = UpdateFoes
BW.Acting = Acting
BW.KeepPoint = KeepPoint
BW.ClearFoes = ClearFoes
