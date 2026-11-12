-- Broodwatch HUD: the level-up cards, the build bar, health and experience.

local _, BW = ...
local HERO_HP = BW.HERO_HP
local SPELLS = BW.SPELLS
local SPELL_ORDER = BW.SPELL_ORDER
local EVOLUTIONS = BW.EVOLUTIONS
local FUSIONS = BW.FUSIONS
local MAX_SPELLS = BW.MAX_SPELLS
local EARLY_SPELLS = BW.EARLY_SPELLS
local PASSIVES = BW.PASSIVES
local HEAL_ICON = BW.HEAL_ICON
local UNKNOWN_ICON = BW.UNKNOWN_ICON
local PASSIVE_ORDER = BW.PASSIVE_ORDER
local MAX_LEVEL = BW.MAX_LEVEL
local ROMAN = BW.ROMAN
local EASE = BW.EASE
local AT_ONCE = BW.AT_ONCE
local offers = BW.offers
local Shuffle = BW.Shuffle
local Stat = BW.Stat
local TUNING = BW.TUNING
local GEAR = BW.GEAR
local GEAR_ORDER = BW.GEAR_ORDER

------------------------------------------------------------------------------
-- Level-up offers

local function BuildOffers()
	-- The start: your class's first spell, then two of the other classes'.
	if BW.run.picking == "start" then
		local _, class = UnitClass("player");
		local own = BW.STARTERS[class];
		local others = {};
		for _, key in pairs(BW.STARTERS) do
			if key ~= own then
				tinsert(others, key);
			end
		end
		Shuffle(others);
		if own then
			tinsert(others, 1, own);
		end
		for i = 1, 3 do
			offers[i] = { kind = "spell", key = others[i], level = 1 };
		end
		return;
	end
	-- A golden chest's weapon pick: three of the weapons.
	if BW.run.picking == "chest" and BW.run.weaponPick then
		local weapons = BW.GearOffers(true);
		Shuffle(weapons);
		for i = 1, 3 do
			offers[i] = weapons[i] and { kind = "gear", key = weapons[i] } or { kind = "heal" };
		end
		return;
	end
	local all = {};
	local owned = 0;
	for _ in pairs(BW.run.spells) do
		owned = owned + 1;
	end
	for _, key in ipairs(SPELL_ORDER) do
		local state = BW.run.spells[key];
		if SPELLS[key].evolved then
			-- An evolution comes only from its spell (below), and has one rank.
		elseif state and state.level < MAX_LEVEL then
			tinsert(all, { kind = "spell", key = key, level = state.level + 1 });
		elseif not state and owned < MAX_SPELLS then
			tinsert(all, { kind = "spell", key = key, level = 1 });
		end
	end
	for _, key in ipairs(PASSIVE_ORDER) do
		local level = (BW.run.passives[key] or 0) + 1;
		if level <= (PASSIVES[key].max or MAX_LEVEL) then
			tinsert(all, { kind = "passive", key = key, level = level });
		end
	end
	Shuffle(all);
	-- An early build must not stall: the first level offers only new spells,
	-- and until you have EARLY_SPELLS spells each level offers at least one.
	local fresh = {};
	for _, o in ipairs(all) do
		if o.kind == "spell" and o.level == 1 then
			tinsert(fresh, o);
		end
	end
	if owned == 1 and table.getn(fresh) >= 3 then
		all = fresh;
	elseif owned < EARLY_SPELLS and table.getn(fresh) > 0 then
		local hasFresh = false;
		for i = 1, 3 do
			local o = all[i];
			if o and o.kind == "spell" and o.level == 1 then
				hasFresh = true;
			end
		end
		if not hasFresh then
			-- The new spell takes the third card's place.
			for i, o in ipairs(all) do
				if o == fresh[1] then
					table.remove(all, i);
					break;
				end
			end
			tinsert(all, 3, fresh[1]);
		end
	end
	-- A chest's pick may offer a piece of worn gear.
	if BW.run.picking == "chest" then
		local worn = BW.GearOffers(false);
		if worn[1] then
			tinsert(all, 1, { kind = "gear", key = worn[math.random(table.getn(worn))] });
		end
	end
	-- An evolution ready (its spell at rank V, its passive owned) always
	-- takes the first card.
	for _, key in ipairs(SPELL_ORDER) do
		local evolution = EVOLUTIONS[key];
		local state = BW.run.spells[key];
		if evolution and state and state.level >= MAX_LEVEL and BW.run.passives[evolution.needs] then
			tinsert(all, 1, { kind = "evolve", key = evolution.into, from = key, level = 1 });
			break;
		end
	end
	-- A fusion ready (both its spells at rank V) comes before that.
	for _, fusion in ipairs(FUSIONS) do
		local a, b = BW.run.spells[fusion.a], BW.run.spells[fusion.b];
		if a and b and a.level >= MAX_LEVEL and b.level >= MAX_LEVEL then
			tinsert(all, 1, { kind = "fuse", key = fusion.into, a = fusion.a, b = fusion.b, level = 1 });
			break;
		end
	end
	for i = 1, 3 do
		offers[i] = all[i] or { kind = "heal" };
	end
