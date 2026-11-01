-- Backport of retail's Blizzard_SharedXMLBase/ErrorUtil.lua.
--
-- Implementation differences from the retail source:
--   - Lua 5.0 has no `...` as an expression and strings have no methods;
--     varargs go through the implicit `arg` table and string.format.

function CallErrorHandler(...)
	SetErrorCallstackHeight(GetCallstackHeight() - 1); -- report error from the previous function
	local result = geterrorhandler()(unpack(arg));
	SetErrorCallstackHeight(nil);
	return result;
end

function assertsafe(cond, msgStringOrFunction, ...)
	if not cond then
		local error = msgStringOrFunction or "non-fatal assertion failed";
		if type(msgStringOrFunction) == 'string' and arg.n > 0 then
			error = string.format(msgStringOrFunction, unpack(arg));
		elseif type(msgStringOrFunction) == 'function' then
			error = msgStringOrFunction(unpack(arg));
		end

		SetErrorCallstackHeight(GetCallstackHeight() - 1); -- report error from the previous function
		if geterrorhandler() then
			geterrorhandler()(error);
		elseif ProcessExceptionClient then
			local framesToSkip = 1;
			ProcessExceptionClient(error, error, framesToSkip);

		end
		SetErrorCallstackHeight(nil);
	end

	-- Parity with regular 'assert' which returns the input.
	return cond;
end
