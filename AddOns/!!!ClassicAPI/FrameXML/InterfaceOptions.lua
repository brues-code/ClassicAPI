-- Backport of the Interface Options panel's addon side (InterfaceOptionsFrame,
-- InterfaceOptions_AddCategory, InterfaceOptionsFrame_OpenToCategory) to
-- vanilla 1.12 / Lua 5.0.
--
-- Later clients list addon option panels in InterfaceOptionsFrame. Addons add
-- theirs with InterfaceOptions_AddCategory(panel) - Ace3's
-- AceConfigDialog:AddToBlizOptions does it for every Ace3 addon - and open them
-- with InterfaceOptionsFrame_OpenToCategory. Frame and list names match later
-- clients because addons reach for them directly (Zygor Guides Viewer 2.0
-- re-tints the frame's first region; LibBetterBlizzOptions-1.0 resizes the
-- lists through their `buttons` / `buttonHeight` / `update` fields).
--
-- Implementation differences from the modern source:
--   - Addon panels only. Vanilla's own options stay in UIOptionsFrame and
--     friends; the Categories list exists (empty, hidden) for code that
--     expects it.
--   - Panel protocol as on later clients: panel.name (required), panel.parent
--     (name of the category to nest under), and optional okay / cancel /
--     default / refresh, each called with the panel. Categories with children
--     start collapsed. Okay and Cancel call okay / cancel on every panel;
--     Defaults calls default on the one shown; Escape just closes.
--   - Lua 5.0: no `...` expression or select().

if InterfaceOptionsFrame then return end -- a real implementation wins

INTERFACEOPTIONS_ADDONCATEGORIES = INTERFACEOPTIONS_ADDONCATEGORIES or {}
local categories = INTERFACEOPTIONS_ADDONCATEGORIES

local BUTTON_HEIGHT = 18
local NUM_BUTTONS = 24

local function report(err)
	local handler = geterrorhandler and geterrorhandler()
	if handler then handler(err) end
end

local LIST_BACKDROP = {
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
	tile = true, tileSize = 16, edgeSize = 16,
	insets = { left = 5, right = 5, top = 5, bottom = 5 },
}

-- ---------------------------------------------------------------------------
-- Frame
-- ---------------------------------------------------------------------------

local frame = CreateFrame("Frame", "InterfaceOptionsFrame", UIParent)
frame:Hide()
frame:SetWidth(648)
frame:SetHeight(568)
frame:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
frame:SetFrameStrata("DIALOG")
frame:SetToplevel(true)
frame:EnableMouse(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)

-- The background is the frame's first region: addons re-tint it through
-- InterfaceOptionsFrame:GetRegions().
local background = frame:CreateTexture(nil, "BACKGROUND")
background:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Background")
background:SetPoint("TOPLEFT", frame, "TOPLEFT", 11, -12)
background:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -12, 11)
frame:SetBackdrop({
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 11 },
})

local header = frame:CreateTexture(nil, "ARTWORK")
header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
header:SetWidth(360)
header:SetHeight(64)
header:SetPoint("TOP", frame, "TOP", 0, 12)
local title = frame:CreateFontString(nil, "ARTWORK", "GameFontNormal")
title:SetPoint("TOP", header, "TOP", 0, -14)
title:SetText(ADDON_OPTIONS or "AddOn Options")

frame:SetScript("OnMouseDown", function()
	if arg1 == "LeftButton" then this:StartMoving() end
end)
frame:SetScript("OnMouseUp", function() this:StopMovingOrSizing() end)
tinsert(UISpecialFrames, "InterfaceOptionsFrame")

-- ---------------------------------------------------------------------------
-- Category lists and the panel container
-- ---------------------------------------------------------------------------

