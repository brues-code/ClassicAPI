------------------------------------------------------------------------------
-- DebugTools.lua  (1.12 / Lua 5.0 backport of Blizzard_DebugTools 3.3.5)
--
-- Major changes from the 3.3.5 source:
--  * Lua 5.0 has no "..." varargs and no select(); script handlers take
--    explicit (this, event, arg1..arg9) parameters from XML inline scripts.
--  * RegisterAllEvents is used when available; the curated default list is
--    a fallback if the client doesn't expose it.
--  * /fstack accepts "true" (as on 3.3.5) or "hidden" to list hidden
--    frames too.
--  * No UIPanelDialogTemplate, no parentKey, no clampedToScreen, no
--    OnSizeChanged in vanilla -- frames have fixed dimensions.
--  * No debuglocals
--
-- UX additions cherry-picked from modern Blizzard_EventTrace
-- (10.x+, anniversary _classic_era_):
--  * SavedVariables persist the user filter list, pause state, and
--    Args/Time display toggles across sessions (DebugToolsSavedVars).
--  * Toolbar row inside EventTraceFrame: Pause/Resume button + Args /
--    Time checkboxes + Search EditBox. Search filters the visible
--    rows by substring match against the event name.
--  * Args toggle appends a comma-separated arg dump after the event
--    name in the row (was tooltip-only before). Time toggle blanks
--    the time column without re-anchoring frames.
--
-- New: TableInspector window (see TableInspector.lua) — open via
-- `/tinspect <luaexpr>` with a navigable, breadcrumb-tracked view of
-- any table.
------------------------------------------------------------------------------

EVENT_TRACE_EVENT_HEIGHT = 16;
EVENT_TRACE_MAX_ENTRIES  = 1000;
-- Toolbar above the event list (Pause + Args/Time toggles + Search) eats
-- one row's worth of vertical real estate; 26 visible rows instead of 28
-- keeps the frame the same height as before the enhancements landed.
EVENT_TRACE_NUM_BUTTONS  = 26;
EVENT_TRACE_FRAME_HEIGHT = (EVENT_TRACE_NUM_BUTTONS * EVENT_TRACE_EVENT_HEIGHT) + 68;

DEBUGLOCALS_LEVEL = 4;
local _normalFontColor = { 1, .82, 0, 1 };

EVENT_TRACE_SYSTEM_TIMES = {};
EVENT_TRACE_SYSTEM_TIMES["System"]  = true;
EVENT_TRACE_SYSTEM_TIMES["Elapsed"] = true;

EVENT_TRACE_EVENT_COLORS = {};
EVENT_TRACE_EVENT_COLORS["System"]  = _normalFontColor;
EVENT_TRACE_EVENT_COLORS["Elapsed"] = { .6, .6, .6, 1 };

------------------------------------------------------------------------------
-- Saved variables — persist user filter list + display toggles across
-- sessions. WoW fills `DebugToolsSavedVars` from
-- `WTF\Account\<acct>\SavedVariables\DebugTools.lua` on ADDON_LOADED,
-- so any reference to it before that event fires is nil.
------------------------------------------------------------------------------

DebugToolsSavedVars = nil;

-- Defaults applied on a fresh install (no saved file yet) AND merged
-- field-by-field into older saved files so new fields don't require
-- a manual wipe to surface.
local _DEFAULT_FILTERS = {
    "GLOBAL_MOUSE_DOWN", "GLOBAL_MOUSE_UP",
    "MODIFIER_STATE_CHANGED",
    "PLAYER_STARTED_LOOKING",  "PLAYER_STOPPED_LOOKING",
    "PLAYER_STARTED_TURNING",  "PLAYER_STOPPED_TURNING",
    "PLAYER_STARTED_MOVING",   "PLAYER_STOPPED_MOVING",
    "CHAT_MSG_ADDON",
};

local function _CloneArray(src)
    local t = {};
    for i = 1, table.getn(src) do t[i] = src[i]; end
    return t;
end

local function _DefaultSavedVars()
    return {
        Filters = { User = _CloneArray(_DEFAULT_FILTERS) },
        ShowArguments = false,
        ShowTimestamp = true,
        Paused = false,
    };
end

local function _MergeDefaults(stored)
    local defaults = _DefaultSavedVars();
    if (type(stored) ~= "table") then return defaults; end
    for k, v in pairs(defaults) do
        if (stored[k] == nil) then stored[k] = v; end
    end
    if (type(stored.Filters) ~= "table") then
        stored.Filters = { User = _CloneArray(_DEFAULT_FILTERS) };
    end
    if (type(stored.Filters.User) ~= "table") then
        stored.Filters.User = _CloneArray(_DEFAULT_FILTERS);
    end
    return stored;
end

