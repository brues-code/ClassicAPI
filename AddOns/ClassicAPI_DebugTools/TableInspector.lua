------------------------------------------------------------------------------
-- TableInspector.lua
--
-- Vanilla 1.12 backport of the modern Blizzard_TableInspector UX,
-- scoped to the "minimal" feature set: one navigable window with a
-- breadcrumb showing the current path, a scrollable list of (key,
-- value) rows for the current table, and click-to-drill / Back-to-pop
-- navigation through sub-tables.
--
-- Trade-offs vs the modern source:
--   * No DataProvider / ScrollBox infrastructure — we use the same
--     fixed-row + slider pattern EventTraceFrame uses (vanilla
--     FauxScrollFrame works too but we already validated the slider
--     approach in this addon).
--   * No anchor / attribute providers, no live polling — open once
--     against the current snapshot.
--   * No multi-tab or pinning — one window, navigate via Back.
--
-- API:
--   TableInspector_Show(tbl, label)   -- open with `tbl` as root, `label` shown in breadcrumb
--   TableInspector_Push(tbl, label)   -- drill in (called by row click)
--   TableInspector_Back()             -- pop one level
--   TableInspector_Refresh()          -- re-read the current level's live table (scroll kept)
--   TableInspector_Close()            -- hide the window
--   /tinspect <luaexpr>               -- evaluate `<luaexpr>` as Lua and open if result is a table
------------------------------------------------------------------------------

TABLE_INSPECTOR_ROW_HEIGHT  = 16;
TABLE_INSPECTOR_NUM_ROWS    = 22;

local _TableInspectorFrame;

-- Navigation stack — `{ {tbl=..., label=...}, ... }`. The last entry
-- is the active view. Push when drilling in, pop on Back.
local _navStack = {};

------------------------------------------------------------------------------
-- Key + value formatting
------------------------------------------------------------------------------

-- Returns three lists: numeric keys (sorted ascending), string keys
-- (sorted), and other-type keys (booleans, tables-as-keys, etc.).
-- The other-type bucket stays unsorted — there's no canonical order
-- across types and these are rare in practice.
local function _SortedKeys(tbl)
    local nums, strs, others = {}, {}, {};
    for k in pairs(tbl) do
        local t = type(k);
        if (t == "number") then
            table.insert(nums, k);
        elseif (t == "string") then
            table.insert(strs, k);
        else
            table.insert(others, k);
        end
    end
    table.sort(nums);
    table.sort(strs);
    return nums, strs, others;
end

-- Returns (displayText, valueType, isDrillable). `isDrillable` is true
-- only for tables (the only type that can be navigated into).
local function _FormatValue(v)
    local t = type(v);
    if (t == "string") then
        -- Truncate very long strings to keep rows scannable; user can
        -- always tooltip-hover for the full content if needed.
        if (string.len(v) > 80) then
            return string.format('"%s..."', string.sub(v, 1, 77)), "string", false;
        end
        return string.format('"%s"', v), "string", false;
    elseif (t == "number") then
        return tostring(v), "number", false;
    elseif (t == "boolean") then
        return v and "true" or "false", "boolean", false;
    elseif (t == "table") then
        return tostring(v), "table", true;
    elseif (t == "function") then
        return tostring(v), "function", false;
    elseif (t == "userdata") then
        return tostring(v), "userdata", false;
    end
    return tostring(v), t, false;
end

local _TYPE_COLORS = {
    ["string"]   = { 1.00, 0.75, 0.40 },
    ["number"]   = { 0.60, 0.85, 1.00 },
    ["boolean"]  = { 0.70, 0.70, 1.00 },
    ["table"]    = { 0.60, 1.00, 0.60 },
    ["function"] = { 1.00, 0.60, 0.60 },
    ["userdata"] = { 1.00, 0.60, 0.60 },
};

local function _ValueColor(t)
    local c = _TYPE_COLORS[t];
    if (c) then return c[1], c[2], c[3]; end
    return 1, 1, 1;
end

local function _FormatKey(k)
    local t = type(k);
    if (t == "string") then return k; end
    if (t == "number") then return string.format("[%d]", k); end
    return string.format("[%s]", tostring(k));
