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
-- health over its head, from the log's advanced data, and the log's lines
-- scroll by in the corner in the client's own combat log words. Scene
-- positions are map positions. Click a unit, or its name in the unit list,
-- to follow it with the camera, which then stops in front of walls as the
-- world's camera does; click the ground to stop. Drag to turn and tilt the camera, right drag to move it across
-- the view. With the cursor over the scene, W, A, S and D fly the camera
-- along its view, Space and X up and down, Shift faster. /replay toggles the
-- window.

local ANIM_STAND, ANIM_WALK, ANIM_RUN, ANIM_MOUNT = 0, 4, 5, 91
-- In the air (Offsets.h ANIM_JUMP, ANIM_FALL): from a jump, or off a ledge.
local ANIM_JUMP, ANIM_FALL = 38, 40;
-- Combat (AnimationData.dbc): dying, then lying dead; a swing, unarmed (a
-- creature) or one-handed (a player).
local ANIM_DEATH, ANIM_DEAD = 1, 6;
local ANIM_ATTACK_UNARMED = 16;
-- A swing by the main hand's weapon (item class 2), as the client picks it
-- (0x006246A0): the subclass indexes the bytes at 0x006247AC into 17
-- Attack1H, 18 Attack2H, 19 Attack2HL, 85 Attack1HPierce or 16 AttackUnarmed.
local ITEM_CLASS_WEAPON = 2;
local SWING_BY_SUBCLASS = { [0] = 17, 18, 16, 16, 17, 18, 19, 17, 18, 16, 19, 17, 18, 16, 17, 85, 16, 19, 16, 16, 19 };
-- How long a swing holds its animation, in replay seconds, and how long the
-- dying animation plays before the corpse lies still, in real seconds.
local SWING_TIME = 0.8;
local DYING_TIME = 1.5;
-- A step in time longer than this (replay seconds), or one back, is a jump:
-- the combat state is set as it was, not played.
local MAX_PLAYED_STEP = 1;
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
-- it sits (pixels), and its colors.
local HEALTH_BAR_WIDTH, HEALTH_BAR_HEIGHT, HEALTH_BAR_RISE = 50, 5, 14;
local PLAYER_HEALTH_COLOR = { 0, 0.8, 0 };
local CREATURE_HEALTH_COLOR = { 0.85, 0.1, 0.1 };
-- The feed of the log's lines: how many show, how long each stays, and the
-- colors of its lines, heals and deaths.
local FEED_LINES, FEED_WIDTH, FEED_SECONDS = 8, 440, 12;
local FEED_TEXT_COLOR = { 1, 1, 1 };
local FEED_HEAL_COLOR = { 0.2, 1, 0.2 };
local FEED_DEATH_COLOR = { 1, 0.25, 0.25 };
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
local now = 0;
local playing = false;
local playSpeed = DEFAULT_PLAY_SPEED;
local focus;          -- the unit the camera follows
local settingTimeline = false;
local timelineValue, timeSecond; -- what the timeline and the time text show
local pressX, pressY;
local flying = {};     -- the fly keys held, by key name
local combatTime;      -- the replay time the combat state is at, nil before the first
local freeBars = {};   -- health bars not in use
local feed;            -- the ScrollingMessageFrame of the log's lines
local unitListChanged; -- a unit was named since the list was filled

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

-- A living unit below its full health shows a bar over its name.
local function ShowBar(u, time, isDead)
	local health, maxHealth;
	if not isDead then
		health, maxHealth = C_CombatLogReplay.GetUnitHealth(u.index, time);
	end
	local px, py;
	if health and health < maxHealth then
		px, py = ScenePoint(u.actor);
	end
	if not px then
		HideBar(u);
		return;
	end
	local bar = u.bar;
	if not bar then
		bar = tremove(freeBars) or NewBar();
		u.bar = bar;
		local color = u.isPlayer and PLAYER_HEALTH_COLOR or CREATURE_HEALTH_COLOR;
		bar:SetStatusBarColor(color[1], color[2], color[3]);
	end
	bar:SetValue(health / maxHealth);
	bar:ClearAllPoints();
	bar:SetPoint("BOTTOM", scene, "BOTTOMLEFT", px, py + HEALTH_BAR_RISE);
	bar:Show();