end

-- Card colours by kind: a new spell gold, a spell's next rank blue, a
-- passive green, the heal when nothing is left red.
local CARD_COLORS = {
	new = { 1, 0.82, 0 },
	spell = { 0.35, 0.65, 1 },
	passive = { 0.3, 0.9, 0.4 },
	heal = { 1, 0.45, 0.45 },
	evolve = { 0.85, 0.5, 1 },
	fuse = { 1, 0.55, 0.15 },
	gear = { 0.64, 0.21, 0.93 }, -- epic, until the item's own quality colour loads
}

-- A piece of gear's colour: its item quality's.
local function GearColor(gear)
	return gear.quality or CARD_COLORS.gear;
end
local STAT_LABELS = {
	damage = "Damage", count = "Targets", jumps = "Jumps", totems = "Totems", spread = "Spreads to",
	heal = "Heal per hit", radius = "Radius", slow = "Enemy speed", duration = "Lasts", cooldown = "Every",
	absorb = "Absorbs", root = "Freezes for", fear = "Fears for", weaken = "Their bites",
	hit = "Infernal's blows", burn = "Immolation per second", orbs = "Orbs", selfBurn = "Burns you for",
	length = "Reaches", ignite = "Burns per second", traps = "Traps", freeze = "Freezes for", stun = "Stuns for",
	pulseDamage = "Pulse",
}
local STAT_UNITS = {
	radius = " yd", duration = " s", cooldown = " s", root = " s", fear = " s", length = " yd", freeze = " s",
	stun = " s",
}
local STAT_ORDER = {
	"damage", "hit", "pulseDamage", "burn", "ignite", "selfBurn", "orbs", "count", "jumps", "totems", "traps", "spread", "heal",
	"absorb", "radius", "length", "slow", "root", "freeze", "stun", "fear",
	"weaken",
	"duration", "cooldown",
}

local function Num(v)
	if v == math.floor(v) then
		return string.format("%d", v);
	end
	local text = string.format("%.2f", v);
	if string.sub(text, -1) == "0" then
		text = string.sub(text, 1, -2);
	end
	return text;
end

-- A stat as the cards show it: the slow as a percentage of speed, the rest
-- with their unit.
local function StatText(field, v)
	if field == "slow" then
		return math.floor(v * 100 + 0.5) .. "%";
	elseif field == "weaken" then
		return "-" .. math.floor(v * 100 + 0.5) .. "%";
	elseif field == "absorb" then
		return Num(v) .. "% of health";
	end
	return Num(v) .. (STAT_UNITS[field] or "");
end

local function Icon(spellID)
	return C_Spell.GetSpellTexture(spellID) or UNKNOWN_ICON;
end

