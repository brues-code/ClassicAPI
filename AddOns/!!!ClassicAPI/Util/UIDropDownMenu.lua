-- The 3.x UIDropDownMenu surface, and EasyMenu, on vanilla's dropdown code.
--
-- Vanilla and 3.x draw menus with the same DropDownList frames, but 3.x
-- changed argument orders and the init and click conventions, and added
-- button options. Served here without changing how vanilla callers behave:
--
--   * Setters whose argument order changed take either order, told apart by
--     the argument types (a frame first is the 3.x order):
--     UIDropDownMenu_SetWidth(frame, width [, padding]),
--     UIDropDownMenu_SetButtonWidth(frame, width),
--     UIDropDownMenu_SetText(frame, text),
--     UIDropDownMenu_JustifyText(frame, justification),
--     UIDropDownMenu_SetAnchor(dropdown, xOffset, yOffset, point, relativeTo,
--     relativePoint), UIDropDownMenu_SetButtonText(level, id, text, colorCode).
--   * 3.x-only functions: UIDropDownMenu_EnableDropDown / _DisableDropDown /
--     _IsEnabled / _GetValue, OpenColorPicker(info),
--     ColorPicker_GetPreviousValues(); UIDropDownMenuButton_GetChecked /
--     _GetName / _OpenColorPicker also take the button as an argument.
--   * Button options vanilla ignores: `checked` as a function (evaluated
--     when the button is drawn), `colorCode`, `fontObject`, `padding`,
--     `menuList`, `tooltipOnButton`, `tooltipWhileDisabled`, `noClickSound`.
--   * The `menuList` argument of UIDropDownMenu_Initialize and
--     ToggleDropDownMenu, and submenus opened from a button's `menuList`.
--   * EasyMenu(menuList, menuFrame, anchor, x, y, displayMode) and
--     EasyMenu_Initialize(frame, level, menuList).
--   * Menus deeper than three levels or longer than 40 buttons:
--     UIDropDownMenu_CreateFrames(level, index) makes the lists and buttons on
--     demand, as 3.3.5 does.
--
-- EasyMenu's buttons also click the 3.x way: the check toggles (or the menu
-- closes) first, then func(button, arg1, arg2, checked) runs. Buttons added
-- any other way keep vanilla's func(arg1, arg2), with the button as `this`:
-- a vanilla init function and one written for 3.x can't be told apart from
-- the info table alone.

local orig = {
	Initialize = UIDropDownMenu_Initialize,
	AddButton = UIDropDownMenu_AddButton,
	Toggle = ToggleDropDownMenu,
	OnClick = UIDropDownMenuButton_OnClick,
	SetWidth = UIDropDownMenu_SetWidth,
	SetButtonWidth = UIDropDownMenu_SetButtonWidth,
	SetText = UIDropDownMenu_SetText,
	JustifyText = UIDropDownMenu_JustifyText,
	SetAnchor = UIDropDownMenu_SetAnchor,
	SetButtonText = UIDropDownMenu_SetButtonText,
	GetChecked = UIDropDownMenuButton_GetChecked,
	GetName = UIDropDownMenuButton_GetName,
	OpenColorPicker = UIDropDownMenuButton_OpenColorPicker,
}

-- ---------------------------------------------------------------- more levels and buttons

-- 3.3.5's UIDropDownMenu_CreateFrames(level, index): a menu deeper, or longer,
-- than the DropDownList frames that exist gets new ones, made on demand as 3.x
-- does. Vanilla has three lists of 40 buttons, and past them its AddButton
-- refuses ("Too many levels" / "Too many buttons") and ToggleDropDownMenu
-- indexes a missing list. Zygor Guides Viewer's guide menu nests deeper.
-- UIDROPDOWNMENU_MAXLEVELS / UIDROPDOWNMENU_MAXBUTTONS grow with the frames, so
-- the client's own loops over the lists cover the new ones. A new list closes
-- the lists under it when it hides, as the client's own do, and clears its
-- OPEN_DROPDOWNMENUS entry where the client keeps that table (OctoWoW).
local function ClearOpenMenus(level)
	local open = type(OPEN_DROPDOWNMENUS) == "table" and OPEN_DROPDOWNMENUS[level]
	if type(open) == "table" then
		for k in pairs(open) do open[k] = nil end
	end
