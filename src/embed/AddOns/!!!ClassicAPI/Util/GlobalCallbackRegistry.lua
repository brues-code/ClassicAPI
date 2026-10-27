-- Backport of 3.3.5 FrameXML/GlobalCallbackRegistry.lua to vanilla 1.12 / Lua 5.0.
--
-- Implementation notes:
--   - 3.3.5 ref-counts frame events through a SetAttribute/OnAttributeChanged
--     dance (attribute mutations are taint-safe under secure templates).
--     ClassicAPI backports SetAttribute, but routing counters through it is
--     heavier than a plain table: every set lowercases the name, runs the
--     secure-attribute parsing, and fires OnAttributeChanged. So we keep an
--     ordinary {[event]=count} table and Register/Unregister on 0<->1.
--   - OnEvent takes its (self, event, ...) positionally (modern script args,
--     on by default) and forwards straight to TriggerEvent, falling back to
--     the event/arg globals if that switch is turned off.

EventRegistry = CreateFromMixins(CallbackRegistryMixin);

function EventRegistry:OnLoad()
	CallbackRegistryMixin.OnLoad(self);
	self:SetUndefinedEventsAllowed(true);

	self.eventCounts = {};
	self.frameEventFrame = CreateFrame("Frame");
	self.frameEventFrame:SetScript("OnEvent", function(frameEventFrame, evt, ...)
		if evt ~= nil then
			-- Modern positional script args (default): (self, event, payload...),
			-- forwarded with the exact arg count.
			self:TriggerEvent(evt, ...);
		else
			-- SetModernScriptArgs is off: read the event/arg globals 1.12 always
			-- sets. This path keeps the historical 9-arg cap.
			self:TriggerEvent(event, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
		end
	end);

	self.logoutFrame = CreateFrame("Frame");
	self.logoutFrame:RegisterEvent("PLAYER_LOGOUT");
	self.logoutFrame:SetScript("OnEvent", function()
		self.frameEventFrame:UnregisterAllEvents();
		self.frameEventFrame:SetScript("OnEvent", nil);
	end);
end

function EventRegistry:RegisterFrameEvent(frameEvent)
	local n = (self.eventCounts[frameEvent] or 0) + 1;
	self.eventCounts[frameEvent] = n;
	if n == 1 then
		self.frameEventFrame:RegisterEvent(frameEvent);
	end
end

function EventRegistry:UnregisterFrameEvent(frameEvent)
	local n = self.eventCounts[frameEvent] or 0;
	if n > 0 then
		n = n - 1;
		self.eventCounts[frameEvent] = n;
		if n == 0 then
			self.frameEventFrame:UnregisterEvent(frameEvent);
		end
	end
end

function EventRegistry:RegisterFrameEventAndCallback(frameEvent, ...)
	self:RegisterFrameEvent(frameEvent);
	return self:RegisterCallback(frameEvent, ...);
end

local function CreateCallbackHandle(cbr, cbrHandle, frameEvent)
	-- Wrapped in a table for future flexibility.
	local handle = {
		Unregister = function()
			cbr:UnregisterFrameEvent(frameEvent);
			cbrHandle:Unregister();
		end,
	};
	return handle;
end


function EventRegistry:RegisterFrameEventAndCallbackWithHandle(frameEvent, ...)
	self:RegisterFrameEvent(frameEvent);
	local cbrHandle = self:RegisterCallbackWithHandle(frameEvent, ...);
	return CreateCallbackHandle(self, cbrHandle, frameEvent);
end

function EventRegistry:UnregisterFrameEventAndCallback(frameEvent, ...)
	self:UnregisterFrameEvent(frameEvent);
	self:UnregisterCallback(frameEvent, ...);
end

function EventRegistry:GetEventCounts(...)
	local counts = {};
	for i = 1, select("#", ...) do
		local frameEvent = select(i, ...);
		local count = self.eventCounts[frameEvent] or "?";
		table.insert(counts, ("%s : %s"):format(frameEvent, tostring(count)));
	end

	return table.concat(counts, "\n");
end

EventRegistry:OnLoad();