end

------------------------------------------------------------------------------
-- Units

local function ReleaseUnits()
	combatTime = nil;
	for i = 1, #units do
		HideBar(units[i]);
	end
	scene:ReleaseAllActors();
	units = {};
	unitOfActor = {};
	focus = nil;
end

-- A pooled actor keeps what ApplyFromModelSceneActorInfo set at its last
-- use: scene 4's "unwrapped" actor centres its model on its position
-- (useCenterForOriginZ, which sinks a unit by half its height) and carries a
-- display info's scale, which the mixin applies again whenever a model
-- loads. A new one stands on its position at its own size.
local function Acquire(u)
	local actor = scene:AcquireActor();
	actor:SetUseCenterForOrigin(false, false, false);
	actor:SetPitch(0);
	actor:SetRoll(0);
	actor:SetAlpha(1);
	actor:StopAnimationKit();
	actor:SetSpellVisualKit(nil);
	actor:SetNormalizedScaleAggressiveness(0);
	actor:SetRequestedScale(1);
	unitOfActor[actor] = u;
	return actor;
end


local function HideUnit(u)
	HideBar(u);
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

local function ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
		mountDisplay, scale, standState, sheathState, isDead)
	local actor = u.actor;
	if not actor then
		actor = Acquire(u);
		u.actor = actor;
		u.named = nil;
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
	if isDead then
		animation = u.dyingUntil and GetTime() < u.dyingUntil and ANIM_DEATH or ANIM_DEAD;
	elseif falling then
		animation = jumping and ANIM_JUMP or ANIM_FALL;
	elseif speed > MOVING_SPEED then
		animation = speed >= RUN_SPEED and ANIM_RUN or ANIM_WALK;
	elseif u.swingUntil and time < u.swingUntil then
		animation = u.swingAnimation;
	else
		animation = STAND_ANIMATION[standState] or ANIM_STAND;
	end
	if u.animation ~= animation then
		body:SetAnimation(animation);
		u.animation = animation;
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

-- The swing animation of the unit's main hand at `time`.
local function SwingAnimation(index, time)
	local itemClass, subclass = C_CombatLogReplay.GetUnitWeapons(index, time);
	return itemClass == ITEM_CLASS_WEAPON and SWING_BY_SUBCLASS[subclass] or ANIM_ATTACK_UNARMED;
end

local function UnitName(index)
	local u = index and units[index];
	return u and u.name ~= "" and u.name or UNKNOWN;
end

local function SpellName(spellID)
	return spellID and GetSpellInfo(spellID) or UNKNOWN;
end

