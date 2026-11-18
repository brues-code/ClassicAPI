-- ClassicAPI Replay: time: playing, seeking, the playback bar and its death
-- marks, and the camera's fly keys.

local _, ns = ...;

-- How far the camera tilts up or down, short of straight up or down (where
-- its yaw would flip), in radians.
local MAX_PITCH = 1.45;
-- The free camera: yards a second, and how much faster with Shift. Each key
-- moves along the view (forward), across it (right) or straight up.
local FLY_SPEED, FLY_FAST = 20, 4;
local FLY_KEYS = {
	W = { 1, 0, 0 }, S = { -1, 0, 0 },
	D = { 0, 1, 0 }, A = { 0, -1, 0 },
	SPACE = { 0, 0, 1 }, X = { 0, 0, -1 },
};
-- A player's death on the timeline: its mark, and how many seconds before
-- the death a click on it goes to, to see what killed it.
local DEATH_MARK_WIDTH, DEATH_MARK_HEIGHT = 3, 14;
local DEATH_MARK_COLOR = { 1, 0.15, 0.15, 0.9 };
local DEATH_MARK_LEAD = 5;
-- The playback bar's steps, in seconds: the mouse wheel over the timeline and
-- the Left and Right arrow keys over the window step `small`, `big` with
-- Shift; the back and forward buttons step `big`.
local STEP = { small = 1, big = 10 };
local timelineValue, timeSecond; -- what the timeline and the time text show
local deathMarks = {}; -- the timeline's death marks in use
local freeMarks = {};  -- death marks not in use

------------------------------------------------------------------------------
-- Playback

local function UpdatePlayButton()
	ns.frame.PlayButton:SetText(ns.playing and REPLAY_PAUSE or REPLAY_PLAY);
end
ns.UpdatePlayButton = UpdatePlayButton;

-- Where the timeline's thumb centre sits for a time, from the slider's left
-- edge, and how far it travels: the thumb stays inside the slider, so its
-- centre runs half a thumb in from each end. The span comes from the
-- slider's edges: GetWidth gives its XML size (10), not its anchored width.
local function TimelinePlace(time)
	local timeline = ns.frame.Timeline;
	local left, right = timeline:GetLeft(), timeline:GetRight();
	local span = left and right and right - left or timeline:GetWidth();
	local thumb = timeline:GetThumbTexture():GetWidth();
	local travel = math.max(span - thumb, 1);
	return thumb / 2 + travel * (ns.duration > 0 and time / ns.duration or 0), travel;
end

-- The timeline's thumb moves once it would move a pixel, and the time text
-- once its second changes: setting the slider runs its scripts (about half a
-- millisecond a call), too much for every frame. `force` sets both.
local function UpdateTimeline(force)
	local _, travel = TimelinePlace(0);
	local step = ns.duration / travel;
	if force or not timelineValue or math.abs(ns.now - timelineValue) >= step then
		ns.settingTimeline = true;
		ns.frame.Timeline:SetValue(ns.now);
		ns.settingTimeline = false;
		timelineValue = ns.now;
	end
	local second = math.floor(ns.now);
	if force or second ~= timeSecond then
		ns.frame.Time:SetText(string.format(REPLAY_TIME, ns.FormatTime(ns.now), ns.FormatTime(ns.duration)));
		timeSecond = second;
	end
end

local function SetPlaying(on)
	ns.playing = on;
	ns.scene:SetPaused(not on);
	UpdatePlayButton();
end
ns.SetPlaying = SetPlaying;