local function CreateList(name)
	local list = CreateFrame("Frame", name, frame)
	list:SetWidth(185)
	list:SetPoint("TOPLEFT", frame, "TOPLEFT", 22, -40)
	list:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 22, 50)
	list:SetBackdrop(LIST_BACKDROP)
	list:SetBackdropColor(0, 0, 0, 0.5)
	list:SetBackdropBorderColor(0.6, 0.6, 0.6)
	list.buttonHeight = BUTTON_HEIGHT
	list.buttons = {}
	for i = 1, NUM_BUTTONS do
		local button = CreateFrame("Button", name .. "Button" .. i, list, "InterfaceOptionsListButtonTemplate")
		if i == 1 then
			button:SetPoint("TOPLEFT", list, "TOPLEFT", 5, -8)
		else
			button:SetPoint("TOPLEFT", list.buttons[i - 1], "BOTTOMLEFT", 0, 0)
		end
		button:Hide()
		list.buttons[i] = button
	end
	return list
end

-- Vanilla keeps its own options elsewhere; this list stays empty.
local gameList = CreateList("InterfaceOptionsFrameCategories")
gameList.update = function() end
gameList:Hide()

local addOnList = CreateList("InterfaceOptionsFrameAddOns")

local scroll = CreateFrame("ScrollFrame", "InterfaceOptionsFrameAddOnsList", addOnList, "FauxScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", addOnList, "TOPLEFT", 0, -8)
scroll:SetPoint("BOTTOMRIGHT", addOnList, "BOTTOMRIGHT", -30, 8)

local container = CreateFrame("Frame", "InterfaceOptionsFramePanelContainer", frame)
container:SetPoint("TOPLEFT", addOnList, "TOPRIGHT", 16, 0)
container:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -22, 50)
container:SetBackdrop(LIST_BACKDROP)
container:SetBackdropColor(0, 0, 0, 0.5)
container:SetBackdropBorderColor(0.6, 0.6, 0.6)

-- ---------------------------------------------------------------------------
-- Categories
-- ---------------------------------------------------------------------------

local displayed -- panel shown in the container

local function FindCategory(name)
	for _, panel in ipairs(categories) do
		if panel.name == name then return panel end
	end
end

local function HasChildren(panel)
	for _, p in ipairs(categories) do
		if p.parent == panel.name then return true end
	end
end

-- Top-level categories in order, each followed by its children when expanded.
-- A child whose parent is not registered is listed at the top level.
local function VisibleCategories()
	local out = {}
	for _, panel in ipairs(categories) do
		if not panel.parent or not FindCategory(panel.parent) then
			table.insert(out, panel)
			if not panel.collapsed then
				for _, child in ipairs(categories) do
					if child.parent == panel.name then table.insert(out, child) end
				end
			end
		end
	end
	return out
end

function InterfaceAddOnsList_Update()
	local entries = VisibleCategories()
	local offset = FauxScrollFrame_GetOffset(scroll) or 0
	local numButtons = table.getn(addOnList.buttons)
	for i = 1, numButtons do
		local button = addOnList.buttons[i]
		local panel = entries[i + offset]
		local text = getglobal(button:GetName() .. "Text")
		local toggle = getglobal(button:GetName() .. "Toggle")
		if panel then
			button.element = panel
			text:SetText(panel.name)
			local isChild = panel.parent and FindCategory(panel.parent)
			text:SetFontObject(isChild and GameFontHighlightSmall or GameFontNormal)
			text:ClearAllPoints()
			text:SetPoint("LEFT", button, "LEFT", isChild and 20 or 8, 0)
			if toggle then
				if HasChildren(panel) then
					local tex = panel.collapsed and "Plus" or "Minus"
					toggle:SetNormalTexture("Interface\\Buttons\\UI-" .. tex .. "Button-UP")
					toggle:SetPushedTexture("Interface\\Buttons\\UI-" .. tex .. "Button-DOWN")
					toggle:Show()
				else
					toggle:Hide()
				end
			end
			if panel == displayed then button:LockHighlight() else button:UnlockHighlight() end
			button:Show()
		else
			button.element = nil
			button:UnlockHighlight()
			button:Hide()
		end
	end
	FauxScrollFrame_Update(scroll, table.getn(entries), numButtons, BUTTON_HEIGHT)
