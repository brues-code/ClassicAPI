-- Trade-skill *list* links (`|Htrade:...|h`) — backport of the 2.x+ shareable
-- profession link. The link is built entirely in the DLL
-- (`GetTradeSkillListLink` / `GetCraftListLink`, see src/tradeskill/Link.cpp);
-- this file only intercepts clicks on received links and renders the linker's
-- recipe list in a scroll frame (the UI half).
--
--   link:  |cffffd000|Htrade:<skillLineID>:<curRank>:<maxRank>:<linkerName>:<base64 bits>|h[Name]|h|r
--
-- "Which recipes are known" is a bitfield over the skill line's canonical
-- recipe list (deterministic per DBC). The viewer lists the recipes the linker
-- knows (with an "X of Y" summary of the whole skill line), regardless of what
-- the reader can craft themselves.

-- Frame geometry mirrors the real TradeSkillFrame. Turtle ships an HD
-- 768x512 two-column frame (TW-TradeSkill art); stock 1.12.1 only has the
-- vanilla 384x512 single-column art, so on that client we fall back to the
-- native Blizzard_TradeSkillUI geometry (list on top, detail below) rather
-- than stretch the wide art. LO holds every layout value that differs.
local TURTLE     = (TURTLE_WOW_VERSION ~= nil);
local FRAME_W    = TURTLE and 768 or 384;
local FRAME_H    = 512;
local NUM_ROWS   = TURTLE and 19 or 8;
local ROW_HEIGHT = 16;

local LO = TURTLE and {
	hit     = { 2, 82, 2, 73 },
	quads   = {
		{ "Interface\\TradeSkillFrame\\TW-TradeSkill-TopLeft",   512, 256, "TOPLEFT" },
		{ "Interface\\TradeSkillFrame\\TW-TradeSkill-TopRight",  256, 256, "TOPRIGHT" },
		{ "Interface\\TradeSkillFrame\\TW-TradeSkill-BotLeft2",  512, 256, "BOTTOMLEFT" },
		{ "Interface\\TradeSkillFrame\\TW-TradeSkill-BotRight2", 256, 256, "BOTTOMRIGHT" },
	},
	close   = { -77, -8 },
	bar     = { 596, 15, 76, -44 },
	barBdr  = { 604, 22 },
	summary = { "TOPRIGHT", -110, -70 },
	drop    = { 40, -60 },
	scroll  = { 296, 330, 37, -98 },
	-- detail scroll frame: { x, y, width, height }. Tall enough that the
	-- content fits (scrollbar auto-hides) — Turtle's right column is roomy.
	detail  = { 371, -110, 340, 290 },
	exit    = { "CENTER", "BOTTOMRIGHT", -128, 92 },
} or {
	hit     = { 0, 34, 0, 75 },
	quads   = {
		{ "Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopLeft",  256, 256, "TOPLEFT" },
		{ "Interface\\ClassTrainerFrame\\UI-ClassTrainer-TopRight", 128, 256, "TOPRIGHT" },
		{ "Interface\\TradeSkillFrame\\UI-TradeSkill-BotLeft",      256, 256, "BOTTOMLEFT" },
		{ "Interface\\ClassTrainerFrame\\UI-ClassTrainer-BotRight", 128, 256, "BOTTOMRIGHT" },
	},
	close   = { -29, -8 },
	bar     = { 268, 15, 73, -37 },
	barBdr  = { 278, 22 },
	summary = { "TOPRIGHT", -40, -58 },
	drop    = { 12, -62 },
	scroll  = { 296, 130, 21, -96 },
	-- detail scroll frame { x, y, width, height } — native TradeSkillFrame's
	-- TradeSkillDetailScrollFrame values. Small enough that many-reagent
	-- recipes scroll (clipping partial rows), like native.
	detail  = { 20, -234, 297, 176 },
	exit    = { "CENTER", "TOPLEFT", 305, -422 },
	sep     = { 15, -221, 331 },
};

-- Reagents render in a 2-column grid inside the detail scroll child (see
-- the detail pane below). The detail is a real ScrollFrame like the
-- native TradeSkillFrame, so the whole pane pixel-scrolls and clips
-- partial reagent rows — `REAGENT_ROW_HEIGHT` is just the vertical stride
-- of one reagent row.
local REAGENT_ROW_HEIGHT = 43;

