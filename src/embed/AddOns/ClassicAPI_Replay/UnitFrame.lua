-- ClassicAPI Replay: the followed unit's frame, as 1.12's TargetFrame.

local _, ns = ...;

-- The icon of a spell this client does not have (a log recorded on another),
-- as retail shows one.
local UNKNOWN_SPELL_ICON = "Interface\\Icons\\INV_Misc_QuestionMark";
-- The stand-in portrait of a player the client does not hold (0x00854044).
local STAND_IN_PORTRAIT = "Interface\\CharacterFrame\\TemporaryPortrait-%s-%s";
-- The followed unit's frame, as 1.12's TargetFrame:
-- - at most 5 buffs and 16 debuffs (MAX_TARGET_BUFFS, MAX_TARGET_DEBUFFS),
--   in rows that wrap after 6 debuffs, or 5 while the target of target
--   shows; 21-pixel icons, 17 once they would overlap it. The target of
--   target shows 4 debuffs of 12 pixels;
-- - the level in gold (TargetFrame_CheckLevel's color for a unit the player
--   cannot attack);
-- - the name's background grey for a unit another player tapped, and blue
--   for one with no selection color (TargetFrame_CheckFaction). A unit a
--   player controls (UNIT_FIELD_FLAGS 0x8) is never grey.
-- dispelNames are C_Spell.GetSpellDispelType's numbers as DebuffTypeColor
-- names them; the movement file keeps the power of type powerRage in tenths.
local UNIT_FRAME = {
	buffs = 5, debuffs = 16, auraSize = 21, auraSmallSize = 17, auraGap = 3,
	totDebuffs = 4, totDebuffSize = 12,
	levelColor = { 1, 0.82, 0 },
	tappedColor = { 0.5, 0.5, 0.5 }, noSelectionColor = { 0, 0, 1 },
	playerControlledFlag = 0x8,
	dispelNames = { "Magic", "Curse", "Disease", "Poison" },
	powerRage = 1,
};
local auraButtons = {}; -- its aura buttons, by kind ("buff", "debuff", "tot") and index

------------------------------------------------------------------------------
-- The unit frame

-- A unit's portrait: its actor's, drawn from the actor's model as the client
-- draws a unit's, and drawn again by the actor when its model changes.
-- Before the unit has an actor, a player in its own body shows the stand-in
-- the client shows for a player it does not hold (0x00525BA0):
-- TemporaryPortrait-<Male|Female>-<race file>; a creature, or a player in a
-- form, its display's portrait.
local function SetPortrait(texture, u)
	if u.actor then
		if texture.key ~= u.actor then
			texture.key = u.actor;
			texture:SetTexture(nil);
			u.actor:SetPortraitTexture(texture);
		end
		return;
	end
	local appearance = u.isPlayer and u.display == u.nativeDisplay and ns.AppearanceAt(u, ns.now);
	local race = appearance and C_CreatureInfo.GetRaceInfo(appearance.raceID);
	local key = race and (race.clientFileString .. appearance.sex) or u.display;
	if texture.key == key then
		return;
	end
	texture.key = key;
	texture:SetTexture(nil);
	if race then
		texture:SetTexture(string.format(STAND_IN_PORTRAIT, appearance.sex == 1 and "Female" or "Male",
			race.clientFileString));
	elseif u.display and u.display > 0 then
		SetPortraitTextureFromCreatureDisplayID(texture, u.display);
	end
end

-- The unit's health and power at `time` on two bars; false when the log has
-- no health for it. Rage is kept in tenths, and shows as the client shows it.
local function SetBars(health, mana, u, time)
	local current, maximum = C_CombatLogReplay.GetUnitHealth(u.index, time);
	if u.dead then
		current = 0;
	end
	if not maximum or maximum <= 0 then
		health:Hide();
		mana:Hide();
		return false;
	end
	health:SetMinMaxValues(0, maximum);
	health:SetValue(current);
	health.value, health.maximum = current, maximum;
	health:Show();
	local powerType, power, maxPower = C_CombatLogReplay.GetUnitPower(u.index, time);
	if not powerType or not maxPower or maxPower <= 0 then
		mana:Hide();
		return true;
	end
	if powerType == UNIT_FRAME.powerRage then
		power, maxPower = math.floor(power / 10), math.floor(maxPower / 10);
	end
	local color = ManaBarColor[powerType] or ManaBarColor[0];
	mana:SetStatusBarColor(color.r, color.g, color.b);
	mana:SetMinMaxValues(0, maxPower);
	mana:SetValue(power);
	mana.value, mana.maximum = power, maxPower;
	mana:Show();
	return true;
end

-- A bar's "value / maximum", always shown: a replay is for reading numbers.
local function UpdateBarText(bar)
	if bar.value then
		bar.Text:SetText(bar.value .. " / " .. bar.maximum);
	end
end

local function OnAuraEnter(button)
	GameTooltip:SetOwner(button, "ANCHOR_BOTTOMRIGHT", 15, -25);
	GameTooltip:SetSpellByID(button.spellID);
