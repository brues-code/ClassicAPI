-- ClassicAPI Replay: plays back a stretch of a combat log in a ModelScene.
--
-- C_CombatLogReplay reads the log and its movement file in the DLL; this
-- window picks a log and a stretch of it (the whole log or one encounter),
-- shows the map, and puts an actor where each unit was at the replay's time:
-- its display (a player in its own display as its own body, wearing what it
-- wore), its mount, walking or running at its speed, jumping and falling
-- along the client's arc, its weapons drawn or sheathed, and its stand
-- state's animation (sitting, sleeping, dead, kneeling). Scene positions are
-- map positions. Click a unit to follow it with the camera; click the ground
-- to stop. Drag to turn and tilt the camera, right drag to move it across
-- the view. With the cursor over the scene, W, A, S and D fly the camera
-- along its view, Space and X up and down, Shift faster. /replay toggles the
-- window.

local ANIM_STAND, ANIM_WALK, ANIM_RUN, ANIM_MOUNT = 0, 4, 5, 91
-- In the air (Offsets.h ANIM_JUMP, ANIM_FALL): from a jump, or off a ledge.
local ANIM_JUMP, ANIM_FALL = 38, 40;
-- The animation a stand state shows, as the client's follow camera reads it
-- (Offsets.h ANIM_*): sit, sleep, the three chairs, dead, kneel.
local STAND_ANIMATION = { [1] = 97, [3] = 100, [4] = 102, [5] = 103, [6] = 104, [7] = 6, [8] = 115 };
-- The sheath state with melee weapons in hand.
local SHEATH_MELEE = 1;
-- Below this a moving unit walks (yards a second; walk speed is 2.5, run 7).
local RUN_SPEED = 4;
-- Faster than this a unit is moving, slower it stands.
local MOVING_SPEED = 0.1;
local PLAY_SPEEDS = { 0.25, 0.5, 1, 2, 4 };
local DEFAULT_PLAY_SPEED = 3;
-- The ground shown around the units' area, in yards past its edge, and its
-- bounds (one map tile is 533 yards).
local TERRAIN_MARGIN, MIN_TERRAIN_RADIUS, MAX_TERRAIN_RADIUS = 60, 150, 600;
local CAMERA_ZOOM, CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM = 60, 2, 250;
local CAMERA_FAR_CLIP = 800;
-- A press and release closer than this many pixels is a click, not a drag.
local CLICK_DISTANCE = 4;
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
local PLAYER_NAME_COLOR = { 0.4, 0.8, 1 };
local CREATURE_NAME_COLOR = { 1, 0.82, 0 };

local frame, scene, camera;
local selectedLog;    -- a log's name
local segments;       -- { { text, startTime, endTime } }, the whole log first
local selectedSegment;
local units = {};     -- the replay's units, by unitIndex: { name, isPlayer, actor, mount, ... }
local unitOfActor = {};
local duration = 0;
local now = 0;
local playing = false;
local playSpeed = DEFAULT_PLAY_SPEED;
local focus;          -- the unit the camera follows
local settingTimeline = false;
local pressX, pressY;
local flying = {};     -- the fly keys held, by key name

------------------------------------------------------------------------------
-- Text

local function FormatTime(seconds)
	seconds = math.floor(seconds);
	return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60);
end

-- WoWCombatLog-111626_115628 -> 11/16/26 11:56
local function LogDate(name)
	local _, _, mo, d, y, h, mi = string.find(name, "(%d%d)(%d%d)(%d%d)_(%d%d)(%d%d)");
	if not mo then
		return name;
	end
	return string.format("%s/%s/%s %s:%s", mo, d, y, h, mi);
end

local function LogText(log)
	local text = string.format(REPLAY_LOG_ENTRY, LogDate(log.name),
		string.format("%.1f MB", log.size / 1048576));
	if not log.hasMovement then
		text = text .. ", " .. REPLAY_NO_MOVEMENT;
	end
	return text;
end

local function SetStatus(text)
	frame.Status:SetText(text or "");
end

------------------------------------------------------------------------------
-- Units

local function ReleaseUnits()
	scene:ReleaseAllActors();
	units = {};
	unitOfActor = {};
	focus = nil;
end

