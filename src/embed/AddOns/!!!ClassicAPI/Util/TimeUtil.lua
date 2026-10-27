SECONDS_PER_MIN = 60;
SECONDS_PER_HOUR = 60 * SECONDS_PER_MIN;
SECONDS_PER_DAY = 24 * SECONDS_PER_HOUR;
SECONDS_PER_MONTH = 30 * SECONDS_PER_DAY;
SECONDS_PER_YEAR = 12 * SECONDS_PER_MONTH;

function SecondsToMinutes(seconds)
    return seconds / SECONDS_PER_MIN;
end

function MinutesToSeconds(minutes)
    return minutes * SECONDS_PER_MIN;
end

function ConvertSecondsToUnits(timestamp)
    timestamp = math.max(timestamp, 0);
    local days = math.floor(timestamp / SECONDS_PER_DAY);
    timestamp = timestamp - (days * SECONDS_PER_DAY);
    local hours = math.floor(timestamp / SECONDS_PER_HOUR);
    timestamp = timestamp - (hours * SECONDS_PER_HOUR);
    local minutes = math.floor(timestamp / SECONDS_PER_MIN);
    timestamp = timestamp - (minutes * SECONDS_PER_MIN);
    local seconds = math.floor(timestamp);
    local milliseconds = timestamp - seconds;
    return {
        days = days,
        hours = hours,
        minutes = minutes,
        seconds = seconds,
        milliseconds = milliseconds,
    };
end

function SecondsToClock(seconds, displayZeroHours)
    local units = ConvertSecondsToUnits(seconds);
    if units.hours > 0 or displayZeroHours then
        return string.format(HOURS_MINUTES_SECONDS, units.hours, units.minutes, units.seconds);
    else
        return string.format(MINUTES_SECONDS, units.minutes, units.seconds);
    end
end
