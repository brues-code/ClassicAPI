
function RegisterNewSlashCommand(callback, command, commandAlias)
	local name = string.upper(command);
    _G["SLASH_"..name.."1"] = "/"..command;
    _G["SLASH_"..name.."2"] = "/"..commandAlias;
    SlashCmdList[name] = callback;
end

-- Keys that exist before this file runs, so the block at the bottom can tell
-- the commands we ADD from the ones we replace.
local preexistingKeys = {};
for key in pairs(SlashCmdList) do
	preexistingKeys[key] = true;
end

-- `/cast` and `/use` (the FrameXML ChatFrame.lua shape): conditionals through
-- SecureCmdOptionParse, items before spells, `[@unit]` cast targets.
-- A bare number is an inventory slot only when it is one (1..19); any other
-- number is a spellID (`/cast 5019`), which the spell resolver already
-- accepts anywhere a spell name goes.
local INVSLOT_FIRST, INVSLOT_LAST = 1, 19;

function SecureCmdItemParse(item)
	if ( not item ) then
		return nil, nil, nil;
	end
	local bag, slot = string.match(item, "^(%d+)%s+(%d+)$");
	if ( not bag ) then
		slot = string.match(item, "^(%d+)$");
		if ( slot ) then
			local n = tonumber(slot);
			if ( n < INVSLOT_FIRST or n > INVSLOT_LAST ) then
				slot = nil;
			end
		end
	end
	if ( bag ) then
		item = GetContainerItemLink(bag, slot);
	elseif ( slot ) then
		item = GetInventoryItemLink("player", slot);
	end
	return item, bag, slot;
end

-- A number with no bag or inventory slot behind it. `/cast 5019` names a
-- spell, so the item lookup must never see it: an item reference reads a
-- number as an itemID, and a carried item with that ID would be used in
-- place of the spell.
local function IsBareNumber(text)
	return string.match(text, "^%d+$") ~= nil;
end

-- Targets that name no unit. `@cursor` names the position under the mouse,
-- which a ground-target spell or item is placed at; `@none` asks for no unit
-- at all, so it becomes the untargeted form of whatever verb ran.
local NON_UNIT_TARGETS = { none = true, cursor = true };

-- `@unit` and `target=unit` accept a character name as well as a unit token
-- (`[target=Feral]`), and the engine's unit functions take only tokens.
-- Resolve once here so every command below hands them something they accept.
-- Returns nil for a name with nobody around to match it.
local function SecureCmdTargetUnit(target)
	if ( not target or target == "" or IsUnitToken(target) ) then
		return target;
	end
	-- Keep the non-unit targets out of the by-name search, which matches on
	-- the START of a name: `@none` would happily find a nearby Nonek.
	if ( NON_UNIT_TARGETS[strlower(target)] ) then
		return nil;
	end
	return UnitTokenFromName(target);
end

-- One target normalization for every verb below: an empty target and `@none`
-- become nil (no unit), a non-unit target keeps its own name, and anything
-- else has to resolve to a token. Returns `false` for a name with nobody
-- around to match it, which the callers treat as "do nothing".
local function SecureCmdNormalizeTarget(target)
	if ( not target or target == "" ) then
		return nil;
	end
	local lower = strlower(target);
	if ( lower == "none" ) then
		return nil;
	end
	if ( NON_UNIT_TARGETS[lower] ) then
		return lower;
	end
	return SecureCmdTargetUnit(target) or false;
end

-- The item a `/use` line names, in the form the three calls below take. An
-- explicit bag or inventory slot becomes a location, which names ONE item:
-- looking the item up by name again would take the first match anywhere, and
-- with two stacks of the same thing that is the wrong one.
local function SecureCmdItemLocation(name, bag, slot)
	if ( bag ) then
		return { bagID = tonumber(bag), slotIndex = tonumber(slot) };
	end
	if ( slot ) then
		return { equipmentSlotIndex = tonumber(slot) };
	end
	return name;
end

function SecureCmdUseItem(name, bag, slot, target)
	local item = SecureCmdItemLocation(name, bag, slot);
	if ( not item ) then
		return;
	end
	if ( target == "cursor" ) then
		-- `@cursor` names a world position, so a ground-target item places
		-- its effect there. An item with no ground effect is used normally.
		C_Item.UseAtCursor(item);
	elseif ( target == "player" ) then
		-- `@player` drops a ground-target item at your own feet. Only your
		-- own position is offered this way; aiming one at another unit is
		-- not something you can do by hand either. An item with no ground
		-- effect is used on you, as before.
		C_Item.UseAtUnit(item, "player");
	else
		-- Every form goes through one call, so a slot is aimed at `@unit`
		-- just like a name is. `/use` also always means use: clicking a bag
		-- slot sells the item at a merchant and repairs it under the repair
		-- cursor, which a typed command should never do.
		C_Item.UseItemByName(item, target);
	end
