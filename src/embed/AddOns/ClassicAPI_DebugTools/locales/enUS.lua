------------------------------------------------------------------------------
-- ClassicAPI_DebugTools: English defaults.
-- Every string that appears in zhCN.lua must have a default here so that
-- other locales (or the game running in English) still work.
------------------------------------------------------------------------------

-- [[ FrameStack / /fstack / texture info (DebugTools.lua) ]]
TI_INVALID_COORDS = "invalid coordinates"
TI_FILE_LABEL     = "File"
FS_FSOBJ_LABEL    = "fsobj = %s"

-- [[ Table Inspector main window (TableInspector.lua + .xml) ]]
TI_NO_TABLE_SELECTED = "No Table Selected"
TI_FRAME_ATTRIBUTES  = "Frame Attributes - "
TI_TABLE_ATTRIBUTES  = "Table Attributes"

TI_SHOW              = "Show"
TI_HIGHLIGHT         = "Highlight"
TI_DYNAMIC_UPDATES   = "Dynamic Updates"
TI_ANCHORS_TITLE     = "anchor(s)"

-- [[ Profiler: window (Profiler/Window.lua) ]]
PROFILER_TITLE      = "Profiler"
PROFILER_ANALYZER   = "Analyzer"
PROFILER_COPIED     = "Copied %s bytes to the clipboard."
PROFILER_FRAME_HIDDEN = "%s is hidden."

PROFILER_SEARCH_PLACEHOLDER = "Search name, owner or source"

PROFILER_COPY       = "Copy"
PROFILER_COPY_TIP   = "Copy the shown rows to the clipboard as plain text."

PROFILER_RESET      = "Reset"
PROFILER_RESET_TIP  = "Set every CPU time, call count, allocation and event count to zero."

PROFILER_ON  = "ON"
PROFILER_OFF = "OFF"

PROFILER_DEEP_TRACKING     = "Deep Tracking: "
PROFILER_DEEP_TRACKING_TIP = "Hooks the OnUpdate and OnEvent scripts of every frame to measure the Lua memory each frame allocates, and the CPU time and memory of each event. The hooks never replace a handler, but they add a little time to every script call. They stay until /reload."

PROFILER_CPU_PROFILING      = "CPU Profiling: "
PROFILER_CPU_PROFILING_TIP_ON  = "Turn CPU profiling off"
PROFILER_CPU_PROFILING_TIP_OFF = "Turn CPU profiling on"
PROFILER_CPU_PROFILING_TIP_BODY = " and reload the UI.\n\nProfiling measures the CPU time of every addon, frame, function and event. It makes each Lua call slower, so turn it off when you are done."

-- [[ Profiler: slash commands (Profiler/Core.lua) ]]
PROFILER_RESET_MSG = "CPU and allocation counters reset."
PROFILER_SLASH_1 = "/profiler - toggle the analyzer"
PROFILER_SLASH_2 = "/profiler widget - toggle the memory widget"
PROFILER_SLASH_3 = "/profiler reset - reset CPU and allocation counters"
PROFILER_SLASH_4 = "/profiler profile on|off - CPU profiling (reloads the UI)"

-- [[ Profiler: tab names (passed to ns.AddTab) ]]
PROFILER_TAB_ADDONS  = "Addons"
PROFILER_TAB_FRAMES  = "Frames"
PROFILER_TAB_EVENTS  = "Events"
PROFILER_TAB_GLOBALS = "Globals"

-- [[ Profiler: "Owner" etc. labels shared by several tooltips ]]
PROFILER_LABEL_OWNER      = "Owner"
PROFILER_LABEL_TYPE       = "Type"
PROFILER_LABEL_STATE      = "State"
PROFILER_LABEL_ALL_SCRIPTS = "All scripts"
PROFILER_LABEL_ALLOCATED  = "Allocated"
PROFILER_LABEL_EVENTS     = "Events"
PROFILER_LABEL_LISTENERS  = "Listeners"
PROFILER_LABEL_FIRED      = "Fired"
PROFILER_LABEL_HANDLERS_RAN = "Handlers ran"
PROFILER_LABEL_HANDLER_CPU = "Handler CPU"
PROFILER_LABEL_CPU        = "CPU"
PROFILER_LABEL_CALLS      = "Calls"
PROFILER_LABEL_PER_CALL   = "Per call"
PROFILER_LABEL_ENTRIES    = "Entries"
PROFILER_LABEL_DEBUG_NAME = "Debug name"
PROFILER_LABEL_VALUE      = "Value"

PROFILER_STATE_VISIBLE     = "visible"
PROFILER_STATE_SHOWN_HIDDEN = "shown, parent hidden"
PROFILER_STATE_HIDDEN     = "hidden"

