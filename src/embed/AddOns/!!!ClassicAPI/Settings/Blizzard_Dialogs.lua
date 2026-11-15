-- Backport of retail's Blizzard_Settings_Shared/Blizzard_Dialogs.lua.
--
-- Differences from the retail source:
--   - 1.12's StaticPopup has OnAccept for button1, OnCancel for button2 and,
--     on clients whose StaticPopup has a third button (Turtle's), OnAlt for
--     button3, which it shows between the other two. It has no
--     OnButton1..3 or selectCallbackByIndex, and Escape runs OnCancel as if
--     button2 was clicked. So button2 is always the choice that changes
--     nothing: the discard dialog's Apply and Exit is its button3. With the
--     third button both dialogs show retail's three choices in retail's
--     order. A client without it shows only the first two: the defaults
--     dialog then has no These Settings choice, and the discard dialog has no
--     Apply and Exit (the window's own Apply button still applies).
--   - No timed revert dialog: no revertable settings are ported (see
--     Blizzard_SettingsPanel.lua).
--   - No key binding reset dialog: there is no Keybindings category yet.

local function DefineGameSettingsApplyDefaultsDialog()
	StaticPopupDialogs["GAME_SETTINGS_APPLY_DEFAULTS"] = {
		text = CONFIRM_RESET_INTERFACE_SETTINGS,
		button1 = ALL_SETTINGS,
		button3 = CURRENT_SETTINGS,
		button2 = CANCEL,
		OnAccept = function()
			SettingsPanel:SetAllSettingsToDefaults();
		end,
		OnAlt = function()
			SettingsPanel:SetCurrentCategorySettingsToDefaults();
		end,
		OnCancel = function() end,
		hideOnEscape = 1,
		whileDead = 1,
		timeout = 0,
	}
end

local function DefineGameSettingsConfirmDiscardDialog()
	StaticPopupDialogs["GAME_SETTINGS_CONFIRM_DISCARD"] = {
		text = SETTINGS_CONFIRM_DISCARD,
		button1 = SETTINGS_UNAPPLIED_EXIT,
		button3 = SETTINGS_UNAPPLIED_APPLY_AND_EXIT,
		button2 = SETTINGS_UNAPPLIED_CANCEL,
		OnAccept = function()
			SettingsPanel:ExitWithoutCommit();
		end,
		OnAlt = function()
			SettingsPanel:ExitWithCommit();
		end,
		OnCancel = function() end,
		hideOnEscape = 1,
		whileDead = 1,
		timeout = 0,
	}
end

DefineGameSettingsApplyDefaultsDialog();
DefineGameSettingsConfirmDiscardDialog();
