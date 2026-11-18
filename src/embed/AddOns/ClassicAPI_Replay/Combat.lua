-- ClassicAPI Replay: the log's events: the feed of its lines, and the casts,
-- auras, swings and deaths they play on the actors.

local _, ns = ...;

-- How long the dying animation plays before the corpse lies still, in real
-- seconds.
local DYING_TIME = 1.5;
-- The feed: how many seconds of lines a jump in time fills it with, and the
-- colors of its lines and heals (a death's is ns.FEED_DEATH_COLOR).
local FEED_SECONDS = 12;
local FEED_TEXT_COLOR = { 1, 1, 1 };
local FEED_HEAL_COLOR = { 0.2, 1, 0.2 };
-- A raid boss emote, in the chat frame's color for one once the chat colors
-- have loaded, else its default (4.3.4 ChatFrame.cpp: FF DD 00).
local FEED_EMOTE_COLOR = { 1, 0.87, 0 };
-- The client's combat log words for a missed swing and a missed spell, by
-- miss type (GlobalStrings).
local SWING_MISS_TEXT = {
	MISS = MISSEDOTHEROTHER, DODGE = VSDODGEOTHEROTHER, PARRY = VSPARRYOTHEROTHER,
	BLOCK = VSBLOCKOTHEROTHER, ABSORB = VSABSORBOTHEROTHER, EVADE = VSEVADEOTHEROTHER,
	IMMUNE = VSIMMUNEOTHEROTHER,
};
local SPELL_MISS_TEXT = {
	MISS = SPELLMISSOTHEROTHER, DODGE = SPELLDODGEDOTHEROTHER, PARRY = SPELLPARRIEDOTHEROTHER,
	BLOCK = SPELLBLOCKEDOTHEROTHER, RESIST = SPELLRESISTOTHEROTHER, ABSORB = SPELLLOGABSORBOTHEROTHER,
	EVADE = SPELLEVADEDOTHEROTHER,
};

------------------------------------------------------------------------------
-- Combat

-- The shown actor of a unit index, or nil.
local function ShownActor(index)
	local u = index and ns.units[index];
	local actor = u and u.actor;
	return actor and actor:IsShown() and actor or nil, u;
end

-- A unit in a feed line: a link that follows it, a player's in its class
-- color.
local function UnitLink(index)
	local u = index and ns.units[index];
	if not u then
		return UNKNOWN;
	end
	local link = "|Hreplayunit:" .. index .. "|h" .. (u.name ~= "" and u.name or UNKNOWN) .. "|h";
	if not u.isPlayer then
		return link;
	end
	local color = u.nameColor;
	return string.format("|cff%02x%02x%02x%s|r", math.floor(color[1] * 255), math.floor(color[2] * 255),
		math.floor(color[3] * 255), link);
end

-- A spell in a feed line: its spell link, or for a spell this client does
-- not have, its name in the log.
local function SpellLink(spellID, logName)
	local link = spellID and GetSpellLink(spellID);
	if link then
		return link;
	end
	return logName and logName ~= "" and logName or UNKNOWN;
end

-- The event in the client's combat log words, and its color; nil for one
-- the feed leaves out.
local function FeedText(e)
	local kind = e.event;
	local source, dest = UnitLink(e.sourceIndex), UnitLink(e.destIndex);
	local spell = SpellLink(e.spellID, e.spellName);
	if kind == "SWING_DAMAGE" then
		return string.format(e.critical and COMBATHITCRITOTHEROTHER or COMBATHITOTHEROTHER, source, dest, e.amount);
	elseif kind == "SPELL_DAMAGE" or kind == "SPELL_PERIODIC_DAMAGE" or kind == "RANGE_DAMAGE"
			or kind == "DAMAGE_SHIELD" then
		return string.format(e.critical and SPELLLOGCRITOTHEROTHER or SPELLLOGOTHEROTHER, source,
			spell, dest, e.amount);
	elseif kind == "SPELL_HEAL" or kind == "SPELL_PERIODIC_HEAL" then
		return string.format(e.critical and HEALEDCRITOTHEROTHER or HEALEDOTHEROTHER, source,
			spell, dest, e.amount), FEED_HEAL_COLOR;
	elseif kind == "SWING_MISSED" then
		local text = SWING_MISS_TEXT[e.missType];
		return text and string.format(text, source, dest);
	elseif kind == "SPELL_MISSED" or kind == "SPELL_PERIODIC_MISSED" or kind == "RANGE_MISSED" then
		if e.missType == "IMMUNE" then
			return string.format(SPELLIMMUNEOTHEROTHER, source, spell, dest);
		end
		local text = SPELL_MISS_TEXT[e.missType];
		return text and string.format(text, source, spell, dest);
	elseif kind == "SPELL_CAST_START" then
		return string.format(SPELLCASTOTHERSTART, source, spell);
	elseif kind == "SPELL_CAST_SUCCESS" then
		if e.destIndex and e.destIndex ~= e.sourceIndex then
			return string.format(SPELLCASTGOOTHERTARGETTED, source, spell, dest);
		end
		return string.format(SPELLCASTGOOTHER, source, spell);
	elseif kind == "SPELL_AURA_APPLIED" then
		return string.format(e.auraType == "DEBUFF" and AURAADDEDOTHERHARMFUL or AURAADDEDOTHERHELPFUL, dest,
			spell);
	elseif kind == "SPELL_AURA_REMOVED" then
		return string.format(AURAREMOVEDOTHER, spell, dest);
	elseif kind == "SPELL_INTERRUPT" then
		return string.format(SPELLINTERRUPTOTHEROTHER, source, dest, SpellLink(e.extraSpellID, e.extraSpellName));
	elseif kind == "UNIT_DIED" then
		return string.format(UNITDIESOTHER, dest), ns.FEED_DEATH_COLOR;
	elseif kind == "EMOTE" then
		local info = ChatTypeInfo.RAID_BOSS_EMOTE;
		return e.text, info and info.r and { info.r, info.g, info.b } or FEED_EMOTE_COLOR;
	end
end

-- A feed line starts with its event's time, a link that goes there.
local function AddFeedLine(e)
	local text, color = FeedText(e);
	if text then
		color = color or FEED_TEXT_COLOR;
		ns.feed:AddMessage("|Hreplay:" .. e.time .. "|h[" .. ns.FormatTime(e.time) .. "]|h " .. text, color[1], color[2],
			color[3]);
	end
end

-- One log event, as time passes it.
local function PlayEvent(e)
	AddFeedLine(e);
	local source, u = ShownActor(e.sourceIndex);
	local target, du = ShownActor(e.destIndex);
	local kind = e.event;
	if kind == "SPELL_CAST_START" then
		-- The preparation lasts as long as the log's cast did, at the play
		-- speed. One that failed or was interrupted prepares for its whole
		-- cast time and stops when the cast stopped, before it goes off.
		if source then
			u.playedCastStart = e.time;
			-- The missile flies to the success's destination.
			target = ShownActor(e.castTargetIndex);
			local castMs = e.castTime and e.castTime * 1000 / ns.PLAY_SPEEDS[ns.playSpeed];
			if e.castFailed then
				-- Its due end, when the logs give one past the stop; else the
				-- spell's own cast time (nil), which runs past the stop too.
				local _, _, endTime = C_CombatLogReplay.GetUnitCast(e.sourceIndex, e.time);
				castMs = endTime and endTime > e.time + e.castTime
					and (endTime - e.time) * 1000 / ns.PLAY_SPEEDS[ns.playSpeed] or nil;
			end
			local castID = source:PlaySpellVisual(e.spellID, target, castMs);
			if castID then
				u.casts = u.casts or {};
				u.casts[e.spellID] = castID;
				if e.castFailed then
					u.stopCastID, u.stopCastAt = castID, e.time + e.castTime;
				end
			end
		end
	elseif kind == "SPELL_CAST_SUCCESS" or kind == "SPELL_CAST_FAILED" then
		local castID = u and u.casts and u.casts[e.spellID];
		if castID then
			u.casts[e.spellID] = nil;
		end
		if kind == "SPELL_CAST_FAILED" then
			if source and castID then
				source:StopSpellVisual(castID);
			end
		elseif source and not e.endsCast then
			-- A channel this begins channels until the log's channel stopped.
			local channelMs;
			local spellID, startTime, _, stopTime, isChannel = C_CombatLogReplay.GetUnitCast(e.sourceIndex, e.time);
			if isChannel and spellID == e.spellID and startTime == e.time then
				u.playedCastStart = e.time;
				channelMs = stopTime and (stopTime - startTime) * 1000 / ns.PLAY_SPEEDS[ns.playSpeed];
			end
			source:PlaySpellVisual(e.spellID, target, 0, channelMs);
		end
	elseif kind == "RANGE_DAMAGE" or kind == "RANGE_MISSED" then
		if source then
			source:PlaySpellVisual(e.spellID, target, 0);
		end
	elseif kind == "SPELL_AURA_APPLIED" or kind == "SPELL_AURA_REFRESH" then
		if target then
			target:AddAuraVisual(e.spellID);
		end
	elseif kind == "SPELL_AURA_REMOVED" then
		if target then
			target:RemoveAuraVisual(e.spellID);
		end
	elseif kind == "SWING_DAMAGE" or kind == "SWING_MISSED" then
		-- The attacker swings, and the victim reacts when the swing strikes.
		-- Without an attacker in view, a hit shows at once, as the client
		-- shows a swing from a unit it does not know.
		local result = kind == "SWING_DAMAGE" and "HIT" or e.missType;
		if source then
			source:PlaySwing(target, result, e.critical, e.offHand);
		elseif target and result == "HIT" then
			target:PlayWoundAnimation(e.critical);
		end
	elseif kind == "UNIT_DIED" then
		if du then
			du.dyingUntil = GetTime() + DYING_TIME / ns.PLAY_SPEEDS[ns.playSpeed];
		end
	end
end

-- Each unit's combat state as it was at `time`: no cast or swing under way,
-- its auras then; a dead unit lies still.
local function SetCombatState(time)
	-- The feed holds the lines just before then.
	ns.feed:Clear();
	for _, e in ipairs(C_CombatLogReplay.GetEvents(math.max(0, time - FEED_SECONDS), time)) do
		AddFeedLine(e);
	end
	for i = 1, #ns.units do
		local u = ns.units[i];
		u.casts, u.dyingUntil, u.stopCastID, u.stopCastAt = nil, nil, nil, nil;
		if u.actor then
			u.actor:StopSpellVisual();
			u.actor:RemoveAuraVisual();
			for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(i, time)) do
				u.actor:AddAuraVisual(aura.spellID);
			end
		end
	end
