-- /fstack: the owner, script CPU time and allocations of the highlighted
-- frame, which FrameStackTooltip_OnTooltipSetFrameStack adds to the tooltip.
local _, ns = ...

local function Line(tooltip, left, right)
  tooltip:AddDoubleLine(left, right, .2, 1, .8, 1, 1, 1)
end

function ns.AddFrameStackLines(tooltip, region)
  if not region then return end
  -- a texture or font string runs no scripts: report its frame's
  local frame = region.GetScript and region or region:GetParent()

  tooltip:AddLine(" ")
  Line(tooltip, "Owner", ns.OwnerLabel(ns.Owner(region)))
  if not frame then return end

  for _, script in ipairs({ "OnUpdate", "OnEvent" }) do
    local fn = frame:GetScript(script)
    if fn then
      local text = ns.OwnerLabel(ns.Owner(fn))
      if ns.profiling then
        local ms, calls = GetFunctionCPUUsage(fn)
        text = text .. ns.GREY .. "  " .. ns.MS(ms) .. " in " .. ns.Count(calls) .. " calls"
      end
      Line(tooltip, script, text)
    end
  end

  if ns.profiling then
    local ms, calls = GetFrameCPUUsage(frame, false)
    Line(tooltip, "All scripts", ns.MS(ms) .. ns.GREY .. " in " .. ns.Count(calls) .. " calls")
  end

  local rec = ns.track.frames[frame]
  if rec and (rec.hookedUpdate or rec.hookedEvent) then
    Line(tooltip, "Allocated", ns.KB(rec.alloc) .. ns.GREY .. " (" .. ns.KBRate(rec.allocRate) .. ")")
  end
end