end

if not UIDropDownMenu_CreateFrames then
	function UIDropDownMenu_CreateFrames(level, index)
		while level > UIDROPDOWNMENU_MAXLEVELS do
			local id = UIDROPDOWNMENU_MAXLEVELS + 1
			local name = "DropDownList" .. id
			local list = CreateFrame("Button", name, nil, "UIDropDownListTemplate")
			list:SetFrameStrata("FULLSCREEN_DIALOG")
			list:SetToplevel(1)
			list:Hide()
			list:SetID(id)
			list:SetWidth(180)
			list:SetHeight(10)
			list.numButtons = 0
			list.maxWidth = 0
			-- The template's buttons, then any the other lists have grown past it.
			local have = 0
			while _G[name .. "Button" .. (have + 1)] do have = have + 1 end
			for i = have + 1, UIDROPDOWNMENU_MAXBUTTONS do
				local button = CreateFrame("Button", name .. "Button" .. i, list, "UIDropDownMenuButtonTemplate")
				button:SetID(i)
			end
			if type(OPEN_DROPDOWNMENUS) == "table" then
				OPEN_DROPDOWNMENUS[id] = OPEN_DROPDOWNMENUS[id] or {}
			end
			list:SetScript("OnHide", function() ClearOpenMenus(id) end)
			local above = _G["DropDownList" .. (id - 1)]
			local aboveOnHide = above:GetScript("OnHide")
			above:SetScript("OnHide", function()
				if aboveOnHide then aboveOnHide() end
				CloseDropDownMenus(id)
			end)
			UIDROPDOWNMENU_MAXLEVELS = id
		end
		while index > UIDROPDOWNMENU_MAXBUTTONS do
			local id = UIDROPDOWNMENU_MAXBUTTONS + 1
			for i = 1, UIDROPDOWNMENU_MAXLEVELS do
				local listName = "DropDownList" .. i
				if not _G[listName .. "Button" .. id] then
					local button = CreateFrame("Button", listName .. "Button" .. id, _G[listName], "UIDropDownMenuButtonTemplate")
					button:SetID(id)
				end
			end
			UIDROPDOWNMENU_MAXBUTTONS = id
		end
	end
end

-- menuList for the level ToggleDropDownMenu is opening, while it runs.
local pendingMenuList = {}
-- Set while EasyMenu_Initialize adds its buttons.
local addingEasyMenu = false

-- The menuList a 3.x client would hand an init function for `level`.
local function MenuListFor(frame, level)
	return pendingMenuList[level or 1] or ((level or 1) == 1 and frame and frame.menuList) or nil
end

-- ---------------------------------------------------------------- init / toggle

function UIDropDownMenu_Initialize(frame, initFunction, displayMode, level, menuList)
	frame = frame or this
	-- 3.x keeps the list on the frame; vanilla's own re-initializations pass
	-- none, so only a list given here replaces it.
	if frame and menuList ~= nil then
		frame.menuList = menuList
	end
	return orig.Initialize(frame, initFunction, displayMode, level)
end

function ToggleDropDownMenu(level, value, dropDownFrame, anchorName, xOffset, yOffset, menuList, button)
	local lvl = level or 1
	if menuList == nil and lvl > 1 then
		-- Vanilla's button and expand-arrow OnEnter pass no list; 3.x passes
		-- the hovered button's. `this` is that button, or its expand arrow.
		local from = button or this
		if from and from.hasArrow == nil and from.GetParent then
			from = from:GetParent()
		end
		menuList = from and from.menuList
	end
	UIDropDownMenu_CreateFrames(lvl, 0)
	local saved = pendingMenuList[lvl]
	pendingMenuList[lvl] = menuList
	orig.Toggle(level, value, dropDownFrame, anchorName, xOffset, yOffset)
	pendingMenuList[lvl] = saved
end

