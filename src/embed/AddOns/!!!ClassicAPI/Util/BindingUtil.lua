-- Backport of retail's Blizzard_SharedXML/BindingUtil.lua.
--
-- Differences from the retail source:
--   - Binding names are globals: this client has no GetCurrentEnvironment.
--   - No META in a key chord: this client has no IsMetaKeyDown.
--   - Mouse buttons stop at Button5: this client binds five.
--   - Binding text goes through 1.12's GetBindingText with the "KEY_"
--     prefix. Retail's GetBindingText uses that prefix by itself; 1.12's,
--     from FrameXML, uses no prefix unless it is given one.
--   - No custom binding buttons (CustomBindingManager), no quick keybind
--     mode and no mouseover casting: this client has none of them.

BindingUtil = {};

local metaKeys =
{
	LALT = 1,
	RALT = 2,
	LCTRL = 3,
	RCTRL = 4,
	LSHIFT = 5,
	RSHIFT = 6,
	LMETA = 7,
	RMETA = 8,
	ALT = 9,
	CTRL = 10,
	SHIFT = 11,
	META = 12,
};

local ignoredKeys =
{
	UNKNOWN = true,
	BUTTON1 = true,
	BUTTON2 = true,
};

local mouseButtonNameConversion =
{
	LeftButton = "BUTTON1",
	RightButton = "BUTTON2",
	MiddleButton = "BUTTON3",
	Button1 = "BUTTON1",
	Button2 = "BUTTON2",
	Button3 = "BUTTON3",
	Button4 = "BUTTON4",
	Button5 = "BUTTON5",
};

function GetConvertedKeyOrButton(keyOrButton)
	return mouseButtonNameConversion[keyOrButton] or keyOrButton;
end

function IsMouseButton(button)
	return mouseButtonNameConversion[button] ~= nil;
end

function IsLeftMouseButton(button)
	return mouseButtonNameConversion[button] == "BUTTON1";
end

function IsRightMouseButton(button)
	return mouseButtonNameConversion[button] == "BUTTON2";
end

function IsMiddleMouseButton(button)
	return mouseButtonNameConversion[button] == "BUTTON3";
end

function IsMetaKey(key)
	return metaKeys[key] ~= nil;
end

function IsKeyPressIgnoredForBinding(key)
	return IsMetaKey(key) or ignoredKeys[key] == true;
end

local function KeyComparator(key1, key2)
	local key1Priority = metaKeys[key1];
	local key2Priority = metaKeys[key2];
	if key1Priority and key2Priority then
		return key1Priority < key2Priority;
	end

	return key1Priority ~= nil;
end

function CreateKeyChordStringFromTable(keys, preventSort)
	if not preventSort then
		table.sort(keys, KeyComparator);
	end

	return table.concat(keys, "-");
end

function CreateKeyChordString(key, ...)
	return CreateKeyChordStringFromTable({ key, ... });
end

function CreateKeyChordStringUsingMetaKeyState(key)
	local chord = {};
	if IsAltKeyDown() then
		table.insert(chord, "ALT");
	end

	if IsControlKeyDown() then
		table.insert(chord, "CTRL");
	end

	if IsShiftKeyDown() then
		table.insert(chord, "SHIFT");
	end

	if not IsMetaKey(key) then
		table.insert(chord, key);
	end

	local preventSort = true;
	return CreateKeyChordStringFromTable(chord, preventSort);
end

function GetBindingName(binding)
	local bindingName = _G["BINDING_NAME_"..binding];
	if ( bindingName ) then
		return bindingName;
	end

	return binding;
end

function GetBindingKeyForAction(action, useNotBound, useParentheses)
	local key = GetBindingKey(action);
	if key then
		key = GetBindingText(key, "KEY_");
	elseif useNotBound then
		key = NOT_BOUND;
	end

	if key and useParentheses then
		return ("(%s)"):format(key);
	end

	return key;
end

-- Gets the key string for the action and formats it into keyStringFormat, then formats
-- that and the original text into bindingAvailableFormat and returns the resulting string.
-- NOTE: The argument ordering for bindingAvailableFormat should be original text and then binding string text.
-- NOTE: If there is no binding and useNotBound is set, then the NOT_BOUND string is formatted into the location
-- where the key string would normally appear, otherwise only the original text is returned.
function FormatBindingKeyIntoText(text, action, bindingAvailableFormat, keyStringFormat, useNotBound, useParentheses)
	local bindingKey = GetBindingKeyForAction(action, useNotBound, useParentheses);

	if bindingKey then
		bindingAvailableFormat = bindingAvailableFormat or "%s %s";
		keyStringFormat = keyStringFormat or "%s";
		local keyString = keyStringFormat:format(bindingKey);
		return bindingAvailableFormat:format(text, keyString);
	end

	return text;
end

function MicroButtonTooltipText(text, action)
	local keyStringFormat = WrapTextInColor("(%s)", NORMAL_FONT_COLOR);
	local bindingAvailableFormat = "%s %s";
	return FormatBindingKeyIntoText(text, action, bindingAvailableFormat, keyStringFormat);
end

function BindingButtonTemplate_SetSelected(keyBindingButton, isSelected)
	keyBindingButton.selectedHighlight:SetShown(isSelected);
	keyBindingButton.isSelected = isSelected;

	if isSelected then
		keyBindingButton:GetHighlightTexture():SetAlpha(0);
	else
		keyBindingButton:GetHighlightTexture():SetAlpha(1);
	end

	return isSelected;
end

function BindingButtonTemplate_ToggleSelected(keyBindingButton)
	return BindingButtonTemplate_SetSelected(keyBindingButton, not keyBindingButton.isSelected);
end

function BindingButtonTemplate_IsSelected(keyBindingButton)
	return keyBindingButton.isSelected;
end

function BindingButtonTemplate_SetupBindingButton(binding, button)
	local bindingText;

	if binding then
		bindingText = GetBindingText(binding, "KEY_");
	end

	if bindingText then
		button:SetText(bindingText);
		button:SetAlpha(1);
	else
		button:SetText(GRAY_FONT_COLOR:WrapTextInColorCode(NOT_BOUND));
		button:SetAlpha(0.8);
	end
	return bindingText;
end
