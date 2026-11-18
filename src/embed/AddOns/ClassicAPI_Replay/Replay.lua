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
-- data, a casting unit its cast bar, a marked unit its raid target icon, and
-- the log's lines scroll by in the corner in the client's own combat log
-- words: each starts with its time, a link that goes there, players show in
-- class colors, a name links to its unit and a spell is a spell link. Scene positions are map positions. Click a unit, or its name in the
-- unit list, to follow it with the camera, which then stops in front of walls
-- as the world's camera does; click the ground to stop. The followed unit
-- shows in a target frame, with its target of target. Drag to turn and tilt the camera, right drag to move it across
-- the view. With the cursor over the scene, W, A, S and D fly the camera
-- along its view, Space and X up and down, Shift faster. /replay toggles the
-- window.

local ANIM_STAND, ANIM_WALK, ANIM_RUN, ANIM_MOUNT = 0, 4, 5, 91
-- In the air (Offsets.h ANIM_JUMP, ANIM_FALL): from a jump, or off a ledge.
local ANIM_JUMP, ANIM_FALL = 38, 40;
-- Combat (AnimationData.dbc): dying, then lying dead.
local ANIM_DEATH, ANIM_DEAD = 1, 6;
-- An attacker's stance between swings, as the client picks it
-- (GetReadyAnimation 0x005FCDC0): by the main hand's weapon subclass (item
-- class 2) 26 Ready1H, 27 Ready2H, 28 Ready2HL, else 25 ReadyUnarmed.
local ITEM_CLASS_WEAPON = 2;
local ANIM_READY_UNARMED = 25;
local READY_BY_SUBCLASS = { [0] = 26, 27, nil, nil, 26, 27, 28, 26, 27, nil, 28, 26, 27, 26, 26, 26, nil, 28 };
-- How long the dying animation plays before the corpse lies still, in real
-- seconds.
local DYING_TIME = 1.5;
-- A step in time longer than this (replay seconds), or one back, is a jump:
-- the combat state is set as it was, not played.
local MAX_PLAYED_STEP = 1;
-- The animation a stand state shows, as the client's follow camera reads it
-- (Offsets.h ANIM_*): sit, sleep, the three chairs, dead, kneel.
local STAND_ANIMATION = { [1] = 97, [3] = 100, [4] = 102, [5] = 103, [6] = 104, [7] = 6, [8] = 115 };
-- A creature's held slots: main hand, off hand, ranged.
local HELD_SLOTS = { 16, 17, 18 };
-- Below this a moving unit walks (yards a second; walk speed is 2.5, run 7).
local RUN_SPEED = 4;
-- Faster than this a unit is moving, slower it stands.
local MOVING_SPEED = 0.1;
local PLAY_SPEEDS = { 0.25, 0.5, 1, 2, 4 };
local DEFAULT_PLAY_SPEED = 3;
-- The UiModelScene the window's scene starts from (its camera's settings).
local MODEL_SCENE_ID = 4;
local CAMERA_ZOOM, CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM = 60, 2, 250;
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
-- A name's color: a player's class color, else these; grey once dead. The
-- unit list fades the name of a unit out of view.
local PLAYER_NAME_COLOR = { 0.4, 0.8, 1 };
local CREATURE_NAME_COLOR = { 1, 0.82, 0 };
local DEAD_NAME_COLOR = { 0.5, 0.5, 0.5 };
local OUT_OF_VIEW_ALPHA = 0.4;
local UNIT_ROW_HEIGHT = 16;
-- The health bar over a hurt unit: its size, how far above the name point
-- it sits (pixels), and its colors, the nameplate's four (0x007CB120 to
-- 0x007CB1E0).
local HEALTH_BAR_WIDTH, HEALTH_BAR_HEIGHT, HEALTH_BAR_RISE = 50, 5, 14;
-- The icon of a spell this client does not have (a log recorded on another),
-- as retail shows one.
local UNKNOWN_SPELL_ICON = "Interface\\Icons\\INV_Misc_QuestionMark";
-- The spell ID line at the bottom of a spell link's tooltip, in retail's
-- GRAY_FONT_COLOR.
local SPELL_ID_COLOR = { 0.5, 0.5, 0.5 };
-- The stand-in portrait of a player the client does not hold (0x00854044).
local STAND_IN_PORTRAIT = "Interface\\CharacterFrame\\TemporaryPortrait-%s-%s";
local PLATE_COLORS = {
	hostile = { 1, 0, 0 }, player = { 0, 0, 1 }, friendly = { 0, 1, 0 }, neutral = { 1, 1, 0 },
};
-- The cast bar over a casting unit, above its health bar, as retail's Classic
-- cast bar shows a cast (CastingBarFrame.lua's CastingBarTypeInfo): yellow
-- filling while it casts, green emptying while it channels, full green once
-- it went off and red with INTERRUPTED when it did not. Then it fades
-- (CastingBarFrameTemplates.xml: after 0.2 seconds, or 1 when interrupted,
-- over 0.3), in replay seconds.
local CAST_BAR_WIDTH, CAST_BAR_HEIGHT, CAST_BAR_GAP = 80, 10, 2;
local CAST_COLOR = { 1, 0.7, 0 };
local CHANNEL_COLOR = { 0, 1, 0 };
local CAST_DONE_COLOR = { 0, 1, 0 };
local CAST_INTERRUPTED_COLOR = { 1, 0, 0 };
local CAST_DONE_HOLD, CAST_INTERRUPTED_HOLD, CAST_FADE = 0.2, 1, 0.3;
-- A marked unit's raid target icon, above its bars.
local RAID_ICON_SIZE, RAID_ICON_GAP = 20, 2;
-- The followed unit's frame, as 1.12's TargetFrame:
-- - at most 5 buffs and 16 debuffs (MAX_TARGET_BUFFS, MAX_TARGET_DEBUFFS),
--   in rows that wrap after 6 debuffs, or 5 while the target of target
--   shows; 21-pixel icons, 17 once they would overlap it. The target of
--   target shows 4 debuffs of 12 pixels;
-- - the level in gold (TargetFrame_CheckLevel's color for a unit the player
--   cannot attack);
-- - the name's background grey for a unit another player tapped, and blue
--   for one with no selection color (TargetFrame_CheckFaction). A unit a
--   player controls (UNIT_FIELD_FLAGS 0x8) is never grey.
-- dispelNames are C_Spell.GetSpellDispelType's numbers as DebuffTypeColor
-- names them; the movement file keeps the power of type powerRage in tenths.
local UNIT_FRAME = {
	buffs = 5, debuffs = 16, auraSize = 21, auraSmallSize = 17, auraGap = 3,
	totDebuffs = 4, totDebuffSize = 12,
	levelColor = { 1, 0.82, 0 },
	tappedColor = { 0.5, 0.5, 0.5 }, noSelectionColor = { 0, 0, 1 },
	playerControlledFlag = 0x8,
	dispelNames = { "Magic", "Curse", "Disease", "Poison" },
	powerRage = 1,
};
-- The feed of the log's lines: how many show (they stay until newer ones push
-- them out), how many seconds of lines a jump in time fills it with, and the
-- colors of its lines, heals and deaths.
local FEED_LINES, FEED_WIDTH, FEED_SECONDS = 8, 440, 12;
local FEED_TEXT_COLOR = { 1, 1, 1 };
local FEED_HEAL_COLOR = { 0.2, 1, 0.2 };
local FEED_DEATH_COLOR = { 1, 0.25, 0.25 };
-- A raid boss emote, in the chat frame's color for one once the chat colors
-- have loaded, else its default (4.3.4 ChatFrame.cpp: FF DD 00).
local FEED_EMOTE_COLOR = { 1, 0.87, 0 };
-- A player's death on the timeline: its mark, and how many seconds before
-- the death a click on it goes to, to see what killed it.
local DEATH_MARK_WIDTH, DEATH_MARK_HEIGHT = 3, 14;
local DEATH_MARK_COLOR = { 1, 0.15, 0.15, 0.9 };
local DEATH_MARK_LEAD = 5;
-- The playback bar's steps, in seconds: the mouse wheel over the timeline and
-- the Left and Right arrow keys over the window step `small`, `big` with
-- Shift; the back and forward buttons step `big`.
local STEP = { small = 1, big = 10 };
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