-- ---------------------------------------------------------------- buttons

local function ShallowCopy(t)
	local c = {}
	for k, v in pairs(t) do c[k] = v end
	return c
end

function UIDropDownMenu_AddButton(info, level)
	level = level or 1
	UIDropDownMenu_CreateFrames(level, 0)
	local listFrame = _G["DropDownList" .. level]
	UIDropDownMenu_CreateFrames(level, (listFrame.numButtons or 0) + 1)
	local button = _G["DropDownList" .. level .. "Button" .. ((listFrame.numButtons or 0) + 1)]

	-- What vanilla draws: `checked` evaluated, `colorCode` applied (3.x drops
	-- it on disabled buttons).
	local drawn = info
	local checked = info.checked
	local disabled = info.disabled or info.isTitle or info.notClickable
	if type(checked) == "function" or (info.colorCode and info.text and not disabled) then
		drawn = ShallowCopy(info)
		if type(checked) == "function" then
			drawn.checked = checked()
		end
		if info.colorCode and info.text and not disabled then
			drawn.text = info.colorCode .. info.text .. "|r"
		end
	end
	orig.AddButton(drawn, level)
	if not button or not button:IsShown() then
		return -- vanilla refused it (too many buttons or levels)
	end

	button.classicapiEasyMenu = addingEasyMenu or nil
	button.menuList = info.menuList
	button.tooltipOnButton = info.tooltipOnButton
	button.tooltipWhileDisabled = info.tooltipWhileDisabled
	button.noClickSound = info.noClickSound
	button.padding = info.padding
	if addingEasyMenu then
		button.checked = info.checked -- 3.x keeps a checked function on the button
	end

	if info.text and info.fontObject then
		button:SetTextFontObject(info.fontObject)
		button:SetHighlightFontObject(info.fontObject)
	end
	if info.text and info.padding then
		-- Vanilla's own width for this button, plus the padding.
		local width = _G[button:GetName() .. "NormalText"]:GetWidth() + 60
		if info.hasArrow or info.hasColorSwatch then width = width + 10 end
		if info.notCheckable then width = width - 30 end
		if info.icon then width = width + 10 end
		width = width + info.padding
		if width > listFrame.maxWidth then listFrame.maxWidth = width end
	end
end

-- 3.x click: toggle or close first, then func(button, arg1, arg2, checked).
function UIDropDownMenuButton_OnClick(self)
	local button = self or this
	if not (button and button.classicapiEasyMenu) then
		return orig.OnClick(self)
	end

	local checked = button.checked
	if type(checked) == "function" then
		checked = checked()
	end
	if button.keepShownOnClick then
		if checked then
			_G[button:GetName() .. "Check"]:Hide()
			checked = false
		else
			_G[button:GetName() .. "Check"]:Show()
			checked = true
		end
	else
		button:GetParent():Hide()
	end
	if type(button.checked) ~= "function" then
		button.checked = checked
	end

	-- Saved first: func may reuse this button for another menu.
	local playSound = not button.noClickSound
	local func = button.func
	if not func then
		return
	end
	func(button, button.arg1, button.arg2, checked)
	if playSound then
		PlaySound("UChatScrollButton")
	end
end

-- tooltipOnButton / tooltipWhileDisabled, as the 3.x button templates show them.
local function ShowButtonTooltip(button)
	if button.tooltipOnButton then
		GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
		GameTooltip:AddLine(button.tooltipTitle, 1.0, 1.0, 1.0)
		GameTooltip:AddLine(button.tooltipText)
		GameTooltip:Show()
	else
		GameTooltip_AddNewbieTip(button.tooltipTitle, 1.0, 1.0, 1.0, button.tooltipText, 1)
	end
end

