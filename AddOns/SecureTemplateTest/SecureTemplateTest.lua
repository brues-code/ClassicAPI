-- SecureTemplateTest — debug addon for the SecureActionButtonTemplate port.
--
-- /sectest          — run all checks and print results to chat
-- /sectest click    — simulate a left-click on the test button (targets "player")
-- /sectest spell    — simulate a left-click configured to cast a spell

local PASS = "|cff00ff00PASS|r"
local FAIL = "|cffff0000FAIL|r"
local INFO = "|cffffff00INFO|r"

local function Log(status, msg)
    DEFAULT_CHAT_FRAME:AddMessage("[SecureTemplateTest] " .. status .. " " .. msg)
end

-- ---------------------------------------------------------------------------
-- Test 1: Template registry — do our templates actually exist?
-- ---------------------------------------------------------------------------
local function TestTemplateRegistry()
    Log(INFO, "--- Template Registry ---")

    local templates = {
        "SecureFrameTemplate",
        "SecureActionButtonTemplate",
        "SecureUnitButtonTemplate",
        "InsecureActionButtonTemplate",
    }
    for _, name in ipairs(templates) do
        if C_XMLUtil and C_XMLUtil.DoesTemplateExist then
            local exists = C_XMLUtil.DoesTemplateExist(name)
            Log(exists and PASS or FAIL, name .. " exists in XML registry: " .. tostring(exists))
        else
            -- Fallback: just try to create a frame with it
            local ok, frame = pcall(CreateFrame, "Button", nil, UIParent, name)
            Log(ok and PASS or FAIL, name .. " CreateFrame: " .. tostring(ok))
            if ok and frame then frame:Hide() end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Test 2: Comma-separated CreateFrame — the core feature
-- ---------------------------------------------------------------------------
local function TestCommaSplit()
    Log(INFO, "--- Comma-Split CreateFrame ---")

    -- Single template (should always work)
    local f1 = CreateFrame("Button", nil, UIParent, "SecureActionButtonTemplate")
    Log(f1 and PASS or FAIL, "CreateFrame with single SecureActionButtonTemplate: " .. tostring(f1 ~= nil))
    if f1 then f1:Hide() end

    -- Comma-separated: visual + secure (the primary use case)
    local f2 = CreateFrame("Button", nil, UIParent, "UIPanelButtonTemplate, SecureActionButtonTemplate")
    Log(f2 and PASS or FAIL, "CreateFrame with 'UIPanelButtonTemplate, SecureActionButtonTemplate': " .. tostring(f2 ~= nil))
    if f2 then
        -- Check it got the visual template's properties (UIPanelButtonTemplate sets a size)
        local w, h = f2:GetWidth(), f2:GetHeight()
        Log((w > 0) and PASS or FAIL, "  Visual template applied (width=" .. tostring(w) .. " height=" .. tostring(h) .. ")")
        f2:Hide()
    end

    -- Reversed order: secure first, visual second
    local f3 = CreateFrame("Button", nil, UIParent, "SecureActionButtonTemplate, UIPanelButtonTemplate")
    Log(f3 and PASS or FAIL, "CreateFrame reversed order: " .. tostring(f3 ~= nil))
    if f3 then f3:Hide() end

    -- Only secure templates (no visual)
    local f4 = CreateFrame("Button", nil, UIParent, "SecureFrameTemplate, SecureActionButtonTemplate")
    Log(f4 and PASS or FAIL, "CreateFrame secure-only combo: " .. tostring(f4 ~= nil))
    if f4 then f4:Hide() end

    -- Whitespace variations
    local f5 = CreateFrame("Button", nil, UIParent, "SecureActionButtonTemplate ,  UIPanelButtonTemplate")
    Log(f5 and PASS or FAIL, "CreateFrame with extra whitespace: " .. tostring(f5 ~= nil))
    if f5 then f5:Hide() end

    -- Single template with trailing comma (edge case)
    local f6 = CreateFrame("Button", nil, UIParent, "UIPanelButtonTemplate,")
    Log(f6 and PASS or FAIL, "CreateFrame trailing comma: " .. tostring(f6 ~= nil))
    if f6 then f6:Hide() end
end