-- Apply the loaded saved-vars to the live frame state. Runs after both
-- the frame OnLoad (which sets _EventTraceFrame) and ADDON_LOADED
-- (which fills DebugToolsSavedVars). Idempotent — safe to re-run.
local function _ApplySavedVars()
    if (not _EventTraceFrame or not DebugToolsSavedVars) then return; end
    _EventTraceFrame.ignoredEvents = {};
    for i = 1, table.getn(DebugToolsSavedVars.Filters.User) do
        _EventTraceFrame.ignoredEvents[DebugToolsSavedVars.Filters.User[i]] = true;
    end
    _EventTraceFrame.paused = DebugToolsSavedVars.Paused and true or false;
    _EventTraceFrame.showArguments = DebugToolsSavedVars.ShowArguments and true or false;
    _EventTraceFrame.showTimestamp = DebugToolsSavedVars.ShowTimestamp and true or false;
    -- Sync the toolbar widgets if they exist (XML may have loaded them
    -- before or after this point depending on file order).
    local pauseBtn = getglobal("EventTraceFramePauseButton");
    if (pauseBtn) then
        pauseBtn:SetText(_EventTraceFrame.paused and "Resume" or "Pause");
    end
    -- Vanilla 1.12 `CheckButton:SetChecked` takes 1 / nil, not true /
    -- false — passing the bool directly silently leaves the box in
    -- whatever state it had before, which is why the Time box looked
    -- unchecked at first launch even though `showTimestamp` defaults
    -- to true. (`true` was accepted starting in TBC.)
    local argsChk = getglobal("EventTraceFrameArgsCheck");
    if (argsChk) then
        argsChk:SetChecked(_EventTraceFrame.showArguments and 1 or nil);
    end
    local timeChk = getglobal("EventTraceFrameTimeCheck");
    if (timeChk) then
        timeChk:SetChecked(_EventTraceFrame.showTimestamp and 1 or nil);
    end
end

local _initFrame = CreateFrame("Frame");
_initFrame:RegisterEvent("ADDON_LOADED");
_initFrame:RegisterEvent("PLAYER_LOGOUT");
_initFrame:SetScript("OnEvent", function()
    if (event == "ADDON_LOADED") then
        if (arg1 ~= "DebugTools") then return; end
        DebugToolsSavedVars = _MergeDefaults(DebugToolsSavedVars);
        _ApplySavedVars();
        _initFrame:UnregisterEvent("ADDON_LOADED");
    elseif (event == "PLAYER_LOGOUT") then
        -- Final sync on logout — captures any state changes made
        -- after ADDON_LOADED (filter add/remove, toggle clicks).
        if (DebugToolsSavedVars and _EventTraceFrame) then
            DebugToolsSavedVars.Paused = _EventTraceFrame.paused;
            DebugToolsSavedVars.ShowArguments = _EventTraceFrame.showArguments;
            DebugToolsSavedVars.ShowTimestamp = _EventTraceFrame.showTimestamp;
            -- Filters.User stays the source of truth — toolbar mutations
            -- already write through to it directly.
        end
    end
end);

local _EventTraceFrame;

_framesSinceLast = 0;
_timeSinceLast   = 0;

local _timer = CreateFrame("Frame");
_timer:SetScript("OnUpdate", function()
    _framesSinceLast = _framesSinceLast + 1;
    _timeSinceLast   = _timeSinceLast + (arg1 or 0);
end);

------------------------------------------------------------------------------
-- EventTraceFrame
------------------------------------------------------------------------------

function EventTraceFrame_OnLoad(self)
    self.buttons          = {};
    self.events           = {};
    self.times            = {};
    self.rawtimes         = {};
    self.args             = { {}, {}, {}, {}, {}, {}, {}, {}, {} };
    self.ignoredEvents    = {};
    self.lastIndex        = 0;
    self.visibleButtons   = EVENT_TRACE_NUM_BUTTONS;
    -- Display state defaults — overwritten by `_ApplySavedVars` once
    -- ADDON_LOADED fires. Set here so the first Update before saved
    -- vars come in has sane values.
    self.paused           = false;
    self.showArguments    = false;
    self.showTimestamp    = true;
    self.searchText       = nil;
    self.filteredIndices  = nil;
    _EventTraceFrame      = self;

    -- If saved-vars beat the frame OnLoad (unusual file order), apply now.
    _ApplySavedVars();

    self:EnableMouse(true);
    self:EnableMouseWheel(true);
    self:SetScript("OnMouseWheel", function() EventTraceFrame_OnMouseWheel(this, arg1); end);

    -- Pre-create the fixed list of buttons.
    for i = 1, EVENT_TRACE_NUM_BUTTONS do
        local button = CreateFrame("Button", "EventTraceFrameButton" .. i,
                                   self, "EventTraceEventTemplate");
        button:SetPoint("BOTTOMLEFT", 12, (EVENT_TRACE_EVENT_HEIGHT * (i - 1)) + 12);
        button:SetPoint("RIGHT", -28, 0);
        table.insert(self.buttons, button);
        button:Show();
    end
end