-- Scroll-frame templates. Stock mimics the native TradeSkillFrame, whose
-- list/detail scrolls use the ClassTrainer templates (they add the
-- decorative UI-ClassTrainer-ScrollBar track). Turtle's HD frame art
-- doesn't want that groove, so it keeps the plain templates. Both detail
-- templates are real ScrollFrames (ClassTrainerDetail... inherits
-- UIPanelScrollFrameTemplate), so the pixel-scroll logic is identical.
local LIST_SCROLL_TEMPLATE   = TURTLE and "FauxScrollFrameTemplate"
	or "ClassTrainerListScrollFrameTemplate";
local DETAIL_SCROLL_TEMPLATE = TURTLE and "UIPanelScrollFrameTemplate"
	or "ClassTrainerDetailScrollFrameTemplate";

local PROFESSION_ICONS = {
	[171] = "Trade_Alchemy",
	[164] = "Trade_BlackSmithing",
	[185] = "INV_Misc_Food_15",         -- Cooking
	[333] = "Trade_Engraving",          -- Enchanting
	[202] = "Trade_Engineering",
	[129] = "Spell_Holy_SealOfSacrifice", -- First Aid
	[165] = "INV_Misc_ArmorKit_17",     -- Leatherworking
	[197] = "Trade_Tailoring",
	[186] = "Trade_Mining",
	[182] = "Trade_Herbalism",
	[393] = "INV_Misc_Pelt_Wolf_01",    -- Skinning
	[356] = "Trade_Fishing",
};

if TURTLE then
	PROFESSION_ICONS[142] = "Trade_Survival"
	PROFESSION_ICONS[755] = "INV_Misc_Gem_01" -- Jewelcrafting
end

local function ProfessionIcon(skillLineID)
	local icon = PROFESSION_ICONS[skillLineID];
	return icon and ("Interface\\Icons\\" .. icon)
		or "Interface\\Icons\\INV_Misc_QuestionMark";
end

local function DifficultyBand(skill, hi, lo)
	if not hi or hi <= 0 then
		return 3;
	end
	if not lo or lo == 0 then
		lo = (hi ~= 25) and (hi - 25) or 0;
	end
	local yellow = (hi + lo) / 2;
	if skill < lo then
		return 0;   -- orange (optimal)
	elseif skill < yellow then
		return 1;   -- yellow (medium)
	elseif skill < hi then
		return 2;   -- green (easy)
	end
	return 3;       -- grey (trivial)
end

local BAND_COLOR = {
	[0] = DIFFICULT_DIFFICULTY_COLOR,  -- orange
	[1] = FAIR_DIFFICULTY_COLOR,       -- yellow
	[2] = EASY_DIFFICULTY_COLOR,       -- green
	[3] = TRIVIAL_DIFFICULTY_COLOR,    -- grey
};

local function CraftableCount(entry)
	local reagents = entry.reagents;
	if not reagents or table.getn(reagents) == 0 then
		return nil;
	end
	local best;
	for i = 1, table.getn(reagents) do
		local r = reagents[i];
		local need = r.count or 1;
		local n = math.floor((C_Item.GetItemCount(r.itemID) or 0) / need);
		if not best or n < best then
			best = n;
		end
	end
	return best;
end

local function ParseTradeLink(link)
	-- Current format, with the linker's name: trade:id:cur:max:Name:bits
	local _, _, id, cur, max, who, bits =
		string.find(link, "^trade:(%d+):(%d+):(%d+):([^:]+):([%w%+/]*)$");
	if not id then
		-- Legacy format (no name): trade:id:cur:max:bits
		who = nil;
		_, _, id, cur, max, bits =
			string.find(link, "^trade:(%d+):(%d+):(%d+):([%w%+/]*)$");
	end
	if not id then
		return nil;
	end
	return tonumber(id), tonumber(cur), tonumber(max), who, bits or "";
end

------------------------------------------------------------------------------
-- Display frame (built lazily on first use)
------------------------------------------------------------------------------

local frame;
local function ResolveSubclasses(list, onLoaded)
	local present, subs, anyItem = {}, {}, false;
	for i = 1, table.getn(list) do
		local r = list[i];
		if r.createdItem and r.createdItem > 0 then
			anyItem = true;
			if not r.subclass then
				-- GetItemInfoInstant's 3rd return is the localized subclass
				-- name (or nil when uncached).
				local _, _, subType = C_Item.GetItemInfoInstant(r.createdItem);
				if subType and subType ~= "" then
					r.subclass = subType;
				elseif onLoaded then
					-- Not cached — re-derive when the item loads. The callback
					-- also runs when the load fails; re-deriving then would ask
					-- for the item again.
					Item:CreateFromItemID(r.createdItem):ContinueOnItemLoad(function(_, success)
						if success then
							onLoaded();
						end
					end);
				end
			end
			if r.subclass and not present[r.subclass] then
				present[r.subclass] = true;
				table.insert(subs, r.subclass);
			end
		end
	end
	table.sort(subs);
	return subs, anyItem;
