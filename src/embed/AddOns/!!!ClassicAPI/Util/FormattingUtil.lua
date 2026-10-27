-- Backport of FrameXML/FormattingUtil.lua (feasible subset) to vanilla
-- 1.12 / Lua 5.0.
--
-- 5.0 notes: no `#`/`%`/string-method syntax — uses string.*, math.mod.
-- GetMoneyString gives the text-symbol form ("12g 34s 56c"). The coin-icon form
-- (GetCoinTextureString / C_CurrencyInfo.GetCoinTextureString) is provided by the
-- ClassicAPI DLL in C++ (src/currency/CoinText.cpp) — like retail, where it's a
-- C function — now that the inline-texture backport renders |T markup in 1.12.
--
-- The FrameXML format-string globals these functions consume
-- (LARGE_NUMBER_SEPERATOR, PERCENTAGE_STRING, GOLD_AMOUNT_SYMBOL,
-- GOLD_AMOUNT_TEXTURE, …) are defined in the locale layer (locales/enUS.lua),
-- which loads first.

COPPER_PER_SILVER = 100;
SILVER_PER_GOLD = 100;
COPPER_PER_GOLD = COPPER_PER_SILVER * SILVER_PER_GOLD;

function FormatLargeNumber(amount)
    amount = tostring(amount);
    local newDisplay = "";
    local strlen = string.len(amount);
    -- Insert a separator before each group of three trailing digits.
    for i = 4, strlen, 3 do
        newDisplay = LARGE_NUMBER_SEPERATOR ..
            string.sub(amount, -(i - 1), -(i - 3)) .. newDisplay;
    end
    -- Prepend everything before the first separator.
    local lead = (math.mod(strlen, 3) == 0) and 3 or math.mod(strlen, 3);
    newDisplay = string.sub(amount, 1, lead) .. newDisplay;
    return newDisplay;
end

function FormatPercentage(percentage, roundToNearestInteger)
    if roundToNearestInteger then
        percentage = Round(percentage * 100);
    else
        percentage = percentage * 100;
    end
    return string.format(PERCENTAGE_STRING, percentage);
end

function FormatFraction(numerator, denominator)
    return string.format(GENERIC_FRACTION_STRING, numerator, denominator);
end

function SplitTextIntoLines(text, delimiter)
    local lines = {};
    local startIndex = 1;
    local foundIndex = string.find(text, delimiter);
    while foundIndex do
        table.insert(lines, string.sub(text, startIndex, foundIndex - 1));
        startIndex = foundIndex + 2;
        foundIndex = string.find(text, delimiter, startIndex);
    end
    if startIndex <= string.len(text) then
        table.insert(lines, string.sub(text, startIndex));
    end
    return lines;
end

function SplitTextIntoHeaderAndNonHeader(text)
    local foundIndex = string.find(text, "|n");
    local len = string.len(text);
    if not foundIndex then
        return text; -- no newline: whole thing is a header
    elseif len == 2 then
        return nil; -- the string was just a newline
    elseif foundIndex == 1 then
        return string.sub(text, 3); -- leading newline: rest is a header
    elseif foundIndex == len - 1 then
        return string.sub(text, 1, foundIndex - 1); -- trailing newline
    else
        return string.sub(text, 1, foundIndex - 1), string.sub(text, foundIndex + 2);
    end
end

-- (money, separateThousands [, checkGoldThreshold, showZeroAsGold])
-- Text-symbol form ("12g 34s 56c"); for the coin-icon form use
-- GetCoinTextureString below. The checkGoldThreshold arg is accepted for
-- signature compatibility but not applied (it relied on modern Constants tables).
function GetMoneyString(money, separateThousands, checkGoldThreshold, showZeroAsGold)
    local gold = math.floor(money / COPPER_PER_GOLD);
    local silver = math.floor(math.mod(money, COPPER_PER_GOLD) / COPPER_PER_SILVER);
    local copper = math.mod(money, COPPER_PER_SILVER);

    local goldString = (separateThousands and FormatLargeNumber(gold) or tostring(gold)) ..
        GOLD_AMOUNT_SYMBOL;
    local silverString = silver .. SILVER_AMOUNT_SYMBOL;
    local copperString = copper .. COPPER_AMOUNT_SYMBOL;

    if showZeroAsGold and money == 0 then
        return goldString;
    end

    local moneyString = "";
    local separator = "";
    if gold > 0 then
        moneyString = goldString;
        separator = " ";
    end
    if silver > 0 then
        moneyString = moneyString .. separator .. silverString;
        separator = " ";
    end
    if copper > 0 or moneyString == "" then
        moneyString = moneyString .. separator .. copperString;
    end
    return moneyString;
end