-- The card's lines: a new spell's blurb and stats, a rank's changes (old in
-- grey, new in green), a passive's rank and its total.
local function OfferLines(o)
	local lines = {};
	if o.kind == "spell" then
		local def = SPELLS[o.key];
		if o.level == 1 then
			tinsert(lines, def.blurb);
			tinsert(lines, "");
		end
		for _, field in ipairs(STAT_ORDER) do
			local b = Stat(def, field, o.level);
			if b then
				if o.level == 1 then
					tinsert(lines, STAT_LABELS[field] .. " " .. StatText(field, b));
				else
					local a = Stat(def, field, o.level - 1);
					if a ~= b then
						tinsert(lines, string.format("%s  |cff888888%s|r  >  |cff40ff40%s|r",
							STAT_LABELS[field], StatText(field, a), StatText(field, b)));
					end
				end
			end
		end
		-- The mastery: a teaser on a new spell's card, the prize at rank V.
		if def.mastery then
			if o.level == MAX_LEVEL then
				tinsert(lines, "");
				tinsert(lines, "|cffffd100Mastery:|r " .. def.mastery.text);
			elseif o.level == 1 then
				tinsert(lines, "");
				tinsert(lines, "|cff888888At rank V: " .. def.mastery.text .. "|r");
			end
		end
		local evolution = EVOLUTIONS[o.key];
		if evolution and o.level == MAX_LEVEL then
			tinsert(lines, "|cffd980ffEvolves with " .. PASSIVES[evolution.needs].name .. ".|r");
		end
		for _, fusion in ipairs(FUSIONS) do
			if o.level == MAX_LEVEL and (fusion.a == o.key or fusion.b == o.key) then
				local other = fusion.a == o.key and fusion.b or fusion.a;
				tinsert(lines, "|cffff8c26Fuses with " .. SPELLS[other].name .. " at rank V.|r");
			end
		end
	elseif o.kind == "evolve" or o.kind == "fuse" then
		local def = SPELLS[o.key];
		if o.kind == "fuse" then
			tinsert(lines, "Fuses " .. SPELLS[o.a].name .. " and " .. SPELLS[o.b].name .. ", and frees a slot.");
		else
			tinsert(lines, "Replaces " .. SPELLS[o.from].name .. ".");
		end
		tinsert(lines, "");
		tinsert(lines, def.blurb);
		tinsert(lines, "");
		for _, field in ipairs(STAT_ORDER) do
			local v = Stat(def, field, 1);
			if v then
				tinsert(lines, STAT_LABELS[field] .. " " .. StatText(field, v));
			end
		end
	elseif o.kind == "gear" then
		tinsert(lines, GEAR[o.key].text);
		tinsert(lines, "");
		tinsert(lines, "|cff888888Kept for the run.|r");
	elseif o.kind == "passive" then
		local p = PASSIVES[o.key];
		tinsert(lines, p.text);
		tinsert(lines, "");
		tinsert(lines, "|cff888888Total:|r " .. string.format(p.total, p.per * o.level));
	else
		tinsert(lines, "Heal 50 health.");
	end
	return table.concat(lines, "\n");
end

-- A card grows to its text (a long name wraps too): the parts above the
-- description (the icon at 20 from the top, 64 tall, the name 10 below it,
-- the tag 4 below that, the description 14 below that) and below it (12
-- above the pips, which sit 40 from the bottom, 6 tall), as Broodwatch.xml
-- lays them out.
local CARD_ABOVE = 20 + 64 + 10 + 4 + 14
local CARD_BELOW = 12 + 6 + 40
local CARD_MIN_HEIGHT = 280
local CARD_DESC_ROOM = 1000 -- the description's height while it is measured