local frame, scene, camera;
local selectedLog;    -- a log's name
local segments;       -- { { text, startTime, endTime } }, the whole log first
local selectedSegment;
local units = {};     -- the replay's units, by unitIndex: { name, isPlayer, actor, mount, ... }
local unitOfActor = {};
local duration = 0;
local openTime = 0;   -- where the replay opens and restarts: its first frame with units
local now = 0;
local playing = false;
local playSpeed = DEFAULT_PLAY_SPEED;
local focus;          -- the unit the camera follows
local settingTimeline = false;
local timelineValue, timeSecond; -- what the timeline and the time text show
local pressX, pressY;
local flying = {};     -- the fly keys held, by key name
local combatTime;      -- the replay time the combat state is at, nil before the first
local objects = {};    -- the replay's game objects, by objectIndex: { actor, display, state, ... }
local objectTime;      -- the replay time the objects are at, nil before the first
local areas = {};      -- the replay's ground areas, from GetGroundAreas, each with its actor once shown
local freeBars = {};   -- health bars not in use
local freeCastBars = {}; -- cast bars not in use
local freeRaidIcons = {}; -- raid target icons not in use
local feed;            -- the ScrollingMessageFrame of the log's lines
local deathMarks = {}; -- the timeline's death marks in use
local freeMarks = {};  -- death marks not in use
local unitListChanged; -- a unit was named since the list was filled
local unitFrame;       -- the followed unit's frame (Replay.xml)
local auraButtons = {}; -- its aura buttons, by kind ("buff", "debuff", "tot") and index

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
-- Health bars

local function NewBar()
	local bar = CreateFrame("StatusBar", nil, scene);
	bar:SetWidth(HEALTH_BAR_WIDTH);
	bar:SetHeight(HEALTH_BAR_HEIGHT);
	bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar");
	bar:SetMinMaxValues(0, 1);
	bar:SetFrameLevel(scene:GetFrameLevel() + 5);
	local background = bar:CreateTexture(nil, "BACKGROUND");
	background:SetAllPoints(bar);
	background:SetTexture(0, 0, 0, 0.6);
	return bar;
end

local function HideBar(u)
	if u.bar then
		u.bar:Hide();
		tinsert(freeBars, u.bar);
		u.bar = nil;
	end
end

-- Where a unit's name point appears on the scene, in the scene's units; nil
-- when it is behind the camera or off the scene.
local function ScenePoint(actor)
	local x, y, z = actor:GetNamePosition();
	if not x then
		return nil;
	end
	local px, py, depth = scene:Project3DPointTo2D(x, y, z);
	if not px or depth < 0 or depth > 1 then
		return nil;
	end
	local scale = scene:GetEffectiveScale();
	px, py = px / scale, py / scale;
	if px < 0 or py < 0 or px > scene:GetWidth() or py > scene:GetHeight() then
		return nil;
	end
	return px, py;
end