end

local function SecureCmdCast(msg)
	local action, target = SecureCmdOptionParse(msg);
	if ( not action or action == "" ) then
		return;
	end
	-- `!Name` asks for the spell to be started but never turned off, for the
	-- abilities that toggle: auto-repeat (Shoot, Auto Shot) and the self-auras
	-- (stances, aspects, seals, forms, tracking). Strip the prefix here and
	-- cast through `CastSpellNoToggle`, which asks the engine whether the
	-- ability is already up before it casts.
	local noToggle;
	if ( string.sub(action, 1, 1) == "!" ) then
		noToggle = true;
		action = string.sub(action, 2);
		if ( action == "" ) then
			return;
		end
	end
	target = SecureCmdNormalizeTarget(target);
	if ( target == false ) then
		return; -- a named unit with nobody around to match it
	end
	local name, bag, slot = SecureCmdItemParse(action);
	if ( slot or (name and not IsBareNumber(action) and C_Item.GetItemCount(name) > 0) ) then
		SecureCmdUseItem(name, bag, slot, target);
	elseif ( target == "cursor" ) then
		C_Spell.CastAtCursor(action);
	elseif ( not target or target == "target" ) then
		if ( noToggle ) then
			CastSpellNoToggle(action);
		else
			CastSpellByName(action);
		end
	elseif ( noToggle ) then
		-- Same unit rules as below: your own feet take a ground-target
		-- spell, another unit does not.
		CastSpellNoToggle(action, target, target == "player");
	elseif ( target == "player" ) then
		-- A ground-target spell lands at your own feet; a normal one is
		-- cast on you.
		C_Spell.CastAtUnit(action, target, true);
	else
		-- Any other unit has the spell cast on it, but a ground-target one
		-- is not placed there: that is aim you do not have by hand. The
		-- reticle comes up as usual.
		C_Spell.CastAtUnit(action, target, false);
	end
end

SlashCmdList["CAST"] = SecureCmdCast;
SlashCmdList["USE"] = SecureCmdCast;

SlashCmdList["FOCUS"] = function(msg)
	if ( msg == "" ) then
		FocusUnit();
	else
		local action, target = SecureCmdOptionParse(msg);
		if ( action ) then
			if ( not target or target == "focus" ) then
				target = action;
			end
			target = SecureCmdTargetUnit(target);
			if ( target ) then
				FocusUnit(target);
			end
		end
	end
end

SlashCmdList["CLEARFOCUS"] = function(msg)
	if ( SecureCmdOptionParse(msg) ) then
		ClearFocus();
	end
end

SlashCmdList["STARTATTACK"] = function(msg)
	local action, target = SecureCmdOptionParse(msg);
	if ( action ) then
		if ( not target or target == "target" ) then
			target = action;
		end
		target = SecureCmdTargetUnit(target);
		if ( target ) then
			StartAttack(target);
		end
	end
end

SlashCmdList["STOPATTACK"] = function(msg)
	if ( SecureCmdOptionParse(msg) ) then
		StopAttack();
	end
end

SlashCmdList["EQUIP_SET"] = function(msg)
	local set = SecureCmdOptionParse(msg);
	if ( set and set ~= "" ) then
        C_EquipmentSet.UseEquipmentSet(C_EquipmentSet.GetEquipmentSetID(set))
	end
end

SlashCmdList["CLICK"] = function(msg)
	local action = SecureCmdOptionParse(msg);
	if ( action and action ~= "" ) then
		local name, mouseButton = string.match(action, "([^%s]+)%s+([^%s]+)");
		if ( not name ) then
			name = action;
		end
		local button = GetClickFrame(name);
		if ( button and button:IsObjectType("Button") ) then
			button:Click(mouseButton);
		end
	end
end

-- Runs the conditions and calls `fn()` when a clause matched. The shape of
-- every command whose only argument is its conditions.
local function SecureCmdGated(msg, fn)
	if ( SecureCmdOptionParse(msg) ) then
		fn();
	end
end

-- Strips a trailing "(Rank N)" so a rank-qualified name still matches.
local function PlainSpellName(spell)
	local plain = string.match(spell, "^([^(]+)%(");
	if ( plain ) then
		return strtrim(plain);
	end
	return spell;
end