end

------------------------------------------------------------------------------
-- View state — manages the flattened (key, value) list for the active
-- table. Rebuilt on push / pop, not on every Update tick.
------------------------------------------------------------------------------

-- Cached array of (key, value) tuples for the active table, in the
-- sort order returned by _SortedKeys. Re-derived on every navigation
-- change so the rows can be rendered by simple `entries[scroll + i]`
-- indexing instead of walking `pairs()` mid-frame.
local _entries = {};

local function _RebuildEntries()
    _entries = {};
    local top = _navStack[table.getn(_navStack)];
    if (not top or type(top.tbl) ~= "table") then return; end
    local nums, strs, others = _SortedKeys(top.tbl);
    for i = 1, table.getn(nums) do
        local k = nums[i];
        table.insert(_entries, { key = k, value = top.tbl[k] });
    end
    for i = 1, table.getn(strs) do
        local k = strs[i];
        table.insert(_entries, { key = k, value = top.tbl[k] });
    end
    for i = 1, table.getn(others) do
        local k = others[i];
        table.insert(_entries, { key = k, value = top.tbl[k] });
    end
end

local function _Breadcrumb()
    local parts = {};
    for i = 1, table.getn(_navStack) do
        table.insert(parts, _navStack[i].label or "?");
    end
    return table.concat(parts, " > ");
end

------------------------------------------------------------------------------
-- Public API
------------------------------------------------------------------------------

function TableInspector_Show(tbl, label)
    if (type(tbl) ~= "table") then
        DEFAULT_CHAT_FRAME:AddMessage(
            "|cffff8888TableInspector: argument is not a table|r");
        return;
    end
    _navStack = { { tbl = tbl, label = label or tostring(tbl) } };
    _RebuildEntries();
    if (_TableInspectorFrame) then
        _TableInspectorFrame:Show();
        TableInspectorFrame_Update();
    end
end

function TableInspector_Push(tbl, label)
    if (type(tbl) ~= "table") then return; end
    table.insert(_navStack, { tbl = tbl, label = label or tostring(tbl) });
    _RebuildEntries();
    -- Reset scroll to top so the user starts at the beginning of the
    -- newly-drilled table.
    local scroll = getglobal("TableInspectorFrameScroll");
    if (scroll) then scroll:SetValue(0); end
    TableInspectorFrame_Update();
end

function TableInspector_Back()
    if (table.getn(_navStack) <= 1) then
        TableInspector_Close();
        return;
    end
    table.remove(_navStack);
    _RebuildEntries();
    local scroll = getglobal("TableInspectorFrameScroll");
    if (scroll) then scroll:SetValue(0); end
    TableInspectorFrame_Update();
end

function TableInspector_Refresh()
    local top = _navStack[table.getn(_navStack)];
    if (not top or type(top.tbl) ~= "table") then return; end
    _RebuildEntries();
    TableInspectorFrame_Update();
end

function TableInspector_Close()
    if (_TableInspectorFrame) then _TableInspectorFrame:Hide(); end
    _navStack = {};
    _entries = {};
end

------------------------------------------------------------------------------
-- Frame scripts (called from TableInspector XML in DebugTools.xml)
------------------------------------------------------------------------------

function TableInspectorFrame_OnLoad(self)
    self.rows = {};
    _TableInspectorFrame = self;

    self:EnableMouse(true);
    self:EnableMouseWheel(true);
    self:SetScript("OnMouseWheel", function()
        TableInspectorFrame_OnMouseWheel(this, arg1);
    end);

    for i = 1, TABLE_INSPECTOR_NUM_ROWS do
        local row = CreateFrame("Button", "TableInspectorFrameRow" .. i, self,
                                "TableInspectorRowTemplate");
        row:SetPoint("TOPLEFT", 12, -(56 + (i - 1) * TABLE_INSPECTOR_ROW_HEIGHT));
        row:SetPoint("RIGHT", -28, 0);
        row.rowIndex = i;
        table.insert(self.rows, row);
    end
end