end

local SUBCLASS_ALL = {};

local function SubClassButton_OnClick()
	local v = this.value;
	if v == SUBCLASS_ALL then
		v = nil;
	end
	frame.subclassFilter = v;
	UIDropDownMenu_SetText(v or ALL_SUBCLASSES, frame.dropdown);
	frame:ApplyFilter();
end

local function SubClassDropDown_Initialize()
	local info = {};
	info.text = ALL_SUBCLASSES;
	info.value = SUBCLASS_ALL;
	info.checked = (frame.subclassFilter == nil);
	info.func = SubClassButton_OnClick;
	UIDropDownMenu_AddButton(info);
	local subs = frame.subclasses or {};
	for i = 1, table.getn(subs) do
		local b = {};
		b.text = subs[i];
		b.value = subs[i];
		b.checked = (frame.subclassFilter == subs[i]);
		b.func = SubClassButton_OnClick;
		UIDropDownMenu_AddButton(b);
	end
end

local function RefreshSubclasses()
	if not (frame and frame:IsShown() and frame.recipes) then
		return;
	end
	local subs, anyItem = ResolveSubclasses(frame.recipes, RefreshSubclasses);
	frame.subclasses = subs;
	if anyItem then
		UIDropDownMenu_Initialize(frame.dropdown, SubClassDropDown_Initialize);
		UIDropDownMenu_SetWidth(120, frame.dropdown);
		UIDropDownMenu_SetText(frame.subclassFilter or ALL_SUBCLASSES, frame.dropdown);
		frame.dropdown:Show();
	else
		frame.dropdown:Hide();
	end
	if frame.subclassFilter then
		frame:ApplyFilter();
	end
end