-- ---------------------------------------------------------------------
-- Targeting
--
-- Each verb has one entry that takes a unit token and another that takes a
-- character name, and each errors on the other kind. `SecureCmdOptionParse`
-- hands back either, so pick with `IsUnitToken`.
-- ---------------------------------------------------------------------

local function SecureCmdTarget(msg, exactMatch)
	local action, target = SecureCmdOptionParse(msg);
	if ( not action ) then
		return;
	end
	if ( target ) then
		local lower = strlower(target);
		if ( lower == "none" ) then
			ClearTarget();  -- `@none` clears the target
			return;
		end
		if ( NON_UNIT_TARGETS[lower] ) then
			return;         -- `@cursor` names no unit to target
		end
	end
	if ( not target or target == "target" ) then
		target = action;
	end
	if ( not target or target == "" ) then
		return;
	end
	if ( IsUnitToken(target) ) then
		TargetUnit(target);
	else
		TargetByName(target, exactMatch);
	end
end

SlashCmdList["TARGET"] = function(msg)
	SecureCmdTarget(msg);
end

SlashCmdList["TARGET_EXACT"] = function(msg)
	SecureCmdTarget(msg, 1);
end

SlashCmdList["ASSIST"] = function(msg)
	if ( msg == "" ) then
		AssistUnit("target");
		return;
	end
	local action, target = SecureCmdOptionParse(msg);
	if ( not action ) then
		return;
	end
	if ( target and NON_UNIT_TARGETS[strlower(target)] ) then
		return; -- `@none` and `@cursor` name no unit to assist
	end
	-- An explicit `@target` leaves the trailing name in charge, the same way
	-- `/target` and `/follow` read it.
	if ( not target or target == "target" ) then
		target = action;
	end
	if ( target == "" ) then
		AssistUnit("target");
	elseif ( IsUnitToken(target) ) then
		AssistUnit(target);
	else
		AssistByName(target);
	end
end

SlashCmdList["FOLLOW"] = function(msg)
	if ( msg == "" ) then
		FollowUnit("target");
		return;
	end
	local action, target = SecureCmdOptionParse(msg);
	if ( not action ) then
		return;
	end
	if ( target and NON_UNIT_TARGETS[strlower(target)] ) then
		return; -- `@none` and `@cursor` name no unit to follow
	end
	if ( not target or target == "target" ) then
		target = action;
	end
	if ( target == "" ) then
		FollowUnit("target");
	elseif ( IsUnitToken(target) ) then
		FollowUnit(target);
	else
		FollowByName(target);
	end
end

SlashCmdList["CLEARTARGET"] = function(msg)
	SecureCmdGated(msg, ClearTarget);
end

SlashCmdList["TARGET_LAST_TARGET"] = function(msg)
	SecureCmdGated(msg, TargetLastTarget);
end

-- The cycling selectors take the parsed value as their "reverse" flag, so
-- `/targetenemy [mod:shift] 1` steps backwards while shift is held. A clause
-- that matched with no value has to arrive as nil: an empty string is a true
-- boolean in Lua, which would make every bare `/targetenemy` step backwards.
local function SecureCmdTargetCycle(msg, fn)
	local action = SecureCmdOptionParse(msg);
	if ( action ) then
		fn(action ~= "" and action or nil);
	end
end

SlashCmdList["TARGET_LAST_ENEMY"] = function(msg)
	SecureCmdTargetCycle(msg, TargetLastEnemy);
end

SlashCmdList["TARGET_NEAREST_ENEMY"] = function(msg)
	SecureCmdTargetCycle(msg, TargetNearestEnemy);
end

SlashCmdList["TARGET_NEAREST_ENEMY_PLAYER"] = function(msg)
	SecureCmdTargetCycle(msg, TargetNearestEnemyPlayer);
end

SlashCmdList["TARGET_NEAREST_FRIEND"] = function(msg)
	SecureCmdTargetCycle(msg, TargetNearestFriend);
end

SlashCmdList["TARGET_NEAREST_FRIEND_PLAYER"] = function(msg)
	SecureCmdTargetCycle(msg, TargetNearestFriendPlayer);
end

SlashCmdList["TARGET_NEAREST_PARTY"] = function(msg)
	SecureCmdTargetCycle(msg, TargetNearestPartyMember);
end

SlashCmdList["TARGET_NEAREST_RAID"] = function(msg)
	SecureCmdTargetCycle(msg, TargetNearestRaidMember);
end

-- ---------------------------------------------------------------------
-- Casting and player state
-- ---------------------------------------------------------------------

SlashCmdList["STOPCASTING"] = function(msg)
	SecureCmdGated(msg, SpellStopCasting);
