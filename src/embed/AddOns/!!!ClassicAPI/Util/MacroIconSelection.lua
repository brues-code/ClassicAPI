-- MacroIconSelection.lua -- the Macro UI's macro list shows the icon a
-- `#showtooltip` resolved to, the way the action button does.
--
-- `GetMacroInfo` reports the icon the macro STORES, and it has to keep
-- reporting that one: `MacroFrame_Update` seeds the icon selector from that
-- return, so answering with a resolved icon there would pre-select an icon the
-- player never picked, in the one place that edits the stored icon.
--
-- That leaves the list buttons showing the question mark for a macro whose
-- action button shows a spell. `C_Macro.GetMacroIcon` returns the icon a macro
-- currently shows, so paint the buttons from it once the stock update has
-- drawn them. An empty slot and a macro with a chosen icon both answer with
-- what stock already drew, so those are left alone.

local function ApplyIcon(texture, macroSlot)
    if not texture then
        return
    end
    local shown = C_Macro.GetMacroIcon(macroSlot)
    if shown then
        texture:SetTexture(shown)
    end
end

local function PaintMacroList()
    if not (MacroFrame and MacroFrame.macroBase) then
        return
    end
    for i = 1, MAX_MACROS do
        ApplyIcon(_G["MacroButton" .. i .. "Icon"], MacroFrame.macroBase + i)
    end
    if MacroFrame.selectedMacro then
        ApplyIcon(MacroFrameSelectedMacroButtonIcon, MacroFrame.selectedMacro)
    end
end

local function WrapMacroFrameUpdate()
    if not MacroFrame_Update then
        return
    end

    local original = MacroFrame_Update
    -- No `...`: `MacroFrame_Update` takes no arguments, and Lua 5.0 would
    -- build an `arg` table for every vararg call.
    MacroFrame_Update = function()
        original()
        PaintMacroList()
    end
end

-- Blizzard_MacroUI loads on demand.
EventUtil.ContinueOnAddOnLoaded("Blizzard_MacroUI", WrapMacroFrameUpdate)
