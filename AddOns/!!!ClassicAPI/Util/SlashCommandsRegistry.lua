
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

function SecureCmdUseItem(name, bag, slot, target)
	if ( bag ) then
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
	local name, bag, slot = SecureCmdItemParse(action);
	if ( slot or (name and C_Item.GetItemCount(name) > 0) ) then
		SecureCmdUseItem(name, bag, slot, target);
	elseif ( not target or target == "target" ) then
		CastSpellByName(action);
	else
		C_Spell.CastAtUnit(action, target);
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
			FocusUnit(target);
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
		StartAttack(target);
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
