-- Backport of the print / tostringall infrastructure from
-- 3.3.5's FrameXML/RestrictedEnvironment.lua to vanilla 1.12 / Lua 5.0.
--
-- Provides:
--   tostringall(...)         — tostring() applied to every vararg, returns
--                              the same number of values as passed in
--   setprinthandler(func)    — install a custom print handler
--   getprinthandler()        — query the current handler
--   print(...)               — global print, dispatches to the handler
--                              with pcall + geterrorhandler fallback
--
-- Implementation differences from the 3.3.5 source:
--   - Lua 5.0 has no `select()` and no `...` outside vararg-function bodies,
--     so we work off the implicit `arg` table that 5.0 populates inside
--     vararg functions (`arg.n` for count, `arg[i]` for each value).
--   - No forceinsecure / securecall — vanilla 1.12 has no taint or
--     Secure-Templates system, so the protective wrappers in 3.3.5
--     reduce to a plain pcall.
--

----------------------------------------------------------------------------
-- tostringall: convert every vararg to a string, preserving count.

local TOSTRINGALL_TEMP = {}

function tostringall(...)
    local n = arg.n
    if n == 0 then return end
    if n == 1 then return tostring(arg[1]) end
    if n == 2 then return tostring(arg[1]), tostring(arg[2]) end
    if n == 3 then return tostring(arg[1]), tostring(arg[2]), tostring(arg[3]) end
    if n == 4 then
        return tostring(arg[1]), tostring(arg[2]),
               tostring(arg[3]), tostring(arg[4])
    end

    -- General case: build into a temp table with explicit .n so unpack()
    -- honors the count even after a previous larger call left junk.
    local t = TOSTRINGALL_TEMP
    local prev = t.n or 0
    for i = 1, n do t[i] = tostring(arg[i]) end
    if prev > n then
        for i = n + 1, prev do t[i] = nil end
    end
    t.n = n
    return unpack(t)
end

----------------------------------------------------------------------------
-- Default handler: concat with a space separator, push to default chat.

local function default_print_handler(...)
    local n = arg.n
    if n == 0 then
        DEFAULT_CHAT_FRAME:AddMessage("")
        return
    end
    -- table.concat is the 5.0 native equivalent of strjoin (which is a
    -- post-vanilla WoW global). Tostring everything first since concat
    -- only accepts strings/numbers.
    local parts = {}
    for i = 1, n do parts[i] = tostring(arg[i]) end
    DEFAULT_CHAT_FRAME:AddMessage(table.concat(parts, " "))
end

local LOCAL_PrintHandler = default_print_handler

function setprinthandler(func)
    if type(func) ~= "function" then
        error("Invalid print handler")
    end
    LOCAL_PrintHandler = func
end

function getprinthandler()
    return LOCAL_PrintHandler
end

----------------------------------------------------------------------------
-- print: the public entry point. pcall'd so a broken handler doesn't
-- bring the calling code down with it; routes errors through the
-- standard error handler when available, else falls back to chat.

function print(...)
    local ok, err = pcall(LOCAL_PrintHandler, unpack(arg))
    if not ok then
        local handler = geterrorhandler and geterrorhandler()
        if type(handler) == "function" then
            handler(err)
        else
            DEFAULT_CHAT_FRAME:AddMessage(tostring(err))
        end
    end
end
