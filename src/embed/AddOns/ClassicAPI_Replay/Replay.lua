-- ClassicAPI Replay: plays back a stretch of a combat log in a ModelScene.
--
-- C_CombatLogReplay reads the log and its movement file in the DLL; this
-- window picks a log and a stretch of it (the whole log or one encounter),
-- shows the map, and puts an actor where each unit was at the replay's time:
-- its display (a player in its own display as its own body, wearing what it
-- wore), its mount, walking or running at its speed, jumping and falling
-- along the client's arc, its weapons drawn or sheathed, and its stand
-- state's animation (sitting, sleeping, dead, kneeling). While it plays, the
-- log's casts, auras, swings and deaths play on the actors; a jump in time
-- sets each unit's auras and death as they were then. A hurt unit shows its
-- health over its head in its nameplate's color, from the log's advanced
-- data, a casting unit its cast bar (the Options menu turns these two
-- nameplate bars off), a marked unit its raid target icon, none of them
-- through a wall, and
-- the log's lines scroll by in the corner in the client's own combat log
-- words: each starts with its time, a link that goes there, players show in
-- class colors, a name links to its unit and a spell is a spell link. Scene
-- positions are map positions. Click a unit, or its name in the unit list, to
-- follow it with the camera, which then stops in front of walls as the
-- world's camera does; click the ground to stop. The followed unit shows in a
-- target frame, with its target of target. Drag to turn and tilt the camera,
-- right drag to move it across the view. With the cursor over the scene, W,
-- A, S and D fly the camera along its view, Space and X up and down, Shift
-- faster. /replay toggles the window.
--
-- The files share the replay's state and the functions one calls in another
-- through the addon table (ns), set up in Core.lua; everything else stays
-- local to its file. This file is the window: choosing a log and a stretch,
-- and its OnLoad.

local _, ns = ...;

-- The UiModelScene the window's scene starts from (its camera's settings).
local MODEL_SCENE_ID = 4;
local CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM = 2, 250;
-- A wheel notch (or a zoom button) scales the camera's distance by this, so
-- it zooms at one pace near a unit and high over the map; the camera eases
-- to the new distance.
local ZOOM_STEP = 0.8;
-- A press and release closer than this many pixels is a click, not a drag.
local CLICK_DISTANCE = 4;
local UNIT_ROW_HEIGHT = 16;
-- The spell ID line at the bottom of a spell link's tooltip, in retail's
-- GRAY_FONT_COLOR.
local SPELL_ID_COLOR = { 0.5, 0.5, 0.5 };
-- The feed of the log's lines: how many show (they stay until newer ones push
-- them out), and its width.
local FEED_LINES, FEED_WIDTH = 8, 440;
local selectedLog;    -- a log's name
local segments;       -- { { text, startTime, endTime } }, the whole log first
local selectedSegment;
local pressX, pressY;

------------------------------------------------------------------------------
-- Choosing a log and a stretch

-- The log's stretches, once the DLL has read it; nil while it reads.
local function ReadSegments()
	local encounters, logDuration = C_CombatLogReplay.GetEncounters(selectedLog);
	if not encounters then
		ns.SetStatus(REPLAY_READING);
		return;
	end
	segments = { { text = string.format(REPLAY_WHOLE_LOG, ns.FormatTime(logDuration)),
		startTime = 0, endTime = logDuration } };
	for _, e in ipairs(encounters) do
		local length = ns.FormatTime(e.endTime - e.startTime);
		local format = REPLAY_ENCOUNTER;
		if e.success == true then
			format = REPLAY_ENCOUNTER_KILL;
		elseif e.success == false then
			format = REPLAY_ENCOUNTER_WIPE;
		end
		tinsert(segments, { text = string.format(format, e.encounterName, length),
			startTime = e.startTime, endTime = e.endTime });
	end
	ns.SetStatus("");
	ns.frame.SegmentDropdown:GenerateMenu();
end

local function SelectSegment(index)
	selectedSegment = index;
	local segment = segments[index];
	if C_CombatLogReplay.LoadReplay(selectedLog, segment.startTime, segment.endTime) then
		ns.SetStatus(REPLAY_LOADING);
	else
		ns.SetStatus(REPLAY_LOAD_FAILED);
	end
end

local function SelectLog(name)
	selectedLog = name;
	segments = nil;
	selectedSegment = nil;
	ns.frame.SegmentDropdown:GenerateMenu();
	ReadSegments();
end