end

-- An interrupted cast's visual stops when the cast stopped.
local function StopInterrupted(time)
	for i = 1, #ns.units do
		local u = ns.units[i];
		if u.stopCastAt and time >= u.stopCastAt then
			if u.actor then
				u.actor:StopSpellVisual(u.stopCastID);
			end
			u.stopCastID, u.stopCastAt = nil, nil;
		end
	end
end

-- The casts the combat log leaves out (Opening, a creature's channel with no
-- success line), from the movement file: each one's visual as it begins, at
-- the unit it targeted.
local function PlayUnloggedCasts(from, to)
	local GetUnitCast = C_CombatLogReplay.GetUnitCast;
	local speed = ns.PLAY_SPEEDS[ns.playSpeed];
	for i = 1, #ns.units do
		local source, u = ShownActor(i);
		if source then
			local spellID, startTime, endTime, stopTime, isChannel, interrupted = GetUnitCast(i, to);
			if spellID and startTime > from and u.playedCastStart ~= startTime then
				u.playedCastStart = startTime;
				local target = ShownActor((C_CombatLogReplay.GetUnitState(i, startTime)));
				if isChannel then
					source:PlaySpellVisual(spellID, target, 0, stopTime and (stopTime - startTime) * 1000 / speed);
				else
					local castID = source:PlaySpellVisual(spellID, target, (endTime - startTime) * 1000 / speed);
					if castID and interrupted then
						u.stopCastID, u.stopCastAt = castID, stopTime;
					end
				end
			end
		end
	end
