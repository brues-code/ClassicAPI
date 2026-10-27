-- ActionButtonUseKeyDown: action-bar key bindings act on the key press when
-- the cvar is set, on the release otherwise -- 4.3.4's FrameXML handlers
-- (ActionButton.lua, MultiActionBars.lua, PetActionBarFrame.lua). Each keeps
-- 1.12's own body; 4.3.4 adds only the cvar test: the press always shows the
-- button pushed and acts when the cvar is set, the release shows it normal
-- and acts when it is not. The engine registers the cvar (default 0, so the
-- release stays the default).

local GetCVarBool = ClassicAPI.C_CVar.GetCVarBool;

local function UseKeyDown()
    return GetCVarBool("ActionButtonUseKeyDown");
end

-- 1.12's ActionButtonUp / MultiActionButtonUp body.
local function UseActionButton(button, onSelf)
    if ( MacroFrame_SaveMacro ) then
        MacroFrame_SaveMacro();
    end
    UseAction(ActionButton_GetPagedID(button), 0, onSelf);
    if ( IsCurrentAction(ActionButton_GetPagedID(button)) ) then
        button:SetChecked(1);
    else
        button:SetChecked(0);
    end
end

local function MainBarButton(id)
    if ( BonusActionBarFrame:IsShown() ) then
        return getglobal("BonusActionButton"..id), true;
    end
    return getglobal("ActionButton"..id), false;
end

function ActionButtonDown(id)
    local button = MainBarButton(id);
    if ( button:GetButtonState() == "NORMAL" ) then
        button:SetButtonState("PUSHED");
    end
    if ( UseKeyDown() ) then
        UseActionButton(button);
    end
end

function ActionButtonUp(id, onSelf)
    local button, bonus = MainBarButton(id);
    if ( button:GetButtonState() == "PUSHED" ) then
        button:SetButtonState("NORMAL");
        if ( not UseKeyDown() ) then
            -- 1.12's bonus-bar branch uses the action without onSelf.
            if ( bonus ) then
                UseActionButton(button);
            else
                UseActionButton(button, onSelf);
            end
        end
    end
end

function MultiActionButtonDown(bar, id)
    local button = getglobal(bar.."Button"..id);
    if ( button:GetButtonState() == "NORMAL" ) then
        button:SetButtonState("PUSHED");
    end
    if ( UseKeyDown() ) then
        UseActionButton(button);
    end
end

function MultiActionButtonUp(bar, id, onSelf)
    local button = getglobal(bar.."Button"..id);
    if ( button:GetButtonState() == "PUSHED" ) then
        button:SetButtonState("NORMAL");
        if ( not UseKeyDown() ) then
            UseActionButton(button, onSelf);
        end
    end
end

function PetActionButtonDown(id)
    local button = getglobal("PetActionButton"..id);
    if ( button:GetButtonState() == "NORMAL" ) then
        button:SetButtonState("PUSHED");
    end
    if ( UseKeyDown() ) then
        CastPetAction(id);
    end
end

function PetActionButtonUp(id)
    local button = getglobal("PetActionButton"..id);
    if ( button:GetButtonState() == "PUSHED" ) then
        button:SetButtonState("NORMAL");
        if ( not UseKeyDown() ) then
            CastPetAction(id);
        end
    end
end