local function CreateFrame_TradeSkillLink()
	local f = CreateFrame("Frame", "ClassicAPITradeSkillLinkFrame", UIParent);
	f:SetSize(FRAME_W, FRAME_H);
	f:SetPoint("CENTER");
	f:SetMovable(true);
	f:EnableMouse(true);
	f:SetHitRectInsets(LO.hit[1], LO.hit[2], LO.hit[3], LO.hit[4]);
	f:RegisterForDrag("LeftButton");
	f:SetScript("OnDragStart", function() this:StartMoving(); end);
	f:SetScript("OnDragStop", function() this:StopMovingOrSizing(); end);
	f:SetFrameStrata("DIALOG");
	f:Hide();
	tinsert(UISpecialFrames, f:GetName())

	f:SetScript("OnShow", function() PlaySound("igCharacterInfoOpen"); end);
	f:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); end);

	local portrait = f:CreateTexture(nil, "BACKGROUND");
	portrait:SetSize(60, 60);
	portrait:SetPoint("TOPLEFT", 7, -6);
	f.portrait = portrait;

	for i = 1, table.getn(LO.quads) do
		local q = LO.quads[i];
		local tx = f:CreateTexture(nil, "BORDER");
		tx:SetTexture(q[1]);
		tx:SetSize(q[2], q[3]);
		tx:SetPoint(q[4]);
	end

	-- Horizontal divider between the recipe list and the detail pane.
	-- Stock only (Turtle's HD frame art already has its own).
	if LO.sep then
		-- Decoded the BLP: it's a thin divider rod whose graphic lives in the
		-- top quarter of the 256x64 texture (a 2nd rod fills the next quarter,
		-- bottom half empty). So sample the top quarter, stretched to width —
		-- the same region the native TradeSkillFrame uses for this divider.
		local bar = f:CreateTexture(nil, "ARTWORK");
		bar:SetTexture("Interface\\ClassTrainerFrame\\UI-ClassTrainer-HorizontalBar");
		bar:SetTexCoord(0, 1, 0, 0.25);
		bar:SetSize(LO.sep[3], 16);
		bar:SetPoint("TOPLEFT", LO.sep[1], LO.sep[2]);
	end

	local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormal");
	title:SetPoint("TOP", 0, -18);
	f.title = title;

	local close = CreateFrame("Button", nil, f, "UIPanelCloseButton");
	close:SetPoint("TOPRIGHT", LO.close[1], LO.close[2]);

	local exit = CreateFrame("Button", nil, f, "UIPanelButtonTemplate");
	exit:SetSize(80, 22);
	exit:SetPoint(LO.exit[1], f, LO.exit[2], LO.exit[3], LO.exit[4]);
	exit:SetText(EXIT);
	exit:SetScript("OnClick", function() f:Hide(); end);

	local bar = CreateFrame("StatusBar", nil, f);
	bar:SetSize(LO.bar[1], LO.bar[2]);
	bar:SetPoint("TOPLEFT", LO.bar[3], LO.bar[4]);
	bar:SetStatusBarTexture("Interface\\PaperDollInfoFrame\\UI-Character-Skills-Bar");
	bar:SetStatusBarColor(0.0, 0.0, 1.0, 0.5);
	bar:SetMinMaxValues(0, 1);
	local barBg = bar:CreateTexture(nil, "BACKGROUND");
	barBg:SetAllPoints(bar);
	barBg:SetTexture(1, 1, 1, 0.2);
	barBg:SetVertexColor(0.0, 0.0, 0.75, 0.5);
	-- Border: tooltip edge around the bar (TradeSkillRankFrameBorder).
	local border = CreateFrame("Frame", nil, bar);
	border:SetSize(LO.barBdr[1], LO.barBdr[2]);
	border:SetPoint("LEFT", -5, 1);
	border:SetBackdrop({
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 16,
	});
	border:SetBackdropBorderColor(0.4, 0.4, 0.4);
	local barName = bar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall");
	barName:SetPoint("LEFT", 6, 1);
	local barRank = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall");
	barRank:SetJustifyH("LEFT");
	barRank:SetWidth(128);
	barRank:SetPoint("LEFT", barName, "RIGHT", 13, 0);
	f.bar = bar;
	f.barName = barName;
	f.barRank = barRank;

	local summary = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall");
	summary:SetPoint(LO.summary[1], LO.summary[2], LO.summary[3]);
	f.summary = summary;

	local dropdown = CreateFrame("Frame", "ClassicAPITradeSkillLinkSubClass", f,
		"UIDropDownMenuTemplate");
	dropdown:SetPoint("TOPLEFT", LO.drop[1], LO.drop[2]);
	f.dropdown = dropdown;
	f.subclassFilter = nil; -- nil = all subclasses

	local scroll = CreateFrame("ScrollFrame", "ClassicAPITradeSkillLinkScroll",
		f, LIST_SCROLL_TEMPLATE);
	scroll:SetSize(LO.scroll[1], LO.scroll[2]);
	scroll:SetPoint("TOPLEFT", LO.scroll[3], LO.scroll[4]);
	scroll:SetScript("OnVerticalScroll", function()
		FauxScrollFrame_OnVerticalScroll(ROW_HEIGHT, function() f:Refresh(); end)
	end)
	f.scroll = scroll;

	f.rows = {};
	for i = 1, NUM_ROWS do
		local rowName = "ClassicAPITradeSkillLinkRow" .. i;
		local row = CreateFrame("Button", rowName, f,
			"ClassTrainerSkillButtonTemplate");
		row:SetWidth(290); -- height (16) comes from the template
		if i == 1 then
			row:SetPoint("TOPLEFT", scroll, "TOPLEFT", 0, 0);
		else
			row:SetPoint("TOPLEFT", f.rows[i - 1], "BOTTOMLEFT", 0, 0);
		end

		row:SetNormalTexture("");
		row:SetDisabledTexture("");
		row:SetHighlightTexture("");

		local txt = getglobal(rowName .. "Text");
		if txt then
			txt:ClearAllPoints();
			txt:SetPoint("LEFT", row, "LEFT", 4, 0);
		end

		local sel = row:CreateTexture(nil, "BACKGROUND");
		sel:SetTexture("Interface\\Buttons\\UI-Listbox-Highlight2");
		sel:SetAllPoints(row);
		sel:Hide();
		row.select = sel;

		-- Override the template's trainer scripts with ours.
		row:SetScript("OnClick", function()
			if this.entry then
				f:SelectRecipe(this.entry);
			end
		end);

		f.rows[i] = row;
	end

	-- Detail pane: a real ScrollFrame (like the native TradeSkillFrame's
	-- TradeSkillDetailScrollFrame) whose scroll child holds the product
	-- header + reagent grid, so the whole pane pixel-scrolls — clipping
	-- partial reagent rows — with the standard UIPanelScrollBar. The bar
	-- auto-hides when the content fits (scrollBarHideable).
	local detailScroll = CreateFrame("ScrollFrame",
		"ClassicAPITradeSkillLinkDetailScroll", f, DETAIL_SCROLL_TEMPLATE);
	detailScroll:SetSize(LO.detail[3], LO.detail[4]);
	detailScroll:SetPoint("TOPLEFT", LO.detail[1], LO.detail[2]);
	-- Bar visibility + wheel are driven per-render by SetDetailBarShown (from
	-- the computed overflow). `scrollBarHideable` still lets the template's
	-- ScrollFrame_OnScrollRangeChanged manage the slider on range changes.
	-- Start with the wheel off; the first RenderDetail/ClearDetail sets it.
	detailScroll.scrollBarHideable = 1;
	detailScroll:EnableMouseWheel(false);
	f.detailScroll = detailScroll;

	local detailChild = CreateFrame("Frame",
		"ClassicAPITradeSkillLinkDetailChild", detailScroll);
	detailChild:SetSize(LO.detail[3], LO.detail[4]);
	detailScroll:SetScrollChild(detailChild);
	f.detailChild = detailChild;

	local prodIcon = CreateFrame("Button", nil, detailChild);
	prodIcon:SetSize(37, 37);  -- native TradeSkillSkillIcon (37x37 @ 8,-3)
	prodIcon:SetPoint("TOPLEFT", 8, -3);
	prodIcon:SetNormalTexture("Interface\\Icons\\INV_Misc_QuestionMark");
	local hdrLeft = detailChild:CreateTexture(nil, "BACKGROUND");
	hdrLeft:SetTexture("Interface\\ClassTrainerFrame\\UI-ClassTrainer-DetailHeaderLeft");
	hdrLeft:SetSize(256, 64); -- Blizzard's native fill width (+64 cap = 320 total)
	hdrLeft:SetPoint("TOPLEFT", prodIcon, "TOPLEFT", -8, 6);
	local hdrRight = detailChild:CreateTexture(nil, "BACKGROUND");
	hdrRight:SetTexture("Interface\\ClassTrainerFrame\\UI-ClassTrainer-DetailHeaderRight");
	hdrRight:SetSize(64, 64);
	hdrRight:SetPoint("TOPLEFT", hdrLeft, "TOPRIGHT", 0, 0);
	local prodCount = prodIcon:CreateFontString(nil, "ARTWORK", "NumberFontNormal");
	prodCount:SetPoint("BOTTOMRIGHT", -5, 2);
	prodIcon.count = prodCount;
	f.prodIcon = prodIcon;

	local prodName = detailChild:CreateFontString(nil, "ARTWORK", "GameFontNormal");
	prodName:SetPoint("LEFT", prodIcon, "RIGHT", 5, 0);
	prodName:SetWidth(LO.detail[3] - 60);
	prodName:SetJustifyH("LEFT");
	prodName:SetJustifyV("MIDDLE");
	f.prodName = prodName;

	local prodDesc = detailChild:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall");
	prodDesc:SetPoint("TOPLEFT", prodIcon, "BOTTOMLEFT", 0, -6);
	prodDesc:SetWidth(LO.detail[3] - 20);
	prodDesc:SetJustifyH("LEFT");
	prodDesc:SetJustifyV("TOP");
	f.prodDesc = prodDesc;

	local reagentLabel = detailChild:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall");
	reagentLabel:SetPoint("TOPLEFT", prodDesc, "BOTTOMLEFT", 0, -8);
	reagentLabel:SetText(SPELL_REAGENTS or "Reagents:");
	f.reagentLabel = reagentLabel;

	prodIcon:SetScript("OnEnter", function()
		GameTooltip:SetOwner(this, "ANCHOR_RIGHT");
		if this.itemID then
			GameTooltip:SetHyperlink("item:"..this.itemID);
		elseif this.spellID then
			GameTooltip:SetSpellByID(this.spellID);
		end
		GameTooltip:Show();
	end);
	prodIcon:SetScript("OnLeave", GameTooltip_Hide);

	-- Reagent buttons live in the scroll child (2 per row), built on
	-- demand so any reagent count is handled and they scroll/clip with
	-- the child.
	f.reagents = {};
	local function AcquireReagent(i)
		if f.reagents[i] then
			return f.reagents[i];
		end
		local rowN = math.floor((i - 1) / 2);
		local col = (i - 1) - rowN * 2;
		local btn = CreateFrame("Button", nil, detailChild);
		btn:SetSize(39, 39);
		btn:SetPoint("TOPLEFT", reagentLabel, "BOTTOMLEFT",
			col * 134 - 5, -3 - rowN * REAGENT_ROW_HEIGHT);
		-- Extend the hit area rightward over the name plate so the tooltip
		-- fires anywhere on the element, not just the icon. Stop short of the
		-- next column (pitch 134 − icon 39) so hovers don't bleed across.
		btn:SetHitRectInsets(0, -(134 - 39), 0, 0);
		btn:SetNormalTexture("Interface\\Icons\\INV_Misc_QuestionMark");
		local plate = btn:CreateTexture(nil, "BACKGROUND");
		plate:SetTexture("Interface\\QuestFrame\\UI-QuestItemNameFrame");
		plate:SetSize(114, 64);
		plate:SetPoint("LEFT", btn, "RIGHT", -10, 0);
		local cnt = btn:CreateFontString(nil, "ARTWORK", "NumberFontNormal");
		cnt:SetPoint("BOTTOMRIGHT", -4, 1);
		btn.count = cnt;
		local nm = btn:CreateFontString(nil, "ARTWORK", "GameFontHighlight");
		nm:SetPoint("LEFT", plate, "LEFT", 15, 0);
		nm:SetSize(80, 36);
		nm:SetJustifyH("LEFT");
		nm:SetJustifyV("MIDDLE");
		btn.nameText = nm;
		btn:SetScript("OnEnter", function()
			if this.itemID then
				GameTooltip:SetOwner(this, "ANCHOR_RIGHT");
				GameTooltip:SetHyperlink("item:"..this.itemID);
				GameTooltip:Show();
			end
		end);
		btn:SetScript("OnClick", function()
			if this.itemLink and IsShiftKeyDown() and ChatFrameEditBox:IsVisible() then
				ChatFrameEditBox:Insert(this.itemLink);
			end
		end);
		btn:SetScript("OnLeave", GameTooltip_Hide);
		f.reagents[i] = btn;
		return btn;
	end

	-- Show the detail bar/borders only when the content overflows the pane.
	-- Driven by our own computed overflow, NOT the engine: scrollBarHideable's
	-- auto-hide only fires on a range CHANGE (so content that fits from the
	-- start never hides), and GetVerticalScrollRange isn't reliably readable
	-- the same tick as UpdateScrollChildRect.
	function f:SetDetailBarShown(show)
		self.detailScroll:EnableMouseWheel(show);
		local base = "ClassicAPITradeSkillLinkDetailScroll";
		local sb = getglobal(base .. "ScrollBar");
		if sb then sb:SetShown(show) end
		local top = getglobal(base .. "Top");
		if top then top:SetShown(show) end
		local bottom = getglobal(base .. "Bottom");
		if bottom then bottom:SetShown(show) end
	end

	function f:RenderDetail(entry)
		local function itemInfo(id)
			local item = Item:CreateFromItemID(id);
			if not item:IsItemDataCached() then
				item:ContinueOnItemLoad(function(_, success)
					if success and self:IsShown() and self.selectedSpellID == entry.spellID then
						self:RenderDetail(entry);
					end
				end);
			end
			return item:GetItemName(), item:GetItemIcon(), item:GetItemLink();
		end

		local prodTex, prodText, prodLink;
		local created = entry.createdItem or 0;
		if created > 0 then
			local name, tex, link = itemInfo(created);
			prodText = name or entry.name;
			prodTex = tex;
			prodLink = link;
		else
			prodText = entry.name;
			prodTex = C_Spell.GetSpellTexture(entry.spellID);
		end
		local descText = (created > 0) and ""
			or (C_Spell.GetSpellDescription(entry.spellID) or "");
		self.prodDesc:SetText(descText);
		self.prodIcon:SetNormalTexture(prodTex or
			"Interface\\Icons\\INV_Misc_QuestionMark");
		self.prodName:SetText(prodText or "");
		self.prodName:SetTextColor(NORMAL_FONT_COLOR:GetRGB());
		self.prodIcon.itemLink = prodLink;      -- item tooltip when created > 0
		self.prodIcon.spellID = entry.spellID;  -- else spell tooltip
		local made = entry.numMade or 0;
		self.prodIcon.count:SetText(made > 1 and tostring(made) or "");
		self.prodIcon:Show();

		-- Fill every reagent slot; the scroll child clips/scrolls the
		-- overflow. Buttons past the count are hidden.
		local reagents = entry.reagents or {};
		local total = table.getn(reagents);
		for i = 1, total do
			local btn = AcquireReagent(i);
			local reagent = reagents[i];
			local name, tex, link = itemInfo(reagent.itemID);
			btn:SetNormalTexture(tex or "Interface\\Icons\\INV_Misc_QuestionMark");
			btn.itemLink = link;
			btn.itemID = reagent.itemID
			btn.nameText:SetText(name or ("item:" .. reagent.itemID));
			local need = reagent.count or 1;
			local have = C_Item.GetItemCount(reagent.itemID) or 0;
			if have >= need then
				local r, g, b = HIGHLIGHT_FONT_COLOR:GetRGB()
				btn:GetNormalTexture():SetVertexColor(r, g, b);
				btn.nameText:SetTextColor(r, g, b);
			else
				local r, g, b = GRAY_FONT_COLOR:GetRGB()
				btn:GetNormalTexture():SetVertexColor(r, g, b);
				btn.nameText:SetTextColor(r, g, b);
			end
			local haveText = (have >= 100) and "*" or tostring(have);
			btn.count:SetText(haveText .. " /" .. need);
			btn:Show();
		end
		for i = total + 1, table.getn(self.reagents) do
			self.reagents[i].itemLink = nil;
			self.reagents[i].itemID = nil;
			self.reagents[i]:Hide();
		end

		local function applyContentHeight()
			local contentH = LO.detail[4];
			if total > 0 then
				local childTop = self.detailChild:GetTop();
				local lastBtn = self.reagents[total];
				local lastBottom = lastBtn and lastBtn:GetBottom();
				if childTop and lastBottom then
					-- +22: ~12px name-plate overhang below the icon + ~10px pad.
					contentH = (childTop - lastBottom) + 22;
				end
			end
			self.detailChild:SetHeight(math.max(contentH, LO.detail[4]));
			-- Recompute the scroll range from the child's new height, then
			-- show the bar only if the content actually overflows the pane.
			self.detailScroll:UpdateScrollChildRect();
			self:SetDetailBarShown(contentH > LO.detail[4] + 0.5);
		end
		applyContentHeight();
		local sb = getglobal("ClassicAPITradeSkillLinkDetailScrollScrollBar");
		if sb then
			sb:SetValue(0);
		end
		self.detailScroll:SetVerticalScroll(0);
		RunNextFrame(function()
			if self:IsShown() and self.selectedSpellID == entry.spellID then
				applyContentHeight();
			end
		end)
	end

	function f:SelectRecipe(entry)
		self.selectedSpellID = entry.spellID;
		self.selectedEntry = entry;
		self:RenderDetail(entry);   -- also resets the detail scroll to top
		self:Refresh();
	end

	function f:ClearDetail()
		self.selectedSpellID = nil;
		self.selectedEntry = nil;
		self.prodIcon:Hide();
		self.prodName:SetText("");
		self.prodDesc:SetText("");
		for i = 1, table.getn(self.reagents) do
			self.reagents[i].itemLink = nil;
			self.reagents[i].itemID = nil;
			self.reagents[i]:Hide();
		end
		self.detailChild:SetHeight(LO.detail[4]);
		self.detailScroll:UpdateScrollChildRect();
		self:SetDetailBarShown(false);
		self.detailScroll:SetVerticalScroll(0);
	end

	function f:Refresh()
		local recipes = self.displayed or {};
		local total = table.getn(recipes);
		FauxScrollFrame_Update(self.scroll, total, NUM_ROWS, ROW_HEIGHT);
		local offset = FauxScrollFrame_GetOffset(self.scroll);
		for i = 1, NUM_ROWS do
			local row = self.rows[i];
			local entry = recipes[offset + i];
			if entry then
				-- Append " [N]" when the reader can craft any (Blizzard's list).
				local craftable = CraftableCount(entry);
				local label = entry.name or "";
				if craftable and craftable > 0 then
					label = label .. " [" .. craftable .. "]";
				end
				row:SetText(label);
				local r, g, b = BAND_COLOR[entry.band or 3]:GetRGB();
				if self.selectedSpellID and entry.spellID == self.selectedSpellID then
					row.select:SetVertexColor(r, g, b);
					row.select:Show();
					row:SetTextColor(1, 1, 1);
				else
					row.select:Hide();
					row:SetTextColor(r, g, b);
				end
				row.spellID = entry.spellID;
				row.entry = entry;
				row:Show();
			else
				row.spellID = nil;
				row.entry = nil;
				row.select:Hide();
				row:Hide();
			end
		end
	end

	function f:ApplyFilter()
		local src = self.recipes or {};
		if not self.subclassFilter then
			self.displayed = src;
		else
			local out = {};
			for i = 1, table.getn(src) do
				if src[i].subclass == self.subclassFilter then
					table.insert(out, src[i]);
				end
			end
			self.displayed = out;
		end
		local sb = getglobal("ClassicAPITradeSkillLinkScrollScrollBar");
		if sb then
			sb:SetValue(0);
		end
		self:Refresh();
	end

	f:RegisterEvent("BAG_UPDATE_DELAYED");
	f:SetScript("OnEvent", function()
		if not f:IsShown() then
			return;
		end
		f:Refresh();
		if f.selectedEntry then
			f:RenderDetail(f.selectedEntry);
		end
	end);

	return f;
end

local function ShowTradeSkillLink(link, text)
	local id, cur, max, who, bits = ParseTradeLink(link);
	if not id then
		return false;
	end
	local recipes = C_TradeSkillUI.GetTradeSkillListRecipes(id, bits);
	if not recipes then
		return false;
	end

	local prof = text and string.match(text, "|h%[(.+)%]|h") or "Trade Skills";
	local total = table.getn(recipes);
	local knownList = {};
	for i = 1, total do
		local r = recipes[i];
		if r.isKnown then
			r.band = DifficultyBand(cur or 0, r.trivialLevel, r.greenLevel);
			r.name = C_Spell.GetSpellName(r.spellID) or ("spell:" .. r.spellID);
			table.insert(knownList, r);
		end
	end
	table.sort(knownList, function(a, b)
		if a.band ~= b.band then
			return a.band < b.band;
		end
		return a.name < b.name;
	end);
	local known = table.getn(knownList);

	if not frame then
		frame = CreateFrame_TradeSkillLink();
	end
	frame.title:SetText(who and who ~= "" and (who .. " - " .. prof) or prof);
	SetPortraitToTexture(frame.portrait, ProfessionIcon(id));
	frame.bar:SetMinMaxValues(0, (max and max > 0) and max or 1);
	frame.bar:SetValue(cur or 0);
	frame.barName:SetText(prof);
	frame.barRank:SetFormattedText("%d/%d", cur or 0, max or 0);
	frame.summary:SetFormattedText("%d of %d recipes known", known, total);
	frame.recipes = knownList;
	frame.subclassFilter = nil;

	frame:ClearDetail();
	frame:Show();
	RefreshSubclasses();
	frame:ApplyFilter();
	local first = frame.displayed and frame.displayed[1];
	if first then
		frame:SelectRecipe(first);
	end
	return true;
end

------------------------------------------------------------------------------
-- Click interception: handle `trade:` before the engine's SetHyperlink (which
-- in 1.12 only knows item:/enchant: and would reject the link). Registered
-- on LinkUtil's SetItemRef dispatch (Util/LinkUtil.lua) instead of wrapping
-- SetItemRef a second time, so there is exactly one override to chain.
------------------------------------------------------------------------------
LinkUtil.RegisterLinkHandler("trade", function(link, text, linkData, contextData)
	if IsShiftKeyDown() and ChatFrameEditBox and ChatFrameEditBox:IsVisible() then
		ChatFrameEditBox:Insert(text);
		return LinkProcessorResponse.Handled;
	end
	if ShowTradeSkillLink(link, text) then
		return LinkProcessorResponse.Handled;
	end
	return LinkProcessorResponse.Unhandled;
end);

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
end

EventUtil.ContinueOnAddOnLoaded("Blizzard_TradeSkillUI", function()
	CreateProfessionLinkButton("TradeSkillFrame", C_TradeSkillUI.GetTradeSkillListLink);
end)

EventUtil.ContinueOnAddOnLoaded("Blizzard_CraftUI", function()
	CreateProfessionLinkButton("CraftFrame", C_TradeSkillUI.GetCraftListLink)
end)

EventUtil.ContinueOnAddOnLoaded("AdvancedTradeSkillWindow", function()
	CreateProfessionLinkButton("ATSWFrame", function()
		return atsw_oldmode and C_TradeSkillUI.GetCraftListLink() or C_TradeSkillUI.GetTradeSkillListLink()
	end)
end)

EventUtil.ContinueOnAddOnLoaded("AdvancedTradeSkillWindow2", function()
	CreateProfessionLinkButton("ATSWFrame", function()
		return ATSW_TradeSkill() and C_TradeSkillUI.GetTradeSkillListLink() or C_TradeSkillUI.GetCraftListLink()
	end)
end)
