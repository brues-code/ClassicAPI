-- SecureCmdOptionParse(options) -> value[, target]
--
-- Backports the macro-conditional parser (added in 2.0) so a modern option
-- string like "[combat,@focus] Foo; [nostealth] Bar; Baz" resolves to the
-- value of the first matching clause. Used by Util/SecureStateDriver.lua and
-- callable by any macro / click-verb system that wants Blizzard's grammar.
--
-- Grammar reverse-engineered from the 3.3.5 Wow.exe (functions 0x005f0df0
-- clause splitter, 0x005f0bb0 group parser, 0x005f0040 condition parser,
-- 0x005ef5c0 evaluator), so it matches Blizzard's own precedence exactly:
--   * clauses are split on ';' at bracket depth 0;
--   * a clause is zero or more [group] blocks followed by a value (the text
--     after the last ']', edge-trimmed -- may be empty);
--   * a clause with NO groups always matches; groups are OR'd (first pass
--     wins); conditions inside a group are comma-split and AND'd;
--   * an '@unit' or 'target=unit' piece (case-insensitive) sets the group's
--     target; every other piece is a condition, optionally negated by a 'no'
--     prefix (stripped only when the piece is >= 3 chars) and optionally
--     carrying ':a/b/c' args (each trimmed, multiple args OR'd);
--   * the match returns the clause value plus the passing group's target
--     token (nil when the passing group set none).
--
-- The 33 condition keywords are the 3.3.5 set, matched case-sensitively, plus
-- one ClassicAPI extension: [known:spellID] / [known:name].
-- Where a keyword's underlying state does not exist on 1.12 (flying, flyable,
-- vehicleui, unithasvehicleui) the condition is a constant false; where 1.12
-- only has a partial answer (spec is always primary, button/actionbar default
-- to the resting state) the closest honest mapping is used. An UNKNOWN keyword
-- is a hard false with a one-time warning -- never a silent pass -- so a typo
-- can't wrongly show a frame. See docs/API.md for the full contract.
--
-- Each unique options string is parsed ONCE into a clause structure and cached
-- (a plain table, matching the 3.3.5 C hash -- entries are never evicted, and
-- the set is bounded by the strings addons actually register). Evaluation then
-- just walks the structure calling predicate functions, so the 0.2s driver
-- poll re-parses nothing.

local type = type;
local tonumber = tonumber;
local strlen = string.len;
local strsub = string.sub;
local strlower = string.lower;
local strmatch = string.match;
local gmatch = string.gmatch;

local function Trim(s)
    return strmatch(s, "^%s*(.-)%s*$") or "";
end

local function AlwaysTrue()
    return true;
end

local function AlwaysFalse()
    return false;
end

-- ---------------------------------------------------------------------------
-- Query helpers used by more than one predicate
-- ---------------------------------------------------------------------------

-- The shapeshift-BAR index of the active form (1-based), or 0 when formless.
-- [stance:N] / [form:N] number by this bar order (druid 1 = Bear, 3 = Cat),
-- which is NOT the DBC form id GetShapeshiftFormID() returns -- so the ':N'
-- form deliberately does not use that faster reader.
local function GetCurrentFormIndex()
    for i = 1, GetNumShapeshiftForms() do
        local _, _, active = GetShapeshiftFormInfo(i);
        if active then
            return i;
        end
    end
    return 0;
end

-- True when any equipped item (slots 1..19) matches `want` by localized item
-- type, subtype, or equip-location slot name. C_Item.GetItemInfoInstant is
-- synchronous and link-accepting; worn gear is always cached, and a nil field
-- simply fails the match (fail-safe).
local function IsEquippedTypeMatch(want)
    want = strlower(want);
    for slot = 1, 19 do
        local link = GetInventoryItemLink("player", slot);
        if link then
            local _, itemType, itemSubType, equipLoc = C_Item.GetItemInfoInstant(link);
            if (itemType and strlower(itemType) == want)
                or (itemSubType and strlower(itemSubType) == want) then
                return true;
            end
            if equipLoc and equipLoc ~= "" then
                local slotName = _G[equipLoc];
                if slotName and strlower(slotName) == want then
                    return true;
                end
            end
        end
    end
    return false;