end

local function OnAuraLeave()
	GameTooltip:Hide();
end

local function AuraButton(kind, i, parent)
	local buttons = auraButtons[kind];
	if not buttons then
		buttons = {};
		auraButtons[kind] = buttons;
	end
	local button = buttons[i];
	if not button then
		button = CreateFrame("Button", nil, parent, "ClassicAPIReplayAuraTemplate");
		button:SetScript("OnEnter", OnAuraEnter);
		button:SetScript("OnLeave", OnAuraLeave);
		buttons[i] = button;
	end
	return button;
end

-- The time an aura has left as a cooldown's countdown shows it: whole
-- seconds below a minute, then minutes, then hours, each rounded up.
local function AuraTimeText(left)
	if left < 60 then
		return tostring(math.ceil(left));
	elseif left < 3600 then
		return math.ceil(left / 60) .. "m";
	end
	return math.ceil(left / 3600) .. "h";
end

-- The aura's timer at `time`, from when it ended in the log: the sweep of
-- its duration, paused where the replay is (CooldownFrame_SetDisplayAsPercentage),
-- so it follows the replay's speed and pauses; with `showTime`, the time
-- left. None for an aura whose end the replay does not know.
local function SetAuraTimer(button, aura, time, showTime)
	local left = aura.expirationTime and aura.expirationTime - time;
	if not left or left <= 0 then
		button.Cooldown:Clear();
		button.TextFrame.Time:SetText("");
		return;
	end
	if aura.duration and aura.duration > 0 then
		CooldownFrame_SetDisplayAsPercentage(button.Cooldown, 1 - left / aura.duration);
	else
		button.Cooldown:Clear();
	end
	button.TextFrame.Time:SetText(showTime and AuraTimeText(left) or "");
end

-- Shows an aura on a button, as TargetDebuffButton_Update does: a debuff
-- with its border in its dispel type's color and its stacks above 1; then
-- its timer, the time left only on a full-size button.
local function SetAura(button, aura, size, time)
	button.spellID = aura.spellID;
	local _, _, icon = GetSpellInfo(aura.spellID);
	button.Icon:SetTexture(icon or UNKNOWN_SPELL_ICON);
	button:SetWidth(size);
	button:SetHeight(size);
	if aura.auraType == "DEBUFF" then
		local color = DebuffTypeColor[UNIT_FRAME.dispelNames[C_Spell.GetSpellDispelType(aura.spellID)] or "none"];
		button.Border:SetVertexColor(color.r, color.g, color.b);
		button.Border:SetWidth(size + 2);
		button.Border:SetHeight(size + 2);
		button.Border:Show();
		if aura.applications and aura.applications > 1 then
			button.TextFrame.Count:SetText(aura.applications);
			button.TextFrame.Count:Show();
		else
			button.TextFrame.Count:Hide();
		end
	else
		button.Border:Hide();
		button.TextFrame.Count:Hide();
	end
	SetAuraTimer(button, aura, time, size >= UNIT_FRAME.auraSmallSize);
	button:Show();
end

local function HideAuras(kind, from)
	for i = from, #(auraButtons[kind] or {}) do
		auraButtons[kind][i]:Hide();
	end
end

