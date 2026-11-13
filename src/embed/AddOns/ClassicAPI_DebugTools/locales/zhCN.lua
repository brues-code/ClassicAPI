------------------------------------------------------------------------------
-- ClassicAPI_DebugTools: 简体中文
------------------------------------------------------------------------------
if GetLocale() ~= "zhCN" then return end

-- [[ FrameStack / /fstack / texture info (DebugTools.lua) ]]
TI_INVALID_COORDS = "无效坐标"
TI_FILE_LABEL     = "文件"
FS_FSOBJ_LABEL    = "fsobj = %s"

-- [[ Table Inspector main window (TableInspector.lua + .xml) ]]
TI_NO_TABLE_SELECTED = "未选择表"
TI_FRAME_ATTRIBUTES  = "Frame 属性 - "
TI_TABLE_ATTRIBUTES  = "Table 属性"

TI_SHOW              = "显示"
TI_HIGHLIGHT         = "高亮"
TI_DYNAMIC_UPDATES   = "动态更新"
TI_ANCHORS_TITLE     = "锚点"

-- [[ Profiler: window (Profiler/Window.lua) ]]
PROFILER_TITLE      = "性能分析器"
PROFILER_ANALYZER   = "分析器"
PROFILER_COPIED     = "已复制 %s 字节到剪贴板。"
PROFILER_FRAME_HIDDEN = "%s 处于隐藏状态。"

PROFILER_SEARCH_PLACEHOLDER = "搜索名称、所有者或来源"

PROFILER_COPY       = "复制"
PROFILER_COPY_TIP   = "将当前显示的行以纯文本复制到剪贴板。"

PROFILER_RESET      = "重置"
PROFILER_RESET_TIP  = "将所有 CPU 时间、调用次数、内存分配和事件计数归零。"

PROFILER_DEEP_TRACKING_ON  = "深度追踪: 开"
PROFILER_DEEP_TRACKING_OFF = "深度追踪: 关"
PROFILER_DEEP_TRACKING_TIP = "挂接每个 Frame 的 OnUpdate 和 OnEvent 脚本，测量该 Frame 分配的 Lua 内存，以及每个事件的 CPU 时间和内存占用。钩子不会替换原有处理函数，但每次脚本调用会增加少量开销，直到 /reload 才会移除。"

PROFILER_CPU_PROFILING_ON   = "CPU 分析: 开"
PROFILER_CPU_PROFILING_OFF  = "CPU 分析: 关"
PROFILER_CPU_PROFILING_TIP_ON  = "关闭 CPU 分析"
PROFILER_CPU_PROFILING_TIP_OFF = "开启 CPU 分析"
PROFILER_CPU_PROFILING_TIP_BODY = " 并重载 UI。\n\n分析器会测量每个插件、Frame、函数和事件的 CPU 时间。这会让每次 Lua 调用变慢，完成后请及时关闭。"

-- [[ Profiler: slash commands (Profiler/Core.lua) ]]
PROFILER_RESET_MSG = "CPU 与内存分配计数器已重置。"
PROFILER_SLASH_1 = "/profiler - 打开或关闭分析器"
PROFILER_SLASH_2 = "/profiler widget - 打开或关闭内存小部件"
PROFILER_SLASH_3 = "/profiler reset - 重置 CPU 和内存分配计数器"
PROFILER_SLASH_4 = "/profiler profile on|off - CPU 分析（会重载 UI）"

-- [[ Profiler: tab names (passed to ns.AddTab) ]]
PROFILER_TAB_ADDONS  = "插件"
PROFILER_TAB_FRAMES  = "Frames"
PROFILER_TAB_EVENTS  = "事件"
PROFILER_TAB_GLOBALS = "全局变量"

-- [[ Profiler: "Owner" etc. labels shared by several tooltips ]]
PROFILER_LABEL_OWNER       = "所有者"
PROFILER_LABEL_TYPE        = "类型"
PROFILER_LABEL_STATE       = "状态"
PROFILER_LABEL_ALL_SCRIPTS = "所有脚本"
PROFILER_LABEL_ALLOCATED   = "已分配"
PROFILER_LABEL_EVENTS      = "事件"
PROFILER_LABEL_LISTENERS   = "监听器"
PROFILER_LABEL_FIRED       = "触发"
PROFILER_LABEL_HANDLERS_RAN = "处理函数执行次数"
PROFILER_LABEL_HANDLER_CPU = "处理函数 CPU"
PROFILER_LABEL_CPU         = "CPU"
PROFILER_LABEL_CALLS       = "调用次数"
PROFILER_LABEL_PER_CALL    = "每次调用"
PROFILER_LABEL_ENTRIES     = "条目数"
PROFILER_LABEL_DEBUG_NAME  = "调试名"
PROFILER_LABEL_VALUE       = "值"

PROFILER_STATE_VISIBLE       = "可见"
PROFILER_STATE_SHOWN_HIDDEN = "已显示，父级隐藏"
PROFILER_STATE_HIDDEN       = "隐藏"

