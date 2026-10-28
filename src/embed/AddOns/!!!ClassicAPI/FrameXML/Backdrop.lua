-- Backport of retail's Blizzard_SharedXML/Backdrop.lua. The BACKDROP_*
-- tables are verbatim. Retail builds the backdrop out of nine textures of its
-- own because its frames lost the C backdrop; 1.12's frames still have it, and
-- it draws the same nine slices, so the mixin keeps retail's methods and state
-- (backdropInfo, HasBackdropInfo, the OnLoad key values) and hands the drawing
-- to the engine. The engine re-slices the edges on every resize, so retail's
-- OnBackdropSizeChanged has nothing to do here. GetBackdrop and the colour
-- methods are the engine's own.

BACKDROP_ACHIEVEMENTS_0_64 = {
	edgeFile = "Interface\\AchievementFrame\\UI-Achievement-WoodBorder",
	edgeSize = 64,
	tileEdge = true,
};

BACKDROP_ARENA_32_32 = {
	bgFile = "Interface\\CharacterFrame\\UI-Party-Background",
	edgeFile = "Interface\\ArenaEnemyFrame\\UI-Arena-Border",
	tile = true,
	tileEdge = true,
	tileSize = 32,
	edgeSize = 32,
	insets = { left = 32, right = 32, top = 32, bottom = 32 },
};

BACKDROP_DIALOG_32_32 = {
	bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	tile = true,
	tileEdge = true,
	tileSize = 32,
	edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 11 },
};

BACKDROP_DARK_DIALOG_32_32 = {
	bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	tile = true,
	tileEdge = true,
	tileSize = 32,
	edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 11 },
};

BACKDROP_DIALOG_EDGE_32  = {
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	tileEdge = true,
	edgeSize = 32,
};

BACKDROP_GOLD_DIALOG_32_32 = {
	bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border",
	tile = true,
	tileEdge = true,
	tileSize = 32,
	edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 11 },
};

BACKDROP_WATERMARK_DIALOG_0_16 = {
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-TestWatermark-Border",
	tileEdge = true,
	edgeSize = 16,
};

BACKDROP_SLIDER_8_8 = {
	bgFile = "Interface\\Buttons\\UI-SliderBar-Background",
	edgeFile = "Interface\\Buttons\\UI-SliderBar-Border",
	tile = true,
	tileEdge = true,
	tileSize = 8,
	edgeSize = 8,
	insets = { left = 3, right = 3, top = 6, bottom = 6 },
};

BACKDROP_PARTY_32_32 = {
	bgFile = "Interface\\CharacterFrame\\UI-Party-Background",
	edgeFile = "Interface\\CharacterFrame\\UI-Party-Border",
	tile = true,
	tileEdge = true,
	tileSize = 32,
	edgeSize = 32,
	insets = { left = 32, right = 32, top = 32, bottom = 32 },
};

BACKDROP_TOAST_12_12 = {
	bgFile = "Interface\\FriendsFrame\\UI-Toast-Background",
	edgeFile = "Interface\\FriendsFrame\\UI-Toast-Border",
	tile = true,
	tileEdge = true,
	tileSize = 12,
	edgeSize = 12,
	insets = { left = 5, right = 5, top = 5, bottom = 5 },
};

BACKDROP_CALLOUT_GLOW_0_16 = {
	edgeFile = "Interface\\TutorialFrame\\UI-TutorialFrame-CalloutGlow",
	edgeSize = 16,
	tileEdge = true,
};

BACKDROP_CALLOUT_GLOW_0_20 = {
	edgeFile = "Interface\\TutorialFrame\\UI-TutorialFrame-CalloutGlow",
	edgeSize = 20,
	tileEdge = true,
};

BACKDROP_TEXT_PANEL_0_16 = {
	edgeFile = "Interface\\Glues\\Common\\TextPanel-Border",
	tileEdge = true,
	edgeSize = 16,
};

BACKDROP_CHARACTER_CREATE_TOOLTIP_32_32 = {
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	edgeFile = "Interface\\Glues\\Common\\TextPanel-Border",
	tile = true,
	tileEdge = true,
	tileSize = 32,
	edgeSize = 32,
	insets = { left = 8, right = 4, top = 4, bottom = 8 },
};

BACKDROP_WRATH_CHARACTER_CREATE_TOOLTIP_32_32 = {
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	tile = true,
	tileSize = 32,
	insets = { left = 10, right = 0, top = 10, bottom = 6 },
};

BACKDROP_MISTS_CHARACTER_CREATE_TOOLTIP_32_32 = {
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	tile = true,
	tileSize = 32,
	insets = { left = 10, right = 10, top = 10, bottom = 6 },
};


BACKDROP_TUTORIAL_16_16 = {
	bgFile = "Interface\\TutorialFrame\\TutorialFrameBackground",
	edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
	tile = true,
	tileEdge = true,
	tileSize = 16,
	edgeSize = 16,
	insets = { left = 3, right = 5, top = 3, bottom = 5 },
};

local NativeSetBackdrop = UIParent.SetBackdrop;
local NativeGetBackdrop = UIParent.GetBackdrop;

BackdropTemplateMixin = { };

function BackdropTemplateMixin:OnBackdropLoaded()
	if self.backdropInfo then
		-- check for invalid info
		if not self.backdropInfo.edgeFile and not self.backdropInfo.bgFile then
			self.backdropInfo = nil;
			return;
		end
		self:ApplyBackdrop();
		do
			local r, g, b = 1, 1, 1;
			if self.backdropColor then
				r, g, b = self.backdropColor:GetRGB();
			end
			local a = self.backdropColorAlpha or 1;
			self:SetBackdropColor(r, g, b, a);
		end
		do
			local r, g, b = 1, 1, 1;
			if self.backdropBorderColor then
				r, g, b = self.backdropBorderColor:GetRGB();
			end
			local a = self.backdropBorderColorAlpha or 1;
			self:SetBackdropBorderColor(r, g, b, a);
		end
	end
end

-- An unset or zero size draws at the engine's default, so report that.
function BackdropTemplateMixin:GetEdgeSize()
	if self.backdropInfo.edgeSize and self.backdropInfo.edgeSize > 0 then
		return self.backdropInfo.edgeSize;
	else
		return NativeGetBackdrop(self).edgeSize;
	end
end

function BackdropTemplateMixin:HasBackdropInfo(backdropInfo)
	return self.backdropInfo == backdropInfo;
end

function BackdropTemplateMixin:ClearBackdrop()
	if self.backdropInfo then
		NativeSetBackdrop(self, nil);
		self.backdropInfo = nil;
	end
end

-- A new engine backdrop starts with white colours, as retail's ApplyBackdrop
-- sets them.
function BackdropTemplateMixin:ApplyBackdrop()
	NativeSetBackdrop(self, self.backdropInfo);
end

function BackdropTemplateMixin:SetBackdrop(backdropInfo)
	if backdropInfo then
		if self:HasBackdropInfo(backdropInfo) then
			return;
		end

		if not backdropInfo.edgeFile and not backdropInfo.bgFile then
			self:ClearBackdrop();
			return;
		end

		self.backdropInfo = backdropInfo;
		self:ApplyBackdrop();
	else
		self:ClearBackdrop();
	end
end
