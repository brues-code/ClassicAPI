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
--   - The stack and locals are retail's GetErrorData, taken in the SetText
--     slot: GetErrorCallstackHeight finds the code that failed however many
--     handlers sit between it and the slot. No |K strings to remove, and no
--     C_StringUtil.EscapeDecimalNonPrintables.
--   - No scriptErrors cvar (1.12 always shows errors), no LUA_WARNING, no
--     gamepad legend.
--   - Lua 5.0: no `...` expression, no # operator.

local function GetErrorData()
	-- Example of how debug stack level is calculated
	-- Current stack: [1, 2, 3, 4, 5] (current function is at 1, total current height is 5)
	-- Stack at time of error: [1, 2] (these are currently now index 4 and 5, but at the time of error the stack height is 2)
	-- To calcuate the level to debug (4): curentStackHeight - (errorStackHeight - 1) = 5 - (2 - 1) = 4
	local currentStackHeight = GetCallstackHeight();
	local errorCallStackHeight = GetErrorCallstackHeight();
	local errorStackOffset = errorCallStackHeight and (errorCallStackHeight - 1);
	local debugStackLevel = currentStackHeight - (errorStackOffset or 0);
	local skipFunctionsAndUserdata = true;

	local stack = debugstack(debugStackLevel);
	local locals = debuglocals(debugStackLevel, skipFunctionsAndUserdata);

	return stack, locals;
end

local ERROR_FORMAT = "|cffffd200Message:|r|cffffffff %s|r\n"
	.. "|cffffd200Time:|r|cffffffff %s|r\n"
	.. "|cffffd200Count:|r|cffffffff %s|r\n"
	.. "|cffffd200Stack:|r\n"
	.. "|cffffffff%s|r\n"
	.. "|cffffd200Locals:|r\n"
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
		ScriptErrorsFrame_OnError(message, GetErrorData());
	end
	ScriptErrors.Show = function() end
end

function ScriptErrorsFrame_OnError(message, stack, locals)
	local self = ScriptErrorsFrame;
	message = tostring(message);
	stack = stack or "";
	locals = locals or "<none>";

	local messageKey = message .. "\n" .. stack;
	local index = self.seen[messageKey];
	if ( index ) then
		local errorData = self.errorData[index];
		errorData.count = errorData.count + 1;
	else
		table.insert(self.errorData, { count = 1, message = message, time = date(), stack = stack, locals = locals });
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
		self.ScrollingEditBox:SetText(string.format(ERROR_FORMAT, errorData.message, errorData.time, errorData.count, errorData.stack, errorData.locals));
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
