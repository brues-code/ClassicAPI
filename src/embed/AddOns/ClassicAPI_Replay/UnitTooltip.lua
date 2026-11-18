-- ClassicAPI Replay: a unit's tooltip in the window, as the client's unit
-- tooltip shows a unit (the builder 0x00529FE0) with FrameXML's name color
-- (GameTooltip_UnitColor), from the recording player's view at the replay's
-- time. It is the window's own GameTooltip (Replay.xml UnitTooltip), at the
-- scene's bottom right as the world's mouseover tooltip sits at the screen's
-- (GameTooltip_SetDefaultAnchor), above a spell link's tooltip while one is
-- open there. A unit under the cursor in the scene shows it, and so do the
-- unit list's rows, the unit frame and its target of target.
--
-- Left out: a player's PvP rank title (not recorded), the owner line (the
-- movement file does not tell a charmer from a creator, which picks its
-- words), and the two parts that need the recording player, which the replay
-- does not know: the level shown as ?? for a hostile unit ten or more levels
-- above it, and the faction line.

local _, ns = ...;

-- The rank words (0x00854158): elite for ranks 1 and 2, boss for the world
-- boss rank 3, none for normal (0) and rare (4).
local RANK_TEXT = { [1] = ELITE, [2] = ELITE, [3] = BOSS };
local RANK_WORLD_BOSS = 3;
-- CreatureType 10, "Not specified", whose name the tooltip leaves out. A
-- creature's type shows only to a unit whose reaction (UnitReaction's 1 to 8)
-- is below TYPE_REACTION, the engine's 4.
local TYPE_NOT_SPECIFIED = 10;
local TYPE_REACTION = 5;
-- UNIT_FIELD_FLAGS: controlled by a player (UnitPlayerControlled) and PvP
-- flagged (UnitIsPVP).
local FLAG_PLAYER_CONTROLLED, FLAG_PVP = 0x8, 0x1000;
-- A shown tooltip is built again this often (real seconds), so its health
-- and state follow the replay, and the scene looks again for the unit under
-- a still cursor, which the units move under.
local REFRESH = 0.2;
-- Its place: in from the scene's bottom right corner, leaving room for the
-- health bar under it, or this far above an open spell link tooltip.
local INSET_X, INSET_Y, STACK_GAP = -8, 18, 12;

local shown;    -- the unit the tooltip shows, nil when none
local owner;    -- the frame it is shown for
local builtAt;  -- GetTime() of its last build
local pickedAt, pickX, pickY; -- the scene's last look under the cursor

-- GameTooltip_UnitColor: a unit a player controls is red when it can attack
-- the player and the player it, green when it is PvP flagged, else white; any
-- other unit takes its reaction's FACTION_BAR_COLORS. The file keeps the
-- reaction, not whether either could attack, so a hostile reaction stands
-- for both: the yellow of a unit the player could attack but that is not
-- hostile (a duel) shows white.
local function NameColor(u, flags, reaction)
	local color;
	if u.isPlayer or bit.band(flags, FLAG_PLAYER_CONTROLLED) ~= 0 then
		if reaction and reaction <= 2 then
			color = FACTION_BAR_COLORS[2];
		elseif bit.band(flags, FLAG_PVP) ~= 0 then
			color = FACTION_BAR_COLORS[6];
		end
	elseif reaction then
		color = FACTION_BAR_COLORS[reaction];
	end
	if not color then
		return 1, 1, 1;
	end
	return color.r, color.g, color.b;
end

-- The level line: "Level <level> <class text> (<type text>)", each text in
-- only when it has one. The class text is CORPSE for a dead unit, a
-- player's race and class, or a creature's type; the type text is PLAYER, or
-- a creature's rank word.
local function LevelLine(u, time, level, reaction, info, dead)
	local rank = info and info.rank or 0;
	local levelText = (rank == RANK_WORLD_BOSS or not level or level <= 0) and "??" or level;
	local classText;
	if dead then
		classText = CORPSE;
	elseif u.isPlayer then
		local appearance = ns.AppearanceAt(u, time);
		local race = appearance and C_CreatureInfo.GetRaceInfo(appearance.raceID);
		local class = appearance and C_CreatureInfo.GetClassInfo(appearance.classID);
		if race and class then
			classText = race.raceName .. " " .. class.className;
		end
	elseif info and info.type ~= TYPE_NOT_SPECIFIED and reaction and reaction < TYPE_REACTION then
		local typeInfo = C_CreatureInfo.GetCreatureTypeInfo(info.type);
		classText = typeInfo and typeInfo.name ~= "" and typeInfo.name or nil;
	end
	local typeText = u.isPlayer and PLAYER or RANK_TEXT[rank];
	if classText and typeText then
		return string.format(TOOLTIP_UNIT_LEVEL_CLASS_TYPE, levelText, classText, typeText);
	elseif classText then
		return string.format(TOOLTIP_UNIT_LEVEL_CLASS, levelText, classText);
	elseif typeText then
		return string.format(TOOLTIP_UNIT_LEVEL_TYPE, levelText, typeText);
	end
	return string.format(TOOLTIP_UNIT_LEVEL, levelText);