function EventTraceFrame_OnEvent(self, event, a1, a2, a3, a4, a5, a6, a7, a8, a9)
    if (self.ignoredEvents[event]) then return; end
    -- Pause swallows real engine events but lets the synthetic
    -- "Begin Capture"/"End Capture"/"On Update" markers through so the
    -- log records the boundary clearly.
    if (self.paused and event ~= "Begin Capture" and event ~= "End Capture"
        and event ~= "On Update") then
        return;
    end

    if (not self.ignoreElapsed and _framesSinceLast ~= 0) then
        self.ignoreElapsed = true;
        EventTraceFrame_OnEvent(self, "On Update");
        self.ignoreElapsed = nil;
    end

    local nextIndex = self.lastIndex + 1;
    if (nextIndex > EVENT_TRACE_MAX_ENTRIES) then
        local staleIndex = nextIndex - EVENT_TRACE_MAX_ENTRIES;
        self.events[staleIndex]   = nil;
        self.times[staleIndex]    = nil;
        self.rawtimes[staleIndex] = nil;
        for k, v in pairs(self.args) do
            self.args[k][staleIndex] = nil;
        end
    end

    if (string.find(event, "Begin Capture", 1, true) or
        string.find(event, "End Capture", 1, true)) then
        self.times[nextIndex] = "System";
        if (self.eventsToCapture) then
            self.events[nextIndex] = string.format("%s (%s events)", event,
                                                   tostring(self.eventsToCapture));
        else
            self.events[nextIndex] = event;
        end
    elseif (event == "On Update") then
        self.times[nextIndex] = "Elapsed";
        self.events[nextIndex] = string.format(
            string.format("%.3f sec", _timeSinceLast) .. " - %d frame(s)",
            _framesSinceLast);
        _timeSinceLast   = 0;
        _framesSinceLast = 0;
    else
        self.events[nextIndex] = event;
        local seconds = GetTime();
        local minutes = math.floor(math.floor(seconds) / 60);
        local hours   = math.floor(minutes / 60);
        seconds = seconds - 60 * minutes;
        minutes = minutes - 60 * hours;
        hours = math.mod(hours, 1000);
        self.times[nextIndex] = string.format("%.2d:%.2d:%06.3f", hours, minutes, seconds);

        local packed = { a1, a2, a3, a4, a5, a6, a7, a8, a9 };
        local numArgs = 0;
        for i = 9, 1, -1 do
            if (packed[i] ~= nil) then
                numArgs = i;
                break;
            end
        end
        for i = 1, numArgs do
            if (not self.args[i]) then self.args[i] = {}; end
            self.args[i][nextIndex] = packed[i];
        end

        if (self.eventsToCapture) then
            self.eventsToCapture = self.eventsToCapture - 1;
        end
    end

    self.rawtimes[nextIndex] = GetTime();
    self.lastIndex = nextIndex;
    -- Keep the filtered view in sync if a search is active. We append-
    -- on-match here rather than re-walking the whole event list each
    -- tick (would be O(n) per event for the most common case where
    -- the search matches none / many).
    if (self.filteredIndices and _EventTrace_MatchesSearch(self.events[nextIndex])) then
        table.insert(self.filteredIndices, nextIndex);
        -- Bound the filtered list to the same MAX_ENTRIES window the
        -- raw store enforces. Drop the head when we overflow.
        while (table.getn(self.filteredIndices) > 0 and
               self.filteredIndices[1] < (nextIndex - EVENT_TRACE_MAX_ENTRIES + 1)) do
            table.remove(self.filteredIndices, 1);
        end
    end
    EventTraceFrame_Update();
    if (self.eventsToCapture and self.eventsToCapture <= 0) then
        self.eventsToCapture = nil;
        EventTraceFrame_StopEventCapture();
    end
end

function EventTraceFrame_OnUpdate(self, elapsed)
    EventTraceFrame_Update();
end

------------------------------------------------------------------------------
-- EventTrace toolbar helpers — search filter, display flags, paused
-- toggle, filter list mutations. Keep these grouped together so the
-- toolbar XML scripts only need to reference one well-defined surface.
------------------------------------------------------------------------------

function _EventTrace_MatchesSearch(eventName)
    if (not _EventTraceFrame) then return false; end
    local needle = _EventTraceFrame.searchText;
    if (not needle or needle == "") then return true; end
    if (not eventName) then return false; end
    return string.find(string.lower(eventName), needle, 1, true) ~= nil;
end

-- Walks the live event store and rebuilds the filtered-index list from
-- scratch. Called when search text changes; cheap (one pass over at
-- most EVENT_TRACE_MAX_ENTRIES entries) and matches the engine's own
-- "build then sort" pattern.
function _EventTrace_RebuildFilter()
    if (not _EventTraceFrame) then return; end
    local needle = _EventTraceFrame.searchText;
    if (not needle or needle == "") then
        _EventTraceFrame.filteredIndices = nil;
        return;
    end
    needle = string.lower(needle);
    local indices = {};
    local first = math.max(1, (_EventTraceFrame.lastIndex or 0) - EVENT_TRACE_MAX_ENTRIES + 1);
    for i = first, (_EventTraceFrame.lastIndex or 0) do
        local ev = _EventTraceFrame.events[i];
        if (ev and string.find(string.lower(ev), needle, 1, true)) then
            table.insert(indices, i);
        end
    end
    _EventTraceFrame.filteredIndices = indices;
end

-- Resolves a per-row event index. With a filter active, the scroll bar
-- range is over the filtered list — translate back to the underlying
-- event index here so the rest of the Update loop reads from `events[i]`
-- as usual.
function _EventTrace_ResolveIndex(scrollValue, rowOffset)
    if (_EventTraceFrame.filteredIndices) then
        return _EventTraceFrame.filteredIndices[scrollValue - rowOffset];
    end
    return scrollValue - rowOffset;
end

-- Returns (firstID, lastID) for the slider's current min/max. With a
-- filter active, the range is 1..#filtered. Without a filter it's the
-- raw event-store window.
function _EventTrace_ScrollRange()
    if (_EventTraceFrame.filteredIndices) then
        local n = table.getn(_EventTraceFrame.filteredIndices);
        if (n == 0) then return 0, 0; end
        return 1, n;
    end
    local first = math.max(1, _EventTraceFrame.lastIndex - EVENT_TRACE_MAX_ENTRIES + 1);
    local last  = _EventTraceFrame.lastIndex or 1;
    return first, last;
end

