-- SecureTemplates.lua — Lua-side helpers for SecureActionButtonTemplate et al.
--
-- Backport of the modifier/button attribute-resolution functions from
-- Blizzard's 2.0 SecureTemplates.lua that addons call directly
-- (SecureButton_GetModifiedAttribute, SecureButton_GetModifierPrefix,
-- SecureButton_GetUnit, …), plus the SecureUnitButton_OnLoad convenience.
--
-- Click dispatch is NOT here: the C++ Frame::Attributes module is the single
-- dispatcher. Setting a "type*" attribute on a frame installs a native OnClick
-- that resolves the verb (target/spell/item/macro/focus/assist/stop/menu/
-- action/pet/click/custom), so the templates carry no <OnClick> and there is no
-- parallel Lua dispatcher to drift out of sync.

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
-- SecureUnitButton_OnLoad — convenience for the common unit-button setup:
-- left-click targets, any other button opens the right-click unit menu. The
-- click behavior itself is native: setting these "type*"/"unit" attributes is
-- what makes the C++ Frame::Attributes dispatcher act on the clicks.
-- ---------------------------------------------------------------------------

function SecureUnitButton_OnLoad(self, unit)
    self:SetAttribute("type1", "target");
    self:SetAttribute("type*", "menu");
    self:SetAttribute("unit", unit);
end
