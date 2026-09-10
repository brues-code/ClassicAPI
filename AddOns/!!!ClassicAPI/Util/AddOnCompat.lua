if C_AddOns.DoesAddOnExist('pfUI') then
    EventUtil.ContinueOnAddOnLoaded('pfUI', function()
        -- pfUI\pfUI.lua redeclares the RAID_CLASS_COLORS object at least 14 times (insane!!)
        -- This stops it from replacing all our ColorMixin values
        pfUI.UpdateColors = function() end
        RAID_CLASS_COLORS = setmetatable(RAID_CLASS_COLORS, { __index = function()
            local unknownColor = CreateColor(0.6, 0.6, 0.6)
            unknownColor.colorStr = unknownColor:GenerateHexColor()
            return unknownColor
        end})

        -- Compatibility with OLDER forks of pfUI. Our DLL adds the modern
        -- HookScript widget method; older pfUI forks branch on
        -- `if button.HookScript` and take a modern code path they were never
        -- written for on their pfActionBar buttons, which breaks them. While
        -- the actionbar module builds those buttons, temporarily wrap
        -- CreateFrame to shadow HookScript with a falsy field so those forks
        -- fall back to their vanilla SetScript path. The wrapper is removed as
        -- soon as the actionbar module finishes loading so it affects nothing
        -- else. A pfUI is trusted to handle HookScript itself if EITHER signal
        -- is present: the maintained "brues" X-Website tag OR the
        -- `pfUI.handlesHookScript` capability flag. Without the website the
        -- flag is required; if neither is present we apply the shim.
        local website = GetAddOnMetadata("pfUI", "X-Website")
        local hasBruesWebsite = website and strfind(website, 'brues')
        if not hasBruesWebsite and not pfUI.handlesHookScript then
            local _createFrame = CreateFrame
            local HookedCreateFrame = function(frameType, name, parent, template)
                local frame = _createFrame(frameType, name, parent, template)
                if frameType == "Button" and strfind(name or "", "pfActionBar") then
                    frame.HookScript = false
                end
                return frame
            end
            hooksecurefunc(pfUI, 'LoadModule', function(frame, moduleName)
                if moduleName == "updatenotify" then
                    CreateFrame = HookedCreateFrame
                elseif moduleName == "actionbar" then
                    CreateFrame = _createFrame
                end
            end)
        end
    end)
end

if C_AddOns.DoesAddOnExist("SpecialTalentUI") then
    EventUtil.ContinueOnAddOnLoaded("SpecialTalentUI", CAPI_ApplyStandardColorGlobals)
end

-- ShaguTweaks libpredict register's TBC events that ClassicAPI backports.
if C_AddOns.DoesAddOnExist("ShaguTweaks") then
    EventUtil.ContinueOnAddOnLoaded("ShaguTweaks", function()
        local libp = ShaguTweaks.libpredict
        if libp then
            libp.sender:UnregisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
            libp.sender:UnregisterEvent("UNIT_SPELLCAST_START")
            libp.sender:UnregisterEvent("UNIT_SPELLCAST_STOP")
            libp.sender:UnregisterEvent("UNIT_SPELLCAST_FAILED")
            libp.sender:UnregisterEvent("UNIT_SPELLCAST_INTERRUPTED")
            libp.sender:UnregisterEvent("UNIT_SPELLCAST_SENT")
        end
    end)
end

