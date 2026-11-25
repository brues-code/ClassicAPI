-- Gamepad buttons in 1.12.1's Key Bindings window (Blizzard_BindingUI), as
-- retail's binding window takes them: a pad button goes through the same
-- handler as a key, so it binds with the modifiers held ("SHIFT-PAD1").
--
-- KeyBindingFrame_OnKeyDown keeps any button name it does not convert from a
-- mouse name, so it needs no change. Setting the script turns on gamepad
-- button input for the frame, and the open window takes every pad button, as
-- it takes every key.

EventUtil.ContinueOnAddOnLoaded("Blizzard_BindingUI", function()
	KeyBindingFrame:SetScript("OnGamePadButtonDown", function(self, button)
		KeyBindingFrame_OnKeyDown(button);
	end);
end);