-- Fills card i with offer o; returns the height the card needs.
local function FillCard(i, o)
	local card = getglobal(BW.cards:GetName() .. "Card" .. i);
	local name = card:GetName();
	local icon, title, tag, colorKey;
	if o.kind == "spell" then
		local def = SPELLS[o.key];
		icon, title = Icon(def.spell), def.name;
		if o.level == 1 then
			tag, colorKey = "New spell", "new";
		else
			tag, colorKey = "Rank " .. ROMAN[o.level], "spell";
		end
	elseif o.kind == "passive" then
		local p = PASSIVES[o.key];
		icon, title = Icon(p.icon), p.name;
		tag, colorKey = "Passive, rank " .. ROMAN[o.level], "passive";
	elseif o.kind == "evolve" then
		local def = SPELLS[o.key];
		icon, title, tag, colorKey = Icon(def.spell), def.name, "Evolution", "evolve";
	elseif o.kind == "fuse" then
		local def = SPELLS[o.key];
		icon, title, tag, colorKey = Icon(def.spell), def.name, "Fusion", "fuse";
	elseif o.kind == "gear" then
		local gear = GEAR[o.key];
		icon, title = gear.icon or UNKNOWN_ICON, gear.name or "";
		tag = gear.slot == "weapon" and "Weapon" or "Worn";
	else
		icon, title, tag, colorKey = Icon(HEAL_ICON), "Second Wind", "Everything is at rank V", "heal";
	end
	local color = o.kind == "gear" and GearColor(GEAR[o.key]) or CARD_COLORS[colorKey];
	card.color = color;
	card:SetBackdropColor(0.04, 0.04, 0.06, 0.95);
	card:SetBackdropBorderColor(color[1], color[2], color[3]);
	getglobal(name .. "Icon"):SetTexture(icon);
	local nameText = getglobal(name .. "Name");
	nameText:SetHeight(CARD_DESC_ROOM);
	nameText:SetText(title);
	nameText:SetHeight(nameText:GetStringHeight());
	nameText:SetTextColor(color[1], color[2], color[3]);
	local tagText = getglobal(name .. "Tag");
	tagText:SetText(tag);
	local desc = getglobal(name .. "Desc");
	desc:SetHeight(CARD_DESC_ROOM);
	desc:SetText(OfferLines(o));
	local descHeight = desc:GetStringHeight();
	desc:SetHeight(descHeight);
	getglobal(name .. "Key"):SetText("Press " .. i);
	-- The ranks: those you have in the card's colour, the new one white.
	local maxRank = o.kind == "passive" and PASSIVES[o.key].max or MAX_LEVEL;
	for r = 1, MAX_LEVEL do
		local pip = getglobal(name .. "Pip" .. r);
		if o.kind == "heal" or o.kind == "evolve" or o.kind == "fuse" or o.kind == "gear" or r > maxRank then
			pip:Hide();
		else
			pip:Show();
			if r < o.level then
				pip:SetVertexColor(color[1], color[2], color[3]);
			elseif r == o.level then
				pip:SetVertexColor(1, 1, 1);
			else
				pip:SetVertexColor(0.2, 0.2, 0.2);
			end
		end
	end
	return CARD_ABOVE + nameText:GetStringHeight() + tagText:GetStringHeight() + descHeight + CARD_BELOW;
end