-- The unit's buffs and debuffs under the frame, in TargetDebuffButton_Update's
-- rows: the buffs, then the debuffs below them.
local function SetAuras(u, time, targetShown)
	local buffs, debuffs = {}, {};
	for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(u.index, time)) do
		if aura.isHidden then
			-- Not one UnitBuff or UnitDebuff lists.
		elseif aura.auraType == "DEBUFF" then
			if #debuffs < UNIT_FRAME.debuffs then
				tinsert(debuffs, aura);
			end
		elseif #buffs < UNIT_FRAME.buffs then
			tinsert(buffs, aura);
		end
	end
	local wrap = targetShown and 5 or 6;
	local crowded = (targetShown and #buffs == UNIT_FRAME.buffs) or #debuffs >= wrap;
	local size = crowded and UNIT_FRAME.auraSmallSize or UNIT_FRAME.auraSize;
	local rowStart;
	for i, aura in ipairs(buffs) do
		local button = AuraButton("buff", i, ns.unitFrame);
		SetAura(button, aura, size, time);
		button:ClearAllPoints();
		if i == 1 then
			button:SetPoint("TOPLEFT", ns.unitFrame, "BOTTOMLEFT", 5, 32);
			rowStart = button;
		else
			button:SetPoint("LEFT", auraButtons.buff[i - 1], "RIGHT", UNIT_FRAME.auraGap, 0);
		end
	end
	HideAuras("buff", #buffs + 1);
	for i, aura in ipairs(debuffs) do
		local button = AuraButton("debuff", i, ns.unitFrame);
		SetAura(button, aura, size, time);
		button:ClearAllPoints();
		if i == 1 then
			if rowStart then
				button:SetPoint("TOPLEFT", rowStart, "BOTTOMLEFT", 0, -2);
			else
				button:SetPoint("TOPLEFT", ns.unitFrame, "BOTTOMLEFT", 5, 32);
			end
			rowStart = button;
		elseif (i - 1) % wrap == 0 then
			button:SetPoint("TOPLEFT", rowStart, "BOTTOMLEFT", 0, -2);
			rowStart = button;
		else
			button:SetPoint("LEFT", auraButtons.debuff[i - 1], "RIGHT", UNIT_FRAME.auraGap, 0);
		end
	end
	HideAuras("debuff", #debuffs + 1);
end

-- The target of target: its portrait, name, bars and first debuffs, as
-- TargetofTargetFrame shows them.
local function SetTargetOfTarget(target, time)
	local tot = ns.unitFrame.TargetOfTarget;
	tot.unit = target;
	SetPortrait(tot.Portrait, target);
	tot.TextureFrame.Name:SetText(target.name ~= "" and target.name or UNKNOWN);
	if target.dead then
		tot.TextureFrame.Dead:Show();
	else
		tot.TextureFrame.Dead:Hide();
	end
	SetBars(tot.HealthBar, tot.ManaBar, target, time);
	local shown = 0;
	for _, aura in ipairs(C_CombatLogReplay.GetUnitAuras(target.index, time)) do
		if aura.auraType == "DEBUFF" and not aura.isHidden and shown < UNIT_FRAME.totDebuffs then
			shown = shown + 1;
			local button = AuraButton("tot", shown, tot);
			SetAura(button, aura, UNIT_FRAME.totDebuffSize, time);
			button:ClearAllPoints();
			if shown == 1 then
				button:SetPoint("TOPLEFT", tot, "TOPRIGHT", 4, -10);
			elseif shown == 3 then
				button:SetPoint("TOPLEFT", auraButtons.tot[1], "BOTTOMLEFT", 0, -1);
			else
				button:SetPoint("LEFT", auraButtons.tot[shown - 1], "RIGHT", 1, 0);
			end
		end
	end
	HideAuras("tot", shown + 1);
	tot:Show();
end

-- The followed unit at `time`, or no frame while none is followed.
local function UpdateUnitFrame(time)
	local u = ns.focus;
	if not u then
		ns.unitFrame:Hide();
		return;
	end
	local f = ns.unitFrame.TextureFrame;
	SetPortrait(ns.unitFrame.Portrait, u);
	f.Name:SetText(u.name ~= "" and u.name or UNKNOWN);
	local target, _, flags, level, _, _, _, isTapDenied = C_CombatLogReplay.GetUnitState(u.index, time);
	-- The name's background as TargetFrame_CheckFaction sets it: grey with a
	-- grey portrait for a creature another player tapped, else the selection
	-- color.
	local tapped = isTapDenied and bit.band(flags, UNIT_FRAME.playerControlledFlag) == 0;
	local r, g, b;
	if tapped then
		r, g, b = UNIT_FRAME.tappedColor[1], UNIT_FRAME.tappedColor[2], UNIT_FRAME.tappedColor[3];
	else
		r, g, b = C_CombatLogReplay.GetUnitSelectionColor(u.index, time);
		if not r then
			r, g, b = UNIT_FRAME.noSelectionColor[1], UNIT_FRAME.noSelectionColor[2], UNIT_FRAME.noSelectionColor[3];
		end
	end
	ns.unitFrame.NameBackground:SetVertexColor(r, g, b);
	local shade = tapped and UNIT_FRAME.tappedColor[1] or 1;
	ns.unitFrame.Portrait:SetVertexColor(shade, shade, shade);
	if level and level > 0 then
		f.Level:SetText(level);
		f.Level:SetVertexColor(UNIT_FRAME.levelColor[1], UNIT_FRAME.levelColor[2], UNIT_FRAME.levelColor[3]);
		f.Level:Show();
	else
		f.Level:Hide();
	end
	if u.dead then
		f.Dead:Show();
	else
		f.Dead:Hide();
	end
	if u.mark then
		SetRaidTargetIconTexture(f.RaidTargetIcon, u.mark);
		f.RaidTargetIcon:Show();
	else
		f.RaidTargetIcon:Hide();
	end
	SetBars(ns.unitFrame.HealthBar, ns.unitFrame.ManaBar, u, time);
	UpdateBarText(ns.unitFrame.HealthBar);
	UpdateBarText(ns.unitFrame.ManaBar);
	target = target and ns.units[target];
	if target then
		SetTargetOfTarget(target, time);
	else
		ns.unitFrame.TargetOfTarget:Hide();
		HideAuras("tot", 1);
	end
	SetAuras(u, time, target ~= nil);
	ns.unitFrame:Show();
end
ns.UpdateUnitFrame = UpdateUnitFrame;

-- Raises a frame and everything in it, each child one level above its
-- parent as at load: a child keeps its level when its parent's changes.
local function SetFrameLevels(f, level)
	f:SetFrameLevel(level);
	local children = { f:GetChildren() };
	for _, child in ipairs(children) do
		SetFrameLevels(child, level + 1);
	end
end
ns.SetFrameLevels = SetFrameLevels;
