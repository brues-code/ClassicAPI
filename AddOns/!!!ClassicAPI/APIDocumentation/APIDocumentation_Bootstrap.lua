-- Backport of Blizzard_APIDocumentation/Blizzard_APIDocumentation_Bootstrap.lua
-- to vanilla 1.12 / Lua 5.0, plus the `/api` command and `api` link handler
-- that retail keeps in Blizzard_ChatFrameBase and Blizzard_UIPanels_Game.
--
-- Implementation differences from the retail source:
--   - Retail loads a generated data addon on demand
--     (LoadAddOnWithErrorHandling). ClassicAPI keeps the documentation in the
--     DLL instead, so APIDocumentation_LoadUI reads it through
--     C_APIDocumentation and feeds each system to AddDocumentationTable. The
--     load still happens on first use, so a session that never types /api
--     builds no tables at all.
--   - IsModifiedClick does not exist on this client. CHATLINK is Shift by
--     default, so a shift-click stands in for it.
--   - Lua 5.0 has no length operator (#); table.getn(t) gives the count.

local loaded = false;

-- Fetches every documented system from the DLL. Returns true once the
-- documentation is in place, false when the DLL is absent (the addon can be
-- installed on its own).
function APIDocumentation_LoadUI()
	if loaded then
		return true;
	end
	-- Latch BEFORE the fetch: a system table the browser chokes on must not
	-- raise the same error on every later /api.
	loaded = true;

	if not C_APIDocumentation then
		APIDocumentation:WriteLine("API documentation is not available (C_APIDocumentation is missing).");
		return false;
	end

	-- ipairs, not a length call: it walks 1..nil whatever the table's internal
	-- shape, which is how every other consumer reads an array the DLL built.
	for i, name in ipairs(C_APIDocumentation.GetSystems()) do
		local system = C_APIDocumentation.GetSystem(name);
		if system then
			APIDocumentation:AddDocumentationTable(system);
		end
	end
	return true;
end

-- Retail calls this `/api`. Here the browser holds only what ClassicAPI adds,
-- not the 1.12 API the client already ships, so it is named for what it
-- documents. `/capi` is the short form.
SLASH_CLASSICAPI1 = "/classicapi";
SLASH_CLASSICAPI2 = "/capi";
SlashCmdList["CLASSICAPI"] = function(msg)
	APIDocumentation_LoadUI();
	APIDocumentation:HandleSlashCommand(msg or "");
end

LinkUtil.RegisterLinkHandler(LinkTypes.APIDocumentation, function(link, text, linkData, contextData)
	APIDocumentation_LoadUI();

	local command = APIDocumentation.Commands.Default;
	if contextData and contextData.button == "RightButton" then
		command = APIDocumentation.Commands.CopyAPI;
	elseif IsShiftKeyDown() then
		command = APIDocumentation.Commands.OpenDump;
	end

	APIDocumentation:HandleAPILink(link, command);
end);