-- Moves the camera's target by the fly keys held: along the view, across it
-- level, and straight up. Flying stops following a unit.
local function Fly(elapsed)
	local forward, right, up = 0, 0, 0;
	for key in pairs(ns.flying) do
		local move = FLY_KEYS[key];
		forward, right, up = forward + move[1], right + move[2], up + move[3];
	end
	if forward == 0 and right == 0 and up == 0 then
		return;
	end
	ns.SetFocus(nil);
	local step = FLY_SPEED * elapsed * (IsShiftKeyDown() and FLY_FAST or 1);
	local fx, fy, fz = ns.camera:GetForwardVector();
	-- Right of the view, level: forward x up (the map's y grows to the left).
	local rx, ry = fy, -fx;
	local length = math.sqrt(rx * rx + ry * ry);
	if length > 0 then
		rx, ry = rx / length, ry / length;
	end
	local x, y, z = ns.camera:GetTarget();
	ns.camera:SetTarget(x + (fx * forward + rx * right) * step, y + (fy * forward + ry * right) * step,
		z + (fz * forward + up) * step);
	ns.camera:SnapAllInterpolatedValues();
end

local function OnUpdate(self, elapsed)
	local pitch = ns.camera:GetPitch();
	if math.abs(pitch) > MAX_PITCH then
		ns.camera:SetPitch(pitch > 0 and MAX_PITCH or -MAX_PITCH);
		ns.camera:SnapToTargetInterpolationPitch();
	end
	Fly(elapsed);
	if ns.unitListChanged then
		ns.FillUnitList();
	end
	if #ns.units == 0 then
		return;
	end
	if ns.playing then
		ns.now = ns.now + elapsed * ns.PLAY_SPEEDS[ns.playSpeed];
		if ns.now >= ns.duration then
			ns.now = ns.duration;
			SetPlaying(false);
		end
	end
	ns.Apply(ns.now);
	ns.FollowFocus();
	ns.UpdateUnitList();
	ns.UpdateUnitFrame(ns.now);
	ns.UpdateUnitTooltip();
	UpdateTimeline(ns.now >= ns.duration);
end
ns.OnUpdate = OnUpdate;

local function Seek(time)
	ns.now = math.max(0, math.min(ns.duration, time));
	ns.Apply(ns.now);
	ns.FollowFocus();
	ns.UpdateUnitList();
	ns.UpdateUnitFrame(ns.now);
	UpdateTimeline(true);
end
ns.Seek = Seek;

-- Moves the replay by `seconds`, once one is loaded.
local function Step(seconds)
	if #ns.units > 0 then
		Seek(ns.now + seconds);
	end
end

local function StepSize()
	return IsShiftKeyDown() and STEP.big or STEP.small;
end

-- A fly key pressed over the scene is the scene's, and Left or Right over
-- the window steps the replay; every other key goes on to the game.
local function OnSceneKeyDown(self, key)
	local fly = FLY_KEYS[key] and MouseIsOver(ns.scene);
	local step = (key == "LEFT" or key == "RIGHT") and MouseIsOver(ns.frame);
	self:SetPropagateKeyboardInput(not fly and not step);
	if fly then
		ns.flying[key] = true;
	elseif step then
		Step(key == "LEFT" and -StepSize() or StepSize());
	end
end
ns.OnSceneKeyDown = OnSceneKeyDown;

local function OnSceneKeyUp(self, key)
	ns.flying[key] = nil;
end
ns.OnSceneKeyUp = OnSceneKeyUp;

------------------------------------------------------------------------------
-- Death marks

local function OnMarkEnter(mark)
	GameTooltip:SetOwner(mark, "ANCHOR_TOP");
	GameTooltip:SetText(string.format(UNITDIESOTHER, mark.unit.name), ns.FEED_DEATH_COLOR[1], ns.FEED_DEATH_COLOR[2],
		ns.FEED_DEATH_COLOR[3]);
	GameTooltip:AddLine(ns.FormatTime(mark.time), 1, 1, 1);
	GameTooltip:Show();
end

local function OnMarkLeave()
	GameTooltip:Hide();
end

-- A click goes to just before the death and follows the player.
local function OnMarkClick(mark)
	ns.SetFocus(mark.unit);
	Seek(mark.time - DEATH_MARK_LEAD);
end

local function NewMark()
	local mark = CreateFrame("Button", nil, ns.frame.Timeline);
	mark:SetWidth(DEATH_MARK_WIDTH);
	mark:SetHeight(DEATH_MARK_HEIGHT);
	local texture = mark:CreateTexture(nil, "OVERLAY");
	texture:SetAllPoints(mark);
	texture:SetTexture(DEATH_MARK_COLOR[1], DEATH_MARK_COLOR[2], DEATH_MARK_COLOR[3], DEATH_MARK_COLOR[4]);
	mark:SetScript("OnEnter", OnMarkEnter);
	mark:SetScript("OnLeave", OnMarkLeave);
	mark:SetScript("OnClick", OnMarkClick);
	return mark;
end

-- Each player's death, marked on the timeline where it falls.
local function ShowDeathMarks()
	for _, mark in ipairs(deathMarks) do
		mark:Hide();
		tinsert(freeMarks, mark);
	end
	deathMarks = {};
	if ns.duration <= 0 then
		return;
	end
	for i = 1, #ns.units do
		local u = ns.units[i];
		if u.isPlayer then
			for _, time in ipairs(C_CombatLogReplay.GetUnitDeaths(i)) do
				local mark = tremove(freeMarks) or NewMark();
				mark.unit, mark.time = u, time;
				mark:ClearAllPoints();
				mark:SetPoint("CENTER", ns.frame.Timeline, "LEFT", (TimelinePlace(time)), 0);
				mark:Show();
				tinsert(deathMarks, mark);
			end
		end
	end
end

------------------------------------------------------------------------------
-- The playback bar

-- A hover over the timeline shows the time under the cursor, with a line
-- there, and the mouse wheel steps. A click or a drag goes there
-- (OnValueChanged).
local function SetupTimeline()
	local timeline = ns.frame.Timeline;
	local ghost = timeline.Ghost;
	local shownSecond;
	-- The inverse of TimelinePlace.
	local function TimeAtCursor()
		local x = GetCursorPosition() / timeline:GetEffectiveScale() - timeline:GetLeft();
		local start, travel = TimelinePlace(0);
		return math.max(0, math.min(ns.duration, (x - start) / travel * ns.duration));
	end
	local function OnHoverUpdate()
		local time = TimeAtCursor();
		local x = TimelinePlace(time);
		ghost:ClearAllPoints();
		ghost:SetPoint("CENTER", timeline, "LEFT", x, 0);
		GameTooltip:ClearAllPoints();
		GameTooltip:SetPoint("BOTTOM", timeline, "LEFT", x, 14);
		local second = math.floor(time);
		if second ~= shownSecond then
			GameTooltip:SetText(ns.FormatTime(time), 1, 1, 1);
			shownSecond = second;
		end
	end
	timeline:SetScript("OnEnter", function()
		if #ns.units == 0 or ns.duration <= 0 then
			return;
		end
		GameTooltip:SetOwner(timeline, "ANCHOR_NONE");
		shownSecond = nil;
		OnHoverUpdate();
		GameTooltip:Show();
		ghost:Show();
		timeline:SetScript("OnUpdate", OnHoverUpdate);
	end);
	-- A death mark's own tooltip can take the GameTooltip first.
	timeline:SetScript("OnLeave", function()
		timeline:SetScript("OnUpdate", nil);
		ghost:Hide();
		if GameTooltip:IsOwned(timeline) then
			GameTooltip:Hide();
		end
	end);
	timeline:EnableMouseWheel(true);
	timeline:SetScript("OnMouseWheel", function(_, delta)
		Step(delta * StepSize());
	end);

	local function SetupStepButton(button, seconds, text)
		button:SetScript("OnClick", function()
			Step(seconds);
		end);
		button:SetScript("OnEnter", function()
			GameTooltip:SetOwner(button, "ANCHOR_TOP");
			GameTooltip:SetText(string.format(text, STEP.big), 1, 1, 1);
			GameTooltip:AddLine(string.format(REPLAY_STEP_HINT, STEP.small, STEP.big),
				NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b, 1);
			GameTooltip:Show();
		end);
		button:SetScript("OnLeave", function()
			GameTooltip:Hide();
		end);
	end
	SetupStepButton(ns.frame.BackButton, -STEP.big, REPLAY_STEP_BACK);
	SetupStepButton(ns.frame.ForwardButton, STEP.big, REPLAY_STEP_FORWARD);
end
ns.SetupTimeline = SetupTimeline;

-- The loaded replay, from the start.
local function Build()
	ns.ReleaseUnits();
	local info = C_CombatLogReplay.GetReplayInfo();
	if not info then
		ns.FillUnitList();
		ns.SetStatus(REPLAY_LOAD_FAILED);
		return;
	end
	for i, unit in ipairs(C_CombatLogReplay.GetUnits()) do
		-- A player's name without its realm (Name-Realm).
		local name = unit.name or "";
		if unit.isPlayer then
			name = string.gsub(name, "%-.*", "");
		end
		local appearances = unit.isPlayer and C_CombatLogReplay.GetUnitAppearances(i) or nil;
		ns.units[i] = { index = i, name = name, isPlayer = unit.isPlayer, creatureID = unit.creatureID,
			mountDisplay = 0, appearances = appearances, nameColor = ns.NameColor(unit.isPlayer, appearances),
			petOwnerIndex = unit.petOwnerIndex };
	end
	for i = 1, #ns.units do
		local u = ns.units[i];
		u.petOwner = u.petOwnerIndex and ns.units[u.petOwnerIndex];
	end
	ns.FillUnitList();
	for i = 1, #ns.units do
		ns.RequestName(ns.units[i]);
	end
	for i in ipairs(C_CombatLogReplay.GetGameObjects()) do
		ns.objects[i] = { index = i };
	end
	ns.areas = C_CombatLogReplay.GetGroundAreas();
	ns.duration = info.duration;
	ns.frame.Timeline:SetMinMaxValues(0, math.max(ns.duration, 0.1));
	ns.frame.Timeline:SetValueStep(0.1);
	ns.SetStatus(info.hasMovement and string.format(REPLAY_UNITS, #ns.units) or REPLAY_NO_POSITIONS);
	ns.ShowMap(info.mapID);
	SetPlaying(false);
	-- From the first frame with units: a whole log's movement file starts a
	-- frame after the log.
	ns.openTime = info.firstUnitTime;
	Seek(ns.openTime);
	ShowDeathMarks();
end
ns.Build = Build;
