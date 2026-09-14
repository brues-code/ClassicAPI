
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

-- Cast one resolved spell at one normalized target. Split out of
-- `SecureCmdCast` so `/castsequence` performs its step under exactly the same
-- rules: 1.12's `CastSpellByName` takes no unit, so every targeted form has to
-- go through `C_Spell.CastAtUnit`, and a sequence must not reinvent that.
local function SecureCmdPerformSpell(action, noToggle, target)
	if ( target == "cursor" ) then
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

-- Perform one action at one normalized target: an item the line names, else a
-- spell. `/cast`, `/use` and the random commands all land here, so the
-- item-before-spell rule and the `!` prefix are decided in one place.
local function SecureCmdPerformAction(action, target)
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
	local name, bag, slot = SecureCmdItemParse(action);
	if ( slot or (name and not IsBareNumber(action) and C_Item.GetItemCount(name) > 0) ) then
		SecureCmdUseItem(name, bag, slot, target);
	else
		SecureCmdPerformSpell(action, noToggle, target);
	end
end

local function SecureCmdCast(msg)
	local action, target = SecureCmdOptionParse(msg);
	if ( not action or action == "" ) then
		return;
	end
	target = SecureCmdNormalizeTarget(target);
	if ( target == false ) then
		return; -- a named unit with nobody around to match it
	end
	SecureCmdPerformAction(action, target);
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
-- /castsequence
--
-- One entry per sequence STRING (the text after the conditions), holding the
-- step index and the parsed action list. The string is the key on purpose:
-- the same sequence typed in two macros advances as one, which is how
-- FrameXML behaves and what lets the macro icon ask about a sequence it has
-- only the text of.
--
-- The icon side reads this through `QueryCastSequence`, which the macro
-- display calls whenever it needs the current step. That is Blizzard's own
-- split -- there the C macro code calls the same global by name -- so the
-- index lives here and nowhere else, and execution and display can never
-- disagree about which step is current.

local CastSequenceManager;
local CastSequenceTable = {};
local CastSequenceFreeList = {};

-- Cleared by the command-surrender pass at the bottom of this file if another
-- addon owns `/castsequence`. Our table only advances while OUR handler runs,
-- so once the command is theirs every index we hold is frozen at step one --
-- and the macro icon asks us, not them. Answering then would paint a
-- confidently wrong step on a sequence somebody else is advancing, so stop
-- answering instead and let the icon fall back to unresolved.
local castSequenceOwned = true;

local function Trim(text)
	return (string.gsub(text or "", "^%s*(.-)%s*$", "%1"));
end

-- The comma-separated pieces of a sequence, empty pieces included: a step
-- that resolves to nothing still occupies an index and is skipped at cast
-- time, so dropping it here would renumber the sequence.
local function SplitSequence(text)
	local pieces = {};
	for piece in string.gmatch(text..",", "([^,]*),") do
		table.insert(pieces, Trim(piece));
	end
	return pieces;
end

-- An action is an item when it names a bag or inventory slot, or when the
-- player is carrying one by that name. Same test `/cast` uses, and the same
-- limit: 1.12 can only look an item up by name among the ones you hold.
local function SequenceActionIsItem(action)
	local name, _, slot = SecureCmdItemParse(action);
	return slot ~= nil
		or (name ~= nil and not IsBareNumber(action) and C_Item.GetItemCount(name) > 0);
end

local function CreateCanonicalActions(entry, actions)
	entry.spells = {};
	entry.spellNames = {};
	entry.items = {};
	for i = 1, table.getn(actions) do
		local action = strlower(actions[i]);
		if ( SequenceActionIsItem(action) ) then
			entry.items[i] = action;
			entry.spells[i] = strlower(C_Item.GetItemSpell(action) or "");
			entry.spellNames[i] = entry.spells[i];
		else
			entry.spells[i] = action;
			-- `!Name` is a cast rule, not part of the name the cast events
			-- report back, so the matcher compares against the bare name.
			entry.spellNames[i] = string.gsub(action, "^!*(.*)$", "%1");
		end
	end
	entry.count = table.getn(actions);
end

local function SetCastSequenceIndex(entry, index)
	entry.index = index;
	entry.pending = nil;
end