PROFILER_MORE_LISTENERS   = "and %d more"
PROFILER_CALLS_FORMAT     = " in %s calls"

PROFILER_LABEL_CLICK_SHOW_FRAMES = "Click: show its frames. Right-click: more."
PROFILER_LABEL_RIGHT_CLICK_HINT  = "Right-click: highlight, inspect or copy."
PROFILER_LABEL_TURN_ON_PROFILING = "Turn on CPU profiling to see CPU times."
PROFILER_LABEL_NOT_CLASSICAPI    = "Not ClassicAPI's function: something replaced or hooked it."
PROFILER_LABEL_REPLACED          = " (replaced)"

-- [[ Profiler: Addons tab (Profiler/AddonsTab.lua) ]]
-- column titles
PROFILER_ADDONS_COL_ADDON       = "Addon"
PROFILER_ADDONS_COL_MEMORY      = "Memory"
PROFILER_ADDONS_COL_ALLOC_S     = "Alloc/s"
PROFILER_ADDONS_COL_PEAK_S      = "Peak/s"
PROFILER_ADDONS_COL_CPU_S       = "CPU/s"
PROFILER_ADDONS_COL_CPU_PCT     = "CPU %"
PROFILER_ADDONS_COL_CPU_TOTAL   = "CPU Total"

-- column tips
PROFILER_ADDONS_TIP_MEMORY = "The Lua memory that the addon's objects use now."
PROFILER_ADDONS_TIP_ALLOC_S = "The Lua memory the addon allocated in the last second. Allocations become garbage that the collector must free. A second with a garbage collection keeps the previous value."
PROFILER_ADDONS_TIP_PEAK_S  = "The highest Alloc/s since the last reset."
PROFILER_ADDONS_TIP_CPU_S   = "The CPU time of the addon's own functions in the last second. Needs CPU profiling."
PROFILER_ADDONS_TIP_CPU_PCT = "The addon's share of the CPU time of all addons in the last second."
PROFILER_ADDONS_TIP_CPU_TOTAL = "The CPU time of the addon's own functions since the last reset."

-- tooltip
PROFILER_ADDONS_TT_MEMORY       = "Memory"
PROFILER_ADDONS_TT_ALLOCATED_S  = "Allocated per second"
PROFILER_ADDONS_TT_PEAK_S       = "Peak per second"
PROFILER_ADDONS_TT_CPU_S        = "CPU per second"
PROFILER_ADDONS_TT_CPU_RESET    = "CPU since reset"

-- menu
PROFILER_ADDONS_MENU_SHOW_FRAMES  = "Show frames"
PROFILER_ADDONS_MENU_SHOW_GLOBALS = "Show globals"
PROFILER_ADDONS_MENU_COPY_NAME    = "Copy name"

-- status line
PROFILER_ADDONS_STATUS = "%d addons   Lua %s / %s"
PROFILER_ADDONS_STATUS_SCRIPTS = "   scripts %s/s   events %s/s (%.0f fired/s)"

-- [[ Profiler: Frames tab (Profiler/FramesTab.lua) ]]
PROFILER_FRAMES_COL_FRAME     = "Frame"
PROFILER_FRAMES_COL_OWNER     = "Owner"
PROFILER_FRAMES_COL_CPU_S     = "CPU/s"
PROFILER_FRAMES_COL_CALLS_S   = "Calls/s"
PROFILER_FRAMES_COL_PER_CALL  = "Per Call"
PROFILER_FRAMES_COL_ALLOC_S   = "Alloc/s"
PROFILER_FRAMES_COL_CPU_TOTAL = "CPU Total"

PROFILER_FRAMES_TIP_OWNER = "The addon that made the frame's OnUpdate or OnEvent handler, or the frame itself."
PROFILER_FRAMES_TIP_CPU_S = "The CPU time of all the frame's scripts in the last second. Needs CPU profiling."
PROFILER_FRAMES_TIP_CALLS_S = "How often the frame's scripts ran in the last second. Needs CPU profiling."
PROFILER_FRAMES_TIP_PER_CALL = "The average CPU time of one script call since the last reset."
PROFILER_FRAMES_TIP_ALLOC_S = "The Lua memory the frame's OnUpdate and OnEvent scripts allocated in the last second. Needs deep tracking."
PROFILER_FRAMES_TIP_CPU_TOTAL = "The CPU time of all the frame's scripts since the last reset."

PROFILER_FRAMES_MENU_HIGHLIGHT = "Highlight"
PROFILER_FRAMES_MENU_INSPECT   = "Inspect"
PROFILER_FRAMES_MENU_COPY_NAME = "Copy name"
PROFILER_FRAMES_MENU_COPY_SRC  = "Copy source location"
PROFILER_FRAMES_MENU_SHOW_OWNER = "Show %s frames"