end
addOnList.update = InterfaceAddOnsList_Update

scroll:SetScript("OnVerticalScroll", function()
	FauxScrollFrame_OnVerticalScroll(BUTTON_HEIGHT, InterfaceAddOnsList_Update)
end)

local function DisplayPanel(panel)
	if displayed and displayed ~= panel then displayed:Hide() end
	displayed = panel
	panel:SetParent(container)
	panel:ClearAllPoints()
	panel:SetPoint("TOPLEFT", container, "TOPLEFT", 4, -4)
	panel:SetPoint("BOTTOMRIGHT", container, "BOTTOMRIGHT", -4, 4)
	panel:Show()
	if type(panel.refresh) == "function" then
		local ok, err = pcall(panel.refresh, panel)
		if not ok then report(err) end
	end
	InterfaceAddOnsList_Update()
end

function InterfaceOptionsListButton_OnClick(button)
	if button and button.element then DisplayPanel(button.element) end
end

function InterfaceOptionsListButton_ToggleSubCategories(button)
	local panel = button and button.element
	if not panel then return end
	panel.collapsed = not panel.collapsed
	InterfaceAddOnsList_Update()
end

function InterfaceOptions_AddCategory(panel, addOn, position)
	if type(panel) ~= "table" or type(panel.name) ~= "string" then
		error("Usage: InterfaceOptions_AddCategory(panel [, addOn, position])", 2)
	end
	panel:Hide()
	if panel.parent then
		local parent = FindCategory(panel.parent)
		if parent and parent.collapsed == nil then parent.collapsed = true end
	end
	if position and position >= 1 and position <= table.getn(categories) + 1 then
		table.insert(categories, position, panel)
	else
		table.insert(categories, panel)
	end
	if panel.collapsed == nil and HasChildren(panel) then panel.collapsed = true end
	if frame:IsShown() then InterfaceAddOnsList_Update() end
end

function InterfaceOptionsFrame_OpenToCategory(panel)
	if type(panel) == "string" then panel = FindCategory(panel) end
	if type(panel) ~= "table" then return end
	local p = panel
	while p and p.parent do
		local parent = FindCategory(p.parent)
		if parent then parent.collapsed = false end
		p = parent
	end
	frame:Show()
	DisplayPanel(panel)
end

function InterfaceOptionsFrame_Show()
	if frame:IsShown() then frame:Hide() else frame:Show() end
end

frame:SetScript("OnShow", function()
	if not displayed and categories[1] then
		DisplayPanel(categories[1])
	else
		InterfaceAddOnsList_Update()
	end
end)

-- ---------------------------------------------------------------------------
-- Okay / Cancel / Defaults
-- ---------------------------------------------------------------------------

local function CallAll(method)
	for _, panel in ipairs(categories) do
		local fn = panel[method]
		if type(fn) == "function" then
			local ok, err = pcall(fn, panel)
			if not ok then report(err) end
		end
	end
end

local function PanelButton(name, label, point, x)
	local b = CreateFrame("Button", name, frame, "UIPanelButtonTemplate")
	b:SetWidth(96)
	b:SetHeight(22)
	b:SetPoint(point, frame, point, x, 16)
	b:SetText(label)
	return b
end

PanelButton("InterfaceOptionsFrameCancel", CANCEL or "Cancel", "BOTTOMRIGHT", -16):SetScript("OnClick", function()
	CallAll("cancel")
	frame:Hide()
end)
PanelButton("InterfaceOptionsFrameOkay", OKAY or "Okay", "BOTTOMRIGHT", -114):SetScript("OnClick", function()
	CallAll("okay")
	frame:Hide()
end)
PanelButton("InterfaceOptionsFrameDefaults", DEFAULTS or "Defaults", "BOTTOMLEFT", 22):SetScript("OnClick", function()
	if displayed and type(displayed.default) == "function" then
		local ok, err = pcall(displayed.default, displayed)
		if not ok then report(err) end
	end
end)