for level = 1, UIDROPDOWNMENU_MAXLEVELS do
	for i = 1, UIDROPDOWNMENU_MAXBUTTONS do
		local button = _G["DropDownList" .. level .. "Button" .. i]
		if button then
			-- Vanilla's own OnEnter already shows a newbie tip for tooltipTitle.
			button:HookScript("OnEnter", function(self)
				local b = self or this
				if b.tooltipTitle and b.tooltipOnButton then
					ShowButtonTooltip(b)
				end
			end)
			local invisible = _G[button:GetName() .. "InvisibleButton"]
			if invisible then
				invisible:HookScript("OnEnter", function(self)
					local parent = (self or this):GetParent()
					if parent.tooltipTitle and parent.tooltipWhileDisabled then
						ShowButtonTooltip(parent)
					end
				end)
			end
		end
	end
end

-- ---------------------------------------------------------------- setters (either order)

function UIDropDownMenu_SetWidth(frame, width, padding)
	if type(frame) ~= "table" then
		return orig.SetWidth(frame, width) -- vanilla: (width [, frame])
	end
	local name = frame:GetName()
	_G[name .. "Middle"]:SetWidth(width)
	local defaultPadding = 25
	if padding then
		frame:SetWidth(width + padding)
		_G[name .. "Text"]:SetWidth(width)
	else
		frame:SetWidth(width + defaultPadding + defaultPadding)
		_G[name .. "Text"]:SetWidth(width - defaultPadding)
	end
	frame.noResize = 1
end

function UIDropDownMenu_SetButtonWidth(frame, width)
	if type(frame) ~= "table" then
		return orig.SetButtonWidth(frame, width) -- vanilla: (width [, frame])
	end
	if width == "TEXT" then
		width = _G[frame:GetName() .. "Text"]:GetWidth()
	end
	_G[frame:GetName() .. "Button"]:SetWidth(width)
	frame.noResize = 1
end

function UIDropDownMenu_SetText(frame, text)
	if type(frame) ~= "table" then
		return orig.SetText(frame, text) -- vanilla: (text [, frame])
	end
	_G[frame:GetName() .. "Text"]:SetText(text)
end

function UIDropDownMenu_JustifyText(frame, justification)
	if type(frame) ~= "table" then
		return orig.JustifyText(frame, justification) -- vanilla: (justification [, frame])
	end
	local name = frame:GetName()
	local text = _G[name .. "Text"]
	text:ClearAllPoints()
	if justification == "LEFT" then
		text:SetPoint("LEFT", name .. "Left", "LEFT", 27, 2)
		text:SetJustifyH("LEFT")
	elseif justification == "RIGHT" then
		text:SetPoint("RIGHT", name .. "Right", "RIGHT", -43, 2)
		text:SetJustifyH("RIGHT")
	elseif justification == "CENTER" then
		text:SetPoint("CENTER", name .. "Middle", "CENTER", -5, 2)
		text:SetJustifyH("CENTER")
	end
end

function UIDropDownMenu_SetAnchor(dropdown, xOffset, yOffset, point, relativeTo, relativePoint)
	if type(dropdown) ~= "table" then
		-- vanilla: (xOffset, yOffset [, dropdown], point, relativeTo, relativePoint)
		return orig.SetAnchor(dropdown, xOffset, yOffset, point, relativeTo, relativePoint)
	end
	dropdown.xOffset = xOffset
	dropdown.yOffset = yOffset
	dropdown.point = point
	dropdown.relativeTo = relativeTo
	dropdown.relativePoint = relativePoint
end

function UIDropDownMenu_SetButtonText(level, id, text, colorCode, g, b)
	if type(colorCode) ~= "string" then
		return orig.SetButtonText(level, id, text, colorCode, g, b) -- vanilla: (..., r, g, b)
	end
	_G["DropDownList" .. level .. "Button" .. id]:SetText(colorCode .. text .. "|r")
end

function UIDropDownMenuButton_GetChecked(self)
	if not self then
		return orig.GetChecked()
	end
	return _G[self:GetName() .. "Check"]:IsShown()
end

function UIDropDownMenuButton_GetName(self)
	if not self then
		return orig.GetName()
	end
	return _G[self:GetName() .. "NormalText"]:GetText()
end

-- Vanilla takes (button); 3.x takes (self [, button]). Either way the
-- explicit button, else the one given first, else `this`.
function UIDropDownMenuButton_OpenColorPicker(self, button)
	return orig.OpenColorPicker(button or self)