-- A nameplate's bar color (0x007CBAA0) from the unit's reaction toward the
-- recording player (UnitReaction's 1 to 8): hostile red, else a player blue,
-- friendly green, the rest yellow. With no reaction the unit counts as
-- neutral.
local function PlateColor(u, reaction)
	reaction = reaction or 4;
	if reaction <= 2 then
		return PLATE_COLORS.hostile;
	elseif u.isPlayer then
		return PLATE_COLORS.player;
	elseif reaction >= 5 then
		return PLATE_COLORS.friendly;
	end
	return PLATE_COLORS.neutral;
end

local function ShowHealthBar(u, px, py, health, maxHealth, reaction)
	local bar = u.bar;
	if not bar then
		bar = tremove(freeBars) or NewBar();
		u.bar = bar;
	end
	local color = PlateColor(u, reaction);
	bar:SetStatusBarColor(color[1], color[2], color[3]);
	bar:SetValue(health / maxHealth);
	bar:ClearAllPoints();
	bar:SetPoint("BOTTOM", scene, "BOTTOMLEFT", px, py + HEALTH_BAR_RISE);
	bar:Show();
end

local function NewCastBar()
	local bar = CreateFrame("StatusBar", nil, scene);
	bar:SetWidth(CAST_BAR_WIDTH);
	bar:SetHeight(CAST_BAR_HEIGHT);
	bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar");
	bar:SetMinMaxValues(0, 1);
	bar:SetFrameLevel(scene:GetFrameLevel() + 5);
	local background = bar:CreateTexture(nil, "BACKGROUND");
	background:SetAllPoints(bar);
	background:SetTexture(0, 0, 0, 0.6);
	bar.Text = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall");
	bar.Text:SetPoint("CENTER", bar, "CENTER", 0, 0);
	bar.Text:SetWidth(CAST_BAR_WIDTH);
	bar.Text:SetHeight(CAST_BAR_HEIGHT);
	return bar;
end

local function HideCastBar(u)
	if u.castBar then
		u.castBar:Hide();
		tinsert(freeCastBars, u.castBar);
		u.castBar = nil;
	end
end

-- What an interrupted cast bar says: who interrupted it in their name color,
-- as retail's GetInterruptText does with SPELL_INTERRUPTED_BY.
local function InterruptText(interrupterIndex)
	local by = interrupterIndex and units[interrupterIndex];
	if not by or by.name == "" then
		return INTERRUPTED;
	end
	local color = by.nameColor;
	return string.format(SPELL_INTERRUPTED_BY, string.format("|cff%02x%02x%02x%s|r", math.floor(color[1] * 255),
		math.floor(color[2] * 255), math.floor(color[3] * 255), by.name));
end

-- How the unit's cast bar looks at `time`: its fill, color, text and alpha;
-- nil when it shows none.
local function CastBarState(u, time)
	local spellID, startTime, endTime, stopTime, isChannel, interrupted, interrupterIndex =
		C_CombatLogReplay.GetUnitCast(u.index, time);
	if not spellID then
		return nil;
	end
	if not stopTime or time < stopTime then
		local length = endTime - startTime;
		local done = length > 0 and math.min(math.max((time - startTime) / length, 0), 1) or 1;
		if isChannel then
			return 1 - done, CHANNEL_COLOR, GetSpellInfo(spellID), 1;
		end
		return done, CAST_COLOR, GetSpellInfo(spellID), 1;
	end
	local since = time - stopTime;
	local hold = interrupted and CAST_INTERRUPTED_HOLD or CAST_DONE_HOLD;
	if since >= hold + CAST_FADE then
		return nil;
	end
	local alpha = since < hold and 1 or 1 - (since - hold) / CAST_FADE;
	if interrupted then
		return 1, CAST_INTERRUPTED_COLOR, InterruptText(interrupterIndex), alpha;
	end
	return 1, CAST_DONE_COLOR, GetSpellInfo(spellID), alpha;
end

local function ShowCastBar(u, px, py, fill, color, text, alpha)
	local bar = u.castBar;
	if not bar then
		bar = tremove(freeCastBars) or NewCastBar();
		u.castBar = bar;
	end
	bar:SetValue(fill);
	bar:SetStatusBarColor(color[1], color[2], color[3]);
	bar.Text:SetText(text or "");
	bar:SetAlpha(alpha);
	local rise = HEALTH_BAR_RISE + (u.bar and HEALTH_BAR_HEIGHT + CAST_BAR_GAP or 0);
	bar:ClearAllPoints();
	bar:SetPoint("BOTTOM", scene, "BOTTOMLEFT", px, py + rise);
	bar:Show();
end

local function HideRaidIcon(u)
	if u.raidIcon then
		u.raidIcon:Hide();
		tinsert(freeRaidIcons, u.raidIcon);
		u.raidIcon = nil;
	end
end

-- A frame over the scene's models, as the bars are.
local function NewRaidIcon()
	local icon = CreateFrame("Frame", nil, scene);
	icon:SetWidth(RAID_ICON_SIZE);
	icon:SetHeight(RAID_ICON_SIZE);
	icon:SetFrameLevel(scene:GetFrameLevel() + 5);
	icon.Texture = icon:CreateTexture(nil, "ARTWORK");
	icon.Texture:SetAllPoints(icon);
	icon.Texture:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcons");
	return icon;
end

-- The unit's raid target icon over its bars, as the client draws one
-- (SetRaidTargetIconTexture).
local function ShowRaidIcon(u, px, py, rise)
	local icon = u.raidIcon;
	if not icon then
		icon = tremove(freeRaidIcons) or NewRaidIcon();
		u.raidIcon = icon;
	end
	SetRaidTargetIconTexture(icon.Texture, u.mark);
	icon:ClearAllPoints();
	icon:SetPoint("BOTTOM", scene, "BOTTOMLEFT", px, py + rise);
	icon:Show();
end

-- Over a living unit's name: its health below its full health, its cast bar
-- while it casts or channels, and its raid target mark above them.
local function ShowBars(u, time, isDead)
	local health, maxHealth, fill, color, text, alpha;
	if not isDead then
		health, maxHealth = C_CombatLogReplay.GetUnitHealth(u.index, time);
		fill, color, text, alpha = CastBarState(u, time);
	end
	local hurt = health and health < maxHealth;
	local marked = not isDead and u.mark;
	local px, py;
	if hurt or fill or marked then
		px, py = ScenePoint(u.actor);
	end
	if not px then
		HideBar(u);
		HideCastBar(u);
		HideRaidIcon(u);
		return;
	end
	if hurt then
		local _, _, _, _, _, _, reaction = C_CombatLogReplay.GetUnitState(u.index, time);
		ShowHealthBar(u, px, py, health, maxHealth, reaction);
	else
		HideBar(u);
	end
	if fill then
		ShowCastBar(u, px, py, fill, color, text, alpha);
	else
		HideCastBar(u);
	end
	if marked then
		local rise = HEALTH_BAR_RISE + (u.bar and HEALTH_BAR_HEIGHT + CAST_BAR_GAP or 0)
			+ (u.castBar and CAST_BAR_HEIGHT + RAID_ICON_GAP or 0);
		ShowRaidIcon(u, px, py, rise);
	else
		HideRaidIcon(u);
	end
end

------------------------------------------------------------------------------
-- Units

local function ReleaseUnits()
	combatTime = nil;
	objects, objectTime, areas = {}, nil, {};
	for i = 1, #units do
		HideBar(units[i]);
		HideCastBar(units[i]);
		HideRaidIcon(units[i]);
	end
	scene:ReleaseAllActors();
	units = {};
	unitOfActor = {};
	focus = nil;
	unitFrame:Hide();
end

-- A pooled actor keeps what ApplyFromModelSceneActorInfo set at its last
-- use: scene 4's "unwrapped" actor centres its model on its position
-- (useCenterForOriginZ, which sinks a unit by half its height) and carries a
-- display info's scale, which the mixin applies again whenever a model
-- loads. A new one stands on its position at its own size.
local function NewActor()
	local actor = scene:AcquireActor();
	actor:SetUseCenterForOrigin(false, false, false);
	actor:SetPitch(0);
	actor:SetRoll(0);
	actor:SetAlpha(1);
	actor:StopAnimationKit();
	actor:SetSpellVisualKit(nil);
	actor:SetNormalizedScaleAggressiveness(0);
	actor:SetRequestedScale(1);
	actor:SetCreatureID(0);
	actor:SetAttacking(false);
	actor:SetMoving(false);
	return actor;
end

local function Acquire(u)
	local actor = NewActor();
	unitOfActor[actor] = u;
	return actor;
end


local function HideUnit(u)
	HideBar(u);
	HideCastBar(u);
	HideRaidIcon(u);
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
	-- A seated rider's place and facing are relative to its seat, so it sits
	-- at the seat itself, not at the map place it last walked to. A model
	-- that has not loaded yet seats nothing; try again next frame.
	if not u.seated then
		u.actor:SetPosition(0, 0, 0);
		u.actor:SetYaw(0);
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

-- A creature holds what its unit held (a player holds its items): set again
-- with each new model, and when what it holds changes.
local function Hold(u, time, held1, held2, held3)
	if u.held1 == held1 and u.held2 == held2 and u.held3 == held3 then
		return;
	end
	u.held1, u.held2, u.held3 = held1, held2, held3;
	for _, slot in ipairs(HELD_SLOTS) do
		u.actor:SetHeldItem(slot, C_CombatLogReplay.GetUnitHeldItem(u.index, time, slot));
	end
end

local function ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
		mountDisplay, scale, standState, sheathState, isDead, held1, held2, held3)
	local actor = u.actor;
	if not actor then
		actor = Acquire(u);
		u.actor = actor;
		u.named = nil;
		-- The creature's own data keeps its flinches and parries off.
		actor:SetCreatureID(u.creatureID or 0);
		for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(u.index, time)) do
			actor:AddAuraVisual(aura.spellID);
		end
	end
	if not actor:IsShown() then
		actor:Show();
	end
	-- A dead unit shows no name.
	local named = not isDead;
	if u.named ~= named then
		if named then
			local color = u.nameColor;
			actor:SetNameText(u.name, color[1], color[2], color[3]);
		else
			actor:ClearNameText();
		end
		u.named = named;
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
		u.held1, u.held2, u.held3, u.sheathState = nil, nil, nil, nil;
	end
	if not appearance then
		Hold(u, time, held1, held2, held3);
	end
	-- Weapons in hand or sheathed as the unit held them; 2 is the ranged
	-- weapon drawn.
	if u.sheathState ~= sheathState then
		actor:SetSheatheState(sheathState);
		u.sheathState = sheathState;
	end
	-- Through the mixin, which sets it again once the model has loaded.
	if u.scale ~= scale then
		actor:SetRequestedScale(scale);
		u.scale = scale;
	end
	-- What the actor's swings and reactions read: whether it moves, and
	-- whether it swings at a target.
	u.moving = speed > MOVING_SPEED or falling;
	actor:SetMoving(u.moving);
	local _, _, _, _, _, attackTarget = C_CombatLogReplay.GetUnitState(u.index, time);
	actor:SetAttacking(attackTarget ~= nil);
	local body = SetMount(u, mountDisplay);
	body:SetPosition(x, y, z);
	body:SetYaw(facing);
	local animation;
	if isDead then
		animation = u.dyingUntil and GetTime() < u.dyingUntil and ANIM_DEATH or ANIM_DEAD;
	elseif falling then
		animation = jumping and ANIM_JUMP or ANIM_FALL;
	elseif speed > MOVING_SPEED then
		animation = speed >= RUN_SPEED and ANIM_RUN or ANIM_WALK;
	elseif attackTarget and mountDisplay == 0 then
		-- The pick's attacking leg (0x005FD360): an unmounted attacker
		-- stands ready. Its swings play over this.
		local itemClass, subclass = C_CombatLogReplay.GetUnitWeapons(u.index, time);
		animation = itemClass == ITEM_CLASS_WEAPON and READY_BY_SUBCLASS[subclass] or ANIM_READY_UNARMED;
	else
		animation = STAND_ANIMATION[standState] or ANIM_STAND;
	end
	if u.animation ~= animation then
		body:SetAnimation(animation);
		u.animation = animation;
	end