local function SetupMenus()
	ns.frame.LogDropdown:SetDefaultText(REPLAY_CHOOSE_LOG);
	ns.frame.LogDropdown:SetupMenu(function(dropdown, rootDescription)
		rootDescription:SetScrollMode(400);
		local logs = C_CombatLogReplay.GetLogs();
		if #logs == 0 then
			rootDescription:CreateTitle(REPLAY_NO_LOGS);
		end
		for _, log in ipairs(logs) do
			local name = log.name;
			rootDescription:CreateRadio(ns.LogText(log),
				function() return selectedLog == name; end,
				function() SelectLog(name); end);
		end
	end);
	ns.frame.OptionsDropdown:SetText(REPLAY_OPTIONS);
	ns.frame.OptionsDropdown:SetupMenu(function(dropdown, rootDescription)
		rootDescription:CreateCheckbox(REPLAY_SHOW_NAMES,
			function() return ns.settings.showNames; end,
			function() ns.settings.showNames = not ns.settings.showNames; end);
		rootDescription:CreateCheckbox(REPLAY_SHOW_NAMEPLATES,
			function() return ns.settings.showNameplates; end,
			function() ns.settings.showNameplates = not ns.settings.showNameplates; end);
	end);
	ns.frame.SegmentDropdown:SetDefaultText(REPLAY_CHOOSE_SEGMENT);
	ns.frame.SegmentDropdown:SetupMenu(function(dropdown, rootDescription)
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

local function OnEvent(self, event, ...)
	if event == "COMBAT_LOG_REPLAY_ENCOUNTERS_READY" then
		local logName = ...;
		if logName == selectedLog then
			ReadSegments();
		end
	elseif event == "COMBAT_LOG_REPLAY_LOADED" then
		local success = ...;
		if success then
			ns.Build();
		else
			ns.SetStatus(REPLAY_LOAD_FAILED);
		end
	elseif event == "CVAR_UPDATE" then
		local name = ...;
		if name and string.lower(name) == "farclip" then
			ns.SetFarClip();
		end
	elseif event == "CREATURE_DATA_LOAD_RESULT" then
		local creatureID, success = ...;
		if success then
			ns.NameCreatures(creatureID);
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
	local actor = ns.scene:GetActorAtCursor();
	ns.SetFocus(actor and ns.unitOfActor[actor]);
end

function ClassicAPIReplay_OnLoad(self)
	ns.frame = self;
	ns.scene = self.Scene;
	tinsert(UISpecialFrames, self:GetName());
	self.Title:SetText(REPLAY_TITLE);
	self:SetScript("OnMouseDown", function(f) f:StartMoving(); end);
	self:SetScript("OnMouseUp", function(f) f:StopMovingOrSizing(); end);
	self:SetScript("OnHide", function()
		ns.SetPlaying(false);
		ns.flying = {};
		ns.HideUnitTooltip();
	end);
	self:SetScript("OnEvent", OnEvent);
	self:RegisterEvent("COMBAT_LOG_REPLAY_ENCOUNTERS_READY");
	self:RegisterEvent("COMBAT_LOG_REPLAY_LOADED");
	self:RegisterEvent("CREATURE_DATA_LOAD_RESULT");
	self:RegisterEvent("CVAR_UPDATE");

	ns.scene.ControlFrame:SetModelScene(ns.scene);
	ns.scene:TransitionToModelSceneID(MODEL_SCENE_ID, CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD,
		true);
	ns.SetFarClip();
	-- A follow camera beside the scene's own, with its settings, in its place.
	local _, cameraIDs = C_ModelInfo.GetModelSceneInfoByID(MODEL_SCENE_ID);
	ns.camera = ns.scene:AddCamera(CameraRegistry:CreateCameraByType("FollowCamera"));
	ns.camera:ApplyFromModelSceneCameraInfo(C_ModelInfo.GetModelSceneCameraInfoByID(cameraIDs[1]),
		CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD);
	ns.scene:SetActiveCamera(ns.camera);
	ns.camera:SetTargetSpline(nil);
	ns.camera:SetOrientationSpline(nil);
	ns.camera:SetMinZoomDistance(CAMERA_MIN_ZOOM);
	ns.camera:SetMaxZoomDistance(CAMERA_MAX_ZOOM);
	-- Left drag turns and tilts; right drag moves the camera across the view.
	ns.camera:SetLeftMouseButtonYMode(ORBIT_CAMERA_MOUSE_MODE_PITCH_ROTATION, true);
	ns.camera:SetRightMouseButtonXMode(ORBIT_CAMERA_MOUSE_MODE_TARGET_HORIZONTAL, true);
	ns.camera:SetRightMouseButtonYMode(ORBIT_CAMERA_MOUSE_MODE_TARGET_VERTICAL, true);
	-- The orbit camera's wheel steps a tenth of the zoom range, 25 yards here:
	-- from 2 yards, one notch out lands at 27.
	ns.camera.OnMouseWheel = function(camera, delta)
		local distance = camera:GetZoomDistance();
		camera:SetZoomDistance(delta > 0 and distance * ZOOM_STEP or distance / ZOOM_STEP);
	end;
	ns.scene:HookScript("OnMouseDown", OnSceneMouseDown);
	ns.scene:HookScript("OnMouseUp", OnSceneMouseUp);
	ns.feed = CreateFrame("ScrollingMessageFrame", nil, ns.scene);
	ns.feed:SetPoint("BOTTOMLEFT", ns.scene, "BOTTOMLEFT", 8, 8);
	ns.feed:SetWidth(FEED_WIDTH);
	ns.feed:SetHeight(FEED_LINES * 14);
	ns.feed:SetFontObject(GameFontHighlightSmall);
	ns.feed:SetJustifyH("LEFT");
	ns.feed:SetMaxLines(FEED_LINES * 4);
	ns.feed:SetFading(false);
	ns.feed:SetFrameLevel(ns.scene:GetFrameLevel() + 6);
	ns.feed:EnableMouse(true);
	-- A line's time goes there, a unit's name follows it, and a spell shows
	-- its tooltip in the window's own ItemRefTooltip, which a second click
	-- on the same spell closes.
	local spellTooltip = self.SpellTooltip;
	ns.SetFrameLevels(spellTooltip, ns.scene:GetFrameLevel() + 10);
	ns.SetFrameLevels(self.UnitTooltip, ns.scene:GetFrameLevel() + 10);
	ns.feed:SetScript("OnHyperlinkClick", function(_, link)
		local _, _, time = string.find(link, "^replay:(.+)$");
		if time then
			ns.Seek(tonumber(time));
			return;
		end
		local _, _, index = string.find(link, "^replayunit:(%d+)$");
		if index then
			ns.SetFocus(ns.units[tonumber(index)]);
			return;
		end
		local _, _, spellID = string.find(link, "^spell:(%d+)");
		if not spellID then
			return;
		end
		spellID = tonumber(spellID);
		if spellTooltip:IsShown() and spellTooltip.spellID == spellID then
			spellTooltip:Hide();
			return;
		end
		spellTooltip:SetOwner(self, "ANCHOR_PRESERVE");
		spellTooltip:SetSpellByID(spellID, false, true);
		spellTooltip:AddLine(string.format(REPLAY_SPELL_ID, spellID), SPELL_ID_COLOR[1], SPELL_ID_COLOR[2],
			SPELL_ID_COLOR[3]);
		spellTooltip.spellID = spellID;
		spellTooltip:Show();
	end);
	ns.unitFrame = ns.scene.UnitFrame;
	ns.SetFrameLevels(ns.unitFrame, ns.scene:GetFrameLevel() + 7);
	ns.unitFrame:SetHitRectInsets(96, 6, 4, 9);
	-- A click on the target of target follows it, as one targets it.
	ns.unitFrame.TargetOfTarget:SetScript("OnClick", function(tot)
		ns.SetFocus(tot.unit);
	end);
	-- Each shows its unit's tooltip, as UnitFrame_OnEnter does.
	ns.unitFrame:SetScript("OnEnter", function(f)
		ns.ShowUnitTooltip(f, ns.focus);
	end);
	ns.unitFrame.TargetOfTarget:SetScript("OnEnter", function(tot)
		ns.ShowUnitTooltip(tot, tot.unit);
	end);
	ns.unitFrame:SetScript("OnLeave", ns.HideUnitTooltip);
	ns.unitFrame.TargetOfTarget:SetScript("OnLeave", ns.HideUnitTooltip);
	local view = CreateScrollBoxListLinearView();
	view:SetElementExtent(UNIT_ROW_HEIGHT);
	view:SetElementInitializer("ClassicAPIReplayUnitRowTemplate", ns.InitUnitRow);
	ScrollUtil.InitScrollBoxListWithScrollBar(self.UnitList.ScrollBox, self.UnitList.ScrollBar, view);
	ns.scene:EnableKeyboard(true);
	ns.scene:SetScript("OnKeyDown", ns.OnSceneKeyDown);
	ns.scene:SetScript("OnKeyUp", ns.OnSceneKeyUp);
	self:SetScript("OnUpdate", ns.OnUpdate);

	self.PlayButton:SetScript("OnClick", function()
		if not ns.playing and ns.now >= ns.duration then
			ns.Seek(ns.openTime);
		end
		ns.SetPlaying(not ns.playing and #ns.units > 0);
	end);
	self.SpeedButton:SetScript("OnClick", function()
		ns.playSpeed = ns.playSpeed % #ns.PLAY_SPEEDS + 1;
		self.SpeedButton:SetText(string.format(REPLAY_SPEED, ns.PLAY_SPEEDS[ns.playSpeed]));
	end);
	self.SpeedButton:SetText(string.format(REPLAY_SPEED, ns.PLAY_SPEEDS[ns.playSpeed]));
	self.Timeline:SetMinMaxValues(0, 1);
	self.Timeline:SetScript("OnValueChanged", function(slider, value)
		if not ns.settingTimeline then
			ns.Seek(value);
		end
	end);
	ns.SetupTimeline();
	ns.UpdatePlayButton();
	SetupMenus();
end

function ClassicAPIReplay_Toggle()
	if ns.frame:IsShown() then
		ns.frame:Hide();
	else
		ns.frame:Show();
		ns.ShowMeter();
		ns.frame.LogDropdown:GenerateMenu();
	end
end