-- ---------------------------------------------------------------------------
-- Test 3: Attribute system — SetAttribute/GetAttribute on created frame
-- ---------------------------------------------------------------------------
local function TestAttributes()
    Log(INFO, "--- Attribute System ---")

    local btn = CreateFrame("Button", "SecTestAttrButton", UIParent, "SecureActionButtonTemplate")
    btn:Hide()

    btn:SetAttribute("type1", "target")
    btn:SetAttribute("unit", "player")
    btn:SetAttribute("shift-type1", "focus")

    local t = btn:GetAttribute("type1")
    Log(t == "target" and PASS or FAIL, "GetAttribute('type1') = " .. tostring(t))

    local u = btn:GetAttribute("unit")
    Log(u == "player" and PASS or FAIL, "GetAttribute('unit') = " .. tostring(u))

    local st = btn:GetAttribute("shift-type1")
    Log(st == "focus" and PASS or FAIL, "GetAttribute('shift-type1') = " .. tostring(st))

    -- Test modified attribute resolution (Lua helper)
    local resolved = SecureButton_GetModifiedAttribute(btn, "type", "LeftButton", "", "1")
    Log(resolved == "target" and PASS or FAIL,
        "SecureButton_GetModifiedAttribute(type, LB, '', '1') = " .. tostring(resolved))

    -- Test wildcard
    btn:SetAttribute("*action*", "42")
    local wild = SecureButton_GetModifiedAttribute(btn, "action", "RightButton")
    Log(wild == "42" and PASS or FAIL,
        "Wildcard *action* resolved = " .. tostring(wild))
end

-- ---------------------------------------------------------------------------
-- Test 4: Lua helper functions exist
-- ---------------------------------------------------------------------------
local function TestHelpers()
    Log(INFO, "--- Lua Helper Functions ---")

    local helpers = {
        "SecureButton_GetModifierPrefix",
        "SecureButton_GetButtonSuffix",
        "SecureButton_GetModifiedAttribute",
        "SecureButton_GetModifiedUnit",
        "SecureButton_GetUnit",
        "SecureButton_GetAttribute",
        "SecureActionButton_OnClick",
        "SecureUnitButton_OnClick",
        "SecureUnitButton_OnLoad",
        "ATTRIBUTE_NOOP",
    }
    for _, name in ipairs(helpers) do
        local val = _G[name]
        Log(val ~= nil and PASS or FAIL, name .. " = " .. type(val))
    end

    -- Verify button suffix mapping
    local suffixes = {
        { "LeftButton", "1" },
        { "RightButton", "2" },
        { "MiddleButton", "3" },
        { "Button4", "4" },
        { "Button5", "5" },
    }
    for _, pair in ipairs(suffixes) do
        local result = SecureButton_GetButtonSuffix(pair[1])
        Log(result == pair[2] and PASS or FAIL,
            "GetButtonSuffix('" .. pair[1] .. "') = '" .. tostring(result) .. "' (expected '" .. pair[2] .. "')")
    end
end

-- ---------------------------------------------------------------------------
-- Test 5: Click simulation
-- ---------------------------------------------------------------------------
local function TestClick(mode)
    Log(INFO, "--- Click Test (" .. (mode or "target") .. ") ---")

    local btn = CreateFrame("Button", "SecTestClickButton", UIParent,
                            "UIPanelButtonTemplate, SecureActionButtonTemplate")
    btn:SetPoint("CENTER")
    btn:SetSize(100, 30)
    btn:RegisterForClicks("LeftButtonUp", "RightButtonUp")

    if mode == "spell" then
        btn:SetAttribute("type1", "spell")
        btn:SetAttribute("spell1", "Attack")
        btn:SetAttribute("unit", "target")
        Log(INFO, "Configured: type1=spell, spell1=Attack, unit=target")
        Log(INFO, "Click the button (or /click SecTestClickButton) to test casting")
    else
        btn:SetAttribute("type1", "target")
        btn:SetAttribute("unit", "player")
        Log(INFO, "Configured: type1=target, unit=player")
        Log(INFO, "Click the button (or /click SecTestClickButton) to target yourself")
    end

    btn:Show()
    Log(INFO, "Button shown at screen center. Click it to verify dispatch.")
end

-- ---------------------------------------------------------------------------
-- Slash command
-- ---------------------------------------------------------------------------
SLASH_SECTEST1 = "/sectest"
SlashCmdList["SECTEST"] = function(msg)
    msg = string.lower(msg or "")

    if msg == "click" then
        TestClick("target")
    elseif msg == "spell" then
        TestClick("spell")
    else
        Log(INFO, "=== SecureTemplateTest Suite ===")
        TestTemplateRegistry()
        TestCommaSplit()
        TestAttributes()
        TestHelpers()
        Log(INFO, "=== Done. Use '/sectest click' or '/sectest spell' for interactive tests ===")
    end
end

-- Auto-run on load
Log(INFO, "Loaded. Type /sectest to run diagnostics.")