-- Formats an event entry for in-line display. When `showArguments` is
-- on, appends a comma-separated rendering of the captured args after
-- the event name. System rows (Begin/End Capture, On Update markers)
-- never get args.
function _EventTrace_FormatRow(index)
    local ev = _EventTraceFrame.events[index];
    if (not ev) then return nil; end
    if (not _EventTraceFrame.showArguments) then return ev; end
    if (EVENT_TRACE_SYSTEM_TIMES[_EventTraceFrame.times[index]]) then return ev; end
    local parts = {};
    for i = 1, 9 do
        local argList = _EventTraceFrame.args[i];
        if (argList and argList[index] ~= nil) then
            table.insert(parts, EventTrace_FormatArgValue(argList[index]));
        end
    end
    if (table.getn(parts) == 0) then return ev; end
    return ev .. " (" .. table.concat(parts, ", ") .. ")";
end

------------------------------------------------------------------------------
-- Toolbar widget handlers — wired up from EventTraceFrame.xml.
------------------------------------------------------------------------------

function EventTraceFrame_TogglePause()
    if (not _EventTraceFrame) then return; end
    _EventTraceFrame.paused = not _EventTraceFrame.paused;
    if (DebugToolsSavedVars) then
        DebugToolsSavedVars.Paused = _EventTraceFrame.paused;
    end
    local pauseBtn = getglobal("EventTraceFramePauseButton");
    if (pauseBtn) then
        pauseBtn:SetText(_EventTraceFrame.paused and "Resume" or "Pause");
    end
end

function EventTraceFrame_ToggleArgs(checked)
    if (not _EventTraceFrame) then return; end
    _EventTraceFrame.showArguments = checked and true or false;
    if (DebugToolsSavedVars) then
        DebugToolsSavedVars.ShowArguments = _EventTraceFrame.showArguments;
    end
    EventTraceFrame_Update();
end

function EventTraceFrame_ToggleTimestamp(checked)
    if (not _EventTraceFrame) then return; end
    _EventTraceFrame.showTimestamp = checked and true or false;
    if (DebugToolsSavedVars) then
        DebugToolsSavedVars.ShowTimestamp = _EventTraceFrame.showTimestamp;
    end
    EventTraceFrame_Update();
end

function EventTraceFrame_OnSearchChanged(editBox)
    if (not _EventTraceFrame) then return; end
    local text = editBox:GetText() or "";
    local clearBtn = getglobal("EventTraceFrameSearchClear");
    if (clearBtn) then
        if (text == "") then clearBtn:Hide(); else clearBtn:Show(); end
    end
    if (text == "") then text = nil; end
    if (text == _EventTraceFrame.searchText) then return; end
    _EventTraceFrame.searchText = text;
    _EventTrace_RebuildFilter();
    -- Reset scroll to the newest end of the filtered view so the user
    -- sees fresh matches first.
    local scroll = getglobal("EventTraceFrameScroll");
    if (scroll) then
        local mn, mx = _EventTrace_ScrollRange();
        scroll:SetMinMaxValues(mn, math.max(mn, mx));
        scroll:SetValue(math.max(mn, mx));
    end
    EventTraceFrame_Update();
end

-- Programmatic filter list mutation — also exposed via `/etrace ignore`.
-- Updates both the runtime ignoredEvents map and the saved-vars list
-- so the addition persists across sessions.
function EventTraceFrame_AddFilter(eventName)
    if (not _EventTraceFrame or not eventName or eventName == "") then return; end
    eventName = string.upper(eventName);
    if (_EventTraceFrame.ignoredEvents[eventName]) then return; end
    _EventTraceFrame.ignoredEvents[eventName] = true;
    if (DebugToolsSavedVars and DebugToolsSavedVars.Filters
        and DebugToolsSavedVars.Filters.User) then
        table.insert(DebugToolsSavedVars.Filters.User, eventName);
    end
end

function EventTraceFrame_RemoveFilter(eventName)
    if (not _EventTraceFrame or not eventName) then return; end
    eventName = string.upper(eventName);
    _EventTraceFrame.ignoredEvents[eventName] = nil;
    if (DebugToolsSavedVars and DebugToolsSavedVars.Filters
        and DebugToolsSavedVars.Filters.User) then
        local list = DebugToolsSavedVars.Filters.User;
        for i = table.getn(list), 1, -1 do
            if (list[i] == eventName) then table.remove(list, i); end
        end
    end
end