-- A pooled actor keeps the scale settings of its last use (scene 4's
-- actors carry their display info's), and ModelSceneActorMixin applies them
-- again whenever a model loads; so a new one starts at its own size.
local function Acquire(u)
	local actor = scene:AcquireActor();
	actor:SetNormalizedScaleAggressiveness(0);
	actor:SetRequestedScale(1);
	unitOfActor[actor] = u;
	return actor;
end

local function HideUnit(u)
	if u.actor and u.actor:IsShown() then
		u.actor:Hide();
	end
	if u.mount and u.mount:IsShown() then
		u.mount:Hide();
	end
end

-- Seats the unit on a mount of `mountDisplay`, or takes it off with 0. The
-- actor that moves is the mount while it rides.
local function SetMount(u, mountDisplay)
	if mountDisplay == 0 then
		if u.seated then
			u.mount:DetachFromMount(u.actor);
			u.seated = false;
			u.animation = nil;
		end
		if u.mount and u.mount:IsShown() then
			u.mount:Hide();
		end
		u.mountDisplay = 0;
		return u.actor;
	end
	if not u.mount then
		u.mount = Acquire(u);
	end
	if u.mountDisplay ~= mountDisplay then
		if u.seated then
			u.mount:DetachFromMount(u.actor);
		end
		u.mount:SetModelByCreatureDisplayID(mountDisplay);
		u.mountDisplay = mountDisplay;
		u.seated = false;
		u.animation = nil;
	end
	if not u.mount:IsShown() then
		u.mount:Show();
	end
	-- A model that has not loaded yet seats nothing; try again next frame.
	if not u.seated then
		u.seated = u.mount:AttachToMount(u.actor, ANIM_MOUNT);
	end
	return u.mount;
end

-- The player's appearance at `time`: the last that began by then, else its
-- first (PLAYER_INFO comes with the UNIT_INFO that shows the unit).
local function AppearanceAt(u, time)
	local list = u.appearances;
	if not list or not list[1] then
		return nil;
	end
	local found = list[1];
	for i = 2, #list do
		if list[i].time > time then
			break;
		end
		found = list[i];
	end
	return found;
end

-- Shows the player's own body in `appearance`, wearing its items.
local function Dress(actor, appearance)
	actor:SetModelByCharacterAppearance(appearance);
	for slot, info in ipairs(appearance.items) do
		if info.appearanceID > 0 then
			actor:SetItemTransmogInfo(info, slot);
		end
	end
end

local function ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
		mountDisplay, scale, standState, sheathState)
	local actor = u.actor;
	if not actor then
		actor = Acquire(u);
		u.actor = actor;
		local color = u.isPlayer and PLAYER_NAME_COLOR or CREATURE_NAME_COLOR;
		actor:SetNameText(u.name, color[1], color[2], color[3]);
	end
	if not actor:IsShown() then
		actor:Show();
	end
	local sheathed = sheathState ~= SHEATH_MELEE;
	if u.sheathed ~= sheathed then
		actor:SetSheathed(sheathed);
		u.sheathed = sheathed;
	end
	-- A player shown in its own display is its own body; in any other (a
	-- form, a polymorph) it is that creature, as the client shows it.
	local appearance = display == nativeDisplay and AppearanceAt(u, time);
	local model = appearance or display;
	if u.model ~= model then
		if appearance then
			Dress(actor, appearance);
		else
			actor:SetModelByCreatureDisplayID(display);
		end
		u.model = model;
		u.animation = nil;
		u.seated = false;
	end
	-- Through the mixin, which sets it again once the model has loaded.
	if u.scale ~= scale then
		actor:SetRequestedScale(scale);
		u.scale = scale;
	end
	local body = SetMount(u, mountDisplay);
	body:SetPosition(x, y, z);
	body:SetYaw(facing);
	local animation;
	if falling then
		animation = jumping and ANIM_JUMP or ANIM_FALL;
	elseif speed > MOVING_SPEED then
		animation = speed >= RUN_SPEED and ANIM_RUN or ANIM_WALK;
	else
		animation = STAND_ANIMATION[standState] or ANIM_STAND;
	end
	if u.animation ~= animation then
		body:SetAnimation(animation);
		u.animation = animation;
	end
end

-- Puts every unit where it was at `time`.
local function Apply(time)
	local GetUnitPosition = C_CombatLogReplay.GetUnitPosition;
	local GetUnitLook = C_CombatLogReplay.GetUnitLook;
	for i = 1, #units do
		local u = units[i];
		local x, y, z, facing, speed, falling, jumping = GetUnitPosition(i, time);
		local display, nativeDisplay, mountDisplay, scale, standState, sheathState, _;
		if x then
			display, nativeDisplay, mountDisplay, scale, standState, _, _, sheathState =
				GetUnitLook(i, time);
		end
		if display and display > 0 then
			ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
				mountDisplay, scale, standState, sheathState);
		else
			HideUnit(u);
		end
	end
