-- ClassicAPI Replay: the state the files share (the addon table, ns), the
-- constants more than one file reads, and text.

local _, ns = ...;

-- A step in time longer than this (replay seconds), or one back, is a jump:
-- the combat state is set as it was, not played.
ns.MAX_PLAYED_STEP = 1;
ns.PLAY_SPEEDS = { 0.25, 0.5, 1, 2, 4 };
local DEFAULT_PLAY_SPEED = 3;
-- A death, in the feed and on the timeline.
ns.FEED_DEATH_COLOR = { 1, 0.25, 0.25 };
-- ns.frame, ns.scene, ns.camera: the window, its scene and its follow
-- camera, from the window's OnLoad.
ns.units = {};     -- the replay's units, by unitIndex: { name, isPlayer, actor, mount, ... }
ns.unitOfActor = {};
ns.duration = 0;
ns.openTime = 0;   -- where the replay opens and restarts: its first frame with units
ns.now = 0;
ns.playing = false;
ns.playSpeed = DEFAULT_PLAY_SPEED;
-- ns.focus: the unit the camera follows
ns.settingTimeline = false;
ns.flying = {};     -- the fly keys held, by key name
-- ns.combatTime: the replay time the combat state is at, nil before the first
ns.objects = {};    -- the replay's game objects, by objectIndex: { actor, display, state, ... }
ns.areas = {};      -- the replay's ground areas, from GetGroundAreas, each with its actor once shown
-- ns.feed: the ScrollingMessageFrame of the log's lines
-- ns.unitListChanged: a unit was named since the list was filled
-- ns.unitFrame: the followed unit's frame (Replay.xml)

-- The window's options (its Options menu), saved for the account. They are
-- loaded before the files run (## LoadSavedVariablesFirst).
-- showNames: the names over the units; showNameplates: their health and
-- cast bars.
ClassicAPIReplaySettings = ClassicAPIReplaySettings or {};
ns.settings = ClassicAPIReplaySettings;
if ns.settings.showNames == nil then
	ns.settings.showNames = true;
end
if ns.settings.showNameplates == nil then
	ns.settings.showNameplates = true;
end

------------------------------------------------------------------------------
-- Text

local function FormatTime(seconds)
	seconds = math.floor(seconds);
	return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60);
end
ns.FormatTime = FormatTime;

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
ns.LogText = LogText;

local function SetStatus(text)
	ns.frame.Status.Text:SetText(text or "");
end
ns.SetStatus = SetStatus;