local function ResetCastSequence(sequence, entry)
	SetCastSequenceIndex(entry, 1);
	CastSequenceFreeList[sequence] = entry;
	CastSequenceTable[sequence] = nil;
end

local function SetNextCastSequence(sequence, entry)
	if ( entry.index >= entry.count ) then
		ResetCastSequence(sequence, entry);
	else
		SetCastSequenceIndex(entry, entry.index + 1);
	end
end

-- A sequence restarts when the player holds the modifier its `reset=` names.
local function CastSequenceModifierReset(entry)
	return (IsShiftKeyDown() and string.find(entry.reset, "shift", 1, true))
		or (IsControlKeyDown() and string.find(entry.reset, "ctrl", 1, true))
		or (IsAltKeyDown() and string.find(entry.reset, "alt", 1, true));
end

local function ParseCastSequence(sequence)
	local reset, spells = string.match(sequence, "^reset=([^%s]+)%s*(.*)$");
	if ( not reset ) then
		spells = sequence;
	end
	local entry = {};
	CreateCanonicalActions(entry, SplitSequence(spells));
	entry.reset = strlower(reset or "");
	return entry;
end

-- The cast events carry the spell in different argument positions depending on
-- the event, so read the name and rank per event rather than by one offset.
local function CastEventSpell()
	if ( event == "UNIT_SPELLCAST_SENT" ) then
		return arg1, arg5, arg6; -- unit, target, castGUID, spellID, name, rank
	end
	return arg1, arg4, arg5;     -- unit, castGUID, spellID, name, rank
end

local function CastSequenceManager_OnEvent()
	-- Death restarts every sequence.
	if ( event == "PLAYER_DEAD" ) then
		for sequence, entry in pairs(CastSequenceTable) do
			ResetCastSequence(sequence, entry);
		end
		return;
	end

	if ( event == "UNIT_SPELLCAST_SENT"
		or event == "UNIT_SPELLCAST_SUCCEEDED"
		or event == "UNIT_SPELLCAST_INTERRUPTED"
		or event == "UNIT_SPELLCAST_FAILED"
		or event == "UNIT_SPELLCAST_FAILED_QUIET" ) then
		local unit, name, rank = CastEventSpell();
		if ( not name ) then
			-- A server-side spell with no name of its own. Nothing in any
			-- sequence can match it, so leave every index alone.
			return;
		end
		if ( unit == "player" or unit == "pet" ) then
			name, rank = strlower(name), strlower(rank or "");
			local nameplus = name.."()";
			local fullname = name.."("..rank..")";
			for sequence, entry in pairs(CastSequenceTable) do
				local entryName = entry.spellNames[entry.index];
				if ( entryName == name or entryName == nameplus or entryName == fullname ) then
					if ( event == "UNIT_SPELLCAST_SENT" ) then
						-- In flight. Hold the index so a second press
						-- cannot skip a step the server has not answered.
						entry.pending = 1;
					else
						entry.pending = nil;
						if ( event == "UNIT_SPELLCAST_SUCCEEDED" ) then
							SetNextCastSequence(sequence, entry);
						end
					end
				end
			end
		end
		return;
	end

	local reset = "";
	if ( event == "PLAYER_TARGET_CHANGED" ) then
		reset = "target";
	elseif ( event == "PLAYER_REGEN_ENABLED" ) then
		reset = "combat";
	end
	for sequence, entry in pairs(CastSequenceTable) do
		if ( string.find(entry.reset, reset, 1, true) ) then
			ResetCastSequence(sequence, entry);
		end
	end
end

local function CastSequenceManager_OnUpdate()
	local elapsed = CastSequenceManager.elapsed + (arg1 or 0);
	if ( elapsed < 1 ) then
		CastSequenceManager.elapsed = elapsed;
		return;
	end
	for sequence, entry in pairs(CastSequenceTable) do
		if ( entry.timeout ) then
			if ( elapsed >= entry.timeout ) then
				ResetCastSequence(sequence, entry);
			else
				entry.timeout = entry.timeout - elapsed;
			end
		end
	end
	CastSequenceManager.elapsed = 0;
end

local function CastSequenceEntry(sequence)
	local entry = CastSequenceTable[sequence];
	if ( entry ) then
		return entry;
	end
	entry = CastSequenceFreeList[sequence] or ParseCastSequence(sequence);
	CastSequenceTable[sequence] = entry;
	entry.index = entry.index or 1;
	return entry;
