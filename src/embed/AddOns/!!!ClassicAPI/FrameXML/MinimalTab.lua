-- Backport of retail's Blizzard_SharedXML/Shared/Tabs/MinimalTab.lua.
--
-- The one difference: retail sets the three pieces from Options_Tab
-- atlases, which this client does not have. The tab is the tooltip backdrop
-- the Settings window frames its lists with, and UpdateAtlas colors it by
-- state: the selected tab is lighter with a gold edge, a hovered tab a little
-- lighter than the others.

local TAB_BACKDROP =
{
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
	tile = true, tileSize = 16, edgeSize = 16,
	insets = { left = 5, right = 5, top = 5, bottom = 5 },
};

MinimalTabMixin = {};

function MinimalTabMixin:OnLoad()
	SelectableButtonMixin.OnLoad(self);
	self:SetBackdrop(TAB_BACKDROP);

	self.Text:SetText(self.tabText);
	self:SetWidth(self.Text:GetStringWidth() + 40);

	self:OnSelected(false);
end

function MinimalTabMixin:UpdateAtlas()
	if self:IsSelected() then
		self:SetBackdropColor(0.25, 0.25, 0.25, 0.9);
		self:SetBackdropBorderColor(1, 0.82, 0);
	elseif self.over then
		self:SetBackdropColor(0.15, 0.15, 0.15, 0.8);
		self:SetBackdropBorderColor(0.8, 0.8, 0.8);
	else
		self:SetBackdropColor(0, 0, 0, 0.5);
		self:SetBackdropBorderColor(0.6, 0.6, 0.6);
	end
end

function MinimalTabMixin:OnSelected(newSelected)
	self:UpdateAtlas();

	self.Text:SetPoint("CENTER", 0, 0);
	if newSelected then
		self.Text:SetFontObject("GameFontHighlightSmall");
	else
		self.Text:SetFontObject(self.over and "GameFontHighlightSmall" or "GameFontNormalSmall");
	end
end

function MinimalTabMixin:OnEnter()
	self.over = true;
	self:UpdateAtlas();
	if not self:IsSelected() then
		self.Text:SetFontObject("GameFontHighlightSmall");
	end
end

function MinimalTabMixin:OnLeave()
	self.over = nil;
	self:UpdateAtlas();
	if not self:IsSelected() then
		self.Text:SetFontObject("GameFontNormalSmall");
	end
end

function MinimalTabMixin:OnEnable()
	self:UpdateAtlas();
end

function MinimalTabMixin:OnDisable()
	self:UpdateAtlas();
end
