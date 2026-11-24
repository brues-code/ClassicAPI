-- ClassicAPI Replay: the unit list, the followed unit and the units' names.

local _, ns = ...;

-- A name's color: a player's class color, else these; grey once dead. The
-- unit list fades the name of a unit out of view.
local PLAYER_NAME_COLOR = { 0.4, 0.8, 1 };
local CREATURE_NAME_COLOR = { 1, 0.82, 0 };
local DEAD_NAME_COLOR = { 0.5, 0.5, 0.5 };
local OUT_OF_VIEW_ALPHA = 0.4;
-- A pet's row is under its owner's, its name this much further in.
local PET_INDENT = 10;
local TEXT_INSET = 4;
-- A player's faction icon (the row template's, 12 pixels) and the space after it.
local FACTION_ICON_SIZE, FACTION_ICON_GAP = 12, 2;

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
	if u == ns.focus then
		row.Selected:Show();
	else
		row.Selected:Hide();
	end
end

local function UpdateUnitList()
	ns.frame.UnitList.ScrollBox:ForEachFrame(UpdateUnitRow);
end
ns.UpdateUnitList = UpdateUnitList;

local function SetFocus(u)
	if ns.focus and ns.focus.actor then
		ns.focus.actor:SetHighlight("target", false);
	end
	ns.focus = u;
	if ns.focus and ns.focus.actor then
		ns.focus.actor:SetHighlight("target", true);
	end
	ns.FollowFocus();
	UpdateUnitList();
	ns.UpdateUnitFrame(ns.now);
	if ns.focus then
		ns.frame.UnitList.ScrollBox:ScrollToElementData(ns.focus, ScrollBoxConstants.AlignNearest);
	end
end
ns.SetFocus = SetFocus;

local function OnUnitRowClick(row)
	SetFocus(row.unit);
end

local function OnUnitRowEnter(row)
	ns.ShowUnitTooltip(row, row.unit);
end

local function OnUnitRowLeave(row)
	ns.HideUnitTooltip(row);
end

-- A player's row starts with its faction icon, its name after it.
local function InitUnitRow(row, u)
	row.unit = u;
	local inset = TEXT_INSET + (u.listDepth or 0) * PET_INDENT;
	if u.factionGroup then
		row.Faction:SetTexture("Interface\\TargetingFrame\\UI-PVP-" .. u.factionGroup);
		row.Faction:ClearAllPoints();
		row.Faction:SetPoint("LEFT", row, "LEFT", inset, 0);
		row.Faction:Show();
		inset = inset + FACTION_ICON_SIZE + FACTION_ICON_GAP;
	else
		row.Faction:Hide();
	end
	row.Text:SetPoint("LEFT", row, "LEFT", inset, 0);
	row.Text:SetText(u.name ~= "" and u.name or UNKNOWN);
	row:SetScript("OnClick", OnUnitRowClick);
	row:SetScript("OnEnter", OnUnitRowEnter);
	row:SetScript("OnLeave", OnUnitRowLeave);
	UpdateUnitRow(row);
end
ns.InitUnitRow = InitUnitRow;

-- Players first, then creatures, each by name; units without a name last.
-- Each pet follows its owner, in the same order.
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
	local tops, pets = {}, {};
	for i = 1, #ns.units do
		local u = ns.units[i];
		u.listDepth = nil;
		if u.petOwner then
			pets[u.petOwner] = pets[u.petOwner] or {};
			tinsert(pets[u.petOwner], u);
		else
			tinsert(tops, u);
		end
	end
	local list = {};
	-- Each unit once, should two players own each other.
	local function Add(u, depth)
		if u.listDepth then
			return;
		end
		u.listDepth = depth;
		tinsert(list, u);
		local own = pets[u];
		if own then
			table.sort(own, UnitOrder);
			for _, pet in ipairs(own) do
				Add(pet, depth + 1);
			end
		end
	end
	table.sort(tops, UnitOrder);
	for _, u in ipairs(tops) do
		Add(u, 0);
	end
	-- Owners that own each other, which no top unit reached.
	for owner in pairs(pets) do
		Add(owner, 0);
	end
	ns.frame.UnitList.ScrollBox:SetDataProvider(CreateDataProvider(list), ScrollBoxConstants.RetainScrollPosition);
	ns.unitListChanged = false;
end
ns.FillUnitList = FillUnitList;

-- A creature the log does not name and the client has not cached is asked
-- for; NameCreatures names its units when it arrives.
local function RequestName(unit)
	if unit.name == "" and unit.creatureID then
		C_CreatureInfo.RequestLoadCreatureByID(unit.creatureID);
	end
end
ns.RequestName = RequestName;

local function NameCreatures(creatureID)
	local info = C_CreatureInfo.GetCreatureInfoByID(creatureID);
	if not info or not info.name or info.name == "" then
		return;
	end
	for i = 1, #ns.units do
		local u = ns.units[i];
		if u.creatureID == creatureID and u.name == "" then
			u.name = info.name;
			u.named = nil;
			ns.unitListChanged = true;
		end
	end
end
ns.NameCreatures = NameCreatures;

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
ns.NameColor = NameColor;

-- A player's faction group from its race ("Alliance", "Horde"), the
-- English name UnitFactionGroup gives; nil for a creature.
local function FactionGroup(appearances)
	local appearance = appearances and appearances[1];
	local info = appearance and C_CreatureInfo.GetFactionInfo(appearance.raceID);
	return info and info.groupTag;
end
ns.FactionGroup = FactionGroup;