-- A build slot's tooltip: what the spell or passive does at the rank you
-- have, or what the gear does.
local function ShowSlotTip(slot)
	local tip = GameTooltip;
	tip:SetOwner(slot, "ANCHOR_RIGHT");
	if slot.gearKey then
		local gear = GEAR[slot.gearKey];
		local color = GearColor(gear);
		tip:SetText(gear.name or "", color[1], color[2], color[3]);
		if slot.gearKey == "benediction" and not BW.run.blessed then
			tip:AddLine(gear.anathema.name or "", 0.6, 0.6, 0.6);
		end
		tip:AddLine(gear.text, 1, 1, 1, 1);
	elseif slot.spellKey then
		local def = SPELLS[slot.spellKey];
		local level = BW.run.spells[slot.spellKey].level;
		local color = def.fused and CARD_COLORS.fuse or def.evolved and CARD_COLORS.evolve or CARD_COLORS.spell;
		tip:SetText(def.name, color[1], color[2], color[3]);
		if def.fused then
			for _, fusion in ipairs(FUSIONS) do
				if fusion.into == slot.spellKey then
					tip:AddLine("Fused from " .. SPELLS[fusion.a].name .. " and " .. SPELLS[fusion.b].name, 0.6, 0.6, 0.6, 1);
				end
			end
		elseif def.evolved then
			for from, evolution in pairs(EVOLUTIONS) do
				if evolution.into == slot.spellKey then
					tip:AddLine("Evolved from " .. SPELLS[from].name .. " and " .. PASSIVES[evolution.needs].name,
						0.6, 0.6, 0.6, 1);
				end
			end
		else
			tip:AddLine("Rank " .. ROMAN[level], 0.6, 0.6, 0.6);
		end
		tip:AddLine(def.blurb, 1, 1, 1, 1);
		for _, field in ipairs(STAT_ORDER) do
			local v = Stat(def, field, level);
			if v then
				tip:AddDoubleLine(STAT_LABELS[field], StatText(field, v), 0.8, 0.8, 0.8, 1, 1, 1);
			end
		end
		if def.mastery then
			if level == MAX_LEVEL then
				tip:AddLine("Mastery: " .. def.mastery.text, 1, 0.82, 0, 1);
			else
				tip:AddLine("At rank V: " .. def.mastery.text, 0.53, 0.53, 0.53, 1);
			end
		end
		local evolution = EVOLUTIONS[slot.spellKey];
		if evolution then
			local into = SPELLS[evolution.into];
			tip:AddLine("At rank V with " .. PASSIVES[evolution.needs].name .. ", evolves into " .. into.name .. ".",
				0.85, 0.5, 1, 1);
		end
		for _, fusion in ipairs(FUSIONS) do
			if fusion.a == slot.spellKey or fusion.b == slot.spellKey then
				local other = fusion.a == slot.spellKey and fusion.b or fusion.a;
				tip:AddLine("At rank V with " .. SPELLS[other].name .. " at rank V, fuses into "
					.. SPELLS[fusion.into].name .. ".", 1, 0.55, 0.15, 1);
			end
		end
	else
		local p = PASSIVES[slot.passiveKey];
		local level = BW.run.passives[slot.passiveKey];
		local color = CARD_COLORS.passive;
		tip:SetText(p.name, color[1], color[2], color[3]);
		tip:AddLine("Passive, rank " .. ROMAN[level], 0.6, 0.6, 0.6);
		tip:AddLine(p.text, 1, 1, 1, 1);
		tip:AddLine("Total: " .. string.format(p.total, p.per * level), 0.8, 0.8, 0.8, 1);
	end
	tip:Show();
end

local function HideSlotTip()
	GameTooltip:Hide();
end

function Broodwatch_CardHover(card, over)
	if over then
		card:SetBackdropBorderColor(1, 1, 1);
	elseif card.color then
		card:SetBackdropBorderColor(card.color[1], card.color[2], card.color[3]);
	end
end

-- The build bar: an icon for each spell and passive you have, with its rank.
local BUILD_SLOT = 32
local buildSlots = {}

local function BuildSlot(i)
	local slot = buildSlots[i];
	if not slot then
		slot = CreateFrame("Frame", nil, BW.build);
		slot:SetWidth(BUILD_SLOT);
		slot:SetHeight(BUILD_SLOT);
		slot.icon = slot:CreateTexture(nil, "ARTWORK");
		slot.icon:SetAllPoints(slot);
		-- The spell's recharge sweeps over its icon. No flash at the end:
		-- the fast spells would flash all the time.
		slot.cd = CreateFrame("Cooldown", nil, slot);
		slot.cd:SetAllPoints(slot);
		slot.cd:SetDrawEdge(true);
		slot.cd:SetDrawBling(false);
		slot.cd:SetHideCountdownNumbers(true);
		slot.cd:SetPaused(BW.paused);
		-- The rank sits above the sweep.
		slot.top = CreateFrame("Frame", nil, slot);
		slot.top:SetAllPoints(slot);
		slot.top:SetFrameLevel(slot.cd:GetFrameLevel() + 1);
		slot.rank = slot.top:CreateFontString(nil, "OVERLAY");
		slot.rank:SetFontObject(NumberFontNormal);
		slot.rank:SetPoint("BOTTOMRIGHT", slot, "BOTTOMRIGHT", -1, 1);
		slot:EnableMouse(true);
		slot:SetScript("OnEnter", function() ShowSlotTip(slot); end);
		slot:SetScript("OnLeave", HideSlotTip);
		buildSlots[i] = slot;
	end
	return slot;
