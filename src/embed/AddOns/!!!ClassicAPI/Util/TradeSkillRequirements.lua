-- Live recipe requirements, retail's Blizzard_Professions behaviour on the
-- TradeSkill and Craft windows. GetTradeSkillTools / GetCraftSpellFocus
-- return (name, met) pairs whose spell-focus flag the DLL makes real
-- (src/tradeskill/Tools.cpp): nil while no matching focus is in range.
--   - ProfessionsRecipeSchematicForm re-reads the requirements every 0.75s
--     while shown (FrameUtil.RegisterUpdateFunction), so "Requires: Cooking
--     Fire" turns red and back as the player walks away from and back to
--     the fire. Here the window redraws its selection when a flag changed.
--   - ProfessionsCraftingPage:ValidateCraftRequirements blocks Create while
--     any requirement is not met.

local REQUIREMENT_UPDATE_SECONDS = 0.75;

-- The met flags of a GetTradeSkillTools-shaped return, as "1"/"0" per pair.
local function RequirementFlags(...)
	local flags = "";
	for i = 2, select("#", ...), 2 do
		flags = flags .. (select(i, ...) and "1" or "0");
	end
	return flags;
end

local function RegisterRequirementRefresh(window)
	local shown;

	-- The flags the window last drew, and Create held off while one is unmet.
	hooksecurefunc(window.setSelection, function(id)
		shown = RequirementFlags(window.getTools(id));
		if string.find(shown, "0", 1, true) then
			for i = 1, table.getn(window.createButtons) do
				_G[window.createButtons[i]]:Disable();
			end
		end
	end);

	local updater = CreateFrame("Frame", nil, _G[window.frame]);
	FrameUtil.RegisterUpdateFunction(updater, REQUIREMENT_UPDATE_SECONDS, function()
		local id = window.getSelection();
		if id < 1 or id > window.getNum() or window.isHeader(id) then
			return;
		end
		if RequirementFlags(window.getTools(id)) ~= shown then
			_G[window.setSelection](id);
		end
	end);
end

EventUtil.ContinueOnAddOnLoaded("Blizzard_TradeSkillUI", function()
	RegisterRequirementRefresh({
		frame = "TradeSkillFrame",
		setSelection = "TradeSkillFrame_SetSelection",
		createButtons = { "TradeSkillCreateButton", "TradeSkillCreateAllButton" },
		getTools = GetTradeSkillTools,
		getSelection = GetTradeSkillSelectionIndex,
		getNum = GetNumTradeSkills,
		isHeader = function(id) return select(2, GetTradeSkillInfo(id)) == "header" end,
	});
end)

EventUtil.ContinueOnAddOnLoaded("Blizzard_CraftUI", function()
	RegisterRequirementRefresh({
		frame = "CraftFrame",
		setSelection = "CraftFrame_SetSelection",
		createButtons = { "CraftCreateButton" },
		getTools = GetCraftSpellFocus,
		getSelection = GetCraftSelectionIndex,
		getNum = GetNumCrafts,
		isHeader = function(id) return select(3, GetCraftInfo(id)) == "header" end,
	});
end)