function EventTraceFrame_Update()
    if (not _EventTraceFrame or not _EventTraceFrame.buttons) then
        -- Slider OnValueChanged can fire during slider OnLoad, which happens
        -- before EventTraceFrame_OnLoad in 1.12 XML load order.
        return;
    end
    local scrollBar = getglobal("EventTraceFrameScroll");
    if (not scrollBar) then return; end
    local scrollBarValue = scrollBar:GetValue();
    local minValue, maxValue = scrollBar:GetMinMaxValues();

    local firstID, lastID = _EventTrace_ScrollRange();

    if (firstID >= lastID) then
        scrollBar:SetMinMaxValues(firstID - 1, lastID);
    else
        scrollBar:SetMinMaxValues(firstID, lastID);
    end
    if (scrollBarValue < firstID) then
        scrollBar:SetValue(firstID);
        scrollBarValue = firstID;
    end

    if (scrollBarValue < 1) then
        scrollBarValue = 1;
    elseif (not _EventTraceFrame.selectedEvent) then
        if (scrollBarValue == maxValue) then
            scrollBar:SetValue(lastID);
        end
    end

    for i = 1, _EventTraceFrame.visibleButtons do
        local button = _EventTraceFrame.buttons[i];
        if (button) then
            local index = _EventTrace_ResolveIndex(scrollBarValue, i - 1);
            local event = index and _EventTraceFrame.events[index];
            if (event) then
                local timeString = _EventTraceFrame.times[index];
                button.index = index;
                local timeFS  = getglobal(button:GetName() .. "Time");
                local eventFS = getglobal(button:GetName() .. "Event");
                -- ShowTimestamp toggle blanks the time column rather
                -- than moving frames around — keeps the event column
                -- left edge stable, which matters when toggling on/off
                -- mid-session.
                if (_EventTraceFrame.showTimestamp) then
                    timeFS:SetText(timeString);
                else
                    timeFS:SetText("");
                end
                eventFS:SetText(_EventTrace_FormatRow(index));
                local color = EVENT_TRACE_EVENT_COLORS[event]
                              or EVENT_TRACE_EVENT_COLORS[timeString];
                if (color) then
                    timeFS:SetTextColor(unpack(color));
                    eventFS:SetTextColor(unpack(color));
                else
                    timeFS:SetTextColor(1, 1, 1, 1);
                    eventFS:SetTextColor(1, 1, 1, 1);
                end
                button:Show();
                if (_EventTraceFrame.selectedEvent) then
                    if (index == _EventTraceFrame.selectedEvent) then
                        EventTraceFrameEvent_DisplayTooltip(button);
                        button:GetHighlightTexture():SetVertexColor(.15, .25, 1, .35);
                        button:LockHighlight();
                        button.wasSelected = true;
                    elseif (button.wasSelected) then
                        button.wasSelected = nil;
                        button:GetHighlightTexture():SetVertexColor(.8, .8, 1, .15);
                        button:UnlockHighlight();
                    end
                else
                    if (button.wasSelected) then
                        button.wasSelected = nil;
                        button:GetHighlightTexture():SetVertexColor(.8, .8, 1, .15);
                        button:UnlockHighlight();
                    end
                    if MouseIsOver(button) then
                        EventTraceFrameEvent_OnEnter(button);
                    end
                end
            else
                button.index = index;
                button:Hide();
            end
        end
    end
end

function EventTraceFrame_StartEventCapture()
    if (_EventTraceFrame.started) then return; end

    _EventTraceFrame.started = true;
    _framesSinceLast = 0;
    _timeSinceLast   = 0;

    _EventTraceFrame:RegisterAllEvents();

    EventTraceFrame_OnEvent(_EventTraceFrame, "Begin Capture");
end

function EventTraceFrame_StopEventCapture()
    if (not _EventTraceFrame.started) then return; end

    _EventTraceFrame.started = false;
    _framesSinceLast = 0;
    _timeSinceLast   = 0;
    _EventTraceFrame:UnregisterAllEvents();
    EventTraceFrame_OnEvent(_EventTraceFrame, "End Capture");
end

local function _ChatPrint(msg)
    DEFAULT_CHAT_FRAME:AddMessage("|cff88ff88EventTrace:|r " .. msg);
end

function EventTraceFrame_HandleSlashCmd(msg)
    if (msg == nil) then msg = ""; end
    msg = string.lower(msg);

    -- /etrace add EVENT
    local _, _, addEvent = string.find(msg, "^add%s+(%S+)");
    if (addEvent) then
        addEvent = string.upper(addEvent);
        if (_EventTraceFrame.started) then
            _EventTraceFrame:RegisterEvent(addEvent);
        end
        _ChatPrint("added " .. addEvent);
        return;
    end

    -- /etrace remove EVENT
    local _, _, rmEvent = string.find(msg, "^remove%s+(%S+)");
    if (rmEvent) then
        rmEvent = string.upper(rmEvent);
        if (_EventTraceFrame.started) then
            _EventTraceFrame:UnregisterEvent(rmEvent);
        end
        _ChatPrint("removed " .. rmEvent);
        return;
    end

    -- /etrace ignore EVENT — persists across sessions via saved vars
    local _, _, igEvent = string.find(msg, "^ignore%s+(%S+)");
    if (igEvent) then
        igEvent = string.upper(igEvent);
        EventTraceFrame_AddFilter(igEvent);
        _ChatPrint("ignoring " .. igEvent);
        return;
    end

    -- /etrace unignore EVENT — opposite of ignore
    local _, _, unEvent = string.find(msg, "^unignore%s+(%S+)");
    if (unEvent) then
        unEvent = string.upper(unEvent);
        EventTraceFrame_RemoveFilter(unEvent);
        _ChatPrint("unignored " .. unEvent);
        return;
    end

    if (msg == "start") then
        EventTraceFrame_StartEventCapture();
    elseif (msg == "stop") then
        EventTraceFrame_StopEventCapture();
    elseif (msg == "pause") then
        EventTraceFrame_TogglePause();
        _ChatPrint(_EventTraceFrame.paused and "paused" or "resumed");
    elseif (msg == "filters") then
        if (DebugToolsSavedVars and DebugToolsSavedVars.Filters
            and DebugToolsSavedVars.Filters.User) then
            local list = DebugToolsSavedVars.Filters.User;
            _ChatPrint(string.format("filters (%d):", table.getn(list)));
            for i = 1, table.getn(list) do
                _ChatPrint("  " .. list[i]);
            end
        else
            _ChatPrint("no saved filters");
        end
    elseif (msg == "clear") then
        _EventTraceFrame.events   = {};
        _EventTraceFrame.times    = {};
        _EventTraceFrame.rawtimes = {};
        _EventTraceFrame.args     = { {}, {}, {}, {}, {}, {}, {}, {}, {} };
        _EventTraceFrame.lastIndex = 0;
        _EventTraceFrame.filteredIndices = nil;
        EventTraceFrame_Update();
    elseif (tonumber(msg) and tonumber(msg) > 0) then
        if (not _EventTraceFrame.started) then
            _EventTraceFrame.eventsToCapture = tonumber(msg);
            EventTraceFrame_StartEventCapture();
        end
    elseif (msg == "" or msg == "toggle") then
        if (not _EventTraceFrame:IsShown()) then
            _EventTraceFrame:Show();
            if (_EventTraceFrame.started == nil) then
                EventTraceFrame_StartEventCapture();
            end
        else
            _EventTraceFrame:Hide();
        end
    else
        _ChatPrint("usage:");
        _ChatPrint("  /etrace                -- toggle window (auto-start first time)");
        _ChatPrint("  /etrace start          -- begin event capture");
        _ChatPrint("  /etrace stop           -- end event capture");
        _ChatPrint("  /etrace pause          -- pause/resume in-place");
        _ChatPrint("  /etrace <N>            -- capture next N events");
        _ChatPrint("  /etrace add <EV>       -- register additional event");
        _ChatPrint("  /etrace remove <EV>    -- unregister event");
        _ChatPrint("  /etrace ignore <EV>    -- hide event (persists)");
        _ChatPrint("  /etrace unignore <EV>  -- remove from ignore list");
        _ChatPrint("  /etrace filters        -- list saved ignored events");
        _ChatPrint("  /etrace clear          -- clear log");
        _ChatPrint("  (toolbar has Pause + Args/Time toggles + Search)");
    end
