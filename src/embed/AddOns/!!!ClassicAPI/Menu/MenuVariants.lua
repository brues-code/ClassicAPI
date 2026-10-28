-- Backport of retail's Blizzard_Menu/MenuVariants.lua.
--
-- Implementation differences from the retail source:
--   - 1.12 has no gear button art or tooltip divider. The gear button uses
--     the gear icon, and the divider uses the middle slice of the help frame
--     divider, which has no end caps and stretches to any width (rows 23-38
--     of its 256x64 file: the 8-pixel bar with 4 clear rows above and below).

--[[
MenuVariants functions are suitable to be overwritten for game specific implementations. Some
implementations are here for reference despite being shared code.
]]--

MenuVariants = {};

MenuVariants.GearButtonTexture = [[Interface\Icons\INV_Misc_Gear_01]];
MenuVariants.GearButtonAnchor = CreateAnchor("RIGHT");
MenuVariants.CancelButtonTexture = [[Interface\Buttons\UI-GroupLoot-Pass-Up]];
MenuVariants.CancelButtonAnchor = CreateAnchor("RIGHT", nil, "LEFT", -3, 0);

MenuVariants.DisabledHighlightOpacity = .4;

function MenuVariants.CreateFontString(frame)
	local fontString = frame:AttachFontString();
	fontString:SetPoint("LEFT");
	fontString:SetHeight(20);
	return fontString;
end

function MenuVariants.CreateDivider(frame)
	local divider = frame:AttachTexture();
	divider:SetPoint("LEFT");
	divider:SetPoint("RIGHT");
	divider:SetTexture("Interface\\HelpFrame\\HelpFrameDivider");
	divider:SetTexCoord(0, 1, 23/64, 39/64);
	divider:SetHeight(16);
	return divider;
end

function MenuVariants.CreateSubmenuArrow(frame)
	local arrow = frame:AttachTexture();
	frame.arrow = arrow;
	arrow:SetPoint("RIGHT");
	arrow:SetSize(16, 16);
	arrow:SetTexture("Interface\\ChatFrame\\ChatFrameExpandArrow");
	arrow:SetDrawLayer("ARTWORK");
	return arrow;
end

function MenuVariants.CreateHighlight(frame)
	local highlight = frame:AttachTexture();
	frame.highlight = highlight;
	highlight:SetAllPoints();
	highlight:SetBlendMode("ADD");
	highlight:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight");
	highlight:SetDrawLayer("BACKGROUND");
	highlight:Hide();
	return highlight;
end

function MenuVariants.GetCheckboxCheckSoundKit()
	return SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON;
end

function MenuVariants.GetCheckboxUncheckSoundKit()
	return SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_OFF;
end

function MenuVariants.GetButtonSoundKit()
	return SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON;
end

function MenuVariants.GetDropdownOpenSoundKit()
	return SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON;
end

function MenuVariants.GetDropdownCloseSoundKit()
	return SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_OFF;
end

local function GenerateError()
	error("Requires implementation in game specific version of MenuVariants.lua");
end

function MenuVariants.GetDefaultMenuMixin()
	GenerateError();
end

function MenuVariants.GetDefaultContextMenuMixin()
	GenerateError();
end
