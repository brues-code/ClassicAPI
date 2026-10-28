-- Verbatim backport of retail's Blizzard_SharedXMLBase/EnvironmentUtil.lua.

function InGlue()
	return C_Glue and C_Glue.IsOnGlueScreen and C_Glue.IsOnGlueScreen();
end

function nop()
end