end

local buildCount = 0

local PASSIVE_SLOT = 24

local function AddBuildSlot(icon, level, x, y, size)
	buildCount = buildCount + 1;
	local slot = BuildSlot(buildCount);
	slot:SetWidth(size);
	slot:SetHeight(size);
	slot:ClearAllPoints();
	slot:SetPoint("BOTTOMLEFT", BW.build, "BOTTOMLEFT", x, y);
	slot.icon:SetTexture(icon);
	slot.icon:SetDesaturated(false);
	slot.rank:SetText(ROMAN[level]);
	slot:Show();
	return slot;
end

-- Shows a spell's recharge on its slot: the part of `full` seconds left in
-- `left`, measured on the game's clock (the sweep pauses with the game).
local function ShowRecharge(slot, left, full)
	if left > 0 and full and full > 0 then
		slot.cd:SetCooldown(GetTime() - (full - left), full);
	else
		slot.cd:Clear();
	end
end

local function UpdateBuild()
	buildCount = 0;
	local x = 0;
	for _, key in ipairs(SPELL_ORDER) do
		local state = BW.run.spells[key];
		if state then
			-- A new spell can move the others along the bar: each slot shows
			-- its own spell's recharge again.
			state.slot = AddBuildSlot(Icon(SPELLS[key].spell), state.level, x, 0, BUILD_SLOT);
			state.slot.spellKey, state.slot.passiveKey, state.slot.gearKey = key, nil, nil;
			if SPELLS[key].evolved then
				state.slot.rank:SetText("|cffd980ffE|r");
			end
			ShowRecharge(state.slot, state.cooldown, state.cdFull);
			x = x + BUILD_SLOT + 4;
		end
	end
	x = 0;
	for _, key in ipairs(PASSIVE_ORDER) do
		local level = BW.run.passives[key];
		if level then
			local slot = AddBuildSlot(Icon(PASSIVES[key].icon), level, x, BUILD_SLOT + 6, PASSIVE_SLOT);
			slot.spellKey, slot.passiveKey, slot.gearKey = nil, key, nil;
			slot.cd:Clear();
			slot.icon:SetDesaturated(key == "soulstone" and BW.run.soulstoneUsed and true or false);
			x = x + PASSIVE_SLOT + 3;
		end
	end
	-- The gear, above the passives: the weapon first. An on-use trinket shows
	-- its recharge.
	x = 0;
	local gearY = BUILD_SLOT + 6 + PASSIVE_SLOT + 6;
	for _, key in ipairs(GEAR_ORDER) do
		if BW.run.weapon == key or BW.run.gear[key] then
			local gear = GEAR[key];
			local icon = gear.icon;
			if key == "benediction" and not BW.run.blessed then
				icon = gear.anathema.icon or icon;
			end
			local slot = AddBuildSlot(icon or UNKNOWN_ICON, 1, x, gearY, PASSIVE_SLOT);
			slot.spellKey, slot.passiveKey, slot.gearKey = nil, nil, key;
			slot.rank:SetText("");
			BW.run.gearSlots[key] = slot;
			local ready = BW.run.gearReady[key];
			if ready then
				ShowRecharge(slot, ready - BW.run.time, gear.every);
			else
				slot.cd:Clear();
			end
			x = x + PASSIVE_SLOT + 3;
		end
	end
	for i = buildCount + 1, table.getn(buildSlots) do
		buildSlots[i]:Hide();
	end
