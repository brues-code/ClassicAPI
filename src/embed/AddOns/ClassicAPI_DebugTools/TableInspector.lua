-- Backport of retail's Blizzard_DebugTools/Blizzard_TableInspector.lua.
--
-- Differences from the retail source:
--   - 1.12 has no tool windows (ToolWindowOwnerMixin), so an inspector is
--     always a frame inside the game UI: the GetWindow / MoveToNewWindow
--     branches are gone.
--   - The scroll frame is TableInspectorScrollFrameTemplate (end of file).
--     1.12 sizes the scroll range from the scroll child's own height, so
--     UpdateLines sets LinesContainer's height to fit the lines.

TABLE_INSPECTOR_LINE_HEIGHT = 16;

local tableInspectorPool = CreateFramePool("FRAME", UIParent, "TableAttributeDisplayTemplate");

local DEFAULT_DATA_PROVIDERS = {
	TableInspectorAnchorDataProviderMixin,
	TableInspectorAttributeDataProviderMixin,
};

TableInspectorMixin = {};

function TableInspectorMixin:OnLoad()
	self:Reset();
end

function TableInspectorMixin:SetTableFocusedCallback(tableFocusedCallback)
	self.tableFocusedCallback = tableFocusedCallback;
end

function TableInspectorMixin:Reset()
	self:RemoveAllDataProviders();
	for i, dataProvider in ipairs(DEFAULT_DATA_PROVIDERS) do
		local dataProviderInstance = CreateFromMixins(dataProvider);
		dataProviderInstance:Initialize(self, self.LinesScrollFrame.LinesContainer);
		self:AddDataProvider(dataProviderInstance);
	end

	if self.lines then
		wipe(self.lines);
	else
		self.lines = {};
	end

	if self.tableNavigation then
		wipe(self.tableNavigation);
	else
		self.tableNavigation = {};
	end

	self.tableNavigationIndex = 0;
end

function TableInspectorMixin:OnHide()
	tableInspectorPool:Release(self);
end

function TableInspectorMixin:AddDataProvider(dataProvider)
	if not self.dataProviders then
		self.dataProviders = {};
	end

	table.insert(self.dataProviders, dataProvider);
end

function TableInspectorMixin:ClearData()
	if not self.dataProviders then
		return;
	end

	for _, dataProvider in ipairs(self.dataProviders) do
		dataProvider:Clear(self.focusedTable);
	end
end

function TableInspectorMixin:RemoveAllDataProviders()
	if self.dataProviders then
		wipe(self.dataProviders);
	end
end

function TableInspectorMixin:RefreshAllData()
	if self.dataProviders then
		for _, dataProvider in ipairs(self.dataProviders) do
			dataProvider:RefreshData(self.focusedTable);
		end
	end
end

