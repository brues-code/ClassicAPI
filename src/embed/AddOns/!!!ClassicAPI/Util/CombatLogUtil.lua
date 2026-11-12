-- Backport of the colour functions of retail's
-- Blizzard_CombatLogBase/CombatLogUtil.lua. `settings` is a combat log
-- filter's settings: a table whose `colors` has COMBATLOG_DEFAULT_COLORS's
-- shape, for example `{ colors = COMBATLOG_DEFAULT_COLORS }`.

CombatLogUtil = {};

function CombatLogUtil.WrapTextInColor(text, color)
	local r = color.r * 255;
	local g = color.g * 255;
	local b = color.b * 255;

	return ("|cff%02x%02x%02x%s|r"):format(r, g, b, text);
end

do
	local DefaultColorArray = CreateColor(0.5, 0.5, 0.5, 1.0);

	function CombatLogUtil.GetColorByEventType(event, settings)
		return settings.colors.eventColoring[event] or DefaultColorArray;
	end
end

function CombatLogUtil.GetColorBySchool(school, settings)
	if not school then
		return settings.colors.schoolColoring.default;
	end

	return settings.colors.schoolColoring[school] or settings.colors.defaults.spell;
end

do
	local HighlightMultiplier = 1.5;
	local s_highlightColorTable = {};

	function CombatLogUtil.HighlightColor(colorArray)
		s_highlightColorTable.r = math.min(colorArray.r * HighlightMultiplier, 1);
		s_highlightColorTable.g = math.min(colorArray.g * HighlightMultiplier, 1);
		s_highlightColorTable.b = math.min(colorArray.b * HighlightMultiplier, 1);
		s_highlightColorTable.a = colorArray.a;

		return s_highlightColorTable;
	end
end