end

-- The name, a creature's subtitle, the level line and the health bar. A new
-- build hides the bar (0x00530050), so each build shows it again.
local function Build(u, time)
	local tooltip = ns.frame.UnitTooltip;
	local _, _, flags, level, _, _, reaction = C_CombatLogReplay.GetUnitState(u.index, time);
	local health, maxHealth = C_CombatLogReplay.GetUnitHealth(u.index, time);
	local info = not u.isPlayer and u.creatureID and C_CreatureInfo.GetCreatureInfoByID(u.creatureID);
	local dead = u.dead or (health and health <= 0);
	tooltip:SetText(u.name ~= "" and u.name or UNKNOWN, NameColor(u, flags or 0, reaction));
	if info and info.subName and info.subName ~= "" then
		tooltip:AddLine(info.subName, 1, 1, 1);
	end
	tooltip:AddLine(LevelLine(u, time, level, reaction, info, dead), 1, 1, 1);
	if maxHealth and maxHealth > 0 then
		local bar = getglobal(tooltip:GetName() .. "StatusBar");
		bar:SetMinMaxValues(0, maxHealth);
		bar:SetValue(dead and 0 or health);
		bar:Show();
	end
	tooltip:Show();
	builtAt = GetTime();
end

local function ShowUnitTooltip(frame, u)
	if not u then
		return;
	end
	shown, owner = u, frame;
	local tooltip = ns.frame.UnitTooltip;
	local spellTooltip = ns.frame.SpellTooltip;
	tooltip:SetOwner(frame, "ANCHOR_PRESERVE");
	tooltip:ClearAllPoints();
	if spellTooltip:IsShown() then
		tooltip:SetPoint("BOTTOMRIGHT", spellTooltip, "TOPRIGHT", 0, STACK_GAP);
	else
		tooltip:SetPoint("BOTTOMRIGHT", ns.scene, "BOTTOMRIGHT", INSET_X, INSET_Y);
	end
	Build(u, ns.now);
end
ns.ShowUnitTooltip = ShowUnitTooltip;

-- Hides the tooltip shown for `frame`, or with no frame whichever is shown.
local function HideUnitTooltip(frame)
	if not shown or (frame and owner ~= frame) then
		return;
	end
	local tooltip = ns.frame.UnitTooltip;
	if tooltip:IsOwned(owner) then
		tooltip:Hide();
	end
	shown, owner = nil, nil;
end
ns.HideUnitTooltip = HideUnitTooltip;

-- The unit under the cursor in the scene, while the cursor is on the scene
-- itself (not a frame over it) and no button is held, since a drag turns the
-- camera. It looks again when the cursor moves, else every REFRESH.
local function HoverScene()
	local scene = ns.scene;
	if GetMouseFocus() ~= scene or IsMouseButtonDown() then
		HideUnitTooltip(scene);
		pickedAt = nil;
		return;
	end
	local x, y = GetCursorPosition();
	if pickedAt and x == pickX and y == pickY and GetTime() - pickedAt < REFRESH then
		return;
	end
	pickedAt, pickX, pickY = GetTime(), x, y;
	local actor = scene:GetActorAtCursor();
	local u = actor and ns.unitOfActor[actor];
	if not u then
		HideUnitTooltip(scene);
	elseif not (shown == u and owner == scene) then
		ShowUnitTooltip(scene, u);
	end
end

-- Each frame: the scene's hover, and a shown tooltip built again every
-- REFRESH while its owner still holds it.
local function UpdateUnitTooltip()
	HoverScene();
	if not shown then
		return;
	end
	if not ns.frame.UnitTooltip:IsOwned(owner) then
		shown, owner = nil, nil;
	elseif GetTime() - builtAt >= REFRESH then
		Build(shown, ns.now);
	end
end
ns.UpdateUnitTooltip = UpdateUnitTooltip;