end

local function ExecuteCastSequence(sequence, target)
	if ( not CastSequenceManager ) then
		CastSequenceManager = CreateFrame("Frame");
		CastSequenceManager.elapsed = 0;
		CastSequenceManager:RegisterEvent("PLAYER_DEAD");
		CastSequenceManager:RegisterEvent("UNIT_SPELLCAST_SENT");
		CastSequenceManager:RegisterEvent("UNIT_SPELLCAST_SUCCEEDED");
		CastSequenceManager:RegisterEvent("UNIT_SPELLCAST_INTERRUPTED");
		CastSequenceManager:RegisterEvent("UNIT_SPELLCAST_FAILED");
		CastSequenceManager:RegisterEvent("UNIT_SPELLCAST_FAILED_QUIET");
		CastSequenceManager:RegisterEvent("PLAYER_TARGET_CHANGED");
		CastSequenceManager:RegisterEvent("PLAYER_REGEN_ENABLED");
		CastSequenceManager:SetScript("OnEvent", CastSequenceManager_OnEvent);
		CastSequenceManager:SetScript("OnUpdate", CastSequenceManager_OnUpdate);
	end

	local entry = CastSequenceEntry(sequence);

	-- A step already sent and not yet answered keeps the sequence where it is.
	if ( entry.pending ) then
		return;
	end

	if ( CastSequenceModifierReset(entry) ) then
		SetCastSequenceIndex(entry, 1);
	end

	-- The timeout counts from the last use, so every press renews it.
	local timeout = string.match(entry.reset, "(%d+)");
	if ( timeout ) then
		entry.timeout = CastSequenceManager.elapsed + tonumber(timeout);
	end

	local item, spell = entry.items[entry.index], entry.spells[entry.index];
	if ( item ) then
		local name, bag, slot = SecureCmdItemParse(item);
		if ( slot ) then
			-- A slot names whatever is in it right now, so the spell the
			-- matcher waits for is re-read on every use.
			spell = name and strlower(C_Item.GetItemSpell(name) or "") or "";
			entry.spellNames[entry.index] = spell;
		end
		if ( C_Item.IsEquippableItem(name) and not C_Item.IsEquippedItem(name) ) then
			C_Item.EquipItemByName(name);
		else
			SecureCmdUseItem(name, bag, slot, target);
		end
	else
		local noToggle;
		if ( string.sub(spell or "", 1, 1) == "!" ) then
			noToggle = true;
			spell = string.sub(spell, 2);
		end
		SecureCmdPerformSpell(spell, noToggle, target);
	end
	if ( spell == "" ) then
		-- Nothing castable at this step: step past it so the sequence cannot
		-- stall on an item the player no longer carries.
		SetNextCastSequence(sequence, entry);
	end
end

-- The step a sequence is on, without advancing it. Returns the index, the
-- item the step names (nil for a spell), and the spell name. The macro
-- display calls this by name.
function QueryCastSequence(sequence)
	if ( not castSequenceOwned ) then
		return;
	end
	local index = 1;
	local item, spell;
	local entry = CastSequenceTable[sequence];
	if ( entry ) then
		if ( not CastSequenceModifierReset(entry) ) then
			index = entry.index;
		end
		item, spell = entry.items[index], entry.spells[index];
	else
		entry = CastSequenceFreeList[sequence];
		if ( not entry ) then
			-- Never used yet: parse it so the icon shows step one instead of
			-- the question mark, but do not make it live.
			entry = ParseCastSequence(sequence);
		end
		item, spell = entry.items[index], entry.spells[index];
	end
	if ( item ) then
		local name, _, slot = SecureCmdItemParse(item);
		if ( slot ) then
			spell = name and strlower(C_Item.GetItemSpell(name) or "") or "";
		end
	end
	return index, item, spell;
end

SlashCmdList["CASTSEQUENCE"] = function(msg)
	local sequence, target = SecureCmdOptionParse(msg);
	if ( not sequence or sequence == "" ) then
		return;
	end
	target = SecureCmdNormalizeTarget(target);
	if ( target == false ) then
		return; -- a named unit with nobody around to match it
	end
	ExecuteCastSequence(sequence, target);
end
SLASH_CASTSEQUENCE1 = "/castsequence";