end

local function SetPaused(on)
	BW.paused = on;
	BW.scene:SetPaused(on);
	for _, slot in ipairs(buildSlots) do
		slot.cd:SetPaused(on);
	end
	if on then
		BW.Drive(0, 0);
	end
end

-- Shows the next waiting pick: the start's, then a chest's, then a level's. The game stays
-- paused until the last pick is made.
local function ShowCards()
	if BW.cards:IsShown() then
		return;
	end
	local title;
	if BW.run.startPick then
		BW.run.picking = "start";
		title = "Choose your first spell";
	elseif BW.run.chestPicks > 0 then
		BW.run.picking = "chest";
		title = BW.run.chestTitle;
	elseif BW.run.pendingLevels > 0 then
		BW.run.picking = "level";
		title = "Level " .. (BW.run.level - BW.run.pendingLevels + 1);
	else
		return;
	end
	BuildOffers();
	-- The three cards share the tallest one's height.
	local height = CARD_MIN_HEIGHT;
	for i = 1, 3 do
		height = math.max(height, FillCard(i, offers[i]));
	end
	for i = 1, 3 do
		getglobal(BW.cards:GetName() .. "Card" .. i):SetHeight(height);
	end
	getglobal(BW.cards:GetName() .. "Title"):SetText(title);
	BW.cards:Show();
	SetPaused(true);
end

local function ApplyHeroSpeed()
	local factor = 1 + PASSIVES.sprint.per * (BW.run.passives.sprint or 0) / 100;
	if BW.run.swiftUntil then
		factor = factor * TUNING.SWIFTNESS_FACTOR;
	end
	if BW.run.shoutUntil then
		factor = factor * SPELLS.demoshout.mastery.speed;
	end
	BW.hero:SetMovementSpeed("run", BW.run.heroRun * factor);
end

local function MaxHp()
	return HERO_HP + 25 * (BW.run.passives.vigor or 0);
end

function Broodwatch_PickCard(i)
	if not BW.run or not BW.cards:IsShown() then
		return;
	end
	local o = offers[i];
	if o.kind == "fuse" then
		-- The fusion takes the first spell's place and recharge; the second
		-- spell's slot frees.
		local state = BW.run.spells[o.a];
		BW.run.spells[o.a] = nil;
		BW.run.spells[o.b] = nil;
		state.level = 1;
		BW.run.spells[o.key] = state;
	elseif o.kind == "evolve" then
		-- The evolution takes its spell's place, recharge and all.
		local state = BW.run.spells[o.from];
		BW.run.spells[o.from] = nil;
		state.level = 1;
		BW.run.spells[o.key] = state;
	elseif o.kind == "spell" then
		local state = BW.run.spells[o.key];
		if state then
			state.level = o.level;
		else
			BW.run.spells[o.key] = { level = 1, cooldown = 0, pending = 0 };
		end
	elseif o.kind == "gear" then
		BW.Equip(o.key);
		if GEAR[o.key].slot == "weapon" then
			BW.run.weaponPick = nil;
		end
	elseif o.kind == "passive" then
		BW.run.passives[o.key] = o.level;
		if o.key == "might" then
			BW.run.damageScale = 1 + 0.15 * o.level;
		elseif o.key == "vigor" then
			BW.run.hp = math.min(MaxHp(), BW.run.hp + 25);
		elseif o.key == "sprint" then
			ApplyHeroSpeed();
		end
	else
		BW.run.hp = math.min(MaxHp(), BW.run.hp + 50);
	end
	BW.cards:Hide();
	UpdateBuild();
	BW.UpdateHealth(); -- Vigor's larger bar and heal, Second Wind's heal (defined below)
	if BW.run.picking == "start" then
		BW.run.startPick = nil;
	elseif BW.run.picking == "chest" then
		BW.run.chestPicks = BW.run.chestPicks - 1;
	else
		BW.run.pendingLevels = BW.run.pendingLevels - 1;
	end
	ShowCards();
	if not BW.cards:IsShown() then
		SetPaused(false);
		-- A grace when the last card closes: the brood that crowded you while
		-- you leveled is thrown back, and nothing hurts you for a moment (the
		-- step; you show faded while it lasts).
		BW.run.graceUntil = BW.run.time + TUNING.GRACE_SECONDS;
		BW.hero:SetAlpha(TUNING.GRACE_ALPHA);
		local hx, hy = BW.hero:GetPosition();
		local r2 = TUNING.GRACE_RADIUS * TUNING.GRACE_RADIUS;
		for _, e in ipairs(BW.enemies) do
			if e.alive then
				local x, y = e.actor:GetPosition();
				if BW.Dist2(x, y, hx, hy) <= r2 then
					BW.Blast(e, hx, hy, TUNING.GRACE_KNOCK);
				end
			end
		end
	end
