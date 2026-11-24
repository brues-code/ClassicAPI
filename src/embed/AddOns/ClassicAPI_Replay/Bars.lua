-- ClassicAPI Replay: what shows over a unit's name: its health, its cast bar
-- and its raid target icon, none of them through a wall.

local _, ns = ...;

-- The health bar over a hurt unit: its size, how far above the name point
-- it sits (pixels), and its colors, the nameplate's four (0x007CB120 to
-- 0x007CB1E0).
local HEALTH_BAR_WIDTH, HEALTH_BAR_HEIGHT, HEALTH_BAR_RISE = 50, 5, 14;
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
local freeBars = {};   -- health bars not in use
local freeCastBars = {}; -- cast bars not in use
local freeRaidIcons = {}; -- raid target icons not in use

------------------------------------------------------------------------------
-- Health bars

local function NewBar()
	local bar = CreateFrame("StatusBar", nil, ns.scene);
	bar:SetWidth(HEALTH_BAR_WIDTH);
	bar:SetHeight(HEALTH_BAR_HEIGHT);
	bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar");
	bar:SetMinMaxValues(0, 1);
	bar:SetFrameLevel(ns.scene:GetFrameLevel() + 5);
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
ns.HideBar = HideBar;

-- Where a unit's name point appears on the scene, in the scene's units; nil
-- when it is behind the camera, off the scene, or behind a wall. A wall is
-- a face the world's line of sight tests (TraceSegment: the WMO's walls,
-- floors and roofs, and the terrain) between the camera and the name point.
-- The bars are frames over the scene, so nothing in the scene hides them.
local function ScenePoint(actor)
	local x, y, z = actor:GetNamePosition();
	if not x then
		return nil;
	end
	local px, py, depth = ns.scene:Project3DPointTo2D(x, y, z);
	if not px or depth < 0 or depth > 1 then
		return nil;
	end
	local scale = ns.scene:GetEffectiveScale();
	px, py = px / scale, py / scale;
	if px < 0 or py < 0 or px > ns.scene:GetWidth() or py > ns.scene:GetHeight() then
		return nil;
	end
	local eyeX, eyeY, eyeZ = ns.scene:GetCameraPosition();
	if ns.scene:TraceSegment(eyeX, eyeY, eyeZ, x, y, z) then
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
	bar:SetPoint("BOTTOM", ns.scene, "BOTTOMLEFT", px, py + HEALTH_BAR_RISE);
	bar:Show();
end

local function NewCastBar()
	local bar = CreateFrame("StatusBar", nil, ns.scene);
	bar:SetWidth(CAST_BAR_WIDTH);
	bar:SetHeight(CAST_BAR_HEIGHT);
	bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar");
	bar:SetMinMaxValues(0, 1);
	bar:SetFrameLevel(ns.scene:GetFrameLevel() + 5);
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
ns.HideCastBar = HideCastBar;

-- What an interrupted cast bar says: who interrupted it in their name color,
-- as retail's GetInterruptText does with SPELL_INTERRUPTED_BY.
local function InterruptText(interrupterIndex)
	local by = interrupterIndex and ns.units[interrupterIndex];
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
	bar:SetPoint("BOTTOM", ns.scene, "BOTTOMLEFT", px, py + rise);
	bar:Show();
end

local function HideRaidIcon(u)
	if u.raidIcon then
		u.raidIcon:Hide();
		tinsert(freeRaidIcons, u.raidIcon);
		u.raidIcon = nil;
	end
end
ns.HideRaidIcon = HideRaidIcon;

-- A frame over the scene's models, as the bars are.
local function NewRaidIcon()
	local icon = CreateFrame("Frame", nil, ns.scene);
	icon:SetWidth(RAID_ICON_SIZE);
	icon:SetHeight(RAID_ICON_SIZE);
	icon:SetFrameLevel(ns.scene:GetFrameLevel() + 5);
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
	icon:SetPoint("BOTTOM", ns.scene, "BOTTOMLEFT", px, py + rise);
	icon:Show();
end

-- Over a living unit's name: its health below its full health, its cast bar
-- while it casts or channels (the two only while the Nameplates option is
-- on), and its raid target mark above them.
local function ShowBars(u, time, isDead)
	local health, maxHealth, fill, color, text, alpha;
	if not isDead and ns.settings.showNameplates then
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
ns.ShowBars = ShowBars;
