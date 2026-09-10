
function RegisterNewSlashCommand(callback, command, commandAlias)
	local name = string.upper(command);
    _G["SLASH_"..name.."1"] = "/"..command;
    _G["SLASH_"..name.."2"] = "/"..commandAlias;
    SlashCmdList[name] = callback;
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

-- `@unit` and `target=unit` accept a character name as well as a unit token
-- (`[target=Feral]`), and the engine's unit functions take only tokens.
-- Resolve once here so every command below hands them something they accept.
-- Returns nil for a name with nobody around to match it.
local function SecureCmdTargetUnit(target)
	if ( not target or target == "" or IsUnitToken(target) ) then
		return target;
	end
	return UnitTokenFromName(target);
end

function SecureCmdUseItem(name, bag, slot, target)
	if ( target == "cursor" ) then
		-- `@cursor` names a world position, so a ground-target item places
		-- its effect there. `SecureCmdItemParse` has already turned a bag or
		-- inventory slot into a link, so one call covers every form. An item
		-- with no ground effect is used normally.
		if ( name ) then
			C_Item.UseAtCursor(name);
		end
	elseif ( target == "player" and name ) then
		-- `@player` drops a ground-target item at your own feet. Only your
		-- own position is offered this way; aiming one at another unit is
		-- not something you can do by hand either. An item with no ground
		-- effect is used on you, as before.
		C_Item.UseAtUnit(name, "player");
	elseif ( bag ) then
		UseContainerItem(bag, slot, target == "player");
	elseif ( slot ) then
		UseInventoryItem(slot);
	elseif ( name ) then
		C_Item.UseItemByName(name, target);
	end
end

local function SecureCmdCast(msg)
	local action, target = SecureCmdOptionParse(msg);
	if ( not action or action == "" ) then
		return;
	end
	if ( target and target ~= "" and target ~= "cursor" ) then
		target = SecureCmdTargetUnit(target);
		if ( not target ) then
			return; -- a named unit with nobody around to match it
		end
	end
	local name, bag, slot = SecureCmdItemParse(action);
	if ( slot or (name and C_Item.GetItemCount(name) > 0) ) then
		SecureCmdUseItem(name, bag, slot, target);
	elseif ( target == "cursor" ) then
		C_Spell.CastAtCursor(action);
	elseif ( not target or target == "target" ) then
		CastSpellByName(action);
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
	if ( not target ) then
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
-- `/targetenemy [mod:shift] 1` steps backwards while shift is held.
local function SecureCmdTargetCycle(msg, fn)
	local action = SecureCmdOptionParse(msg);
	if ( action ) then
		fn(action);
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