-- Guard UPDATE_MOUSEOVER_UNIT handlers (issue #12).
function CAPI_MouseoverClearedCompat(frame)
    if not frame then return end
    local originalOnEvent = frame:GetScript("OnEvent")
    frame:SetScript("OnEvent", function()
        if event == "UPDATE_MOUSEOVER_UNIT" and not UnitExists("mouseover") then
            return
        end

        if originalOnEvent then
            originalOnEvent()
        end
    end)
end

CAPI_MouseoverClearedCompat(GameTooltip)

-- Restores vanilla calling for ONE frame's script handler. Our DLL hands script
-- handlers modern positional arguments (`self`, then the event's own args) on
-- top of the `this` / `arg1` globals vanilla uses. That is additive for a
-- handler declared `function()`, but a handler declared with a parameter of its
-- own -- one written to be BOTH called directly and used as a script -- now
-- receives `self` in that parameter instead of the nil it was written for.
--
-- The wrapper below takes no parameters, so those extra arguments stop here,
-- and the original runs with the empty argument list vanilla gave it. The
-- globals are untouched by the nesting, so the handler still reads `this` and
-- `event` normally.
function CAPI_VanillaScriptArgsCompat(frame, script)
    if not frame then return end
    local original = frame:GetScript(script)
    if not original then return end
    frame:SetScript(script, function()
        original()
    end)
end

-- LunaUnitFrames sets `SetupGroupHeader(groupType)` as its group headers'
-- OnEvent while also calling it directly with a group name. As a handler it
-- expects `groupType` to be nil so it falls back to `this.unitGroup`. Given
-- `self` instead, it looks the header up by frame rather than by name, finds
-- nothing, and indexes nil (units.lua:358, issue #41). Its raid counterpart
-- takes the header frame itself, so `self` is already what it wants -- only the
-- group headers need this.
if C_AddOns.DoesAddOnExist("LunaUnitFrames") then
    EventUtil.ContinueOnAddOnLoaded("LunaUnitFrames", function()
        if not (LunaUF and LunaUF.Units) then return end

        local function FixGroupHeader(unit)
            CAPI_VanillaScriptArgsCompat(getglobal("LUFHeader" .. (unit or "")),
                                         "OnEvent")
        end

        -- Headers are built on demand, so cover the ones already up and every
        -- one created later.
        FixGroupHeader("party")
        FixGroupHeader("partytarget")
        FixGroupHeader("partypet")
        hooksecurefunc(LunaUF.Units, "LoadGroupHeader", function(self, unit)
            FixGroupHeader(unit)
        end)
    end)
end

if C_AddOns.DoesAddOnExist("Puppeteer") then
    EventUtil.ContinueOnAddOnLoaded("Puppeteer", function()
        CAPI_MouseoverClearedCompat(PTEnemyUpdater)
    end)
end

-- Cartographer_Notes gates a WorldMap release-spirit hook behind an
-- `if lua51` probe, treating a 5.1-capable Lua as a modern client that has
-- the WorldMapDeathRelease frame. Our 5.1 syntax backport makes the probe
-- pass, so Cartographer takes the modern branch and hooks a frame 1.12's
-- WorldMap never had (WorldMapDeathRelease:SetScript in OnEnable) -> nil
-- index. Provide an inert, hidden stub so the hook succeeds and does nothing
-- -- the same outcome vanilla had, where the branch never ran. The embedded
-- !!!ClassicAPI addon loads first, so the frame exists before Cartographer's
-- OnEnable.
if C_AddOns.DoesAddOnExist("Cartographer") and not WorldMapDeathRelease then
    CreateFrame("Button", "WorldMapDeathRelease", WorldMapButton or UIParent):Hide()
end

-- ModernSpellBook (Spellbook\MSB_Spellbook.lua) re-hooks its "show all ranks"
-- checkbox by calling HookScript as a GLOBAL -- HookScript(frame, "OnClick",
-- handler). That global never existed on any client: HookScript has always
-- been the widget method frame:HookScript, which our DLL provides. MSB even
-- guards the call with `if checkbox.HookScript` (the method IS present), then
-- calls the global (nil) -> "attempt to call global 'HookScript'". Define the
-- global so the call resolves, forwarding to the method. Gated on the addon
-- being present so the global namespace stays clean otherwise.
if C_AddOns.DoesAddOnExist("ModernSpellBook") and not HookScript then
    function HookScript(frame, script, handler)
        if frame and frame.HookScript then
            frame:HookScript(script, handler)
        end
    end
end

-- Compost-2.0 (rev 17406+, bundled by BigWigs among others) replaces most of
-- its own body with stubs once it believes it is on Lua 5.1, on the reasoning
-- that table recycling is pointless when the collector already does it:
--
--     local lua51 = loadstring("return function(...) return ... end") and true or false
--     ...
--     if lua51 then function lib:Erase() return {} end
--     else          function lib:Erase(t) --[[ clears t in place ]] end
--
-- Our 5.1 syntax backport makes that probe pass, so the stubs win. For
-- GetTable and Reclaim that is harmless -- both are pure recycling hints. Erase
-- is not: callers hand it a live table and expect its keys gone. The stub takes
-- no parameter at all, so every `compost:Erase(t)` that clears in place (rather
-- than assigning the returned table) silently stops clearing.
--
-- Puppeteer is the reported casualty. PTUnit:ClearAuras erases its
-- TrackedDebuffTypes, AfflictedDebuffTypes and aura-ID sets in place, so a
-- dispel type never leaves the set once applied: a cursed raid member keeps its
-- debuff colour after the curse is cleaned, and HasDebuffID answers true for
-- auras that are long gone. It takes a second addon shipping the newer Compost
-- to show up, because AceLibrary arbitrates on the highest $Revision and
-- ignores load order -- so a lone Puppeteer keeps its own working rev 11579.
--
-- Restore an in-place Erase matching the pre-stub body. Probed by behavior
-- rather than by revision or addon name, so a Compost that already clears is
-- left alone.
--
-- Re-checked on every ADDON_LOADED rather than once, because AceLibrary can
-- upgrade a library at any point during load -- including from an addon loaded
-- on demand mid-session -- and an upgrade installs the new copy's functions
-- over ours. We cannot simply wait for a late event either, since this addon
-- loads first and AceLibrary does not exist yet at that point.
--
-- Repeating is cheap because the verdict is cached against the function that
-- earned it: a check settles to two identity comparisons once the live Erase
-- has been either vetted or replaced, and only an Erase we have never seen
-- costs a probe. The probe table is reused across checks (refilled each time,
-- so leftover state cannot skew the answer) rather than built per check.
local compostProbe = {}
local vettedErase

local function EraseInPlace(self, t)
    if type(t) ~= "table" then return end
    setmetatable(t, nil)
    for k in pairs(t) do
        t[k] = nil
    end
    table.setn(t, 0)
    return t
end

local function EnsureCompostErasesInPlace()
    if not (AceLibrary and AceLibrary.HasInstance
            and AceLibrary:HasInstance("Compost-2.0")) then
        return
    end

    local compost = AceLibrary("Compost-2.0")
    if compost.Erase == EraseInPlace or compost.Erase == vettedErase then
        return
    end

    compostProbe[1] = "array"
    compostProbe.key = "hash"
    compost:Erase(compostProbe)
    if next(compostProbe) == nil then
        vettedErase = compost.Erase -- clears in place; leave it alone
        return
    end

    compost.Erase = EraseInPlace
end

EventRegistry:RegisterFrameEventAndCallback("ADDON_LOADED",
                                            EnsureCompostErasesInPlace)
EventRegistry:RegisterFrameEventAndCallback("PLAYER_LOGIN",
                                            EnsureCompostErasesInPlace)
EnsureCompostErasesInPlace()