end

-- True when a spell named `want` (case-insensitive) is in the player's
-- spellbook. This is the name form of [known:...]. There is no name -> id
-- resolver, so a name can only resolve against castable spellbook entries;
-- talents / passives / profession recipes that IsPlayerSpell reports for a
-- spell id are not found by name.
local function IsSpellNameKnown(want)
    want = strlower(want);
    local i = 1;
    while true do
        local name = GetSpellName(i, "spell");
        if not name then
            return false;
        end
        if strlower(name) == want then
            return true;
        end
        i = i + 1;
    end
end

-- ---------------------------------------------------------------------------
-- Condition predicates -- signature f(target, args) -> truthy.
-- `args` is nil for a bare condition, else { n = <count>, [i] = <trimmed> };
-- multiple args are OR'd.
-- ---------------------------------------------------------------------------

local CONDITIONS = {};

CONDITIONS[""] = AlwaysTrue;                      -- empty keyword ([] / [ ]) always matches

CONDITIONS.combat = function()
    return UnitAffectingCombat("player");
end

CONDITIONS.exists = function(target)
    return UnitExists(target);
end

CONDITIONS.dead = function(target)
    return UnitIsDeadOrGhost(target);
end

CONDITIONS.help = function(target)
    return UnitCanAssist("player", target);
end

CONDITIONS.harm = function(target)
    return UnitCanAttack("player", target);
end

CONDITIONS.party = function(target)
    return UnitPlayerOrPetInParty(target);
end

CONDITIONS.raid = function(target)
    return UnitInRaid(target);
end

-- [group] = in any group; [group:party] true in a party OR raid (raid members
-- occupy party subgroups, matching retail); [group:raid] true only in a raid.
CONDITIONS.group = function(target, args)
    local inParty = GetNumPartyMembers() > 0;
    local inRaid = GetNumRaidMembers() > 0;
    if args then
        for i = 1, args.n do
            local a = strlower(args[i]);
            if a == "raid" then
                if inRaid then return true; end
            elseif a == "party" then
                if inParty or inRaid then return true; end
            end
        end
        return false;
    end
    return inParty or inRaid;
end

CONDITIONS.stance = function(target, args)
    if args then
        local cur = GetCurrentFormIndex();
        for i = 1, args.n do
            if tonumber(args[i]) == cur then return true; end
        end
        return false;
    end
    return GetShapeshiftFormID() ~= 0;
end

CONDITIONS.stealth = function()
    return IsStealthed();
end

CONDITIONS.mounted = function()
    return IsMounted();
end

CONDITIONS.swimming = function()
    return IsSwimming();
end

CONDITIONS.indoors = function()
    return IsIndoors();
end

CONDITIONS.outdoors = function()
    return IsOutdoors();
end

-- Vanilla has one permanent spec: [spec] and [spec:1] pass, higher indices fail.
CONDITIONS.spec = function(target, args)
    if args then
        for i = 1, args.n do
            if args[i] == "1" then return true; end
        end
        return false;
    end
    return true;
end

CONDITIONS.modifier = function(target, args)
    if args then
        for i = 1, args.n do
            local a = strlower(args[i]);
            if a == "shift" then
                if IsShiftKeyDown() then return true; end
            elseif a == "ctrl" then
                if IsControlKeyDown() then return true; end
            elseif a == "alt" then
                if IsAltKeyDown() then return true; end
            end
        end
        return false;
    end
    return IsShiftKeyDown() or IsControlKeyDown() or IsAltKeyDown();
end

-- Outside a click context the "current" button is the left button.
CONDITIONS.button = function(target, args)
    if args then
        for i = 1, args.n do
            local a = args[i];
            if a == "1" or strlower(a) == "leftbutton" then return true; end
        end
        return false;
    end
    return true;
end

CONDITIONS.actionbar = function(target, args)
    if args then
        local page = CURRENT_ACTIONBAR_PAGE or 1;
        for i = 1, args.n do
            if tonumber(args[i]) == page then return true; end
        end
        return false;
    end
    return true;
end