PROFILER_MORE_LISTENERS = "还有 %d 个"
PROFILER_CALLS_FORMAT   = " 共 %s 次调用"

PROFILER_LABEL_CLICK_SHOW_FRAMES = "左键：显示其 Frame。右键：更多操作。"
PROFILER_LABEL_RIGHT_CLICK_HINT  = "右键：高亮、查看或复制。"
PROFILER_LABEL_TURN_ON_PROFILING = "开启 CPU 分析以查看 CPU 时间。"
PROFILER_LABEL_NOT_CLASSICAPI    = "不是 ClassicAPI 的函数：已被替换或挂接。"
PROFILER_LABEL_REPLACED          = "（已替换）"

-- [[ Profiler: Addons tab (Profiler/AddonsTab.lua) ]]
PROFILER_ADDONS_COL_ADDON       = "插件"
PROFILER_ADDONS_COL_MEMORY      = "内存"
PROFILER_ADDONS_COL_ALLOC_S     = "分配/s"
PROFILER_ADDONS_COL_PEAK_S      = "峰值/s"
PROFILER_ADDONS_COL_CPU_S       = "CPU/s"
PROFILER_ADDONS_COL_CPU_PCT     = "CPU 占比"
PROFILER_ADDONS_COL_CPU_TOTAL   = "CPU 总计"

PROFILER_ADDONS_TIP_MEMORY = "该插件当前对象占用的 Lua 内存。"
PROFILER_ADDONS_TIP_ALLOC_S = "上一秒内该插件分配的 Lua 内存。内存分配最终会变成垃圾，由收集器回收。若上一秒发生了垃圾回收，则保持上次的值。"
PROFILER_ADDONS_TIP_PEAK_S  = "上次重置以来的最高分配/s。"
PROFILER_ADDONS_TIP_CPU_S   = "上一秒内该插件自身函数的 CPU 时间。需要开启 CPU 分析。"
PROFILER_ADDONS_TIP_CPU_PCT = "上一秒内该插件在所有插件 CPU 时间中的占比。"
PROFILER_ADDONS_TIP_CPU_TOTAL = "上次重置以来该插件自身函数的 CPU 时间。"

PROFILER_ADDONS_TT_MEMORY       = "内存"
PROFILER_ADDONS_TT_ALLOCATED_S  = "每秒分配"
PROFILER_ADDONS_TT_PEAK_S       = "每秒峰值"
PROFILER_ADDONS_TT_CPU_S        = "每秒 CPU"
PROFILER_ADDONS_TT_CPU_RESET    = "重置后 CPU"

PROFILER_ADDONS_MENU_SHOW_FRAMES  = "显示 Frame"
PROFILER_ADDONS_MENU_SHOW_GLOBALS = "显示全局变量"
PROFILER_ADDONS_MENU_COPY_NAME    = "复制名称"

PROFILER_ADDONS_STATUS = "%d 个插件   Lua %s / %s"
PROFILER_ADDONS_STATUS_SCRIPTS = "   脚本 %s/s   事件 %s/s (%.0f 次触发/s)"

-- [[ Profiler: Frames tab (Profiler/FramesTab.lua) ]]
PROFILER_FRAMES_COL_FRAME     = "Frame"
PROFILER_FRAMES_COL_OWNER     = "所有者"
PROFILER_FRAMES_COL_CPU_S     = "CPU/s"
PROFILER_FRAMES_COL_CALLS_S   = "调用/s"
PROFILER_FRAMES_COL_PER_CALL  = "每次调用"
PROFILER_FRAMES_COL_ALLOC_S   = "分配/s"
PROFILER_FRAMES_COL_CPU_TOTAL = "CPU 总计"

PROFILER_FRAMES_TIP_OWNER = "为该 Frame 的 OnUpdate 或 OnEvent 提供处理函数的插件，或 Frame 自身。"
PROFILER_FRAMES_TIP_CPU_S = "上一秒内该 Frame 所有脚本的 CPU 时间。需要开启 CPU 分析。"
PROFILER_FRAMES_TIP_CALLS_S = "上一秒内该 Frame 脚本的运行次数。需要开启 CPU 分析。"
PROFILER_FRAMES_TIP_PER_CALL = "上次重置以来单次脚本调用的平均 CPU 时间。"
PROFILER_FRAMES_TIP_ALLOC_S = "上一秒内该 Frame OnUpdate 和 OnEvent 脚本分配的 Lua 内存。需要开启深度追踪。"
PROFILER_FRAMES_TIP_CPU_TOTAL = "上次重置以来该 Frame 所有脚本的 CPU 时间。"

PROFILER_FRAMES_MENU_HIGHLIGHT = "高亮"
PROFILER_FRAMES_MENU_INSPECT   = "查看结构"
PROFILER_FRAMES_MENU_COPY_NAME = "复制名称"
PROFILER_FRAMES_MENU_COPY_SRC  = "复制源码位置"
PROFILER_FRAMES_MENU_SHOW_OWNER = "显示 %s 的 Frame"

