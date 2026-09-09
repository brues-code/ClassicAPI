
function RegisterNewSlashCommand(callback, command, commandAlias)
	local name = string.upper(command);
    _G["SLASH_"..name.."1"] = "/"..command;
    _G["SLASH_"..name.."2"] = "/"..commandAlias;
    SlashCmdList[name] = callback;
end

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
