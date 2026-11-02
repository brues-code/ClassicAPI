-- Backport of the instructions and search box parts of retail's
-- Blizzard_SharedXML/Shared/InputBox/InputBoxTemplates.lua.
--
-- Implementation differences from the retail source:
--   - No narration: ClearButtonMixin has no NarrationGetName.

function EditBox_ClearFocus(self)
	self:ClearFocus();
end

function InputBoxInstructions_OnTextChanged(self)
	self.Instructions:SetShown(self:GetText() == "");
end

function InputBoxInstructions_UpdateColorForEnabledState(self, color)
	if color then
		self:SetTextColor(color:GetRGBA());
	end
end

function InputBoxInstructions_OnDisable(self)
	InputBoxInstructions_UpdateColorForEnabledState(self, self.disabledColor);
end

function InputBoxInstructions_OnEnable(self)
	InputBoxInstructions_UpdateColorForEnabledState(self, self.enabledColor);
end

function InputBoxInstructions_OnEnter(self)
	if self.showTooltipForTruncatedInstructions then
		InputBoxInstructions_ShowTooltipIfTruncated(self);
	end
end

function InputBoxInstructions_OnLeave(self)
	if self.showTooltipForTruncatedInstructions then
		-- Note: Some inheritors of this template may have a special version of GetAppropriateTooltip (Ex. CatalogShopMixin:GetAppropriateTooltip)
		-- If this functionality is desired then this probably needs to be overridden
		GetAppropriateTooltip():Hide();
	end
end

function InputBoxInstructions_ShowTooltipIfTruncated(self)
	if not self.Instructions:IsShown() or not self.Instructions:IsTruncated() then
		return;
	end

	local tooltip = GetAppropriateTooltip();
	tooltip:SetOwner(self, "ANCHOR_RIGHT");
	local text = self.Instructions:GetText();
	GameTooltip_AddHighlightLine(tooltip, text);
	tooltip:Show();
end

function SearchBoxTemplate_OnLoad(self)
	self.searchIcon:SetVertexColor(0.6, 0.6, 0.6);
	self.Instructions:SetText(self.instructionText);
end

function SearchBoxTemplate_OnEditFocusLost(self)
	if ( self:GetText() == "" ) then
		self.searchIcon:SetVertexColor(0.6, 0.6, 0.6);
		self.clearButton:Hide();
	end
end

function SearchBoxTemplate_OnEditFocusGained(self)
	self.searchIcon:SetVertexColor(1.0, 1.0, 1.0);
	self.clearButton:Show();
end

function SearchBoxTemplate_OnTextChanged(self)
	if ( not self:HasFocus() and self:GetText() == "" ) then
		self.searchIcon:SetVertexColor(0.6, 0.6, 0.6);
		self.clearButton:Hide();
	else
		self.searchIcon:SetVertexColor(1.0, 1.0, 1.0);
		self.clearButton:Show();
	end
	InputBoxInstructions_OnTextChanged(self);
end

function SearchBoxTemplate_ClearText(self)
	self:SetText("");
	self:ClearFocus();
end

function SearchBoxTemplateClearButton_OnClick(self)
	PlaySound(SOUNDKIT.IG_MAINMENU_OPTION_CHECKBOX_ON);
	SearchBoxTemplate_ClearText(self:GetParent());
end

ClearButtonMixin = {};

function ClearButtonMixin:OnEnter()
	self.Icon:SetAlpha(1.0);
end

function ClearButtonMixin:OnLeave()
	self.Icon:SetAlpha(0.5);
end

function ClearButtonMixin:OnMouseDown()
	if self:IsEnabled() then
		self.Icon:SetPoint("TOPLEFT", self, "TOPLEFT", 4, -4);
	end
end

function ClearButtonMixin:OnMouseUp()
	self.Icon:SetPoint("TOPLEFT", self, "TOPLEFT", 3, -3);
end

function ClearButtonMixin:OnClick()
	SearchBoxTemplateClearButton_OnClick(self);
end
