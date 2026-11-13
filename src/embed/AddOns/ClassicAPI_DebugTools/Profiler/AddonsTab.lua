-- Addons tab: the memory, allocation rate and CPU time of each addon.
local _, ns = ...

local stats = ns.stats

local function Profiled(fn)
  return function(addon)
    if ns.profiling then return fn(addon) end
  end
end

local tab = ns.AddTab(PROFILER_TAB_ADDONS)

tab.list = ns.CreateList(tab.frame, {
  sort = 3,
  columns = {
    {
      title = PROFILER_ADDONS_COL_ADDON,
      text = function(addon) return addon.title end,
      value = function(addon) return strlower(addon.name) end,
    },
    {
      title = PROFILER_ADDONS_COL_MEMORY, width = 70,
      tip = PROFILER_ADDONS_TIP_MEMORY,
      text = function(addon) return ns.KB(addon.mem) end,
      value = function(addon) return addon.mem end,
    },
    {
      title = PROFILER_ADDONS_COL_ALLOC_S, width = 76,
      tip = PROFILER_ADDONS_TIP_ALLOC_S,
      text = function(addon) return ns.KBRate(addon.memRate) end,
      value = function(addon) return addon.memRate end,
    },
    {
      title = PROFILER_ADDONS_COL_PEAK_S, width = 76,
      tip = PROFILER_ADDONS_TIP_PEAK_S,
      text = function(addon) return ns.KBRate(addon.peakRate) end,
      value = function(addon) return addon.peakRate end,
    },
    {
      title = PROFILER_ADDONS_COL_CPU_S, width = 64,
      tip = PROFILER_ADDONS_TIP_CPU_S,
      text = Profiled(function(addon) return ns.MS(addon.cpuRate) end),
      value = Profiled(function(addon) return addon.cpuRate end),
    },
    {
      title = PROFILER_ADDONS_COL_CPU_PCT, width = 50,
      tip = PROFILER_ADDONS_TIP_CPU_PCT,
      text = Profiled(function(addon) return addon.cpuShare and format("%.1f%%", addon.cpuShare) end),
      value = Profiled(function(addon) return addon.cpuShare end),
    },
    {
      title = PROFILER_ADDONS_COL_CPU_TOTAL, width = 70,
      tip = PROFILER_ADDONS_TIP_CPU_TOTAL,
      text = Profiled(function(addon) return ns.MS(addon.cpu) end),
      value = Profiled(function(addon) return addon.cpu end),
    },
  },

  tooltip = function(addon)
    GameTooltip:AddLine(addon.title)
    GameTooltip:AddLine(addon.name, .6, .6, .6)
    GameTooltip:AddLine(" ")
    GameTooltip:AddDoubleLine(PROFILER_ADDONS_TT_MEMORY, ns.KB(addon.mem), 1, 1, 1, 1, 1, 1)
    GameTooltip:AddDoubleLine(PROFILER_ADDONS_TT_ALLOCATED_S, ns.KBRate(addon.memRate), 1, 1, 1, 1, 1, 1)
    GameTooltip:AddDoubleLine(PROFILER_ADDONS_TT_PEAK_S, ns.KBRate(addon.peakRate), 1, 1, 1, 1, 1, 1)
    if ns.profiling then
      GameTooltip:AddDoubleLine(PROFILER_ADDONS_TT_CPU_S, ns.MS(addon.cpuRate), 1, 1, 1, 1, 1, 1)
      GameTooltip:AddDoubleLine(PROFILER_ADDONS_TT_CPU_RESET, ns.MS(addon.cpu), 1, 1, 1, 1, 1, 1)
    else
      GameTooltip:AddLine(PROFILER_LABEL_TURN_ON_PROFILING, .6, .6, .6)
    end
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine(PROFILER_LABEL_CLICK_SHOW_FRAMES, .6, .6, .6)
  end,

  click = function(addon)
    ns.ShowIn("Frames", addon.name)
  end,

  menu = function(addon, root)
    root:CreateTitle(addon.name)
    root:CreateButton(PROFILER_ADDONS_MENU_SHOW_FRAMES, function() ns.ShowIn("Frames", addon.name) end)
    root:CreateButton(PROFILER_ADDONS_MENU_SHOW_GLOBALS, function() ns.ShowIn("Globals", addon.name) end)
    root:CreateButton(PROFILER_ADDONS_MENU_COPY_NAME, function() ns.Copy(addon.name) end)
  end,
})
tab.list:SetAllPoints()

function tab:OnSelect()
  self.list:SetEntries(stats.list)
end

function tab:OnSample()
  self.list:SetEntries(stats.list)
end

function tab:Status()
  local text = format(PROFILER_ADDONS_STATUS, #stats.list, ns.KB(stats.mem or 0), ns.KB(stats.threshold or 0))
  if ns.profiling and stats.scriptRate then
    text = text .. format(PROFILER_ADDONS_STATUS_SCRIPTS,
      ns.MS(stats.scriptRate), ns.MS(stats.eventRate), stats.eventCountRate)
  end
  return text
end