end

------------------------------------------------------------------------------
-- Game objects and ground areas

-- Puts every game object where it was at `time`, in its state. A state that
-- changes as the replay plays plays its open or close animation first.
local function ApplyObjects(time)
	local played = objectTime and time >= objectTime and time - objectTime <= MAX_PLAYED_STEP;
	objectTime = time;
	local GetGameObjectInfo = C_CombatLogReplay.GetGameObjectInfo;
	for i = 1, #objects do
		local o = objects[i];
		local x, y, z, yaw, pitch, roll, scale, display, _, state = GetGameObjectInfo(i, time);
		if x and o.display ~= display then
			o.actor = o.actor or NewActor();
			o.hasModel = o.actor:SetModelByGameObjectDisplayID(display);
			o.display, o.state, o.x = display, nil, nil;
		end
		if x and o.hasModel then
			local actor = o.actor;
			if not actor:IsShown() then
				actor:Show();
			end
			if o.x ~= x or o.y ~= y or o.z ~= z or o.yaw ~= yaw or o.pitch ~= pitch or o.roll ~= roll
					or o.scale ~= scale then
				actor:SetPosition(x, y, z);
				actor:SetYaw(yaw);
				actor:SetPitch(pitch);
				actor:SetRoll(roll);
				actor:SetRequestedScale(scale);
				o.x, o.y, o.z, o.yaw, o.pitch, o.roll, o.scale = x, y, z, yaw, pitch, roll, scale;
			end
			if o.state ~= state then
				actor:SetGameObjectState(state, played and o.state or nil);
				o.state = state;
			end
		elseif o.actor and o.actor:IsShown() then
			o.actor:Hide();
			o.state = nil;
		end
	end
end

-- Shows each spell's ground area while it was there.
local function ApplyAreas(time)
	for _, area in ipairs(areas) do
		if time >= area.startTime and (not area.endTime or time < area.endTime) then
			if not area.actor then
				area.actor = NewActor();
				area.hasModel = area.actor:SetModelBySpellArea(area.spellID, area.radius);
				area.actor:SetPosition(area.x, area.y, area.z);
				area.actor:SetYaw(area.facing);
			end
			if area.hasModel and not area.actor:IsShown() then
				area.actor:Show();
			end
		elseif area.actor and area.actor:IsShown() then
			area.actor:Hide();
		end
	end
end

------------------------------------------------------------------------------
-- Combat

-- The shown actor of a unit index, or nil.
local function ShownActor(index)
	local u = index and units[index];
	local actor = u and u.actor;
	return actor and actor:IsShown() and actor or nil, u;
end

-- A unit in a feed line: a link that follows it, a player's in its class
-- color.
local function UnitLink(index)
	local u = index and units[index];
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
		return string.format(UNITDIESOTHER, dest), FEED_DEATH_COLOR;
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
		feed:AddMessage("|Hreplay:" .. e.time .. "|h[" .. FormatTime(e.time) .. "]|h " .. text, color[1], color[2],
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
			local castMs = e.castTime and e.castTime * 1000 / PLAY_SPEEDS[playSpeed];
			if e.castFailed then
				-- Its due end, when the logs give one past the stop; else the
				-- spell's own cast time (nil), which runs past the stop too.
				local _, _, endTime = C_CombatLogReplay.GetUnitCast(e.sourceIndex, e.time);
				castMs = endTime and endTime > e.time + e.castTime
					and (endTime - e.time) * 1000 / PLAY_SPEEDS[playSpeed] or nil;
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
				channelMs = stopTime and (stopTime - startTime) * 1000 / PLAY_SPEEDS[playSpeed];
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
			du.dyingUntil = GetTime() + DYING_TIME / PLAY_SPEEDS[playSpeed];
		end
	end
end

-- Each unit's combat state as it was at `time`: no cast or swing under way,
-- its auras then; a dead unit lies still.
local function SetCombatState(time)
	-- The feed holds the lines just before then.
	feed:Clear();
	for _, e in ipairs(C_CombatLogReplay.GetEvents(math.max(0, time - FEED_SECONDS), time)) do
		AddFeedLine(e);
	end
	for i = 1, #units do
		local u = units[i];
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
	for i = 1, #units do
		local u = units[i];
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
	local speed = PLAY_SPEEDS[playSpeed];
	for i = 1, #units do
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
	if combatTime and time >= combatTime and time - combatTime <= MAX_PLAYED_STEP then
		if time > combatTime then
			for _, e in ipairs(C_CombatLogReplay.GetEvents(combatTime, time)) do
				PlayEvent(e);
			end
			PlayUnloggedCasts(combatTime, time);
			StopInterrupted(time);
		end
	else
		SetCombatState(time);
	end
	combatTime = time;
end

-- Puts every unit where it was at `time`.
local function Apply(time)
	local GetUnitPosition = C_CombatLogReplay.GetUnitPosition;
	local GetUnitLook = C_CombatLogReplay.GetUnitLook;
	for i = 1, #units do
		local u = units[i];
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
			ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
				mountDisplay, scale, standState, sheathState, isDead, held1, held2, held3);
			ShowBars(u, time, isDead);
		else
			HideUnit(u);
		end
	end
	ApplyObjects(time);
	ApplyAreas(time);
	Combat(time);
