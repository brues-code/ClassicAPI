-- GetBindingText for gamepad keys, as the 3.80.2 Classic client's C
-- GetBindingText (0x141251250) writes them. This client's GetBindingText is
-- FrameXML Lua (UIParent.lua) and keeps every other key.
--
-- A gamepad key is a key with "PAD" at its start or after a '-'
-- (IsBindingForGamePad). Its text:
--   - the button is KEY_<button><suffix> (KEY_ABBR_ when abbreviated), else
--     the name without the suffix, then the usual KEY_ / _KEY_TEXT names. The
--     suffix is the active gamepad's label style: _LTR (Letters), _REV
--     (Reverse), _SHP (Shapes), none for Generic or without a gamepad;
--   - each modifier is the glyph of the button that emulates it
--     (GamePadEmulateShift / Ctrl / Alt), else its <MOD>_KEY_TEXT or its name;
--   - the modifiers are sorted by 3.80.2's table (0x143B835E0, by the
--     emulating button; a modifier no button emulates comes first) and joined
--     with "+", with nothing between them when abbreviated.

local BindingText = GetBindingText;

local LABEL_STYLE_SUFFIX = { Letters = "_LTR", Reverse = "_REV", Shapes = "_SHP" };

-- By button index 0..31.
local MODIFIER_ORDER = {
	[0] = 0, 1, 0, -1, 0, 0, 0, 0, 0, 0, -2, 2, -3, 3, -3, 3,
	-2, -2, -2, -2, 2, 2, 2, 2, 0, 0, 0, 0, 1, -1, 0, 0,
};

local function StyleSuffix()
	local state = C_GamePad.GetDeviceMappedState(C_GamePad.GetActiveDeviceID());
	return state and LABEL_STYLE_SUFFIX[state.labelStyle] or "";
end

local function Global(name)
	local text = getglobal(name);
	if type(text) == "string" then
		return text;
	end
end

local function ModifierText(token, abbreviate)
	return Global(token .. (abbreviate and "_KEY_TEXT_ABBR" or "_KEY_TEXT")) or token;
end

local function KeyText(name, abbreviate, suffix)
	local prefix = abbreviate and "KEY_ABBR_" or "KEY_";
	return Global(prefix .. name .. suffix)
		or Global(prefix .. name)
		or (abbreviate and Global("KEY_ABBR_" .. name))
		or Global("KEY_" .. name)
		or (abbreviate and Global(name .. "_KEY_TEXT_ABBR"))
		or Global(name .. "_KEY_TEXT")
		or name;
end

local function EmulateCVar(token)
	if strfind(token, "SHIFT", 1, true) then
		return "GamePadEmulateShift";
	elseif strfind(token, "CTRL", 1, true) then
		return "GamePadEmulateCtrl";
	elseif strfind(token, "ALT", 1, true) then
		return "GamePadEmulateAlt";
	end
end

local function GamePadBindingText(key, abbreviate)
	local suffix = StyleSuffix();
	local tokens = {};
	for token in string.gmatch(key, "[^-]+") do
		table.insert(tokens, token);
	end
	local button = table.remove(tokens);
	local modifiers = {};
	for i, token in ipairs(tokens) do
		local cvar = EmulateCVar(token);
		local value = cvar and GetCVar(cvar);
		local index = value and C_GamePad.ButtonBindingToIndex(value);
		local entry = { order = -4, position = i };
		if index then
			local prefix = abbreviate and "KEY_ABBR_" or "KEY_";
			entry.text = Global(prefix .. value .. suffix) or Global(prefix .. value) or value;
			entry.order = MODIFIER_ORDER[index] or 0;
		else
			entry.text = ModifierText(token, abbreviate);
		end
		table.insert(modifiers, entry);
	end
	table.sort(modifiers, function(a, b)
		if a.order ~= b.order then
			return a.order < b.order;
		end
		return a.position < b.position;
	end);
	local separator = abbreviate and "" or "+";
	local text = "";
	for _, entry in ipairs(modifiers) do
		text = text .. entry.text .. separator;
	end
	return text .. KeyText(button or "", abbreviate, suffix);
end

function GetBindingText(name, prefix, returnAbbr)
	if type(name) == "string" and IsBindingForGamePad(name) then
		return GamePadBindingText(name, returnAbbr and true or false);
	end
	return BindingText(name, prefix, returnAbbr);
end
