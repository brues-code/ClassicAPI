-- Backport of retail's Blizzard_ScriptErrorsFrame to vanilla 1.12 / Lua 5.0:
-- the window that shows each Lua error with its time, count and stack, and
-- pages through them. Retail always loads it; it lives here, not in the
-- load-on-demand ClassicAPI_DebugTools, so that it is there for the first
-- error.
--
-- How errors get here: 1.12's error handler (_ERRORMESSAGE, FrameXML\
-- BasicControls.xml) reports each one as
--     ScriptErrors_Message:SetText(message); ScriptErrors:Show();
-- The frame's OnLoad takes those two slots, the same two !BugGrabber takes,
-- so the stock box never shows and this window gets the error instead:
--   - An addon that loads later and takes the slots again (!BugGrabber, which
--     BugSack reads) replaces this window.
--   - An addon that calls seterrorhandler replaces _ERRORMESSAGE, so neither
--     shows; one that chains to the old handler (geterrorhandler) ends here.
-- The stock box stays hidden, so the handler's IsVisible gate always passes
-- and every error arrives. message() calls _ERRORMESSAGE, so its text shows
-- here too.
--
-- Implementation differences from the retail source:
--   - Global functions in the 3.3.5 style, not retail's
--     ScriptErrorsFrameMixin.
--   - The text is a ScrollingEditBoxTemplate with a MinimalScrollBar, the
--     ScrollBox pair retail uses for scrolling edit boxes, not retail's
--     ScrollFrameTemplate, which ClassicAPI does not port.
--   - The stack starts at a fixed level, as in 3.3.5; 1.12 has no
--     GetErrorCallstackHeight. The engine's error function (0x703B40) is C
--     and calls the handler, so from the SetText slot level 1 is the slot,
--     2 is _ERRORMESSAGE, 3 is that C function and 4 is the code that failed.
--     !BugGrabber's GrabError reads level 4 from the same slot. A handler
--     that chains to _ERRORMESSAGE adds one level on top.
--   - No Locals section: 1.12 has no debuglocals.
--   - No scriptErrors cvar (1.12 always shows errors), no LUA_WARNING, no
--     gamepad legend.
--   - Lua 5.0: no `...` expression, no # operator.

DEBUGLOCALS_LEVEL = 4;

local ERROR_FORMAT = "|cffffd200Message:|r|cffffffff %s|r\n"
	.. "|cffffd200Time:|r|cffffffff %s|r\n"
	.. "|cffffd200Count:|r|cffffffff %s|r\n"
	.. "|cffffd200Stack:|r\n"
	.. "|cffffffff%s|r";

local INDEX_ORDER_FORMAT = "%d / %d";

function ScriptErrorsFrame_OnLoad(self)
	self.index = 0;
	self.errorData = {};
	self.seen = {};

	-- No default text: with none set, an empty window would apply nil.
	self.ScrollingEditBox:SetDefaultTextEnabled(false);
	self.ScrollingEditBox:RegisterCallback("OnFocusGained", function(_, editBox)
		editBox:HighlightText(0);
	end, self);

	-- Retail turns the wheel on with the OnMouseWheel script; 1.12 does not.
	local scrollBox = self.ScrollingEditBox:GetScrollBox();
	scrollBox:EnableMouseWheel(true);
	ScrollUtil.RegisterScrollBoxWithScrollBar(scrollBox, self.ScrollBar);

	ScriptErrors_Message.SetText = function(_, message)
		ScriptErrorsFrame_OnError(message, debugstack(DEBUGLOCALS_LEVEL));
	end
	ScriptErrors.Show = function() end
end

function ScriptErrorsFrame_OnError(message, stack)
	local self = ScriptErrorsFrame;
	message = tostring(message);
	stack = stack or "";

	local messageKey = message .. "\n" .. stack;
	local index = self.seen[messageKey];
	if ( index ) then
		local errorData = self.errorData[index];
		errorData.count = errorData.count + 1;
	else
		table.insert(self.errorData, { count = 1, message = message, time = date(), stack = stack });
		index = table.getn(self.errorData);
		self.seen[messageKey] = index;
	end

	if ( not self:IsShown() ) then
		self.index = index;
		self:Show();
	else
		ScriptErrorsFrame_Update();
	end
end

function ScriptErrorsFrame_Update()
	local self = ScriptErrorsFrame;
	if ( not self.errorData[self.index] ) then
		self.index = table.getn(self.errorData);
	end

	-- SetText puts the cursor and the scroll back at the top.
	local errorData = self.errorData[self.index];
	if ( errorData ) then
		self.ScrollingEditBox:SetText(string.format(ERROR_FORMAT, errorData.message, errorData.time, errorData.count, errorData.stack));
		self.ScrollingEditBox:GetEditBox():HighlightText(0);
	else
		self.ScrollingEditBox:ClearText();
	end

	local numErrors = table.getn(self.errorData);
	ScriptErrorsFramePrevious:SetEnabled(numErrors > 1);
	ScriptErrorsFrameNext:SetEnabled(numErrors > 1);
	ScriptErrorsFrameIndexLabel:SetText(string.format(INDEX_ORDER_FORMAT, self.index, numErrors));
end

-- Previous / Next wrap around, as on retail.
function ScriptErrorsFrame_ChangeDisplayedIndex(delta)
	local self = ScriptErrorsFrame;
	local numErrors = table.getn(self.errorData);
	local index = self.index + delta;
	if ( index < 1 ) then
		index = numErrors;
	elseif ( index > numErrors ) then
		index = 1;
	end
	self.index = index;
	ScriptErrorsFrame_Update();
end

function ScriptErrorsFrame_HandleSlashCmd(msg)
	local self = ScriptErrorsFrame;
	if ( string.lower(msg or "") == "clear" ) then
		self.index = 0;
		self.errorData = {};
		self.seen = {};
		ScriptErrorsFrame_Update();
	elseif ( self:IsShown() ) then
		self:Hide();
	else
		self:Show();
	end
end