-- The event in the client's combat log words, and its color; nil for one
-- the feed leaves out.
local function FeedText(e)
	local kind = e.event;
	local source, dest = UnitName(e.sourceIndex), UnitName(e.destIndex);
	if kind == "SWING_DAMAGE" then
		return string.format(e.critical and COMBATHITCRITOTHEROTHER or COMBATHITOTHEROTHER, source, dest, e.amount);
	elseif kind == "SPELL_DAMAGE" or kind == "SPELL_PERIODIC_DAMAGE" or kind == "RANGE_DAMAGE"
			or kind == "DAMAGE_SHIELD" then
		return string.format(e.critical and SPELLLOGCRITOTHEROTHER or SPELLLOGOTHEROTHER, source,
			SpellName(e.spellID), dest, e.amount);
	elseif kind == "SPELL_HEAL" or kind == "SPELL_PERIODIC_HEAL" then
		return string.format(e.critical and HEALEDCRITOTHEROTHER or HEALEDOTHEROTHER, source,
			SpellName(e.spellID), dest, e.amount), FEED_HEAL_COLOR;
	elseif kind == "SWING_MISSED" then
		local text = SWING_MISS_TEXT[e.missType];
		return text and string.format(text, source, dest);
	elseif kind == "SPELL_MISSED" or kind == "SPELL_PERIODIC_MISSED" or kind == "RANGE_MISSED" then
		if e.missType == "IMMUNE" then
			return string.format(SPELLIMMUNEOTHEROTHER, source, SpellName(e.spellID), dest);
		end
		local text = SPELL_MISS_TEXT[e.missType];
		return text and string.format(text, source, SpellName(e.spellID), dest);
	elseif kind == "SPELL_CAST_START" then
		return string.format(SPELLCASTOTHERSTART, source, SpellName(e.spellID));
	elseif kind == "SPELL_CAST_SUCCESS" then
		if e.destIndex and e.destIndex ~= e.sourceIndex then
			return string.format(SPELLCASTGOOTHERTARGETTED, source, SpellName(e.spellID), dest);
		end
		return string.format(SPELLCASTGOOTHER, source, SpellName(e.spellID));
	elseif kind == "SPELL_AURA_APPLIED" then
		return string.format(e.auraType == "DEBUFF" and AURAADDEDOTHERHARMFUL or AURAADDEDOTHERHELPFUL, dest,
			SpellName(e.spellID));
	elseif kind == "SPELL_AURA_REMOVED" then
		return string.format(AURAREMOVEDOTHER, SpellName(e.spellID), dest);
	elseif kind == "UNIT_DIED" then
		return string.format(UNITDIESOTHER, dest), FEED_DEATH_COLOR;
	end
end

local function AddFeedLine(e)
	local text, color = FeedText(e);
	if text then
		color = color or FEED_TEXT_COLOR;
		feed:AddMessage(text, color[1], color[2], color[3]);
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
		-- speed.
		if source then
			local castMs = e.castTime and e.castTime * 1000 / PLAY_SPEEDS[playSpeed];
			local castID = source:PlaySpellVisual(e.spellID, target, castMs);
			if castID then
				u.casts = u.casts or {};
				u.casts[e.spellID] = castID;
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
			source:PlaySpellVisual(e.spellID, target, 0);
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
		if u then
			u.swingUntil = e.time + SWING_TIME;
			u.swingAnimation = SwingAnimation(e.sourceIndex, e.time);
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
	feed:Clear();
	for i = 1, #units do
		local u = units[i];
		u.casts, u.swingUntil, u.dyingUntil = nil, nil, nil;
		if u.actor then
			u.actor:StopSpellVisual();
			u.actor:RemoveAuraVisual();
			for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(i, time)) do
				u.actor:AddAuraVisual(aura.spellID);
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
		if x then
			display, nativeDisplay, mountDisplay, scale, standState, _, _, sheathState, isDead =
				GetUnitLook(i, time);
		end
		u.inView, u.dead = display and display > 0, isDead;
		if u.inView then
			ShowUnit(u, time, x, y, z, facing, speed, falling, jumping, display, nativeDisplay,
				mountDisplay, scale, standState, sheathState, isDead);
			ShowBar(u, time, isDead);
		else
			HideUnit(u);
		end
	end
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
-- The unit list

-- A row shows its unit's name in its color (grey once dead), faded while the
-- unit is out of view, and marked while the camera follows it.
local function UpdateUnitRow(row)
	local u = row.unit;
	local color = u.dead and DEAD_NAME_COLOR or u.nameColor;
	row.Text:SetTextColor(color[1], color[2], color[3]);
	row:SetAlpha(u.inView and 1 or OUT_OF_VIEW_ALPHA);
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

-- The timeline's thumb moves once it would move a pixel, and the time text
-- once its second changes: setting the slider runs its scripts (about half a
-- millisecond a call), too much for every frame. `force` sets both.
local function UpdateTimeline(force)
	local step = duration / math.max(frame.Timeline:GetWidth(), 1);
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
	UpdateTimeline(now >= duration);
end

local function Seek(time)
	now = math.max(0, math.min(duration, time));
	Apply(now);
	FollowFocus();
	UpdateUnitList();
	UpdateTimeline(true);
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
	feed:SetFading(true);
	feed:SetTimeVisible(FEED_SECONDS);
	feed:SetFrameLevel(scene:GetFrameLevel() + 6);
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