CONDITIONS.bonusbar = function(target, args)
    local offset = GetBonusBarOffset();
    if args then
        for i = 1, args.n do
            if tonumber(args[i]) == offset then return true; end
        end
        return false;
    end
    return offset > 0;
end

CONDITIONS.pet = function(target, args)
    if args then
        local petName = UnitName("pet");
        local petFamily = UnitCreatureFamily("pet");
        petName = petName and strlower(petName);
        petFamily = petFamily and strlower(petFamily);
        for i = 1, args.n do
            local a = strlower(args[i]);
            if a == petName or a == petFamily then return true; end
        end
        return false;
    end
    return UnitExists("pet");
end

-- ClassicAPI cedes the global name UnitChannelInfo/ChannelInfo to addons, so the
-- player's channel state is read through the namespaced C_Spell.ChannelInfo().
CONDITIONS.channeling = function(target, args)
    local name = C_Spell.ChannelInfo();
    if args then
        if not name then return false; end
        name = strlower(name);
        for i = 1, args.n do
            if strlower(args[i]) == name then return true; end
        end
        return false;
    end
    return name ~= nil;
end

CONDITIONS.equipped = function(target, args)
    if not args then return false; end          -- [equipped] with no type never matches
    for i = 1, args.n do
        if IsEquippedTypeMatch(args[i]) then return true; end
    end
    return false;
end

CONDITIONS.cursor = function()
    return GetCursorInfo() ~= nil;
end

-- ClassicAPI extension (not a 3.3.5 conditional). [known:spellID] uses
-- IsPlayerSpell (broad knowledge -- spellbook, talents, recipes, racials);
-- [known:name] falls back to a spellbook name scan (see IsSpellNameKnown).
CONDITIONS.known = function(target, args)
    if not args then return false; end
    for i = 1, args.n do
        local a = args[i];
        local id = tonumber(a);
        if id then
            if IsPlayerSpell(id) then return true; end
        elseif IsSpellNameKnown(a) then
            return true;
        end
    end
    return false;
end

-- No such state on 1.12.
CONDITIONS.flying = AlwaysFalse;
CONDITIONS.flyable = AlwaysFalse;
CONDITIONS.vehicleui = AlwaysFalse;
CONDITIONS.unithasvehicleui = AlwaysFalse;

-- Aliases (share the function reference).
CONDITIONS.form = CONDITIONS.stance;
CONDITIONS.mod = CONDITIONS.modifier;
CONDITIONS.btn = CONDITIONS.button;
CONDITIONS.bar = CONDITIONS.actionbar;
CONDITIONS.worn = CONDITIONS.equipped;

-- ---------------------------------------------------------------------------
-- Parser
-- ---------------------------------------------------------------------------

local warned = {};

-- piece -> { pred = fn, neg = bool, args = { n, [i] } or nil }
local function ParseCondition(piece)
    local neg = false;
    if strlen(piece) >= 3 and strsub(piece, 1, 2) == "no" then
        neg = true;
        piece = strsub(piece, 3);
    end

    local keyword, argstr = strmatch(piece, "^([^:]*):?(.*)$");
    keyword = Trim(keyword);

    local args;
    if argstr ~= "" then
        args = { n = 0 };
        for a in gmatch(argstr .. "/", "([^/]*)/") do
            args.n = args.n + 1;
            args[args.n] = Trim(a);
        end
    end

    local pred = CONDITIONS[keyword];
    if not pred then
        if not warned[keyword] then
            warned[keyword] = true;
            print("SecureCmdOptionParse: unknown condition '" .. keyword .. "'");
        end
        pred = AlwaysFalse;
        neg = false;                            -- an unknown keyword is a hard false
    end

    return { pred = pred, neg = neg, args = args };
end

-- group body -> { target = str or nil, n, [i] = cond }
local function ParseGroup(body)
    local group = { n = 0 };
    -- Sentinel comma so a run ending at end-of-body (and an empty body) yields
    -- a piece; each piece is (non-comma)* up to a comma.
    for piece in gmatch(body .. ",", "([^,]*),") do
        piece = Trim(piece);
        if strsub(piece, 1, 1) == "@" then
            group.target = Trim(strsub(piece, 2));
        elseif strlower(strsub(piece, 1, 7)) == "target=" then
            group.target = Trim(strsub(piece, 8));
        else
            group.n = group.n + 1;
            group[group.n] = ParseCondition(piece);
        end
    end
    return group;
