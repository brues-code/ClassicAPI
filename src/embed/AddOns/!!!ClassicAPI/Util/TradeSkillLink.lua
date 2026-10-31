-- Profession links (`|Htrade:...|h`), 3.3.5's UI half. The DLL builds the
-- link (GetTradeSkillListLink / GetCraftListLink, src/tradeskill/Link.cpp)
-- and opens a received one in the profession window itself
-- (src/tradeskill/Window.cpp), through ItemRefTooltip:SetHyperlink as
-- SetItemRef already calls it. This file ports the parts of 3.3.5's
-- Blizzard_TradeSkillUI that go with that:
--   - TradeSkillLinkButton: a button by the window title that puts the
--     window's link in the chat box, shown when the window has a link and
--     shows the player's own profession;
--   - TradeSkillFrame_SetSelection's linked branch: a window that shows
--     another player's profession hides what only works on the player's own
--     (Create, Create All and the count controls).
-- The Craft window gets the same through GetCraftListLink and IsCraftLinked.

local TRADE_SKILL_CREATE_CONTROLS = {
	"TradeSkillCreateButton",
	"TradeSkillCreateAllButton",
	"TradeSkillDecrementButton",
	"TradeSkillInputBox",
	"TradeSkillIncrementButton",
};

local function CreateProfessionLinkButton(parentName, getLink)
	local parent = _G[parentName];
	local btnName = parentName .. "LinkButton";
	if _G[btnName] or not parent then
		return;
	end
	local title = _G[parentName .. "TitleText"];
	local btn = CreateFrame("Button", btnName, parent);
	btn:SetSize(16, 16);

	local function reposition()
		if not title then
			return;
		end
		btn:ClearAllPoints();
		if title:GetJustifyH() == "LEFT" then
			btn:SetPoint("LEFT", title, "LEFT", title:GetStringWidth() + 4, 0);
		else
			btn:SetPoint("LEFT", title, "CENTER", title:GetStringWidth() / 2 + 4, 0);
		end
	end
	reposition()
	btn:SetScript("OnShow", function() RunNextFrame(reposition) end)
	hooksecurefunc(title, "SetText", function() RunNextFrame(reposition) end)

	local icon = "Interface\\GossipFrame\\PetitionGossipIcon";
	btn:SetNormalTexture(icon);
	btn:SetHighlightTexture(icon, "ADD");

	btn:SetScript("OnClick", function()
		local link = getLink();
		if not link then
			return;
		end
		if not ChatFrameEditBox:IsVisible() then
			ChatFrameEditBox:Show();
		end
		ChatFrameEditBox:Insert(link);
	end);
	btn:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_TOPLEFT");
		GameTooltip:SetText(LINK_TRADESKILL_TOOLTIP, nil, nil, nil, nil, 1);
		GameTooltip:Show();
	end);
	btn:SetScript("OnLeave", GameTooltip_Hide);
	return btn;
end

EventUtil.ContinueOnAddOnLoaded("Blizzard_TradeSkillUI", function()
	local linkButton = CreateProfessionLinkButton("TradeSkillFrame", GetTradeSkillListLink);
	hooksecurefunc("TradeSkillFrame_SetSelection", function()
		local linked = IsTradeSkillLinked();
		for i = 1, table.getn(TRADE_SKILL_CREATE_CONTROLS) do
			_G[TRADE_SKILL_CREATE_CONTROLS[i]]:SetShown(not linked);
		end
		if linkButton then
			linkButton:SetShown(not linked and GetTradeSkillListLink() ~= nil);
		end
	end);
end)

EventUtil.ContinueOnAddOnLoaded("Blizzard_CraftUI", function()
	local linkButton = CreateProfessionLinkButton("CraftFrame", GetCraftListLink);
	hooksecurefunc("CraftFrame_SetSelection", function()
		local linked = IsCraftLinked();
		CraftCreateButton:SetShown(not linked);
		if linkButton then
			linkButton:SetShown(not linked and GetCraftListLink() ~= nil);
		end
	end);
end)

EventUtil.ContinueOnAddOnLoaded("AdvancedTradeSkillWindow", function()
	CreateProfessionLinkButton("ATSWFrame", function()
		return atsw_oldmode and GetCraftListLink() or GetTradeSkillListLink()
	end)
end)

EventUtil.ContinueOnAddOnLoaded("AdvancedTradeSkillWindow2", function()
	CreateProfessionLinkButton("ATSWFrame", function()
		return ATSW_TradeSkill() and GetTradeSkillListLink() or GetCraftListLink()
	end)
end)