end

local function UpdateHealth(mode)
	local max = MaxHp();
	BW.healthBar:SetMinMaxValues(0, max, mode or EASE);
	BW.healthBar:SetValue(math.max(BW.run.hp, 0), mode or EASE);
	local text = string.format("%d / %d", math.max(math.ceil(BW.run.hp), 0), max);
	if BW.run.shield == math.huge then -- Divine Shield
		text = text .. "  (immune)";
	elseif BW.run.shield then
		text = text .. string.format("  (+%d)", math.ceil(BW.run.shield));
	end
	if BW.run.invulnUntil then -- a Limited Invulnerability Potion
		text = text .. "  (invulnerable)";
	end
	if BW.run.fireWard then -- a Fire Protection Potion
		text = text .. string.format("  (+%d fire)", math.ceil(BW.run.fireWard));
	end
	BW.healthText:SetText(text);
end

local LEVEL_HEAL = 0.12 -- each level heals this share of the maximum health

-- The XP to the next level: cubic, so a strong run tops out around level 50
-- and has to choose, rather than maxing everything (level 10 needs 55,
-- level 30 needs 635).
local function XpNeeded(level)
	return 5 + 3 * level + math.floor(level * level * level / 50);
end

-- The experience bar and its text: the level, and the experience toward the
-- next one.
local function UpdateXp(mode)
	local needed = XpNeeded(BW.run.level);
	BW.xpBar:SetMinMaxValues(0, needed, mode);
	BW.xpBar:SetValue(BW.run.xp, mode);
	BW.xpText:SetText(string.format("Level %d    %d / %d", BW.run.level, math.floor(BW.run.xp), needed));
end

local function GainXp(xp)
	BW.run.xp = BW.run.xp + xp;
	local wrapped = false;
	while BW.run.xp >= XpNeeded(BW.run.level) do
		BW.run.xp = BW.run.xp - XpNeeded(BW.run.level);
		BW.run.level = BW.run.level + 1;
		BW.run.pendingLevels = BW.run.pendingLevels + 1;
		wrapped = true;
	end
	-- A level starts the bar again from empty at once; a gain within a
	-- level eases.
	if wrapped then
		BW.hero:PlayLevelUpEffect(); -- the world's glow and sound, once however many levels
		local levels = BW.run.pendingLevels;
		BW.run.hp = math.min(MaxHp(), BW.run.hp + MaxHp() * LEVEL_HEAL * levels);
		UpdateHealth();
	end
	UpdateXp(wrapped and AT_ONCE or EASE);
	ShowCards();
end

-- For the files that load after this one.
BW.Icon = Icon
BW.UpdateBuild = UpdateBuild
BW.SetPaused = SetPaused
BW.ApplyHeroSpeed = ApplyHeroSpeed
BW.MaxHp = MaxHp
BW.UpdateHealth = UpdateHealth
BW.XpNeeded = XpNeeded
BW.GainXp = GainXp
BW.UpdateXp = UpdateXp
BW.ShowCards = ShowCards
