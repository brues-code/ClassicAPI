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
-- 3.3.5 never shows whose profession a linked window holds. 4.3.4 does, with
-- TradeSkillLinkNameButton and TradeSkillFrame_SetLinkName, ported here too.
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

local TRADE_SKILL_LINKED_NAME_WIDTH = 120;

-- 4.3.4's TradeSkillLinkNameButton: "[name]" after the title of a linked
-- window, a whisper to that player on click. The returned function is
-- TradeSkillFrame_SetLinkName. It moves the title left by half the name, from
-- the title's own anchor, so the pair stays centered where the title was.
local function CreateLinkNameButton(parentName, isLinked)
	local title = _G[parentName .. "TitleText"];
	local point, relativeTo, relativePoint, x, y = title:GetPoint();
	local btn = CreateFrame("Button", parentName .. "LinkNameButton", _G[parentName]);
	btn:SetHeight(20);
	btn:SetPoint("LEFT", title, "RIGHT", 5, 0);
	local text = btn:CreateFontString(btn:GetName() .. "TitleText", "OVERLAY", "GameFontHighlight");
	text:SetAllPoints(btn);
	btn:SetScript("OnClick", function()
		PlaySound("igMainMenuOptionCheckBoxOn");
		if this.linkedName then
			ChatFrame_OpenChat(SLASH_WHISPER1 .. " " .. this.linkedName .. " ", DEFAULT_CHAT_FRAME);
		end
	end);
	btn:Hide();

	return function()
		local _, linkedName = isLinked();
		title:ClearAllPoints();
		if linkedName then
			btn:Show();
			text:SetText("[" .. linkedName .. "]");
			local linkedNameWidth = math.min(text:GetStringWidth(), TRADE_SKILL_LINKED_NAME_WIDTH);
			btn:SetWidth(linkedNameWidth);
			btn.linkedName = linkedName;
			title:SetPoint(point, relativeTo, relativePoint, x - linkedNameWidth / 2, y);
		else
			btn:Hide();
			title:SetPoint(point, relativeTo, relativePoint, x, y);
		end
	end
end

EventUtil.ContinueOnAddOnLoaded("Blizzard_TradeSkillUI", function()
	local linkButton = CreateProfessionLinkButton("TradeSkillFrame", GetTradeSkillListLink);
	local setLinkName = CreateLinkNameButton("TradeSkillFrame", IsTradeSkillLinked);
	hooksecurefunc("TradeSkillFrame_SetSelection", function()
		local linked = IsTradeSkillLinked();
		for i = 1, table.getn(TRADE_SKILL_CREATE_CONTROLS) do
			_G[TRADE_SKILL_CREATE_CONTROLS[i]]:SetShown(not linked);
		end
		if linkButton then
			linkButton:SetShown(not linked and GetTradeSkillListLink() ~= nil);
		end
		setLinkName();
	end);
end)

EventUtil.ContinueOnAddOnLoaded("Blizzard_CraftUI", function()
	local linkButton = CreateProfessionLinkButton("CraftFrame", GetCraftListLink);
	local setLinkName = CreateLinkNameButton("CraftFrame", IsCraftLinked);
	hooksecurefunc("CraftFrame_SetSelection", function()
		local linked = IsCraftLinked();
		CraftCreateButton:SetShown(not linked);
		if linkButton then
			linkButton:SetShown(not linked and GetCraftListLink() ~= nil);
		end
		setLinkName();
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