function TableInspectorMixin:UpdateLines()
	if not self.dataProviders then
		return;
	end

	self.lines = {};

	local previousLine = nil;
	for _, dataProvider in ipairs(self.dataProviders) do
		dataProvider:HideAllLines();
		local lines = dataProvider:GetLines(self.FilterBox:GetText());
		for _, line in ipairs(lines) do
			table.insert(self.lines, line);
			if previousLine then
				line:SetPoint("TOPLEFT", previousLine, "BOTTOMLEFT", 0, 0);
			else
				line:SetPoint("TOPLEFT", self.LinesScrollFrame.LinesContainer, "TOPLEFT", 0, 0);
			end

			previousLine = line;
			line:Show();
		end
	end

	local linesContainer = self.LinesScrollFrame.LinesContainer;
	linesContainer:SetHeight(math.max(#self.lines * TABLE_INSPECTOR_LINE_HEIGHT, 1));
	self.LinesScrollFrame:UpdateScrollChildRect();
end

function TableInspectorMixin:OpenParentDisplay()
	local parent = self.focusedTable.GetParent and self.focusedTable:GetParent() or nil;
	if parent then
		self:InspectTable(parent);
	end
end

function TableInspectorMixin:CanNavigateBackward()
	return self.tableNavigationIndex > 1;
end

function TableInspectorMixin:NavigateBackward()
	if self:CanNavigateBackward() then
		self.tableNavigationIndex = self.tableNavigationIndex - 1;
		self:InspectTable(self.tableNavigation[self.tableNavigationIndex]);
	end
end

function TableInspectorMixin:CanNavigateForward()
	return self.tableNavigationIndex < #self.tableNavigation;
end

function TableInspectorMixin:NavigateForward()
	if self:CanNavigateForward() then
		self.tableNavigationIndex = self.tableNavigationIndex + 1;
		self:InspectTable(self.tableNavigation[self.tableNavigationIndex]);
	end
end

function TableInspectorMixin:DuplicateAttributeDisplay()
	local copy = DisplayTableInspectorWindow(self.focusedTable);
	copy:ClearAllPoints();
	local point, parent, relativePoint, xOffset, yOffset = self:GetPoint(1);
	copy:SetPoint(point, parent, relativePoint, xOffset + 60, yOffset + 60);
	copy:Show();
	return copy;
end

function TableInspectorMixin:SetFocusedFrameShown(shown)
	if self.focusedTable and self.focusedTable.SetShown then
		self.focusedTable:SetShown(shown);
	end
end

function TableInspectorMixin:SetDynamicUpdates(enabled)
	if enabled then
		self:SetScript("OnUpdate", function()
			self:RefreshAllData();
			self:UpdateLines();
		end);
		self.DynamicUpdateButton:SetChecked(true);
	else
		self:SetScript("OnUpdate", nil);
		self.DynamicUpdateButton:SetChecked(false);
	end
end

function TableInspectorMixin:UpdateFocusedHighlight()
	if self.focusedTable and self.focusedTable.GetDebugName and self.HighlightButton:GetChecked() then
		self.FrameHighlight:Show();
		self.FrameHighlight:HighlightFrame(self.focusedTable);
	else
		self.FrameHighlight:Hide();
	end
end

function TableInspectorMixin:SelectTable(selectedTable, selectedTableTitle)
	self:InspectTable(selectedTable, self.TitleButton.Text:GetText().." - "..selectedTableTitle);
end

function TableInspectorMixin:UpdateTableNavigation(newFocusedTable)
	-- We've branched to a new direction
	if newFocusedTable ~= self.tableNavigation[self.tableNavigationIndex] then
		for i = self.tableNavigationIndex + 1, #self.tableNavigation do
			self.tableNavigation[i] = nil;
		end

		table.insert(self.tableNavigation, newFocusedTable);
		self.tableNavigationIndex = #self.tableNavigation;
	end

	self.NavigateBackwardButton:SetEnabled(self:CanNavigateBackward());
	self.NavigateForwardButton:SetEnabled(self:CanNavigateForward());
end

function TableInspectorMixin:InspectTable(focusedTable, title)
	self.LinesScrollFrame:SetVerticalScroll(0);
	self.FilterBox:SetText("");
	self.FilterBox:ClearFocus();
	self:UpdateTableNavigation(focusedTable);
	self.OpenParentButton:SetEnabled(focusedTable and focusedTable.GetParent and focusedTable:GetParent());
	self.focusedTable = focusedTable;
	if not focusedTable then
		self:ClearData();
		self.TitleButton.Text:SetText("No Table Selected");
		return;
	end

	if focusedTable.SetShown then
		self.VisibilityButton:SetChecked(focusedTable:IsShown());
		self.VisibilityButton:Enable();
		self:UpdateFocusedHighlight();
		self.HighlightButton:Enable();
	else
		self.VisibilityButton:Disable();
		self.HighlightButton:Disable();
		self.FrameHighlight:Hide();
	end

	if not title then
		if focusedTable.GetDebugName then
			title = "Frame Attributes - "..focusedTable:GetDebugName();
		else
			title = "Table Attributes";
		end
	end

	self.TitleButton.Text:SetText(title);

	self:RefreshAllData();
	self:UpdateLines();

	if self.tableFocusedCallback then
		self.tableFocusedCallback(selectedTable);
	end
end

function TableAttributeDisplayDuplicateButton_OnClick(self)
	self:GetParent():DuplicateAttributeDisplay();
end

function TableAttributeDisplayParentButton_OnClick(self)
	self:GetParent():OpenParentDisplay();
end

function TableAttributeDisplayNavigateBackwardButton_OnClick(self)
	self:GetParent():NavigateBackward();
end

function TableAttributeDisplayNavigateForwardButton_OnClick(self)
	self:GetParent():NavigateForward();
end

function TableAttributeDisplayVisibilityButton_OnClick(self)
	self:GetParent():SetFocusedFrameShown(self:GetChecked());
end

function TableAttributeDisplayDynamicUpdateButton_OnClick(self)
	self:GetParent():SetDynamicUpdates(self:GetChecked());
end

function TableAttributeDisplayHighlightButton_OnClick(self)
	self:GetParent():UpdateFocusedHighlight();
end

function TableInspectorFilterBox_OnEnterPressed(self)
	self:ClearFocus();
end

function TableInspectorFilterBox_OnTextChanged(self)
	self:GetParent():UpdateLines();
end

function DisplayTableInspectorWindow(focusedTable, customTitle, tableFocusedCallback)
	local attributeDisplay = tableInspectorPool:Acquire();
	attributeDisplay:OnLoad();
	attributeDisplay:SetTableFocusedCallback(tableFocusedCallback);
	attributeDisplay:SetPoint("LEFT", 64 + math.random(0, 64), math.random(0, 64));
	attributeDisplay:InspectTable(focusedTable, customTitle);
	attributeDisplay:Show();
	return attributeDisplay;
end

-- The 1.12 stand-in for retail's ScrollFrameTemplate: a ScrollFrame with a
-- Slider at self.ScrollBar, both unnamed so that every inspector window gets
-- its own. The bar hides while there is nothing to scroll, as retail's
-- scrollBarHideIfUnscrollable does.
TableInspectorScrollFrameMixin = {};

function TableInspectorScrollFrameMixin:OnLoad()
	self.ScrollBar:SetMinMaxValues(0, 0);
	self.ScrollBar:SetValue(0);
	self.ScrollBar:Hide();
end

function TableInspectorScrollFrameMixin:OnScrollRangeChanged(xRange, yRange)
	yRange = yRange or self:GetVerticalScrollRange();
	self.ScrollBar:SetMinMaxValues(0, yRange);
	self.ScrollBar:SetShown(yRange > 0);
	if self:GetVerticalScroll() > yRange then
		self:SetVerticalScroll(yRange);
	end
end

function TableInspectorScrollFrameMixin:OnVerticalScroll(offset)
	self.ScrollBar:SetValue(offset);
end

function TableInspectorScrollFrameMixin:OnMouseWheel(delta)
	local offset = self:GetVerticalScroll() - delta * TABLE_INSPECTOR_LINE_HEIGHT * 3;
	self:SetVerticalScroll(Clamp(offset, 0, self:GetVerticalScrollRange()));
end

function TableInspectorScrollBar_OnValueChanged(self, value)
	self:GetParent():SetVerticalScroll(value);
end