end

------------------------------------------------------------------------------
-- Camera

local function Aim(x, y, z)
	camera:SetTarget(x, y, z);
	camera:SetZoomDistance(CAMERA_ZOOM);
	camera:SnapAllInterpolatedValues();
end

local function SetFocus(u)
	if focus and focus.actor then
		focus.actor:SetHighlight("target", false);
	end
	focus = u;
	if focus and focus.actor then
		focus.actor:SetHighlight("target", true);
	end
end

local function FollowFocus()
	if not focus then
		return;
	end
	local x, y, z = C_CombatLogReplay.GetUnitPosition(focus.index, now);
	if x then
		camera:SetTarget(x, y, z + 1);
	end
end

------------------------------------------------------------------------------
-- The map

-- The middle of where the units are over the replay, and how far from it
-- most of them stay (AREA_SHARE of the places). The median, not the middle
-- of all of them: the log can hold a unit far from the rest (one Stormwind
-- log had a player 9,900 yards away, and players on flight paths leave the
-- city), which would put the middle in a mountain.
local AREA_SHARE = 0.9;

-- The median of a list, which it leaves as it was.
local function Median(values)
	local sorted = {};
	for i = 1, #values do
		sorted[i] = values[i];
	end
	table.sort(sorted);
	return sorted[math.floor((#sorted + 1) / 2)];
end

local function UnitArea()
	local xs, ys, zs = {}, {}, {};
	for i = 1, #units do
		for _, time in ipairs({ 0, duration / 2, duration }) do
			local x, y, z = C_CombatLogReplay.GetUnitPosition(i, time);
			if x then
				tinsert(xs, x);
				tinsert(ys, y);
				tinsert(zs, z);
			end
		end
	end
	local count = #xs;
	if count == 0 then
		return nil;
	end
	local x, y = Median(xs), Median(ys);
	local distances = {};
	for i = 1, count do
		local dx, dy = xs[i] - x, ys[i] - y;
		distances[i] = math.sqrt(dx * dx + dy * dy);
	end
	table.sort(distances);
	return x, y, Median(zs), distances[math.max(1, math.floor(count * AREA_SHARE))];
end

-- Shows the replay's map: the map's WMO when it is one, else its ground
-- around the units. Aims the camera at the middle of the units.
local function ShowMap(mapID)
	scene:ClearWorldModel();
	scene:ClearWorldTerrain();
	local x, y, z, reach = UnitArea();
	if mapID < 0 then
		SetStatus(REPLAY_NO_MAP);
	elseif not scene:SetWorldModelByMapID(mapID) and x then
		local radius = math.max(MIN_TERRAIN_RADIUS, math.min(MAX_TERRAIN_RADIUS, reach + TERRAIN_MARGIN));
		if not scene:SetWorldTerrain(mapID, x, y, radius) then
			SetStatus(string.format(REPLAY_NO_GROUND, mapID));
		end
	end
	if x then
		Aim(x, y, z);
	end
end

------------------------------------------------------------------------------
-- Playback

local function UpdatePlayButton()
	frame.PlayButton:SetText(playing and REPLAY_PAUSE or REPLAY_PLAY);
end

local function UpdateTimeline()
	settingTimeline = true;
	frame.Timeline:SetValue(now);
	settingTimeline = false;
	frame.Time:SetText(string.format(REPLAY_TIME, FormatTime(now), FormatTime(duration)));
end

local function SetPlaying(on)
	playing = on;
	scene:SetPaused(not on);
	UpdatePlayButton();
end

-- Moves the camera's target by the fly keys held: along the view, across it
-- level, and straight up. Flying stops following a unit.
local function Fly(elapsed)
	local forward, right, up = 0, 0, 0;
	for key in pairs(flying) do
		local move = FLY_KEYS[key];
		forward, right, up = forward + move[1], right + move[2], up + move[3];
	end
	if forward == 0 and right == 0 and up == 0 then
		return;
	end
	SetFocus(nil);
	local step = FLY_SPEED * elapsed * (IsShiftKeyDown() and FLY_FAST or 1);
	local fx, fy, fz = camera:GetForwardVector();
	-- Right of the view, level: forward x up (the map's y grows to the left).
	local rx, ry = fy, -fx;
	local length = math.sqrt(rx * rx + ry * ry);
	if length > 0 then
		rx, ry = rx / length, ry / length;
	end
	local x, y, z = camera:GetTarget();
	camera:SetTarget(x + (fx * forward + rx * right) * step, y + (fy * forward + ry * right) * step,
		z + (fz * forward + up) * step);
	camera:SnapAllInterpolatedValues();
end

-- A fly key pressed over the scene is the scene's; every other key goes on
-- to the game.
local function OnSceneKeyDown(self, key)
	local fly = FLY_KEYS[key] and MouseIsOver(scene);
	self:SetPropagateKeyboardInput(not fly);
	if fly then
		flying[key] = true;
	end
end

local function OnSceneKeyUp(self, key)
	flying[key] = nil;
end

local function OnUpdate(self, elapsed)
	local pitch = camera:GetPitch();
	if math.abs(pitch) > MAX_PITCH then
		camera:SetPitch(pitch > 0 and MAX_PITCH or -MAX_PITCH);
		camera:SnapToTargetInterpolationPitch();
	end
	Fly(elapsed);
	if #units == 0 then
		return;
	end
	if playing then
		now = now + elapsed * PLAY_SPEEDS[playSpeed];
		if now >= duration then
			now = duration;
			SetPlaying(false);
		end
	end
	Apply(now);
	FollowFocus();
	UpdateTimeline();
end

local function Seek(time)
	now = math.max(0, math.min(duration, time));
	Apply(now);
	FollowFocus();
	UpdateTimeline();
end

-- The loaded replay, from the start.
local function Build()
	ReleaseUnits();
	local info = C_CombatLogReplay.GetReplayInfo();
	if not info then
		SetStatus(REPLAY_LOAD_FAILED);
		return;
	end
	for i, unit in ipairs(C_CombatLogReplay.GetUnits()) do
		-- A player's name without its realm (Name-Realm).
		local name = unit.name or "";
		if unit.isPlayer then
			name = string.gsub(name, "%-.*", "");
		end
		units[i] = { index = i, name = name, isPlayer = unit.isPlayer, mountDisplay = 0,
			appearances = unit.isPlayer and C_CombatLogReplay.GetUnitAppearances(i) or nil };
	end
	duration = info.duration;
	frame.Timeline:SetMinMaxValues(0, math.max(duration, 0.1));
	frame.Timeline:SetValueStep(0.1);
	SetStatus(info.hasMovement and string.format(REPLAY_UNITS, #units) or REPLAY_NO_POSITIONS);
	ShowMap(info.mapID);
	SetPlaying(false);
	Seek(0);
end

------------------------------------------------------------------------------
-- Choosing a log and a stretch

-- The log's stretches, once the DLL has read it; nil while it reads.
local function ReadSegments()
	local encounters, logDuration = C_CombatLogReplay.GetEncounters(selectedLog);
	if not encounters then
		SetStatus(REPLAY_READING);
		return;
	end
	segments = { { text = string.format(REPLAY_WHOLE_LOG, FormatTime(logDuration)),
		startTime = 0, endTime = logDuration } };
	for _, e in ipairs(encounters) do
		local length = FormatTime(e.endTime - e.startTime);
		local format = REPLAY_ENCOUNTER;
		if e.success == true then
			format = REPLAY_ENCOUNTER_KILL;
		elseif e.success == false then
			format = REPLAY_ENCOUNTER_WIPE;
		end
		tinsert(segments, { text = string.format(format, e.encounterName, length),
			startTime = e.startTime, endTime = e.endTime });
	end
	SetStatus("");
	frame.SegmentDropdown:GenerateMenu();
end

local function SelectSegment(index)
	selectedSegment = index;
	local segment = segments[index];
	if C_CombatLogReplay.LoadReplay(selectedLog, segment.startTime, segment.endTime) then
		SetStatus(REPLAY_LOADING);
	else
		SetStatus(REPLAY_LOAD_FAILED);
	end
end

local function SelectLog(name)
	selectedLog = name;
	segments = nil;
	selectedSegment = nil;
	frame.SegmentDropdown:GenerateMenu();
	ReadSegments();
end

local function SetupMenus()
	frame.LogDropdown:SetDefaultText(REPLAY_CHOOSE_LOG);
	frame.LogDropdown:SetupMenu(function(dropdown, rootDescription)
		rootDescription:SetScrollMode(400);
		local logs = C_CombatLogReplay.GetLogs();
		if #logs == 0 then
			rootDescription:CreateTitle(REPLAY_NO_LOGS);
		end
		for _, log in ipairs(logs) do
			local name = log.name;
			rootDescription:CreateRadio(LogText(log),
				function() return selectedLog == name; end,
				function() SelectLog(name); end);
		end
	end);
	frame.SegmentDropdown:SetDefaultText(REPLAY_CHOOSE_SEGMENT);
	frame.SegmentDropdown:SetupMenu(function(dropdown, rootDescription)
		rootDescription:SetScrollMode(400);
		for index, segment in ipairs(segments or {}) do
			local i = index;
			rootDescription:CreateRadio(segment.text,
				function() return selectedSegment == i; end,
				function() SelectSegment(i); end);
		end
	end);
end

------------------------------------------------------------------------------
-- The window

local function OnEvent(self, event, logName, success)
	if event == "COMBAT_LOG_REPLAY_ENCOUNTERS_READY" then
		if logName == selectedLog then
			ReadSegments();
		end
	elseif event == "COMBAT_LOG_REPLAY_LOADED" then
		if success then
			Build();
		else
			SetStatus(REPLAY_LOAD_FAILED);
		end
	end
end

-- A click on a unit follows it; a click on anything else stops following.
-- A drag turns the camera, as the scene does by itself.
local function OnSceneMouseDown(self, button)
	pressX, pressY = GetCursorPosition();
end

local function OnSceneMouseUp(self, button)
	if button ~= "LeftButton" or not pressX then
		return;
	end
	local x, y = GetCursorPosition();
	if math.abs(x - pressX) + math.abs(y - pressY) > CLICK_DISTANCE then
		return;
	end
	local actor = scene:GetActorAtCursor();
	SetFocus(actor and unitOfActor[actor]);
end

function ClassicAPIReplay_OnLoad(self)
	frame = self;
	scene = self.Scene;
	tinsert(UISpecialFrames, self:GetName());
	self.Title:SetText(REPLAY_TITLE);
	self:SetScript("OnMouseDown", function(f) f:StartMoving(); end);
	self:SetScript("OnMouseUp", function(f) f:StopMovingOrSizing(); end);
	self:SetScript("OnHide", function()
		SetPlaying(false);
		flying = {};
	end);
	self:SetScript("OnEvent", OnEvent);
	self:RegisterEvent("COMBAT_LOG_REPLAY_ENCOUNTERS_READY");
	self:RegisterEvent("COMBAT_LOG_REPLAY_LOADED");

	scene.ControlFrame:SetModelScene(scene);
	scene:TransitionToModelSceneID(4, CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD, true);
	scene:SetCameraFarClip(CAMERA_FAR_CLIP);
	camera = scene:GetActiveCamera();
	camera:SetTargetSpline(nil);
	camera:SetOrientationSpline(nil);
	camera:SetMinZoomDistance(CAMERA_MIN_ZOOM);
	camera:SetMaxZoomDistance(CAMERA_MAX_ZOOM);
	-- Left drag turns and tilts; right drag moves the camera across the view.
	camera:SetLeftMouseButtonYMode(ORBIT_CAMERA_MOUSE_MODE_PITCH_ROTATION, true);
	camera:SetRightMouseButtonXMode(ORBIT_CAMERA_MOUSE_MODE_TARGET_HORIZONTAL, true);
	camera:SetRightMouseButtonYMode(ORBIT_CAMERA_MOUSE_MODE_TARGET_VERTICAL, true);
	scene:HookScript("OnMouseDown", OnSceneMouseDown);
	scene:HookScript("OnMouseUp", OnSceneMouseUp);
	scene:EnableKeyboard(true);
	scene:SetScript("OnKeyDown", OnSceneKeyDown);
	scene:SetScript("OnKeyUp", OnSceneKeyUp);
	self:SetScript("OnUpdate", OnUpdate);

	self.PlayButton:SetScript("OnClick", function()
		if not playing and now >= duration then
			Seek(0);
		end
		SetPlaying(not playing and #units > 0);
	end);
	self.SpeedButton:SetScript("OnClick", function()
		playSpeed = playSpeed % #PLAY_SPEEDS + 1;
		self.SpeedButton:SetText(string.format(REPLAY_SPEED, PLAY_SPEEDS[playSpeed]));
	end);
	self.SpeedButton:SetText(string.format(REPLAY_SPEED, PLAY_SPEEDS[playSpeed]));
	self.Timeline:SetMinMaxValues(0, 1);
	self.Timeline:SetScript("OnValueChanged", function(slider, value)
		if not settingTimeline then
			Seek(value);
		end
	end);
	UpdatePlayButton();
	SetupMenus();
end

function ClassicAPIReplay_Toggle()
	if frame:IsShown() then
		frame:Hide();
	else
		frame:Show();
		frame.LogDropdown:GenerateMenu();
	end
end
