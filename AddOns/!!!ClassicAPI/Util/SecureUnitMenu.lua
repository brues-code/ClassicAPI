-- Unit right-click menu for attribute-driven unit frames.
--
-- Backs the `menu` / `togglemenu` click verb of the Frame:SetAttribute
-- backport (see src/frame/Attributes.cpp). When a frame with a `unit`
-- attribute resolves a click to the `menu` verb, the DLL calls
-- ClassicAPI_ToggleUnitMenu(unit), which pops the standard unit dropdown at
-- the cursor -- the same UnitPopup menu Blizzard's PlayerFrame / TargetFrame /
-- PartyMemberFrame show on right-click. Lets a unit frame express its
-- right-click menu purely as an attribute (e.g. type2 = "menu") instead of a
-- hand-rolled OnClick handler.
--
-- The unit -> menu-type resolution mirrors Blizzard's own generic resolver
-- (TargetFrameDropDown_Initialize in FrameXML): self -> SELF, pet -> PET, a
-- grouped player -> PARTY (whisper / inspect / trade / follow / promote / ...),
-- any other player -> PLAYER (adds INVITE), anything else (NPC) ->
-- RAID_TARGET_ICON. Vanilla's stock UI never wires a unit to the "RAID" menu
-- (there are no clickable raid unit frames in 1.12), and PARTY carries the
-- options players actually want on a grouped member, so grouped players (party
-- or raid) use PARTY -- matching how pfUI drives its raid menu.

local dropdown;

local function EnsureDropdown()
    if not dropdown then
        dropdown = CreateFrame("Frame", "ClassicAPIUnitMenuDropDown", UIParent, "UIDropDownMenuTemplate");
        dropdown.displayMode = "MENU";
    end
    return dropdown;
end

local function ResolveMenu(unit)
    if UnitIsUnit(unit, "player") then
        return "SELF";
    elseif UnitIsUnit(unit, "pet") then
        return "PET";
    elseif UnitIsPlayer(unit) then
        if UnitInParty(unit) or UnitInRaid(unit) then
            return "PARTY";
        end
        return "PLAYER";
    end
    return "RAID_TARGET_ICON";
end

function ClassicAPI_ToggleUnitMenu(unit)
    if not unit or not UnitExists(unit) then
        return;
    end
    local which = ResolveMenu(unit);
    local name;
    if which == "RAID_TARGET_ICON" then
        name = RAID_TARGET_ICON;
    end
    local dd = EnsureDropdown();
    dd.initialize = function()
        UnitPopup_ShowMenu(dd, which, unit, name);
    end
    ToggleDropDownMenu(1, nil, dd, "cursor");
end
