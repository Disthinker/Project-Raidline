#include "world_clock.h"

#include <algorithm>
#include <limits>

WorldClockProjection projectWorldClock(
    const WorldClockState &state) noexcept
{
    const std::uint64_t minuteOfDay =
        state.elapsedWorldMinutes % kWorldMinutesPerDay;
    const std::uint32_t hour = static_cast<std::uint32_t>(
        minuteOfDay / kWorldMinutesPerHour);
    const std::uint32_t minute = static_cast<std::uint32_t>(
        minuteOfDay % kWorldMinutesPerHour);
    const bool daylight = hour >= 6U && hour < 18U;
    return WorldClockProjection{
        state.elapsedWorldMinutes / kWorldMinutesPerDay + 1U,
        hour,
        minute,
        daylight ? WorldTimeOfDay::Day : WorldTimeOfDay::Night,
        state.elapsedWorldMinutes / kWorldMinutesPerDay,
        kWorldMinutesPerDay - minuteOfDay};
}

WorldClockAdvanceResult advanceWorldClock(
    WorldClockState &state,
    std::uint64_t worldMinutes) noexcept
{
    const std::uint64_t before = state.elapsedWorldMinutes;
    const std::uint64_t completedBefore = before / kWorldMinutesPerDay;
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t available = maximum - before;
    const std::uint64_t applied = std::min(worldMinutes, available);
    state.elapsedWorldMinutes += applied;
    return WorldClockAdvanceResult{
        applied,
        completedBefore,
        state.elapsedWorldMinutes / kWorldMinutesPerDay,
        applied != worldMinutes};
}

const char *worldTimeOfDayName(WorldTimeOfDay timeOfDay) noexcept
{
    switch (timeOfDay)
    {
    case WorldTimeOfDay::Day:
        return "DAY";
    case WorldTimeOfDay::Night:
        return "NIGHT";
    }
    return "UNKNOWN";
}
