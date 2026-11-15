-- The Interface Options API of 3.3.5-era clients (InterfaceOptions_AddCategory,
-- InterfaceOptionsFrame_OpenToCategory and the InterfaceOptionsFrame names),
-- on top of the Settings window (Settings\Blizzard_SettingsPanel.lua).
--
-- Addons from both eras run here. Ace3's AceConfigDialog:AddToBlizOptions
-- calls InterfaceOptions_AddCategory whenever it exists; newer addons call
-- Settings.RegisterCanvasLayoutCategory when Settings and SettingsPanel
-- exist. Both land in the one Settings window: a panel added here is a
-- canvas category in the AddOns set.
--
-- The panel protocol of 3.3.5 maps onto the Settings window's own:
--   - panel.name (required) is the category's name and its ID, as Ace3 and
--     newer addons set it, so Settings.GetCategory(name) finds it.
--   - panel.parent names the category to nest under: a category added here
--     or through Settings, found by ID or by name. A panel whose parent is
--     not registered yet is listed at the top level.
--   - panel.okay runs when the window commits (Close and Apply), panel.default
--     when its defaults are set, panel.refresh when the window opens and when
--     the panel is shown. The panel keeps its own OnCommit, OnDefault or
--     OnRefresh if it has one.
--   - panel.cancel runs when the window closes without committing: the
--     discard dialog's Exit, or InterfaceOptionsFrameCancel. Close and Escape
--     commit, as on retail.
--   - The list sorts addon categories by name, so `position` has no effect.
--
-- The old window's names are the Settings window's frames, because addons
-- reach for them directly (Auctionator sets backdrops on InterfaceOptionsFrame,
-- InterfaceOptionsFrameAddOns and InterfaceOptionsFrameCategories and lays a
-- frame over InterfaceOptionsFrame; skins restyle all of them):
--   InterfaceOptionsFrame                     SettingsPanel
--   InterfaceOptionsFrameAddOns, ...Categories  the category list
--   InterfaceOptionsFrameAddOnsList            the list's ScrollBox
--   InterfaceOptionsFrameAddOnsListScrollBar   the list's scroll bar
--   InterfaceOptionsFramePanelContainer        the canvas a panel is shown in;
--                                              displayedPanel is that panel
--   InterfaceOptionsFrameOkay                  the Close button
--   InterfaceOptionsFrameCancel / ...Defaults  hidden buttons: closing without
--                                              commit / defaults for the shown
--                                              category
-- The list has no fixed rows: its `buttons` table is empty, and its `update`
-- (also InterfaceAddOnsList_Update) rebuilds it. INTERFACEOPTIONS_ADDONCATEGORIES
-- lists the panels added here.

local categoryList = SettingsPanel:GetCategoryList();
local canvas = SettingsPanel:GetSettingsCanvas();

INTERFACEOPTIONS_ADDONCATEGORIES = {};
local panels = INTERFACEOPTIONS_ADDONCATEGORIES;

-- ---------------------------------------------------------------------------
-- Categories
-- ---------------------------------------------------------------------------

local function FindCategoryByName(categories, name)
	for _, category in ipairs(categories) do
		if category:GetName() == name then
			return category;
		end
		local found = FindCategoryByName(category:GetSubcategories(), name);
		if found then
			return found;
		end
	end
end

-- By ID first, as Settings.GetCategory does, then by name: a category
-- registered through Settings keeps its counter ID unless the addon set it.
local function FindCategory(name)
	return Settings.GetCategory(name) or FindCategoryByName(SettingsPanel:GetAllCategories(), name);
end

local function FindCategoryForFrame(categories, frame)
	for _, category in ipairs(categories) do
		local layout = SettingsPanel:GetLayout(category);
		if layout and layout:GetLayoutType() == SettingsLayoutMixin.LayoutType.Canvas and layout:GetFrame() == frame then
			return category;
		end
		local found = FindCategoryForFrame(category:GetSubcategories(), frame);
		if found then
			return found;
		end
	end
end

