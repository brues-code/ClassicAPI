-- Backport of retail's Blizzard_SharedXML/PortraitFrame.lua.
--
-- Implementation differences from the retail source:
--   - The border is a backdrop (SharedUIPanelTemplates.xml), the same with
--     and without the portrait, so SetBorder has no layout to apply.
--   - No SetPortraitToClassIcon or SetPortraitToSpecIcon: this client has no
--     class circles art (CLASS_ICON_TCOORDS) and no specializations.
--   - No DefaultPanelMixin or PortraitFrameFlatBaseMixin: their templates are
--     not ported.

TitledPanelMixin = {};

function TitledPanelMixin:GetTitleText()
	return self.TitleContainer.TitleText;
end

function TitledPanelMixin:SetTitleColor(color)
	self:GetTitleText():SetTextColor(color:GetRGBA());
end

function TitledPanelMixin:SetTitle(title)
	self:GetTitleText():SetText(title);
end

function TitledPanelMixin:SetTitleFormatted(fmt, ...)
	self:GetTitleText():SetFormattedText(fmt, ...);
end

function TitledPanelMixin:SetTitleMaxLinesAndHeight(maxLines, height)
	self:GetTitleText():SetMaxLines(maxLines);
	self:GetTitleText():SetHeight(height);
end

function TitledPanelMixin:SetTitleOffsets(leftOffset, rightOffset)
	self.TitleContainer:SetPoint("TOPLEFT", self, "TOPLEFT", leftOffset or 58, -1);
	self.TitleContainer:SetPoint("TOPRIGHT", self, "TOPRIGHT", rightOffset or -24, -1);
end

PortraitFrameMixin = CreateFromMixins(TitledPanelMixin);

function PortraitFrameMixin:GetPortrait()
	return self.PortraitContainer.portrait;
end

function PortraitFrameMixin:HasPortraitTexture()
	return self.PortraitContainer.portrait:GetTexture();
end

function PortraitFrameMixin:SetBorder(layoutName)
end

function PortraitFrameMixin:SetPortraitToAsset(texture)
	self:GetPortrait():SetTexture(texture);
end

function PortraitFrameMixin:SetPortraitToUnit(unit)
	SetPortraitTexture(self:GetPortrait(), unit);
end

function PortraitFrameMixin:SetPortraitToBag(bagID)
	C_Container.SetBagPortraitTexture(self:GetPortrait(), bagID);
end

function PortraitFrameMixin:SetPortraitTextureRaw(texture)
	self:GetPortrait():SetTexture(texture);
end

function PortraitFrameMixin:SetPortraitAtlasRaw(atlas, ...)
	self:GetPortrait():SetAtlas(atlas, ...);
end

function PortraitFrameMixin:SetPortraitTexCoord(...)
	self:GetPortrait():SetTexCoord(...);
end

function PortraitFrameMixin:SetPortraitShown(shown)
	self:GetPortrait():SetShown(shown);
end

function PortraitFrameMixin:SetPortraitTextureSizeAndOffset(size, offsetX, offsetY)
	local portrait = self:GetPortrait();
	portrait:SetSize(size, size);
	portrait:SetPoint("TOPLEFT", self, "TOPLEFT", offsetX, offsetY);
end

do
	local function SetFrameLevelInternal(frame, level)
		if frame then
			assertsafe(level < 10000, "Base level needs to be low enough to accomodate the existing layering"); -- TODO: Should become a constant
			frame:SetFrameLevel(level);
		end
	end

	function PortraitFrameMixin:SetFrameLevelsFromBaseLevel(baseLevel)
		SetFrameLevelInternal(self.NineSlice, baseLevel + 500);
		SetFrameLevelInternal(self.PortraitContainer, baseLevel + 400);
		SetFrameLevelInternal(self.TitleContainer, baseLevel + 510);
		SetFrameLevelInternal(self.CloseButton, baseLevel + 510);
	end
end