PROFILER_FRAMES_STATUS = "%d 个 Frame   已列出 %d 个"
PROFILER_FRAMES_STATUS_PROMPT = "开启 CPU 分析或深度追踪以测量 Frame 数据。"
PROFILER_FRAMES_STATUS_DEEP  = "   深度追踪已开启"

-- [[ Profiler: Events tab (Profiler/EventsTab.lua) ]]
PROFILER_EVENTS_COL_EVENT   = "事件"
PROFILER_EVENTS_COL_FIRES_S = "触发/s"
PROFILER_EVENTS_COL_LISTENERS = "监听器"
PROFILER_EVENTS_COL_CPU_S   = "CPU/s"
PROFILER_EVENTS_COL_ALLOC_S = "分配/s"
PROFILER_EVENTS_COL_FIRED   = "触发次数"

PROFILER_EVENTS_TIP_FIRES_S   = "上一秒内该事件的触发频率。"
PROFILER_EVENTS_TIP_LISTENERS = "注册了该事件的 Frame 数量。"
PROFILER_EVENTS_TIP_CPU_S     = "上一秒内该事件 OnEvent 处理函数的 CPU 时间。需要深度追踪和 CPU 分析。"
PROFILER_EVENTS_TIP_ALLOC_S   = "上一秒内该事件 OnEvent 处理函数分配的 Lua 内存。需要深度追踪。"
PROFILER_EVENTS_TIP_FIRED     = "分析器打开或上次重置以来该事件的总触发次数。"

PROFILER_EVENTS_MENU_COPY_NAME = "复制名称"

PROFILER_EVENTS_STATUS = "已看到 %d 个事件   每秒触发 %.0f 次"
PROFILER_EVENTS_STATUS_PROMPT = "开启深度追踪以查看每个事件的 CPU 时间和内存占用。"

-- [[ Profiler: Globals tab (Profiler/GlobalsTab.lua) ]]
PROFILER_GLOBALS_COL_NAME  = "名称"
PROFILER_GLOBALS_COL_TYPE  = "类型"
PROFILER_GLOBALS_COL_OWNER = "所有者"
PROFILER_GLOBALS_COL_CPU   = "CPU"
PROFILER_GLOBALS_COL_CALLS = "调用次数"

PROFILER_GLOBALS_TIP_OWNER = "创建该值的插件。字符串、数字和布尔值没有所有者。"
PROFILER_GLOBALS_TIP_CPU   = "上次重置以来该函数的 CPU 时间。需要开启 CPU 分析。"
PROFILER_GLOBALS_TIP_CALLS = "上次重置以来该函数的运行次数。需要开启 CPU 分析。"

PROFILER_GLOBALS_MENU_INSPECT   = "查看结构"
PROFILER_GLOBALS_MENU_HIGHLIGHT = "高亮"
PROFILER_GLOBALS_MENU_COPY_NAME = "复制名称"

PROFILER_GLOBALS_RESCAN           = "重新扫描"
PROFILER_GLOBALS_RESCAN_TIP       = "重新读取全局命名空间，同时刷新 CPU 时间数据。"
PROFILER_GLOBALS_SHOW_FRAMEXML    = "显示 FrameXML"
PROFILER_GLOBALS_SHOW_FRAMEXML_TIP = "同时列出游戏自身代码创建的值，以及普通字符串、数字和布尔值。开启 CPU 分析后可以看到每个游戏 API 函数的开销。"

PROFILER_GLOBALS_STATUS = "已列出 %d 个全局变量   %d 个 ClassicAPI 函数被替换或挂接"

-- [[ Profiler: widget (Profiler/Widget.lua) ]]
PROFILER_WIDGET_MEMORY        = "内存"
PROFILER_WIDGET_RATE          = "速率"
PROFILER_WIDGET_LAST_CLEANUP  = "上次清理"
PROFILER_WIDGET_FRAMERATE     = "帧率"
PROFILER_WIDGET_LUA_CPU       = "Lua CPU"
PROFILER_WIDGET_TOP_ALLOC     = "分配最多"
PROFILER_WIDGET_TOP_CPU       = "CPU 最多"
PROFILER_WIDGET_LAST_PEAK     = "上次峰值(100ms):|r "
PROFILER_WIDGET_COLLECTED     = "已回收 "
PROFILER_WIDGET_NONE_YET      = "尚无数据"
PROFILER_WIDGET_PROFILING_OFF = "分析未开启"
PROFILER_WIDGET_TT_TITLE      = "性能分析器"
PROFILER_WIDGET_ALLOC_PER_SEC = "每秒内存分配"
PROFILER_WIDGET_CPU_PER_SEC   = "每秒 CPU"
PROFILER_WIDGET_LEFT_CLICK    = "左键：重置峰值"
PROFILER_WIDGET_RIGHT_CLICK   = "右键：打开分析器"
PROFILER_WIDGET_DRAG          = "拖拽：移动"
