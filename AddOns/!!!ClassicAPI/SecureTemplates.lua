-- SecureTemplates.lua — Lua-side helpers for SecureActionButtonTemplate et al.
--
-- Backport of Blizzard's 2.0 SecureTemplates.lua for 1.12. Provides the
-- modifier/button attribute resolution functions that addons call directly
-- (SecureButton_GetModifiedAttribute, SecureButton_GetModifierPrefix, etc.)
-- and the OnClick dispatchers referenced by SecureTemplates.xml.
--
-- The heavy click-dispatch logic (target, spell, item, macro, focus, etc.)
-- is handled natively by the C++ Frame::Attributes module for performance
-- and correctness on 1.12. The Lua SecureActionButton_OnClick here acts as
-- a compatibility entry point for addons that invoke it directly; for frames
-- with a C++-installed OnClick closure (any frame with a "type*" attribute),
-- the C++ path takes priority and only chains here for unhandled clicks.

-- The "modified attribute" takes the form of: modifier-name-button
-- The modifier is one of "shift-", "ctrl-", "alt-", and the button is a
-- number from 1 through 5.
--
-- Setting the attribute by itself is equivalent to *attribute*

ATTRIBUTE_NOOP = "";

function SecureButton_GetModifierPrefix()
    local prefix = "";
    if IsShiftKeyDown() then
        prefix = "shift-" .. prefix;
    end
    if IsControlKeyDown() then
        prefix = "ctrl-" .. prefix;
    end
    if IsAltKeyDown() then
        prefix = "alt-" .. prefix;
    end
    return prefix;
end

function SecureButton_GetButtonSuffix(button)
    if button == "LeftButton" then
        return "1";
    elseif button == "RightButton" then
        return "2";
    elseif button == "MiddleButton" then
        return "3";
    elseif button == "Button4" then
        return "4";
    elseif button == "Button5" then
        return "5";
    elseif button and button ~= "" then
        return "-" .. tostring(button);
    end
    return "";
end

function SecureButton_GetModifiedAttribute(frame, name, button, prefix, suffix)
    if not prefix then
        prefix = SecureButton_GetModifierPrefix();
    end
    if not suffix then
        suffix = SecureButton_GetButtonSuffix(button);
    end
    local value = frame:GetAttribute(prefix .. name .. suffix) or
                  frame:GetAttribute("*" .. name .. suffix) or
                  frame:GetAttribute(prefix .. name .. "*") or
                  frame:GetAttribute("*" .. name .. "*") or
                  frame:GetAttribute(name);
    if not value and (frame:GetAttribute("useparent-" .. name) or
                      frame:GetAttribute("useparent*")) then
        local parent = frame:GetParent();
        if parent then
            value = SecureButton_GetModifiedAttribute(parent, name, button, prefix, suffix);
        end
    end
    if value == ATTRIBUTE_NOOP then
        value = nil;
    end
    return value;
end

-- Unmodified attribute lookup (no modifier prefix, no button suffix).
-- Overrides the simpler version in SecureStateDriver.lua with the full
-- parent-walk + ATTRIBUTE_NOOP semantics.
function SecureButton_GetAttribute(frame, name)
    return SecureButton_GetModifiedAttribute(frame, name, nil, "", "");
end

function SecureButton_GetModifiedUnit(self, button)
    local unit = SecureButton_GetModifiedAttribute(self, "unit", button);
    if unit then
        local unitsuffix = SecureButton_GetModifiedAttribute(self, "unitsuffix", button);
        if unitsuffix then
            unit = unit .. unitsuffix;
            unit = string.gsub(unit, "^([^%d]+)([%d]+)[pP][eE][tT]", "%1pet%2");
        end
        return unit;
    end
    if SecureButton_GetModifiedAttribute(self, "checkselfcast", button) then
        if IsAltKeyDown() then
            return "player";
        end
    end
end

function SecureButton_GetUnit(self)
    local unit = SecureButton_GetAttribute(self, "unit");
    if unit then
        local unitsuffix = SecureButton_GetAttribute(self, "unitsuffix");
        if unitsuffix then
            unit = unit .. unitsuffix;
            unit = string.gsub(unit, "^([^%d]+)([%d]+)[pP][eE][tT]", "%1pet%2");
        end
        return unit;
    end
end

-- ---------------------------------------------------------------------------
-- SecureActionButton_OnClick
--
-- The primary click dispatcher. Called from the SecureActionButtonTemplate's
-- XML <OnClick> handler. The C++ Frame::Attributes module installs its own
-- native closure over this when SetAttribute("type*", ...) is called —
-- that closure takes priority and only chains here for unhandled clicks.
-- This Lua version exists for:
--   1. Addons that call SecureActionButton_OnClick(frame, button) directly
--   2. Frames where the C++ closure hasn't been installed yet
-- ---------------------------------------------------------------------------