function TableInspectorFrame_Update()
    if (not _TableInspectorFrame or not _TableInspectorFrame.rows) then return; end

    local scroll = getglobal("TableInspectorFrameScroll");
    if (not scroll) then return; end

    local total = table.getn(_entries);
    local visible = TABLE_INSPECTOR_NUM_ROWS;
    local maxOffset = math.max(0, total - visible);
    scroll:SetMinMaxValues(0, maxOffset);
    local offset = math.floor(scroll:GetValue() + 0.5);
    if (offset > maxOffset) then offset = maxOffset; end
    if (offset < 0) then offset = 0; end

    -- Update breadcrumb each tick — cheap (string concat over a tiny
    -- nav stack) and avoids a separate "breadcrumb changed" signal.
    local crumb = getglobal("TableInspectorFrameBreadcrumb");
    if (crumb) then
        crumb:SetText(_Breadcrumb() .. string.format("  (%d entries)", total));
    end

    for i = 1, visible do
        local row = _TableInspectorFrame.rows[i];
        local entry = _entries[offset + i];
        if (entry) then
            local keyFS = getglobal(row:GetName() .. "Key");
            local valFS = getglobal(row:GetName() .. "Value");
            keyFS:SetText(_FormatKey(entry.key));
            local disp, vt, drillable = _FormatValue(entry.value);
            valFS:SetText(disp);
            valFS:SetTextColor(_ValueColor(vt));
            row.entryKey = entry.key;
            row.entryValue = entry.value;
            row.drillable = drillable;
            row:Show();
        else
            row.entryKey = nil;
            row.entryValue = nil;
            row.drillable = false;
            row:Hide();
        end
    end
end

function TableInspectorFrame_OnMouseWheel(self, delta)
    local scroll = getglobal("TableInspectorFrameScroll");
    if (not scroll) then return; end
    local mn, mx = scroll:GetMinMaxValues();
    local v = scroll:GetValue() - delta * 3;
    if (v < mn) then v = mn; end
    if (v > mx) then v = mx; end
    scroll:SetValue(v);
end

function TableInspectorRow_OnClick(self)
    if (not self.drillable or type(self.entryValue) ~= "table") then return; end
    TableInspector_Push(self.entryValue, _FormatKey(self.entryKey));
end

------------------------------------------------------------------------------
-- Slash command — /tinspect <luaexpr>
--
-- Evaluates `<luaexpr>` in a fresh chunk with `return ` prefixed, so
-- bare expressions (`_G`, `GameTooltip`, `MyAddOn.foo`) work without
-- the user having to write `return ...` themselves.
------------------------------------------------------------------------------

local function _ChatErr(msg)
    DEFAULT_CHAT_FRAME:AddMessage("|cffff8888TableInspector:|r " .. msg);
end

function TableInspector_HandleSlashCmd(msg)
    if (not msg) then msg = ""; end
    msg = string.gsub(msg, "^%s*(.-)%s*$", "%1"); -- trim
    if (msg == "" or msg == "help") then
        DEFAULT_CHAT_FRAME:AddMessage(
            "|cff88ff88/tinspect|r <expr> -- open inspector on the result of <expr>");
        DEFAULT_CHAT_FRAME:AddMessage("  example: /tinspect _G");
        DEFAULT_CHAT_FRAME:AddMessage("  example: /tinspect ChatTypeInfo");
        return;
    end
    if (msg == "close") then
        TableInspector_Close();
        return;
    end
    local chunk, parseErr = loadstring("return " .. msg);
    if (not chunk) then
        _ChatErr("parse error: " .. tostring(parseErr));
        return;
    end
    -- Run in the global environment so the user's expression sees
    -- the usual addon globals. setfenv on the loadstring chunk
    -- defaults to that already in vanilla, but be explicit.
    setfenv(chunk, getfenv(0));
    local ok, result = pcall(chunk);
    if (not ok) then
        _ChatErr("eval error: " .. tostring(result));
        return;
    end
    if (type(result) ~= "table") then
        _ChatErr(string.format(
            "result is %s (%s), expected table",
            type(result), tostring(result)));
        return;
    end
    TableInspector_Show(result, msg);
end
