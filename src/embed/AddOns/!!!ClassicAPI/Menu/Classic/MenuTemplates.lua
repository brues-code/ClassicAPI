-- Backport of retail's Blizzard_Menu/Classic/MenuTemplates.lua.
--
-- Implementation differences from the retail source:
--   - The Classic dropdown and menu atlases are art 1.12 does not have, so
--     the styles are drawn the way 1.12's own dropdowns are drawn:
--       - Dropdowns use the dropdown label frame and the scroll-down button
--         (UIDropDownMenuTemplate). The button's state picks the arrow
--         texture; hover is the button's highlight texture.
--       - Filter dropdowns use the stretched panel button (UIPanelButtonTemplate2).
--       - MenuStyle1 is the dialog box backdrop and MenuStyle2 the tooltip
--         backdrop, the two backdrops of 1.12's DropDownList. The insets are
--         its geometry: 15 (UIDROPDOWNMENU_BORDER_HEIGHT) above and below.
--         At the sides the dialog styles keep the dialog border's own inset
--         (12) so an expand arrow or color swatch clears the border, and the
--         tooltip style keeps the 10 where 1.12's menu mode puts them; the
--         same on both sides, so a divider sits centred. The thin style
--         keeps the dialog backdrop's exact insets (11 left, 12 right).
--         Retail's atlases leave a clear margin inside those insets where
--         1.12's opaque borders do not, so the rows get 10 of extent
--         padding, what 1.12 adds to a row with an arrow or a swatch.
--   - Without atlas sizes, the dropdown background no longer switches art by
--     width, so there is no OnSizeChanged.

-- Requires the button to inherit ButtonStateBehaviorMixin
function GetWowStyle1ArrowButtonState(button)
	if button:IsEnabled() then
		if button:IsDown() then
			return "Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Down";
		else
			return "Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up";
		end
	end
	return "Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Disabled";
end

function WowStyle1DropdownMixin:GetArrowAtlas()
	return GetWowStyle1ArrowButtonState(self);
end

function WowStyle1DropdownMixin:OnButtonStateChanged()
	local enabled = self:IsEnabled();
	if enabled then
		self.Text:SetTextColor(HIGHLIGHT_FONT_COLOR:GetRGB());
	else
		self.Text:SetTextColor(DISABLED_FONT_COLOR:GetRGB());
	end

	self.Arrow:SetTexture(self:GetArrowAtlas());
end

function WowStyle1ArrowDropdownMixin:OnButtonStateChanged()
	self.Arrow:SetTexture(GetWowStyle1ArrowButtonState(self));
end

function WowStyle1FilterDropdownMixin:OnButtonStateChanged()
	local texture = self:GetBackgroundAtlas();
	self.Left:SetTexture(texture);
	self.Middle:SetTexture(texture);
	self.Right:SetTexture(texture);
end

MenuStyle1Mixin = CreateFromMixins(MenuStyleMixin);

function MenuStyle1Mixin:Generate()
	self:SetBackdrop(BACKDROP_DIALOG_32_32);
end

do
	local inset = 
	{
		left = 12, 
		top = 15, 
		right = 12,
		bottom = 15,
	};

	function MenuStyle1Mixin:GetInset()
		return inset;
	end
end

do
	local padding =
	{
		width = 10,
		height = 0,
	};

	function MenuStyle1Mixin:GetChildExtentPadding()
		return padding;
	end
end

MenuStyle1ThinMixin = CreateFromMixins(MenuStyle1Mixin);

do
	-- For thin menus (e.g., TimeManager frame with 2-character dropdown options), use tighter insets.
	local inset = 
	{
		left = 11, 
		top = 15, 
		right = 12,
		bottom = 15,
	};

	function MenuStyle1ThinMixin:GetInset()
		return inset;
	end
end

MenuStyle2Mixin = CreateFromMixins(MenuStyleMixin);

do
	local backdrop =
	{
		bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		tile = true,
		tileSize = 16,
		edgeSize = 16,
		insets = { left = 5, right = 5, top = 5, bottom = 5 },
	};

	function MenuStyle2Mixin:Generate()
		self:SetBackdrop(backdrop);
		self:SetBackdropBorderColor(TOOLTIP_DEFAULT_COLOR.r, TOOLTIP_DEFAULT_COLOR.g, TOOLTIP_DEFAULT_COLOR.b);
		self:SetBackdropColor(TOOLTIP_DEFAULT_BACKGROUND_COLOR.r, TOOLTIP_DEFAULT_BACKGROUND_COLOR.g, TOOLTIP_DEFAULT_BACKGROUND_COLOR.b);
	end
end

do
	local inset = 
	{
		left = 10, 
		top = 15, 
		right = 10,
		bottom = 15,
	};

	function MenuStyle2Mixin:GetInset()
		return inset;
	end
end

do
	local padding =
	{
		width = 10,
		height = 0,
	};

	function MenuStyle2Mixin:GetChildExtentPadding()
		return padding;
	end
end
