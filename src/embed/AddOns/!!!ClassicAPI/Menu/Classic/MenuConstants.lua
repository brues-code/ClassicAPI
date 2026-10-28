-- Backport of retail's Blizzard_Menu/Classic/MenuConstants.lua.
--
-- Implementation differences from the retail source:
--   - The Classic filter dropdown atlases are art 1.12 does not have. Each
--     state names the panel button texture instead; the hover states are the
--     button's own highlight texture (see Classic/MenuTemplates.xml).

WowStyle1FilterDropdownStateDownOver = "Interface\\Buttons\\UI-Panel-Button-Down";
WowStyle1FilterDropdownStateOver = "Interface\\Buttons\\UI-Panel-Button-Up";
WowStyle1FilterDropdownStateDown = "Interface\\Buttons\\UI-Panel-Button-Down";
WowStyle1FilterDropdownStateEnabled = "Interface\\Buttons\\UI-Panel-Button-Up";
WowStyle1FilterDropdownStateOpen = WowStyle1FilterDropdownStateEnabled;
WowStyle1FilterDropdownStateDisabled = "Interface\\Buttons\\UI-Panel-Button-Disabled";
