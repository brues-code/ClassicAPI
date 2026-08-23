-- SecureStateDriver: automatic frame state / visibility from macro conditionals.
--
-- Backports FrameXML's SecureStateDriverManager. A registered driver re-runs
-- SecureCmdOptionParse (Util/MacroOptions.lua) on a 0.2s poll (plus an instant
-- rescan on the events below) and applies the result:
--   * RegisterStateDriver(frame, "visibility", "[combat] hide; show") -> the
--     value "show"/"hide" drives frame:Show()/Hide();
--   * RegisterStateDriver / RegisterAttributeDriver for any other state ->
--     frame:SetAttribute("state-"..state, value), so an OnAttributeChanged
--     handler reacts to it;
--   * RegisterUnitWatch(frame [, asState]) -> shows/hides (or sets
--     "state-unitexists") as the frame's `unit` attribute comes and goes.
--
-- Base: 3.3.5 FrameXML/SecureStateDriver.lua, with the anniversary refinements
-- grafted in (the resolveDriver() consolidation, tonumber() coercion of driver
-- values, the literal string 'nil' -> nil, and the "statehidden" attribute).
--
-- Three deliberate deviations from upstream, all because 1.12 has no taint and
-- our attribute system fires OnAttributeChanged SYNCHRONOUSLY:
--   1. No attribute-bootstrap manager. Upstream drives itself through
--      SecureStateDriverManager:SetAttribute("setframe"/"setstate"/...) -- a
--      taint-boundary trick. Our SetAttribute has a recursion guard (a nested
--      SetAttribute stores the value but does NOT fire the handler, see
--      src/frame/Attributes.cpp), so doing the registration-time resolve from
--      inside that handler would silently swallow the target frame's own
--      OnAttributeChanged. Instead the public Register* functions write plain
--      local tables and resolve immediately at top level. Public signatures are
--      unchanged from retail.
--   2. GetTime()-deadline throttle instead of the OnUpdate `elapsed` arg (1.12
--      handlers take no parameters).
--   3. UpdateUnitWatch guards its "statehidden"/"state-unitexists" writes on a
--      real change, so a watched frame's handler is not fired at 5 Hz.

-- ---------------------------------------------------------------------------
-- SecureButton_* (minimal -- no taint, no modifier/button suffix machinery)
-- ---------------------------------------------------------------------------

-- SecureButton_GetAttribute and SecureButton_GetUnit are defined in
-- SecureTemplates.lua (loaded earlier via SecureTemplates.xml).

-- ---------------------------------------------------------------------------
-- Private state
-- ---------------------------------------------------------------------------

local secureAttributeDrivers = {};              -- [frame][attribute] = optionsString
local unitExistsWatchers = {};                  -- [frame] = doState (boolean)
local unitExistsCache = setmetatable({}, {
    __index = function(t, k)
        local v = (UnitExists(k) and true) or false;
        t[k] = v;
        return v;
    end,
});

local STATE_DRIVER_UPDATE_THROTTLE = 0.2;
local nextUpdate = 0;

local pairs = pairs;

function UnitWatchRegistered(frame)
    return unitExistsWatchers[frame] ~= nil;
end

-- ---------------------------------------------------------------------------
-- Resolvers
-- ---------------------------------------------------------------------------

local function UpdateUnitWatch(frame, doState)
    local unit = SecureButton_GetUnit(frame);
    local exists = (unit and unitExistsCache[unit]) or false;
    if doState then
        if frame:GetAttribute("state-unitexists") ~= exists then
            frame:SetAttribute("state-unitexists", exists);
        end
    else
        if exists then
            frame:Show();
        else
            frame:Hide();
        end
        local hidden = (not exists) or nil;
        if frame:GetAttribute("statehidden") ~= hidden then
            frame:SetAttribute("statehidden", hidden);
        end
    end
end

local function resolveDriver(frame, attribute, values)
    local newValue = SecureCmdOptionParse(values);

    if attribute == "state-visibility" then
        if newValue == "show" then
            frame:Show();
            if frame:GetAttribute("statehidden") ~= nil then
                frame:SetAttribute("statehidden", nil);
            end
        elseif newValue == "hide" then
            frame:Hide();
            if frame:GetAttribute("statehidden") ~= true then
                frame:SetAttribute("statehidden", true);
            end
        end
    elseif newValue then
        if newValue == "nil" then
            newValue = nil;
        else
            newValue = tonumber(newValue) or newValue;
        end
        if frame:GetAttribute(attribute) ~= newValue then
            frame:SetAttribute(attribute, newValue);
        end
    end