end

function EventTraceFrame_OnMouseWheel(self, delta)
    local scrollBar = getglobal("EventTraceFrameScroll");
    local minVal, maxVal = scrollBar:GetMinMaxValues();
    local currentValue = scrollBar:GetValue();

    local newValue = currentValue - (delta * 3);
    if (newValue < minVal) then newValue = minVal; end
    if (newValue > maxVal) then newValue = maxVal; end
    if (newValue ~= currentValue) then
        scrollBar:SetValue(newValue);
    end
end

local TIME_ENTRY_FORMAT     = "Time:";
local DETAILS_ENTRY_FORMAT  = "Details:";
local ARGUMENT_ENTRY_FORMAT = "arg %d:";

-- `||` is WoW's escape for a literal pipe in FontString display text.
-- Arg values carrying `|c...|r` color codes or `|Hitem:...|h` hyperlinks
-- (chat-event args, combat-log fields, anything from a Lua addon that
-- pushed a colored string) would otherwise get re-interpreted by the
-- renderer when concatenated into a row. A stray unterminated color
-- code leaks state into the rest of the row text.
function _EventTrace_EscapePipes(s)
    return (string.gsub(s, "|", "||"));
end

-- Global (not local) because `_EventTrace_FormatRow` defined earlier in
-- this chunk calls it — a `local function` here wouldn't be in scope at
-- that earlier declaration site in Lua 5.0.
function EventTrace_FormatArgValue(val)
    local t = type(val);
    if (t == "string") then
        return string.format('"%s"', _EventTrace_EscapePipes(val));
    elseif (t == "number") then
        return tostring(val);
    elseif (t == "boolean") then
        return string.format('|cffaaaaff%s|r', tostring(val));
    elseif (t == "table" or t == "function" or t == "userdata") then
        return string.format('|cffffaaaa%s|r',
                             _EventTrace_EscapePipes(tostring(val)));
    end
    return _EventTrace_EscapePipes(tostring(val));
end

function EventTraceFrameEvent_DisplayTooltip(eventButton)
    local index = eventButton.index;
    if (not index) then return; end

    local tooltip = EventTraceTooltip;
    tooltip:SetOwner(eventButton, "ANCHOR_NONE");
    tooltip:SetPoint("TOPLEFT", eventButton, "TOPRIGHT", 24, 2);
    local timeString = _EventTraceFrame.times[index];
    if (EVENT_TRACE_SYSTEM_TIMES[timeString]) then
        tooltip:AddLine(timeString, 1, 1, 1);
        tooltip:AddDoubleLine(TIME_ENTRY_FORMAT,
                              tostring(_EventTraceFrame.rawtimes[index]),
                              1, .82, 0, 1, 1, 1);
        tooltip:AddDoubleLine(DETAILS_ENTRY_FORMAT,
                              _EventTraceFrame.events[index],
                              1, .82, 0, 1, 1, 1);
    else
        tooltip:AddLine(_EventTraceFrame.events[index], 1, 1, 1);
        tooltip:AddDoubleLine(TIME_ENTRY_FORMAT,
                              tostring(_EventTraceFrame.rawtimes[index]),
                              1, .82, 0, 1, 1, 1);
        for k, v in ipairs(_EventTraceFrame.args) do
            if (v[index] ~= nil) then
                tooltip:AddDoubleLine(string.format(ARGUMENT_ENTRY_FORMAT, k),
                                      EventTrace_FormatArgValue(v[index]),
                                      1, .82, 0, 1, 1, 1);
            end
        end
    end
    tooltip:Show();