end

-- ---------------------------------------------------------------- 3.x-only

if not UIDropDownMenu_DisableDropDown then
	function UIDropDownMenu_DisableDropDown(dropDown)
		local label = _G[dropDown:GetName() .. "Label"]
		if label then
			label:SetVertexColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b)
		end
		_G[dropDown:GetName() .. "Text"]:SetVertexColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b)
		_G[dropDown:GetName() .. "Button"]:Disable()
		dropDown.isDisabled = 1
	end
end

if not UIDropDownMenu_EnableDropDown then
	function UIDropDownMenu_EnableDropDown(dropDown)
		local label = _G[dropDown:GetName() .. "Label"]
		if label then
			label:SetVertexColor(NORMAL_FONT_COLOR.r, NORMAL_FONT_COLOR.g, NORMAL_FONT_COLOR.b)
		end
		_G[dropDown:GetName() .. "Text"]:SetVertexColor(HIGHLIGHT_FONT_COLOR.r, HIGHLIGHT_FONT_COLOR.g, HIGHLIGHT_FONT_COLOR.b)
		_G[dropDown:GetName() .. "Button"]:Enable()
		dropDown.isDisabled = nil
	end
end

if not UIDropDownMenu_IsEnabled then
	function UIDropDownMenu_IsEnabled(dropDown)
		return not dropDown.isDisabled
	end
end

if not UIDropDownMenu_GetValue then
	-- Only meaningful right after the dropdown was initialized, as on 3.x.
	function UIDropDownMenu_GetValue(id)
		local button = _G["DropDownList1Button" .. id]
		if button then
			return button.value
		end
		return nil
	end
end

if not OpenColorPicker then
	function OpenColorPicker(info)
		ColorPickerFrame.func = info.swatchFunc
		ColorPickerFrame.hasOpacity = info.hasOpacity
		ColorPickerFrame.opacityFunc = info.opacityFunc
		ColorPickerFrame.opacity = info.opacity
		ColorPickerFrame.previousValues = { r = info.r, g = info.g, b = info.b, opacity = info.opacity }
		ColorPickerFrame.cancelFunc = info.cancelFunc
		ColorPickerFrame.extraInfo = info.extraInfo
		-- Last: it triggers a call to ColorPickerFrame.func().
		ColorPickerFrame:SetColorRGB(info.r, info.g, info.b)
		ShowUIPanel(ColorPickerFrame)
	end
end

if not ColorPicker_GetPreviousValues then
	function ColorPicker_GetPreviousValues()
		local p = ColorPickerFrame.previousValues
		return p.r, p.g, p.b
	end
end

-- ---------------------------------------------------------------- EasyMenu

-- EasyMenu(menuList, menuFrame, anchor, x, y, displayMode, autoHideDelay):
-- shows `menuList` (a list of UIDropDownMenu_AddButton info tables, `menuList`
-- on an entry making a submenu) from `menuFrame`, a UIDropDownMenuTemplate
-- frame. `autoHideDelay` is accepted and unused, as on 3.x.
function EasyMenu(menuList, menuFrame, anchor, x, y, displayMode, autoHideDelay)
	if displayMode == "MENU" then
		menuFrame.displayMode = displayMode
	end
	UIDropDownMenu_Initialize(menuFrame, EasyMenu_Initialize, displayMode, nil, menuList)
	ToggleDropDownMenu(1, nil, menuFrame, anchor, x, y, menuList)
end

function EasyMenu_Initialize(frame, level, menuList)
	if type(frame) ~= "table" then
		-- Called the vanilla way, init(level), by UIDropDownMenu_Initialize.
		frame, level = _G[UIDROPDOWNMENU_INIT_MENU], frame
		menuList = MenuListFor(frame, level)
	end
	if not menuList then
		return
	end
	addingEasyMenu = true
	for index = 1, table.getn(menuList) do
		local value = menuList[index]
		if value.text then
			value.index = index
			UIDropDownMenu_AddButton(value, level)
		end
	end
	addingEasyMenu = false
end