end

-- Brings the combat state to `time`: the events since the last time when
-- time went a short way forward, else the state as it was then.
local function Combat(time)
	if ns.combatTime and time >= ns.combatTime and time - ns.combatTime <= ns.MAX_PLAYED_STEP then
		if time > ns.combatTime then
			for _, e in ipairs(C_CombatLogReplay.GetEvents(ns.combatTime, time)) do
				PlayEvent(e);
			end
			PlayUnloggedCasts(ns.combatTime, time);
			StopInterrupted(time);
		end
	else
		SetCombatState(time);
	end
	ns.combatTime = time;
end

-- Puts every unit where it was at `time`.
local function Apply(time)
	local GetUnitPosition = C_CombatLogReplay.GetUnitPosition;
	local GetUnitLook = C_CombatLogReplay.GetUnitLook;
	for i = 1, #ns.units do
		local u = ns.units[i];
		local x, y, z, facing, speed, falling, jumping = GetUnitPosition(i, time);
		local display, nativeDisplay, mountDisplay, scale, standState, sheathState, isDead, _;
		local held1, held2, held3;
		if x then
			display, nativeDisplay, mountDisplay, scale, standState, _, _, sheathState, isDead, held1, held2,
				held3 = GetUnitLook(i, time);
		end
		u.inView, u.dead, u.display, u.nativeDisplay = display and display > 0, isDead, display, nativeDisplay;
		u.mark = C_CombatLogReplay.GetUnitRaidTargetIndex(i, time);
		if u.inView then
			ns.ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
				mountDisplay, scale, standState, sheathState, isDead, held1, held2, held3);
			ns.ShowBars(u, time, isDead);
		else
			ns.HideUnit(u);
		end
	end
	ns.ApplyObjects(time);
	ns.ApplyAreas(time);
	Combat(time);
end
ns.Apply = Apply;