end

------------------------------------------------------------------------------
-- Camera

local function Aim(x, y, z)
	camera:SetTarget(x, y, z);
	camera:SetZoomDistance(CAMERA_ZOOM);
	camera:SnapAllInterpolatedValues();
end

-- The camera follows the followed unit's moving body (its mount while it
-- rides) as the world's camera follows a unit: at its pivot height, stopped
-- in front of the walls between it and the eye. With no unit, or one out of
-- view, it is a free orbit camera where it was.
local function FollowFocus()
	local u = focus;
	local body = u and u.actor and u.actor:IsShown() and (u.seated and u.mount or u.actor) or nil;
	if camera:GetFollowActor() ~= body then
		camera:SetFollowActor(body);
	end
end

------------------------------------------------------------------------------
-- The unit frame

-- A unit's portrait: its actor's, drawn from the actor's model as the client
-- draws a unit's, and drawn again by the actor when its model changes.
-- Before the unit has an actor, a player in its own body shows the stand-in
-- the client shows for a player it does not hold (0x00525BA0):
-- TemporaryPortrait-<Male|Female>-<race file>; a creature, or a player in a
-- form, its display's portrait.
local function SetPortrait(texture, u)
	if u.actor then
		if texture.key ~= u.actor then
			texture.key = u.actor;
			texture:SetTexture(nil);
			u.actor:SetPortraitTexture(texture);
		end
		return;
	end
	local appearance = u.isPlayer and u.display == u.nativeDisplay and AppearanceAt(u, now);
	local race = appearance and C_CreatureInfo.GetRaceInfo(appearance.raceID);
	local key = race and (race.clientFileString .. appearance.sex) or u.display;
	if texture.key == key then
		return;
	end
	texture.key = key;
	texture:SetTexture(nil);
	if race then
		texture:SetTexture(string.format(STAND_IN_PORTRAIT, appearance.sex == 1 and "Female" or "Male",
			race.clientFileString));
	elseif u.display and u.display > 0 then
		SetPortraitTextureFromCreatureDisplayID(texture, u.display);
	end
end

-- The unit's health and power at `time` on two bars; false when the log has
-- no health for it. Rage is kept in tenths, and shows as the client shows it.
local function SetBars(health, mana, u, time)
	local current, maximum = C_CombatLogReplay.GetUnitHealth(u.index, time);
	if u.dead then
		current = 0;
	end
	if not maximum or maximum <= 0 then
		health:Hide();
		mana:Hide();
		return false;
	end
	health:SetMinMaxValues(0, maximum);
	health:SetValue(current);
	health.value, health.maximum = current, maximum;
	health:Show();
	local powerType, power, maxPower = C_CombatLogReplay.GetUnitPower(u.index, time);
	if not powerType or not maxPower or maxPower <= 0 then
		mana:Hide();
		return true;
	end
	if powerType == UNIT_FRAME.powerRage then
		power, maxPower = math.floor(power / 10), math.floor(maxPower / 10);
	end
	local color = ManaBarColor[powerType] or ManaBarColor[0];
	mana:SetStatusBarColor(color.r, color.g, color.b);
	mana:SetMinMaxValues(0, maxPower);
	mana:SetValue(power);
	mana.value, mana.maximum = power, maxPower;
	mana:Show();
	return true;
end

-- A bar's "value / maximum", always shown: a replay is for reading numbers.
local function UpdateBarText(bar)
	if bar.value then
		bar.Text:SetText(bar.value .. " / " .. bar.maximum);
	end
end

local function OnAuraEnter(button)
	GameTooltip:SetOwner(button, "ANCHOR_BOTTOMRIGHT", 15, -25);
	GameTooltip:SetSpellByID(button.spellID);
end

local function OnAuraLeave()
	GameTooltip:Hide();
end

local function AuraButton(kind, i, parent)
	local buttons = auraButtons[kind];
	if not buttons then
		buttons = {};
		auraButtons[kind] = buttons;
	end
	local button = buttons[i];
	if not button then
		button = CreateFrame("Button", nil, parent, "ClassicAPIReplayAuraTemplate");
		button:SetScript("OnEnter", OnAuraEnter);
		button:SetScript("OnLeave", OnAuraLeave);
		buttons[i] = button;
	end
	return button;
end

-- The time an aura has left as a cooldown's countdown shows it: whole
-- seconds below a minute, then minutes, then hours, each rounded up.
local function AuraTimeText(left)
	if left < 60 then
		return tostring(math.ceil(left));
	elseif left < 3600 then
		return math.ceil(left / 60) .. "m";
	end
	return math.ceil(left / 3600) .. "h";
end

-- The aura's timer at `time`, from when it ended in the log: the sweep of
-- its duration, paused where the replay is (CooldownFrame_SetDisplayAsPercentage),
-- so it follows the replay's speed and pauses; with `showTime`, the time
-- left. None for an aura whose end the replay does not know.
local function SetAuraTimer(button, aura, time, showTime)
	local left = aura.expirationTime and aura.expirationTime - time;
	if not left or left <= 0 then
		button.Cooldown:Clear();
		button.TextFrame.Time:SetText("");
		return;
	end
	if aura.duration and aura.duration > 0 then
		CooldownFrame_SetDisplayAsPercentage(button.Cooldown, 1 - left / aura.duration);
	else
		button.Cooldown:Clear();
	end
	button.TextFrame.Time:SetText(showTime and AuraTimeText(left) or "");
end

-- Shows an aura on a button, as TargetDebuffButton_Update does: a debuff
-- with its border in its dispel type's color and its stacks above 1; then
-- its timer, the time left only on a full-size button.
local function SetAura(button, aura, size, time)
	button.spellID = aura.spellID;
	local _, _, icon = GetSpellInfo(aura.spellID);
	button.Icon:SetTexture(icon or UNKNOWN_SPELL_ICON);
	button:SetWidth(size);
	button:SetHeight(size);
	if aura.auraType == "DEBUFF" then
		local color = DebuffTypeColor[UNIT_FRAME.dispelNames[C_Spell.GetSpellDispelType(aura.spellID)] or "none"];
		button.Border:SetVertexColor(color.r, color.g, color.b);
		button.Border:SetWidth(size + 2);
		button.Border:SetHeight(size + 2);
		button.Border:Show();
		if aura.applications and aura.applications > 1 then
			button.TextFrame.Count:SetText(aura.applications);
			button.TextFrame.Count:Show();
		else
			button.TextFrame.Count:Hide();
		end
	else
		button.Border:Hide();
		button.TextFrame.Count:Hide();
	end
	SetAuraTimer(button, aura, time, size >= UNIT_FRAME.auraSmallSize);
	button:Show();
end

local function HideAuras(kind, from)
	for i = from, #(auraButtons[kind] or {}) do
		auraButtons[kind][i]:Hide();
	end
end