end

function EventTraceFrameEvent_OnEnter(self)
    if (_EventTraceFrame.selectedEvent) then return; end
    EventTraceFrameEvent_DisplayTooltip(self);
end

function EventTraceFrameEvent_OnLeave(self)
    if (not _EventTraceFrame.selectedEvent) then
        EventTraceTooltip:Hide();
    end
end

function EventTraceFrameEvent_OnClick(self)
    if (_EventTraceFrame.selectedEvent == self.index) then
        _EventTraceFrame.selectedEvent = nil;
    else
        _EventTraceFrame.selectedEvent = self.index;
    end
    EventTraceFrame_Update();
end

------------------------------------------------------------------------------
-- ScriptErrorsFrame
------------------------------------------------------------------------------

local ERROR_FORMAT = "|cffffd200Message:|r|cffffffff %s|r\n"
                  .. "|cffffd200Time:|r|cffffffff %s|r\n"
                  .. "|cffffd200Count:|r|cffffffff %s|r\n"
                  .. "|cffffd200Stack:|r|cffffffff %s|r";

local INDEX_ORDER_FORMAT = "%d / %d";

local _ScriptErrorsFrame;
local _previousErrorHandler;

function ScriptErrorsFrame_OnLoad(self)
    local titleFS = getglobal(self:GetName() .. "Title");
    if (titleFS) then
        titleFS:SetText("Lua Error");
    end
    self:RegisterForDrag("LeftButton");
    self.seen     = {};
    self.order    = {};
    self.count    = {};
    self.messages = {};
    self.times    = {};
    _ScriptErrorsFrame = self;
end

function ScriptErrorsFrame_OnShow(self)
    ScriptErrorsFrame_Update();
end

function ScriptErrorsFrame_OnError(message, keepHidden)
    if (message == nil) then return; end
    local stack = debugstack(DEBUGLOCALS_LEVEL) or "";

    local messageStack = message .. stack;

    if (_ScriptErrorsFrame) then
        local index = _ScriptErrorsFrame.seen[messageStack];
        if (index) then
            _ScriptErrorsFrame.count[index]    = _ScriptErrorsFrame.count[index] + 1;
            _ScriptErrorsFrame.messages[index] = message;
            _ScriptErrorsFrame.times[index]    = date();
        else
            table.insert(_ScriptErrorsFrame.order, stack);
            index = table.getn(_ScriptErrorsFrame.order);
            _ScriptErrorsFrame.count[index]    = 1;
            _ScriptErrorsFrame.messages[index] = message;
            _ScriptErrorsFrame.times[index]    = date();
            _ScriptErrorsFrame.seen[messageStack] = index;
        end

        if (not _ScriptErrorsFrame:IsShown() and not keepHidden) then
            _ScriptErrorsFrame.index = index;
            _ScriptErrorsFrame:Show();
        else
            ScriptErrorsFrame_Update();
        end
    end

    -- Chain through to whatever previous handler existed (e.g. BugGrabber)
    if (_previousErrorHandler and _previousErrorHandler ~= ScriptErrorsFrame_OnError) then
        pcall(_previousErrorHandler, message);
    end
end

function ScriptErrorsFrame_Update()
    local editBox = getglobal("ScriptErrorsFrameScrollFrameText");
    local index = _ScriptErrorsFrame.index;
    if (not index or not _ScriptErrorsFrame.order[index]) then
        index = table.getn(_ScriptErrorsFrame.order);
        _ScriptErrorsFrame.index = index;
    end

    if (index == 0) then
        editBox:SetText("");
        ScriptErrorsFrame_UpdateButtons();
        return;
    end

    local text = string.format(ERROR_FORMAT,
        _ScriptErrorsFrame.messages[index],
        _ScriptErrorsFrame.times[index],
        _ScriptErrorsFrame.count[index],
        _ScriptErrorsFrame.order[index]);

    editBox:SetText(text);
    editBox:HighlightText(0);
    editBox:SetCursorPosition(0);

    local scrollFrame = getglobal("ScriptErrorsFrameScrollFrame");
    if (scrollFrame and scrollFrame.SetVerticalScroll) then
        scrollFrame:SetVerticalScroll(0);
    end

    ScriptErrorsFrame_UpdateButtons();
end

function ScriptErrorsFrame_UpdateButtons()
    local index = _ScriptErrorsFrame.index;
    local numErrors = table.getn(_ScriptErrorsFrame.order);
    local nextBtn = getglobal("ScriptErrorsFrameNext");
    local prevBtn = getglobal("ScriptErrorsFramePrevious");
    local idxLbl  = getglobal("ScriptErrorsFrameIndexLabel");

    if (index == 0 or numErrors == 0) then
        nextBtn:Disable();
        prevBtn:Disable();
    else
        if (numErrors == 1) then
            nextBtn:Disable();
            prevBtn:Disable();
        elseif (index == 1) then
            nextBtn:Enable();
            prevBtn:Disable();
        elseif (index == numErrors) then
            nextBtn:Disable();
            prevBtn:Enable();
        else
            nextBtn:Enable();
            prevBtn:Enable();
        end
    end

    idxLbl:SetText(string.format(INDEX_ORDER_FORMAT, index, numErrors));
end