end

-- clause text -> { value = str, groups = { n, [i] = group } }
local function ParseClause(text)
    local groups = { n = 0 };
    -- Greedy up to the LAST ']' splits the [group] blocks from the value.
    local body, value = strmatch(text, "^%s*(.*%])(.*)$");
    if not body then
        return { value = Trim(text), groups = groups };
    end
    for groupBody in gmatch(body, "%[([^%]]*)%]") do
        groups.n = groups.n + 1;
        groups[groups.n] = ParseGroup(groupBody);
    end
    return { value = Trim(value), groups = groups };
end

-- options -> { n, [i] = clause }.  Splits on ';' at bracket depth 0.
local function ParseOptions(options)
    local clauses = { n = 0 };
    local len = strlen(options);
    local pos = 1;
    while pos <= len + 1 do
        local i = pos;
        local depth = 0;
        while i <= len do
            local c = strsub(options, i, i);
            if c == "[" then
                depth = 1;
            elseif c == "]" then
                depth = 0;
            elseif c == ";" and depth == 0 then
                break;
            end
            i = i + 1;
        end
        clauses.n = clauses.n + 1;
        clauses[clauses.n] = ParseClause(strsub(options, pos, i - 1));
        pos = i + 1;
    end
    return clauses;
end

-- ---------------------------------------------------------------------------
-- Evaluation
-- ---------------------------------------------------------------------------

local cache = {};

-- Resolve a `@unit` piece to something the engine's unit functions accept,
-- and report whether a unit is actually there.
--   a unit token -> itself, plus whether it currently has a unit
--   a name       -> the token for the unit of that name, or nil
-- `@unit` takes a character name as well as a token (`[target=Feral]`), and
-- the pcall is what tells them apart: the engine raises for a string that
-- names no token, which is exactly the case to hand to the by-name search.
-- That also keeps a typo failing the group instead of the caller.
local function ResolveTarget(target)
    local ok, exists = pcall(UnitExists, target);
    if ok then
        return target, (exists and true or false);
    end
    local token = UnitTokenFromName(target);
    return token, token ~= nil;
end

-- Targets that do not name a unit. `none` clears the target and `cursor`
-- names a world position, so neither can be tested with UnitExists and both
-- always pass the existence gate below.
local NON_UNIT_TARGETS = { none = true, cursor = true };

local function GroupPasses(group)
    local target = group.target;
    local named = target and not NON_UNIT_TARGETS[strlower(target)];
    if group.n == 0 then
        -- A group that only names a unit (`[@mouseover]`) passes only while
        -- that unit exists, so `[@mouseover][] Spell` falls through to the
        -- next group when nothing is moused over. A bare `[]` and the
        -- non-unit targets always pass. Groups with conditions leave
        -- existence to them (`[@focus,noexists]` still works).
        if not named then
            return true;
        end
        local _, exists = ResolveTarget(target);
        return exists;
    end
    if named then
        -- The conditions ask the engine about this unit, so a name has to
        -- become a token first.
        target = ResolveTarget(target);
        if not target then
            return false;
        end
    end
    target = target or "target";
    for i = 1, group.n do
        local cond = group[i];
        local ok = cond.pred(target, cond.args);
        if cond.neg then
            ok = not ok;
        end
        if not ok then
            return false;
        end
    end
    return true;
end

function SecureCmdOptionParse(options)
    if type(options) ~= "string" then
        error("Usage: SecureCmdOptionParse(\"options\")");
        return nil;                             -- WoW's error() may not unwind
    end

    local clauses = cache[options];
    if not clauses then
        clauses = ParseOptions(options);
        cache[options] = clauses;
    end

    for i = 1, clauses.n do
        local clause = clauses[i];
        local groups = clause.groups;
        if groups.n == 0 then
            return clause.value;
        end
        for j = 1, groups.n do
            if GroupPasses(groups[j]) then
                return clause.value, groups[j].target;
            end
        end
    end
    return nil;
end