-- The unit's buffs and debuffs under the frame, in TargetDebuffButton_Update's
-- rows: the buffs, then the debuffs below them.
local function SetAuras(u, time, targetShown)
	local buffs, debuffs = {}, {};
	for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(u.index, time)) do
		if aura.isHidden then
			-- Not one UnitBuff or UnitDebuff lists.
		elseif aura.auraType == "DEBUFF" then
			if #debuffs < UNIT_FRAME.debuffs then
				tinsert(debuffs, aura);
			end
		elseif #buffs < UNIT_FRAME.buffs then
			tinsert(buffs, aura);
		end
	end
	local wrap = targetShown and 5 or 6;
	local crowded = (targetShown and #buffs == UNIT_FRAME.buffs) or #debuffs >= wrap;
	local size = crowded and UNIT_FRAME.auraSmallSize or UNIT_FRAME.auraSize;
	local rowStart;
	for i, aura in ipairs(buffs) do
		local button = AuraButton("buff", i, unitFrame);
		SetAura(button, aura, size, time);
		button:ClearAllPoints();
		if i == 1 then
			button:SetPoint("TOPLEFT", unitFrame, "BOTTOMLEFT", 5, 32);
			rowStart = button;
		else
			button:SetPoint("LEFT", auraButtons.buff[i - 1], "RIGHT", UNIT_FRAME.auraGap, 0);
		end
	end
	HideAuras("buff", #buffs + 1);
	for i, aura in ipairs(debuffs) do
		local button = AuraButton("debuff", i, unitFrame);
		SetAura(button, aura, size, time);
		button:ClearAllPoints();
		if i == 1 then
			if rowStart then
				button:SetPoint("TOPLEFT", rowStart, "BOTTOMLEFT", 0, -2);
			else
				button:SetPoint("TOPLEFT", unitFrame, "BOTTOMLEFT", 5, 32);
			end
			rowStart = button;
		elseif (i - 1) % wrap == 0 then
			button:SetPoint("TOPLEFT", rowStart, "BOTTOMLEFT", 0, -2);
			rowStart = button;
		else
			button:SetPoint("LEFT", auraButtons.debuff[i - 1], "RIGHT", UNIT_FRAME.auraGap, 0);
		end
	end
	HideAuras("debuff", #debuffs + 1);
end

-- The target of target: its portrait, name, bars and first debuffs, as
-- TargetofTargetFrame shows them.
local function SetTargetOfTarget(target, time)
	local tot = unitFrame.TargetOfTarget;
	tot.unit = target;
	SetPortrait(tot.Portrait, target);
	tot.TextureFrame.Name:SetText(target.name ~= "" and target.name or UNKNOWN);
	if target.dead then
		tot.TextureFrame.Dead:Show();
	else
		tot.TextureFrame.Dead:Hide();
	end
	SetBars(tot.HealthBar, tot.ManaBar, target, time);
	local shown = 0;
	for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(target.index, time)) do
		if aura.auraType == "DEBUFF" and not aura.isHidden and shown < UNIT_FRAME.totDebuffs then
			shown = shown + 1;
			local button = AuraButton("tot", shown, tot);
			SetAura(button, aura, UNIT_FRAME.totDebuffSize, time);
			button:ClearAllPoints();
			if shown == 1 then
				button:SetPoint("TOPLEFT", tot, "TOPRIGHT", 4, -10);
			elseif shown == 3 then
				button:SetPoint("TOPLEFT", auraButtons.tot[1], "BOTTOMLEFT", 0, -1);
			else
				button:SetPoint("LEFT", auraButtons.tot[shown - 1], "RIGHT", 1, 0);
			end
		end
	end
	HideAuras("tot", shown + 1);
	tot:Show();
end

-- The followed unit at `time`, or no frame while none is followed.
local function UpdateUnitFrame(time)
	local u = focus;
	if not u then
		unitFrame:Hide();
		return;
	end
	local f = unitFrame.TextureFrame;
	SetPortrait(unitFrame.Portrait, u);
	f.Name:SetText(u.name ~= "" and u.name or UNKNOWN);
	local target, _, flags, level, _, _, _, isTapDenied = C_CombatLogReplay.GetUnitState(u.index, time);
	-- The name's background as TargetFrame_CheckFaction sets it: grey with a
	-- grey portrait for a creature another player tapped, else the selection
	-- color.
	local tapped = isTapDenied and bit.band(flags, UNIT_FRAME.playerControlledFlag) == 0;
	local r, g, b;
	if tapped then
		r, g, b = UNIT_FRAME.tappedColor[1], UNIT_FRAME.tappedColor[2], UNIT_FRAME.tappedColor[3];
	else
		r, g, b = C_CombatLogReplay.GetUnitSelectionColor(u.index, time);
		if not r then
			r, g, b = UNIT_FRAME.noSelectionColor[1], UNIT_FRAME.noSelectionColor[2], UNIT_FRAME.noSelectionColor[3];
		end
	end
	unitFrame.NameBackground:SetVertexColor(r, g, b);
	local shade = tapped and UNIT_FRAME.tappedColor[1] or 1;
	unitFrame.Portrait:SetVertexColor(shade, shade, shade);
	if level and level > 0 then
		f.Level:SetText(level);
		f.Level:SetVertexColor(UNIT_FRAME.levelColor[1], UNIT_FRAME.levelColor[2], UNIT_FRAME.levelColor[3]);
		f.Level:Show();
	else
		f.Level:Hide();
	end
	if u.dead then
		f.Dead:Show();
	else
		f.Dead:Hide();
	end
	if u.mark then
		SetRaidTargetIconTexture(f.RaidTargetIcon, u.mark);
		f.RaidTargetIcon:Show();
	else
		f.RaidTargetIcon:Hide();
	end
	SetBars(unitFrame.HealthBar, unitFrame.ManaBar, u, time);
	UpdateBarText(unitFrame.HealthBar);
	UpdateBarText(unitFrame.ManaBar);
	target = target and units[target];
	if target then
		SetTargetOfTarget(target, time);
	else
		unitFrame.TargetOfTarget:Hide();
		HideAuras("tot", 1);
	end
	SetAuras(u, time, target ~= nil);
	unitFrame:Show();
end

-- Raises a frame and everything in it, each child one level above its
-- parent as at load: a child keeps its level when its parent's changes.
local function SetFrameLevels(f, level)
	f:SetFrameLevel(level);
	local children = { f:GetChildren() };
	for _, child in ipairs(children) do
		SetFrameLevels(child, level + 1);
	end
end

------------------------------------------------------------------------------
-- The unit list

-- A row shows its unit's name in its color (grey once dead), faded while the
-- unit is out of view, and marked while the camera follows it.
local function UpdateUnitRow(row)
	local u = row.unit;
	local color = u.dead and DEAD_NAME_COLOR or u.nameColor;
	row.Text:SetTextColor(color[1], color[2], color[3]);
	row:SetAlpha(u.inView and 1 or OUT_OF_VIEW_ALPHA);
	if u.mark then
		SetRaidTargetIconTexture(row.Mark, u.mark);
		row.Mark:Show();
	else
		row.Mark:Hide();
	end
	if u == focus then
		row.Selected:Show();
	else
		row.Selected:Hide();
	end
end

local function UpdateUnitList()
	frame.UnitList.ScrollBox:ForEachFrame(UpdateUnitRow);
end

local function SetFocus(u)
	if focus and focus.actor then
		focus.actor:SetHighlight("target", false);
	end
	focus = u;
	if focus and focus.actor then
		focus.actor:SetHighlight("target", true);
	end
	FollowFocus();
	UpdateUnitList();
	UpdateUnitFrame(now);
	if focus then
		frame.UnitList.ScrollBox:ScrollToElementData(focus, ScrollBoxConstants.AlignNearest);
	end
end

local function OnUnitRowClick(row)
	SetFocus(row.unit);
end

local function InitUnitRow(row, u)
	row.unit = u;
	row.Text:SetText(u.name ~= "" and u.name or UNKNOWN);
	row:SetScript("OnClick", OnUnitRowClick);
	UpdateUnitRow(row);
end

-- Players first, then creatures, each by name; units without a name last.
local function UnitOrder(a, b)
	if (a.name == "") ~= (b.name == "") then
		return b.name == "";
	end
	if not a.isPlayer ~= not b.isPlayer then
		return not b.isPlayer;
	end
	if a.name ~= b.name then
		return a.name < b.name;
	end
	return a.index < b.index;
end

local function FillUnitList()
	local list = {};
	for i = 1, #units do
		list[i] = units[i];
	end
	table.sort(list, UnitOrder);
	frame.UnitList.ScrollBox:SetDataProvider(CreateDataProvider(list), ScrollBoxConstants.RetainScrollPosition);
	unitListChanged = false;
end

-- A creature the log does not name and the client has not cached is asked
-- for; NameCreatures names its units when it arrives.
local function RequestName(unit)
	if unit.name == "" and unit.creatureID then
		C_CreatureInfo.RequestLoadCreatureByID(unit.creatureID);
	end
end

local function NameCreatures(creatureID)
	local info = C_CreatureInfo.GetCreatureInfoByID(creatureID);
	if not info or not info.name or info.name == "" then
		return;
	end
	for i = 1, #units do
		local u = units[i];
		if u.creatureID == creatureID and u.name == "" then
			u.name = info.name;
			u.named = nil;
			unitListChanged = true;
		end
	end
end

-- A player's class color, else the name color of its kind.
local function NameColor(isPlayer, appearances)
	local appearance = appearances and appearances[1];
	local info = appearance and C_CreatureInfo.GetClassInfo(appearance.classID);
	local color = info and RAID_CLASS_COLORS[info.classFile];
	if color then
		return { color.r, color.g, color.b };
	end
	return isPlayer and PLAYER_NAME_COLOR or CREATURE_NAME_COLOR;
end

------------------------------------------------------------------------------
-- The map

-- The camera sees as far as the world's camera: the farclip setting, which
-- also sets how much ground the scene keeps loaded.
local function SetFarClip()
	local farClip = tonumber(GetCVar("farclip"));
	if farClip then
		scene:SetCameraFarClip(farClip);
	end
end

-- The middle of where the units are over the replay: the median, not the
-- middle of all of them, since the log can hold a unit far from the rest
-- (one Stormwind log had a player 9,900 yards away, and players on flight
-- paths leave the city), which would put the middle in a mountain.

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
	if #xs == 0 then
		return nil;
	end
	return Median(xs), Median(ys), Median(zs);
end

-- Shows the replay's map: the map's WMO when it is one, else its ground,
-- which follows the camera. Aims the camera at the middle of the units.
local function ShowMap(mapID)
	scene:ClearWorldModel();
	scene:ClearWorldTerrain();
	local x, y, z = UnitArea();
	if mapID < 0 then
		SetStatus(REPLAY_NO_MAP);
	elseif not scene:SetWorldModelByMapID(mapID) and not scene:SetWorldTerrain(mapID) then
		SetStatus(string.format(REPLAY_NO_GROUND, mapID));
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

-- Where the timeline's thumb centre sits for a time, from the slider's left
-- edge, and how far it travels: the thumb stays inside the slider, so its
-- centre runs half a thumb in from each end. The span comes from the
-- slider's edges: GetWidth gives its XML size (10), not its anchored width.
local function TimelinePlace(time)
	local timeline = frame.Timeline;
	local left, right = timeline:GetLeft(), timeline:GetRight();
	local span = left and right and right - left or timeline:GetWidth();
	local thumb = timeline:GetThumbTexture():GetWidth();
	local travel = math.max(span - thumb, 1);
	return thumb / 2 + travel * (duration > 0 and time / duration or 0), travel;
end

-- The timeline's thumb moves once it would move a pixel, and the time text
-- once its second changes: setting the slider runs its scripts (about half a
-- millisecond a call), too much for every frame. `force` sets both.
local function UpdateTimeline(force)
	local _, travel = TimelinePlace(0);
	local step = duration / travel;
	if force or not timelineValue or math.abs(now - timelineValue) >= step then
		settingTimeline = true;
		frame.Timeline:SetValue(now);
		settingTimeline = false;
		timelineValue = now;
	end
	local second = math.floor(now);
	if force or second ~= timeSecond then
		frame.Time:SetText(string.format(REPLAY_TIME, FormatTime(now), FormatTime(duration)));
		timeSecond = second;
	end
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

local function OnUpdate(self, elapsed)
	local pitch = camera:GetPitch();
	if math.abs(pitch) > MAX_PITCH then
		camera:SetPitch(pitch > 0 and MAX_PITCH or -MAX_PITCH);
		camera:SnapToTargetInterpolationPitch();
	end
	Fly(elapsed);
	if unitListChanged then
		FillUnitList();
	end
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
	UpdateUnitList();
	UpdateUnitFrame(now);
	UpdateTimeline(now >= duration);
end

local function Seek(time)
	now = math.max(0, math.min(duration, time));
	Apply(now);
	FollowFocus();
	UpdateUnitList();
	UpdateUnitFrame(now);
	UpdateTimeline(true);
end

-- Moves the replay by `seconds`, once one is loaded.
local function Step(seconds)
	if #units > 0 then
		Seek(now + seconds);
	end
end

local function StepSize()
	return IsShiftKeyDown() and STEP.big or STEP.small;
end

-- A fly key pressed over the scene is the scene's, and Left or Right over
-- the window steps the replay; every other key goes on to the game.
local function OnSceneKeyDown(self, key)
	local fly = FLY_KEYS[key] and MouseIsOver(scene);
	local step = (key == "LEFT" or key == "RIGHT") and MouseIsOver(frame);
	self:SetPropagateKeyboardInput(not fly and not step);
	if fly then
		flying[key] = true;
	elseif step then
		Step(key == "LEFT" and -StepSize() or StepSize());
	end
end

local function OnSceneKeyUp(self, key)
	flying[key] = nil;
end

------------------------------------------------------------------------------
-- Death marks

local function OnMarkEnter(mark)
	GameTooltip:SetOwner(mark, "ANCHOR_TOP");
	GameTooltip:SetText(string.format(UNITDIESOTHER, mark.unit.name), FEED_DEATH_COLOR[1], FEED_DEATH_COLOR[2],
		FEED_DEATH_COLOR[3]);
	GameTooltip:AddLine(FormatTime(mark.time), 1, 1, 1);
	GameTooltip:Show();
end

local function OnMarkLeave()
	GameTooltip:Hide();
end

-- A click goes to just before the death and follows the player.
local function OnMarkClick(mark)
	SetFocus(mark.unit);
	Seek(mark.time - DEATH_MARK_LEAD);
end

local function NewMark()
	local mark = CreateFrame("Button", nil, frame.Timeline);
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
	if duration <= 0 then
		return;
	end
	for i = 1, #units do
		local u = units[i];
		if u.isPlayer then
			for _, time in ipairs(C_CombatLogReplay.GetUnitDeaths(i)) do
				local mark = tremove(freeMarks) or NewMark();
				mark.unit, mark.time = u, time;
				mark:ClearAllPoints();
				mark:SetPoint("CENTER", frame.Timeline, "LEFT", (TimelinePlace(time)), 0);
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
	local timeline = frame.Timeline;
	local ghost = timeline.Ghost;
	local shownSecond;
	-- The inverse of TimelinePlace.
	local function TimeAtCursor()
		local x = GetCursorPosition() / timeline:GetEffectiveScale() - timeline:GetLeft();
		local start, travel = TimelinePlace(0);
		return math.max(0, math.min(duration, (x - start) / travel * duration));
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
			GameTooltip:SetText(FormatTime(time), 1, 1, 1);
			shownSecond = second;
		end
	end
	timeline:SetScript("OnEnter", function()
		if #units == 0 or duration <= 0 then
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
	SetupStepButton(frame.BackButton, -STEP.big, REPLAY_STEP_BACK);
	SetupStepButton(frame.ForwardButton, STEP.big, REPLAY_STEP_FORWARD);
end

-- The loaded replay, from the start.
local function Build()
	ReleaseUnits();
	local info = C_CombatLogReplay.GetReplayInfo();
	if not info then
		FillUnitList();
		SetStatus(REPLAY_LOAD_FAILED);
		return;
	end
	for i, unit in ipairs(C_CombatLogReplay.GetUnits()) do
		-- A player's name without its realm (Name-Realm).
		local name = unit.name or "";
		if unit.isPlayer then
			name = string.gsub(name, "%-.*", "");
		end
		local appearances = unit.isPlayer and C_CombatLogReplay.GetUnitAppearances(i) or nil;
		units[i] = { index = i, name = name, isPlayer = unit.isPlayer, creatureID = unit.creatureID,
			mountDisplay = 0, appearances = appearances, nameColor = NameColor(unit.isPlayer, appearances) };
	end
	FillUnitList();
	for i = 1, #units do
		RequestName(units[i]);
	end
	for i in ipairs(C_CombatLogReplay.GetGameObjects()) do
		objects[i] = { index = i };
	end
	areas = C_CombatLogReplay.GetGroundAreas();
	duration = info.duration;
	frame.Timeline:SetMinMaxValues(0, math.max(duration, 0.1));
	frame.Timeline:SetValueStep(0.1);
	SetStatus(info.hasMovement and string.format(REPLAY_UNITS, #units) or REPLAY_NO_POSITIONS);
	ShowMap(info.mapID);
	SetPlaying(false);
	-- From the first frame with units: a whole log's movement file starts a
	-- frame after the log.
	openTime = info.firstUnitTime;
	Seek(openTime);
	ShowDeathMarks();
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

local function OnEvent(self, event, ...)
	if event == "COMBAT_LOG_REPLAY_ENCOUNTERS_READY" then
		local logName = ...;
		if logName == selectedLog then
			ReadSegments();
		end
	elseif event == "COMBAT_LOG_REPLAY_LOADED" then
		local success = ...;
		if success then
			Build();
		else
			SetStatus(REPLAY_LOAD_FAILED);
		end
	elseif event == "CVAR_UPDATE" then
		local name = ...;
		if name and string.lower(name) == "farclip" then
			SetFarClip();
		end
	elseif event == "CREATURE_DATA_LOAD_RESULT" then
		local creatureID, success = ...;
		if success then
			NameCreatures(creatureID);
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
	self:RegisterEvent("CREATURE_DATA_LOAD_RESULT");
	self:RegisterEvent("CVAR_UPDATE");

	scene.ControlFrame:SetModelScene(scene);
	scene:TransitionToModelSceneID(MODEL_SCENE_ID, CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD,
		true);
	SetFarClip();
	-- A follow camera beside the scene's own, with its settings, in its place.
	local _, cameraIDs = C_ModelInfo.GetModelSceneInfoByID(MODEL_SCENE_ID);
	camera = scene:AddCamera(CameraRegistry:CreateCameraByType("FollowCamera"));
	camera:ApplyFromModelSceneCameraInfo(C_ModelInfo.GetModelSceneCameraInfoByID(cameraIDs[1]),
		CAMERA_TRANSITION_TYPE_IMMEDIATE, CAMERA_MODIFICATION_TYPE_DISCARD);
	scene:SetActiveCamera(camera);
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
	feed = CreateFrame("ScrollingMessageFrame", nil, scene);
	feed:SetPoint("BOTTOMLEFT", scene, "BOTTOMLEFT", 8, 8);
	feed:SetWidth(FEED_WIDTH);
	feed:SetHeight(FEED_LINES * 14);
	feed:SetFontObject(GameFontHighlightSmall);
	feed:SetJustifyH("LEFT");
	feed:SetMaxLines(FEED_LINES * 4);
	feed:SetFading(false);
	feed:SetFrameLevel(scene:GetFrameLevel() + 6);
	feed:EnableMouse(true);
	-- A line's time goes there, a unit's name follows it, and a spell shows
	-- its tooltip in the window's own ItemRefTooltip, which a second click
	-- on the same spell closes.
	local spellTooltip = self.SpellTooltip;
	SetFrameLevels(spellTooltip, scene:GetFrameLevel() + 10);
	feed:SetScript("OnHyperlinkClick", function(_, link)
		local _, _, time = string.find(link, "^replay:(.+)$");
		if time then
			Seek(tonumber(time));
			return;
		end
		local _, _, index = string.find(link, "^replayunit:(%d+)$");
		if index then
			SetFocus(units[tonumber(index)]);
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
	unitFrame = scene.UnitFrame;
	SetFrameLevels(unitFrame, scene:GetFrameLevel() + 7);
	unitFrame:SetHitRectInsets(96, 6, 4, 9);
	-- A click on the target of target follows it, as one targets it.
	unitFrame.TargetOfTarget:SetScript("OnClick", function(tot)
		SetFocus(tot.unit);
	end);
	local view = CreateScrollBoxListLinearView();
	view:SetElementExtent(UNIT_ROW_HEIGHT);
	view:SetElementInitializer("ClassicAPIReplayUnitRowTemplate", InitUnitRow);
	ScrollUtil.InitScrollBoxListWithScrollBar(self.UnitList.ScrollBox, self.UnitList.ScrollBar, view);
	scene:EnableKeyboard(true);
	scene:SetScript("OnKeyDown", OnSceneKeyDown);
	scene:SetScript("OnKeyUp", OnSceneKeyUp);
	self:SetScript("OnUpdate", OnUpdate);

	self.PlayButton:SetScript("OnClick", function()
		if not playing and now >= duration then
			Seek(openTime);
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
	SetupTimeline();
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