end

SlashCmdList["CANCELFORM"] = function(msg)
	SecureCmdGated(msg, CancelShapeshiftForm);
end

SlashCmdList["DISMOUNT"] = function(msg)
	SecureCmdGated(msg, Dismount);
end

SlashCmdList["CANCELAURA"] = function(msg)
	local spell = SecureCmdOptionParse(msg);
	if ( spell and spell ~= "" ) then
		CancelSpellByName(PlainSpellName(spell));
	end
end

-- ---------------------------------------------------------------------
-- Equipment
-- ---------------------------------------------------------------------

SlashCmdList["EQUIP"] = function(msg)
	local action = SecureCmdOptionParse(msg);
	if ( action and action ~= "" ) then
		local item = SecureCmdItemParse(action);
		if ( item ) then
			C_Item.EquipItemByName(item);
		end
	end
end

SlashCmdList["EQUIP_TO_SLOT"] = function(msg)
	local action = SecureCmdOptionParse(msg);
	if ( not action ) then
		return;
	end
	local slot, name = string.match(action, "^(%d+)%s+(.+)");
	slot = tonumber(slot);
	if ( not slot or slot < INVSLOT_FIRST or slot > INVSLOT_LAST ) then
		return;
	end
	local item = SecureCmdItemParse(name);
	if ( item ) then
		C_Item.EquipItemByName(item, slot);
	end
end

-- ---------------------------------------------------------------------
-- Action bars
--
-- `ChangeActionBarPage` takes no argument here. It announces the page in
-- `CURRENT_ACTIONBAR_PAGE`, so set that first.
-- ---------------------------------------------------------------------

local function SetActionBarPage(page)
	if ( page and page >= 1 and page <= NUM_ACTIONBAR_PAGES ) then
		CURRENT_ACTIONBAR_PAGE = page;
		ChangeActionBarPage();
	end
end

SlashCmdList["CHANGEACTIONBAR"] = function(msg)
	local action = SecureCmdOptionParse(msg);
	if ( action and action ~= "" ) then
		SetActionBarPage(tonumber(action));
	end
end

SlashCmdList["SWAPACTIONBAR"] = function(msg)
	local action = SecureCmdOptionParse(msg);
	if ( not action ) then
		return;
	end
	local a, b = string.match(action, "(%d+)%s+(%d+)");
	a, b = tonumber(a), tonumber(b);
	if ( a and b ) then
		if ( CURRENT_ACTIONBAR_PAGE == a ) then
			SetActionBarPage(b);
		else
			SetActionBarPage(a);
		end
	end
end

-- ---------------------------------------------------------------------
-- Pet
--
-- `PetAttack` takes no target here: it always sends the pet at the
-- player's current target. Conditions still decide whether it fires, but
-- `/petattack [@mouseover]` cannot aim at the mouseover.
-- ---------------------------------------------------------------------

SlashCmdList["PET_ATTACK"] = function(msg)
	SecureCmdGated(msg, PetAttack);
end

SlashCmdList["PET_FOLLOW"] = function(msg)
	SecureCmdGated(msg, PetFollow);
end

SlashCmdList["PET_STAY"] = function(msg)
	SecureCmdGated(msg, PetWait);
end

SlashCmdList["PET_PASSIVE"] = function(msg)
	SecureCmdGated(msg, PetPassiveMode);
end

SlashCmdList["PET_DEFENSIVE"] = function(msg)
	SecureCmdGated(msg, PetDefensiveMode);
end

SlashCmdList["PET_AGGRESSIVE"] = function(msg)
	SecureCmdGated(msg, PetAggressiveMode);
end

-- Autocast is a spellbook-slot toggle here, so a name has to be resolved
-- against the pet book, and on/off read the current state and toggle only
-- when it differs.
local function PetSpellSlot(name)
	local numPetSpells = HasPetSpells();
	if ( not numPetSpells ) then
		return nil;
	end
	name = strlower(name);
	for i = 1, numPetSpells do
		local spell = GetSpellName(i, BOOKTYPE_PET);
		if ( spell and strlower(spell) == name ) then
			return i;
		end
	end
end

local function SecureCmdPetAutocast(msg, enable)
	local spell = SecureCmdOptionParse(msg);
	if ( not spell or spell == "" ) then
		return;
	end
	local slot = PetSpellSlot(PlainSpellName(spell));
	if ( not slot ) then
		return;
	end
	if ( enable == nil ) then
		ToggleSpellAutocast(slot, BOOKTYPE_PET);
		return;
	end
	local allowed, enabled = GetSpellAutocast(slot, BOOKTYPE_PET);
	if ( allowed and ((enabled and true or false) ~= enable) ) then
		ToggleSpellAutocast(slot, BOOKTYPE_PET);
	end