end

-- ---------------------------------------------------------------------------
-- Manager frame
-- ---------------------------------------------------------------------------

SecureStateDriverManager = CreateFrame("Frame", "SecureStateDriverManager");
SecureStateDriverManager:Hide();                -- hidden -> no OnUpdate until a driver exists

SecureStateDriverManager:SetScript("OnUpdate", function()
    local now = GetTime();
    if now < nextUpdate then
        return;
    end
    nextUpdate = now + STATE_DRIVER_UPDATE_THROTTLE;

    for frame, drivers in pairs(secureAttributeDrivers) do
        for attribute, values in pairs(drivers) do
            resolveDriver(frame, attribute, values);
        end
    end

    for k in pairs(unitExistsCache) do
        unitExistsCache[k] = nil;
    end
    for frame, doState in pairs(unitExistsWatchers) do
        UpdateUnitWatch(frame, doState);
    end
end);

SecureStateDriverManager:SetScript("OnEvent", function()
    nextUpdate = 0;                             -- force a rescan on the next frame
end);

SecureStateDriverManager:SetScript("OnAttributeChanged", function()
    if arg1 == "updatetime" and tonumber(arg2) then
        STATE_DRIVER_UPDATE_THROTTLE = tonumber(arg2);
    end
end);

-- Events that warrant an early rescan. Each is guarded so a name this client
-- doesn't define (e.g. UPDATE_STEALTH) is simply skipped.
local rescanEvents = {
    "MODIFIER_STATE_CHANGED",                   -- ClassicAPI-fired
    "PLAYER_FOCUS_CHANGED",                      -- ClassicAPI-fired
    "UPDATE_SHAPESHIFT_FORM",                    -- ClassicAPI-fired (singular)
    "UPDATE_SHAPESHIFT_FORMS",                   -- native 1.12 (plural)
    "ACTIONBAR_PAGE_CHANGED",
    "UPDATE_BONUS_ACTIONBAR",
    "PLAYER_ENTERING_WORLD",
    "UPDATE_STEALTH",
    "PLAYER_TARGET_CHANGED",
    "PLAYER_REGEN_DISABLED",
    "PLAYER_REGEN_ENABLED",
    "UNIT_PET",
    "RAID_ROSTER_UPDATE",
    "PARTY_MEMBERS_CHANGED",
};
for _, event in ipairs(rescanEvents) do
    if C_EventUtils.IsEventValid(event) then
        SecureStateDriverManager:RegisterEvent(event);
    end
end

-- ---------------------------------------------------------------------------
-- Public API
-- ---------------------------------------------------------------------------

function RegisterAttributeDriver(frame, attribute, values)
    if attribute and values and string.sub(attribute, 1, 1) ~= "_" then
        local drivers = secureAttributeDrivers[frame];
        if not drivers then
            drivers = {};
            secureAttributeDrivers[frame] = drivers;
        end
        drivers[attribute] = values;
        SecureStateDriverManager:Show();
        resolveDriver(frame, attribute, values);
    end
end

function UnregisterAttributeDriver(frame, attribute)
    if attribute then
        local drivers = secureAttributeDrivers[frame];
        if drivers then
            drivers[attribute] = nil;
        end
    else
        secureAttributeDrivers[frame] = nil;
    end
end

function RegisterStateDriver(frame, state, values)
    RegisterAttributeDriver(frame, "state-" .. state, values);
end

function UnregisterStateDriver(frame, state)
    UnregisterAttributeDriver(frame, state and ("state-" .. state));
end

function RegisterUnitWatch(frame, asState)
    local doState = (asState and true) or false;
    unitExistsWatchers[frame] = doState;
    SecureStateDriverManager:Show();
    UpdateUnitWatch(frame, doState);
end

function UnregisterUnitWatch(frame)
    unitExistsWatchers[frame] = nil;
end
