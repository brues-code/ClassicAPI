-- Backport of retail's Blizzard_SharedXMLBase/ErrorUtil.lua.
--
-- Implementation differences from the retail source:
--   - 1.12 has no SetErrorCallstackHeight / GetCallstackHeight, so an
--     error's stack starts at assertsafe rather than at its caller.
--   - Lua 5.0 has no `...` as an expression and strings have no methods;
--     varargs go through the implicit `arg` table and string.format.

function CallErrorHandler(...)
	return geterrorhandler()(unpack(arg));
end

function assertsafe(cond, msgStringOrFunction, ...)
	if not cond then
		local error = msgStringOrFunction or "non-fatal assertion failed";
		if type(msgStringOrFunction) == 'string' and arg.n > 0 then
			error = string.format(msgStringOrFunction, unpack(arg));
		elseif type(msgStringOrFunction) == 'function' then
			error = msgStringOrFunction(unpack(arg));
		end

		if geterrorhandler() then
			geterrorhandler()(error);
		elseif ProcessExceptionClient then
			local framesToSkip = 1;
			ProcessExceptionClient(error, error, framesToSkip);

		end
	end

	-- Parity with regular 'assert' which returns the input.
	return cond;
end