PROFILER_FRAMES_STATUS = "%d frames   %d listed"
PROFILER_FRAMES_STATUS_PROMPT = "Turn on CPU profiling or deep tracking to measure frames."
PROFILER_FRAMES_STATUS_DEEP  = "   deep tracking on"

-- [[ Profiler: Events tab (Profiler/EventsTab.lua) ]]
PROFILER_EVENTS_COL_EVENT   = "Event"
PROFILER_EVENTS_COL_FIRES_S = "Fires/s"
PROFILER_EVENTS_COL_LISTENERS = "Listeners"
PROFILER_EVENTS_COL_CPU_S   = "CPU/s"
PROFILER_EVENTS_COL_ALLOC_S = "Alloc/s"
PROFILER_EVENTS_COL_FIRED   = "Fired"

PROFILER_EVENTS_TIP_FIRES_S  = "How often the event fired in the last second."
PROFILER_EVENTS_TIP_LISTENERS = "The frames registered for the event."
PROFILER_EVENTS_TIP_CPU_S    = "The CPU time of the event's OnEvent handlers in the last second. Needs deep tracking and CPU profiling."
PROFILER_EVENTS_TIP_ALLOC_S  = "The Lua memory the event's OnEvent handlers allocated in the last second. Needs deep tracking."
PROFILER_EVENTS_TIP_FIRED    = "How often the event fired since the analyzer opened or the last reset."

PROFILER_EVENTS_MENU_COPY_NAME = "Copy name"

PROFILER_EVENTS_STATUS = "%d events seen   %.0f fired/s"
PROFILER_EVENTS_STATUS_PROMPT = "Turn on deep tracking for the CPU time and memory of each event."

-- [[ Profiler: Globals tab (Profiler/GlobalsTab.lua) ]]
PROFILER_GLOBALS_COL_NAME = "Name"
PROFILER_GLOBALS_COL_TYPE = "Type"
PROFILER_GLOBALS_COL_OWNER = "Owner"
PROFILER_GLOBALS_COL_CPU  = "CPU"
PROFILER_GLOBALS_COL_CALLS = "Calls"

PROFILER_GLOBALS_TIP_OWNER = "The addon that made the value. Strings, numbers and booleans have no owner."
PROFILER_GLOBALS_TIP_CPU   = "The CPU time of the function since the last reset. Needs CPU profiling."
PROFILER_GLOBALS_TIP_CALLS = "How often the function ran since the last reset. Needs CPU profiling."

PROFILER_GLOBALS_MENU_INSPECT   = "Inspect"
PROFILER_GLOBALS_MENU_HIGHLIGHT = "Highlight"
PROFILER_GLOBALS_MENU_COPY_NAME = "Copy name"

PROFILER_GLOBALS_RESCAN        = "Rescan"
PROFILER_GLOBALS_RESCAN_TIP    = "Read the global namespace again. Also updates the CPU times."
PROFILER_GLOBALS_SHOW_FRAMEXML = "Show FrameXML"
PROFILER_GLOBALS_SHOW_FRAMEXML_TIP = "Also list the values the game's own code made, and plain strings, numbers and booleans. With CPU profiling this shows the cost of every game API function."

PROFILER_GLOBALS_STATUS = "%d globals listed   %d ClassicAPI functions replaced or hooked"

-- [[ Profiler: widget (Profiler/Widget.lua) ]]
PROFILER_WIDGET_MEMORY        = "Memory"
PROFILER_WIDGET_RATE          = "Rate"
PROFILER_WIDGET_LAST_CLEANUP  = "Last Cleanup"
PROFILER_WIDGET_FRAMERATE     = "Framerate"
PROFILER_WIDGET_LUA_CPU       = "Lua CPU"
PROFILER_WIDGET_TOP_ALLOC     = "Top Alloc"
PROFILER_WIDGET_TOP_CPU       = "Top CPU"
PROFILER_WIDGET_LAST_PEAK     = "Last Peak (100ms):|r "
PROFILER_WIDGET_COLLECTED     = "collected "
PROFILER_WIDGET_NONE_YET      = "none yet"
PROFILER_WIDGET_PROFILING_OFF = "profiling off"
PROFILER_WIDGET_TT_TITLE      = "Profiler"
PROFILER_WIDGET_ALLOC_PER_SEC = "Allocated per second"
PROFILER_WIDGET_CPU_PER_SEC   = "CPU per second"
PROFILER_WIDGET_LEFT_CLICK    = "Left-click: reset the peak"
PROFILER_WIDGET_RIGHT_CLICK   = "Right-click: open the analyzer"
PROFILER_WIDGET_DRAG          = "Drag: move"