function SecureActionButton_OnClick(self, button)
    -- Lookup the unit, based on the modifiers and button
    local unit = SecureButton_GetModifiedUnit(self, button);

    -- Don't do anything if our unit doesn't exist
    if unit and unit ~= "none" and not UnitExists(unit) then
        return;
    end

    -- Remap button suffixes based on disposition
    if unit then
        local origButton = button;
        if UnitCanAttack("player", unit) then
            button = SecureButton_GetModifiedAttribute(self, "harmbutton", button) or button;
        elseif UnitCanAssist("player", unit) then
            button = SecureButton_GetModifiedAttribute(self, "helpbutton", button) or button;
        end

        if button ~= origButton then
            unit = SecureButton_GetModifiedUnit(self, button);
            if unit and unit ~= "none" and not UnitExists(unit) then
                return;
            end
        end
    end

    -- Lookup the action type
    local actionType = SecureButton_GetModifiedAttribute(self, "type", button);

    if actionType == "action" then
        local action = SecureButton_GetModifiedAttribute(self, "action", button);
        if action then
            UseAction(action, unit, button);
        end
    elseif actionType == "pet" then
        local action = SecureButton_GetModifiedAttribute(self, "action", button);
        if action then
            CastPetAction(action, unit);
        end
    elseif actionType == "spell" then
        local spell = SecureButton_GetModifiedAttribute(self, "spell", button);
        if spell then
            CastSpellByName(spell, unit);
        end
    elseif actionType == "item" then
        local bag = SecureButton_GetModifiedAttribute(self, "bag", button);
        local slot = SecureButton_GetModifiedAttribute(self, "slot", button);
        if slot then
            if bag then
                UseContainerItem(bag, slot, unit);
            else
                UseInventoryItem(slot, unit);
            end
        else
            local item = SecureButton_GetModifiedAttribute(self, "item", button);
            if item then
                UseItemByName(item, unit);
            end
        end
    elseif actionType == "macro" then
        local macro = SecureButton_GetModifiedAttribute(self, "macro", button);
        if macro then
            RunMacro(macro, button);
        else
            local text = SecureButton_GetModifiedAttribute(self, "macrotext", button);
            if text and RunMacroText then
                RunMacroText(text, button);
            end
        end
    elseif actionType == "stop" then
        if SpellIsTargeting() then
            SpellStopTargeting();
        end
    elseif actionType == "target" then
        if unit then
            if SpellIsTargeting() then
                SpellTargetUnit(unit);
            elseif CursorHasItem() then
                DropItemOnUnit(unit);
            else
                TargetUnit(unit);
            end
        end
    elseif actionType == "focus" then
        if unit then
            FocusUnit(unit);
        end
    elseif actionType == "assist" then
        if unit then
            AssistUnit(unit);
        end
    elseif actionType == "click" then
        local delegate = SecureButton_GetModifiedAttribute(self, "clickbutton", button);
        if delegate then
            delegate:Click(button);
        end
    elseif actionType == "menu" or actionType == "togglemenu" then
        if self.menu then
            self.menu(self, unit);
        elseif unit then
            ClassicAPI_ToggleUnitMenu(unit);
        end
    elseif actionType then
        -- Custom action support
        local func = rawget(self, actionType);
        if func then
            func(self, unit, button);
        end
    end

    -- Target predefined item if we just cast a targeting spell
    if SpellCanTargetItem and SpellCanTargetItem() then
        local bag = SecureButton_GetModifiedAttribute(self, "target-bag", button);
        local slot = SecureButton_GetModifiedAttribute(self, "target-slot", button);
        if slot then
            if bag then
                UseContainerItem(bag, slot);
            else
                UseInventoryItem(slot);
            end
        else
            local item = SecureButton_GetModifiedAttribute(self, "target-item", button);
            if item and SpellTargetItem then
                SpellTargetItem(item);
            end
        end
    end
end

function SecureUnitButton_OnLoad(self, unit, menufunc)
    self:SetAttribute("type1", "target");
    self:SetAttribute("type*", "menu");
    self:SetAttribute("unit", unit);
    self.menu = menufunc;
end

function SecureUnitButton_OnClick(self, button)
    local actionType = SecureButton_GetModifiedAttribute(self, "type", button);
    if actionType == "menu" then
        if SpellIsTargeting() then
            SpellStopTargeting();
            return;
        end
    end
    SecureActionButton_OnClick(self, button);
end