function ScriptErrorsFrameButton_OnClick(self)
    local id = self:GetID();
    if (id == 1) then
        _ScriptErrorsFrame.index = _ScriptErrorsFrame.index + 1;
    else
        _ScriptErrorsFrame.index = _ScriptErrorsFrame.index - 1;
    end
    ScriptErrorsFrame_Update();
end

function ScriptErrorsFrame_TitleButton_OnDragStart()
    _ScriptErrorsFrame.moving = true;
    _ScriptErrorsFrame:StartMoving();
end

function ScriptErrorsFrame_TitleButton_OnDragStop()
    _ScriptErrorsFrame.moving = nil;
    _ScriptErrorsFrame:StopMovingOrSizing();
end

function ScriptErrorsFrame_HandleSlashCmd(msg)
    if (msg == nil) then msg = ""; end
    msg = string.lower(msg);
    if (msg == "on" or msg == "install") then
        _previousErrorHandler = geterrorhandler();
        seterrorhandler(ScriptErrorsFrame_OnError);
        _ChatPrint("Lua error capture installed.");
    elseif (msg == "off" or msg == "uninstall") then
        if (_previousErrorHandler) then
            seterrorhandler(_previousErrorHandler);
        end
        _ChatPrint("Lua error capture uninstalled.");
    elseif (msg == "test") then
        ScriptErrorsFrame_OnError("test error from /luaerrors test");
    elseif (msg == "clear") then
        _ScriptErrorsFrame.seen     = {};
        _ScriptErrorsFrame.order    = {};
        _ScriptErrorsFrame.count    = {};
        _ScriptErrorsFrame.messages = {};
        _ScriptErrorsFrame.times    = {};
        _ScriptErrorsFrame.index    = 0;
        ScriptErrorsFrame_Update();
    else
        if (_ScriptErrorsFrame:IsShown()) then
            _ScriptErrorsFrame:Hide();
        else
            _ScriptErrorsFrame:Show();
        end
    end
end

------------------------------------------------------------------------------
-- FrameStackTooltip
------------------------------------------------------------------------------

function DebugTooltip_OnLoad(self)
    self:SetFrameLevel(self:GetFrameLevel() + 2);
    if (TOOLTIP_DEFAULT_COLOR and self.SetBackdropBorderColor) then
        self:SetBackdropBorderColor(TOOLTIP_DEFAULT_COLOR.r,
                                    TOOLTIP_DEFAULT_COLOR.g,
                                    TOOLTIP_DEFAULT_COLOR.b);
    end
    if (TOOLTIP_DEFAULT_BACKGROUND_COLOR and self.SetBackdropColor) then
        self:SetBackdropColor(TOOLTIP_DEFAULT_BACKGROUND_COLOR.r,
                              TOOLTIP_DEFAULT_BACKGROUND_COLOR.g,
                              TOOLTIP_DEFAULT_BACKGROUND_COLOR.b);
    end
end

function FrameStackTooltip_Toggle(showHidden)
    local tooltip = FrameStackTooltip;
    if (tooltip:IsVisible()) then
        tooltip:Hide();
    else
        tooltip:SetOwner(UIParent, "ANCHOR_NONE");
        tooltip:SetPoint("BOTTOMRIGHT", UIParent, "BOTTOMRIGHT", -32, 96);
        tooltip.default = 1;
        tooltip.showHidden = showHidden;
        tooltip:SetFrameStack(showHidden);
    end
end

FRAMESTACK_UPDATE_TIME = .1;
local _frameStackAccum = 0;

function FrameStackTooltip_OnUpdate(self, elapsed)
    _frameStackAccum = _frameStackAccum + elapsed;
    if (_frameStackAccum >= FRAMESTACK_UPDATE_TIME) then
        _frameStackAccum = 0;
        self:SetFrameStack(self.showHidden);
    end
end

function FrameStackTooltip_OnShow(self)
    local parent = self:GetParent() or UIParent;
    local ps = parent:GetEffectiveScale();
    local px, py = parent:GetCenter();
    if (not px or not py) then return; end
    px, py = px * ps, py * ps;
    local x, y = GetCursorPosition();
    self:ClearAllPoints();
    if (x > px) then
        if (y > py) then
            self:SetPoint("BOTTOMLEFT", parent, "BOTTOMLEFT", 20, 20);
        else
            self:SetPoint("TOPLEFT", parent, "TOPLEFT", 20, -20);
        end
    else
        if (y > py) then
            self:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", -20, 20);
        else
            self:SetPoint("TOPRIGHT", parent, "TOPRIGHT", -20, -20);
        end
    end
end

FrameStackTooltip_OnEnter = FrameStackTooltip_OnShow;

function FrameStackTooltip_HandleSlashCmd(msg)
    if (msg == nil) then msg = ""; end
    local showHidden = (msg == "true" or string.find(msg, "hidden") ~= nil);
    FrameStackTooltip_Toggle(showHidden);
end

------------------------------------------------------------------------------
-- Slash command registration
------------------------------------------------------------------------------

SLASH_ETRACE1 = "/etrace";
SlashCmdList["ETRACE"] = EventTraceFrame_HandleSlashCmd;

SLASH_FRAMESTACK1 = "/framestack";
SLASH_FRAMESTACK2 = "/fstack";
SlashCmdList["FRAMESTACK"] = FrameStackTooltip_HandleSlashCmd;

SLASH_LUAERRORS1 = "/luaerrors";
SLASH_LUAERRORS2 = "/scripterrors";
SlashCmdList["LUAERRORS"] = ScriptErrorsFrame_HandleSlashCmd;