-- ---------------------------------------------------------------------
-- /castrandom and /userandom
--
-- One pick per action list, held until a cast SUCCEEDS. A failed or
-- interrupted cast keeps the same pick, so a spell that was out of range or
-- out of mana is retried rather than swapped for another one -- pressing the
-- button again means "try that again", not "roll again". `/userandom` is the
-- same command as `/castrandom`, the way `/use` is the same as `/cast`: both
-- perform whatever the chosen action turns out to be.

local CastRandomManager;
local CastRandomTable = {};

local function CastRandomManager_OnEvent()
	local unit, name, rank = CastEventSpell();
	if ( not name or unit ~= "player" ) then
		return;
	end
	name, rank = strlower(name), strlower(rank or "");
	local nameplus = name.."()";
	local fullname = name.."("..rank..")";
	for _, entry in pairs(CastRandomTable) do
		if ( entry.pending and entry.value ) then
			local entryName = strlower(entry.value);
			if ( entryName == name or entryName == nameplus or entryName == fullname ) then
				entry.pending = nil;
				if ( event == "UNIT_SPELLCAST_SUCCEEDED" ) then
					entry.value = nil; -- rolled again on the next press
				end
			end
		end
	end
end

-- The action this press uses. Nil when the list holds nothing to pick from.
local function ExecuteCastRandom(actions)
	if ( not CastRandomManager ) then
		CastRandomManager = CreateFrame("Frame");
		CastRandomManager:RegisterEvent("UNIT_SPELLCAST_SUCCEEDED");
		CastRandomManager:RegisterEvent("UNIT_SPELLCAST_INTERRUPTED");
		CastRandomManager:RegisterEvent("UNIT_SPELLCAST_FAILED");
		CastRandomManager:RegisterEvent("UNIT_SPELLCAST_FAILED_QUIET");
		CastRandomManager:SetScript("OnEvent", CastRandomManager_OnEvent);
	end

	local entry = CastRandomTable[actions];
	if ( not entry ) then
		-- Only the raw pieces are kept. The matcher compares the chosen text
		-- against the cast events, so the canonical item/spell split the
		-- sequences build has nothing to do here.
		entry = { list = SplitSequence(actions) };
		CastRandomTable[actions] = entry;
	end
	if ( not entry.value ) then
		local count = table.getn(entry.list);
		if ( count == 0 ) then
			return nil;
		end
		entry.value = entry.list[math.random(count)];
	end
	entry.pending = true;
	return entry.value;
end

local function SecureCmdCastRandom(msg)
	local actions, target = SecureCmdOptionParse(msg);
	if ( not actions or actions == "" ) then
		return;
	end
	target = SecureCmdNormalizeTarget(target);
	if ( target == false ) then
		return; -- a named unit with nobody around to match it
	end
	local action = ExecuteCastRandom(actions);
	if ( action and action ~= "" ) then
		SecureCmdPerformAction(action, target);
	end
end

SlashCmdList["CASTRANDOM"] = SecureCmdCastRandom;
SLASH_CASTRANDOM1 = "/castrandom";
SlashCmdList["USERANDOM"] = SecureCmdCastRandom;
SLASH_USERANDOM1 = "/userandom";

-- `/stopmacro [conditions]` -- stop before the rest of the body runs. The flag
-- belongs to the macro runner, which clears it for each body it starts, so a
-- bare call outside a macro does nothing and cannot leave one armed.
--
-- Through the mirror, not the bare global. `StopMacro` is a plain name in a
-- crowded space, and the global is only ours until an addon declares one of
-- its own -- at which point this command would set THEIR flag, ours would
-- stay clear, and the rest of the macro would keep running with nothing to
-- show for it. The mirror captured our function at registration, before
-- FrameXML and every addon.
local StopMacroSelf = ClassicAPI.StopMacro;

SlashCmdList["STOPMACRO"] = function(msg)
	if ( SecureCmdOptionParse(msg) ) then
		StopMacroSelf();
	end
end
SLASH_STOPMACRO1 = "/stopmacro";

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
			if ( key == "CASTSEQUENCE" ) then
				-- The sequence is theirs to advance now, so stop reporting a
				-- step to the macro icon that we no longer track.
				castSequenceOwned = false;
			end
		end
	end
end)