-- A canvas callback that runs the panel's 3.3.5 one, looked up when it runs:
-- addons set okay / cancel / default / refresh before or after adding the
-- panel.
local function Forward(method)
	return function(panel)
		local fn = panel[method];
		if type(fn) == "function" then
			fn(panel);
		end
	end
end

local OnCommit = Forward("okay");
local OnDefault = Forward("default");
local OnRefresh = Forward("refresh");

function InterfaceOptions_AddCategory(panel, addOn, position)
	if type(panel) ~= "table" or type(panel.name) ~= "string" then
		error("Usage: InterfaceOptions_AddCategory(panel [, addOn, position])", 2);
	end
	panel:Hide();

	if panel.OnCommit == nil then panel.OnCommit = OnCommit; end
	if panel.OnDefault == nil then panel.OnDefault = OnDefault; end
	if panel.OnRefresh == nil then panel.OnRefresh = OnRefresh; end

	local parent = panel.parent and FindCategory(panel.parent);
	local category;
	if parent then
		category = Settings.RegisterCanvasLayoutSubcategory(parent, panel, panel.name);
	else
		category = Settings.RegisterCanvasLayoutCategory(panel, panel.name);
	end
	category.ID = panel.name;
	Settings.RegisterAddOnCategory(category);

	table.insert(panels, panel);
end

function InterfaceOptionsFrame_OpenToCategory(panel)
	local category;
	if type(panel) == "string" then
		category = FindCategory(panel);
	elseif type(panel) == "table" then
		category = FindCategoryForFrame(SettingsPanel:GetAllCategories(), panel);
		if not category and type(panel.name) == "string" then
			category = FindCategory(panel.name);
		end
	end
	if category then
		Settings.OpenToCategory(category:GetID());
	end
end

function InterfaceOptionsFrame_Show()
	if SettingsPanel:IsShown() then
		SettingsPanel:Close();
	else
		SettingsPanel:Open();
	end
end

function InterfaceAddOnsList_Update()
	categoryList:CreateCategories();
end

-- Closing without a commit gives each panel added here its cancel, before
-- the window hides, as the old window's Cancel did.
local ExitWithoutCommit = SettingsPanel.ExitWithoutCommit;
function SettingsPanel:ExitWithoutCommit()
	for _, panel in ipairs(panels) do
		local fn = panel.cancel;
		if type(fn) == "function" then
			local ok, err = pcall(fn, panel);
			if not ok then CallErrorHandler(err); end
		end
	end
	ExitWithoutCommit(self);
end

-- ---------------------------------------------------------------------------
-- The old window's names
-- ---------------------------------------------------------------------------

InterfaceOptionsFrame = SettingsPanel;
InterfaceOptionsFrameAddOns = categoryList;
InterfaceOptionsFrameCategories = categoryList;
InterfaceOptionsFrameAddOnsList = categoryList.ScrollBox;
InterfaceOptionsFrameAddOnsListScrollBar = categoryList.ScrollBar;
InterfaceOptionsFramePanelContainer = canvas;
InterfaceOptionsFrameOkay = SettingsPanel.CloseButton;

categoryList.buttons = {};
categoryList.buttonHeight = 20;
categoryList.update = InterfaceAddOnsList_Update;

hooksecurefunc(SettingsPanel, "DisplayLayout", function(self, layout)
	if layout and layout:GetLayoutType() == SettingsLayoutMixin.LayoutType.Canvas then
		canvas.displayedPanel = layout:GetFrame();
	else
		canvas.displayedPanel = nil;
	end
end);

local cancel = CreateFrame("Button", "InterfaceOptionsFrameCancel", SettingsPanel, "UIPanelButtonTemplate");
cancel:SetText(CANCEL);
cancel:Hide();
cancel:SetScript("OnClick", function()
	SettingsPanel:ExitWithoutCommit();
end);

local defaults = CreateFrame("Button", "InterfaceOptionsFrameDefaults", SettingsPanel, "UIPanelButtonTemplate");
defaults:SetText(SETTINGS_DEFAULTS);
defaults:Hide();
defaults:SetScript("OnClick", function()
	SettingsPanel:SetCurrentCategorySettingsToDefaults();
end);