end

SlashCmdList["PET_AUTOCASTON"] = function(msg)
	SecureCmdPetAutocast(msg, true);
end

SlashCmdList["PET_AUTOCASTOFF"] = function(msg)
	SecureCmdPetAutocast(msg, false);
end

SlashCmdList["PET_AUTOCASTTOGGLE"] = function(msg)
	SecureCmdPetAutocast(msg, nil);
end

-- ---------------------------------------------------------------------
-- Give a contested command back to whoever already owned it
--
-- Vanilla has no `/petattack`, so the entry above ADDS it. A macro addon can
-- ship the same command under its own SlashCmdList key -- SuperCleveRoidMacros
-- registers `PETATTACK` where we register `PET_ATTACK`, both with a
-- `SLASH_*` string of "/petattack". `ChatEdit_ParseText` walks SlashCmdList
-- with `pairs` and takes the first match, so two keys claiming one command
-- make the winner hash order: a coin flip. When ours won, it fed
-- `[hastarget,alive,harm]` to SecureCmdOptionParse, which does not know that
-- addon's conditions, so the line matched nothing and the macro did nothing.
--
-- Rejecting a condition we cannot evaluate is right on its own -- retail does
-- the same -- so the error is owning the command at all. Hand it back. One
-- command split across two condition dialects would be worse than one owner,
-- because the dialects disagree: our `[@unit]`-only group passes only while
-- that unit exists, where 3.3.5's passes unconditionally.
--
-- Only commands we ADD are given up. A command we replace (`/cast`, `/target`)
-- has no second entry to lose to, and an addon that wraps ours afterwards
-- chains through us as it always did.
--
-- And only while the entry is still ours to give. Both halves of the decision
-- are snapshotted below at file scope, while the table holds exactly what this
-- file registered:
--
--   * The handler, so a key another addon has since taken OVER is left alone.
--     SuperCleveRoidMacros writes its own conditional handler into our
--     `SlashCmdList.CANCELAURA`, so clearing the key a frame later would delete
--     that addon's command instead of handing ours back. Taking a key over is
--     already how an addon wins a command; there is nothing left to resolve.
--   * The command strings, because an addon that takes a key over also writes
--     its own aliases into the same `SLASH_<key><i>` globals -- SCRM's
--     `SLASH_CANCELAURA2 = "/unbuff"` replaces ours. Read a frame later, the
--     test would run against a command we never registered, and a third addon
--     owning that command would make us surrender an entry over a collision
--     that is none of our business.
--
-- Resolved a frame later, not at file scope: this addon loads first by design,
-- so while it runs the other addon has not registered yet and there is nothing
-- to detect. It cannot wait on PLAYER_LOGIN either -- that has already fired
-- on a `/reload`, so `ContinueOnPlayerLogin` would run the check immediately
-- and see the same empty table. The addon load pass is synchronous, so the
-- next frame is after every non-demand addon has registered, on a cold login
-- and on a reload alike. An addon that loads on demand later keeps its own
-- entry and the coin flip with it.

-- The command strings a SlashCmdList key answers to, upper-cased the way
-- `ChatEdit_ParseText` compares them.
local function CommandStrings(key)
	local commands = {};
	local i = 1;
	while ( _G["SLASH_"..key..i] ) do
		commands[strupper(_G["SLASH_"..key..i])] = true;
		i = i + 1;
	end
	return commands;
end

local addedKeys = {};
local addedHandlers = {};
local addedCommands = {};
for key, handler in pairs(SlashCmdList) do
	if ( not preexistingKeys[key] ) then
		addedKeys[key] = true;
		addedHandlers[key] = handler;
		addedCommands[key] = CommandStrings(key);
	end
end

local function ClaimedElsewhere(key, commands)
	for otherKey in pairs(SlashCmdList) do
		if ( otherKey ~= key and not addedKeys[otherKey] ) then
			local i = 1;
			while ( _G["SLASH_"..otherKey..i] ) do
				if ( commands[strupper(_G["SLASH_"..otherKey..i])] ) then
					return true;
				end
				i = i + 1;
			end
		end
	end
	return false;
end

RunNextFrame(function()
	for key in pairs(addedKeys) do
		if ( SlashCmdList[key] == addedHandlers[key]
			and ClaimedElsewhere(key, addedCommands[key]) ) then
			SlashCmdList[key] = nil;
		end
	end
end)
