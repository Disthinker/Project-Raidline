#pragma once

#include <cstdint>

inline constexpr std::uint64_t kWorldMinutesPerHour = 60;
inline constexpr std::uint64_t kWorldHoursPerDay = 24;
inline constexpr std::uint64_t kWorldMinutesPerDay =
    kWorldMinutesPerHour * kWorldHoursPerDay;
inline constexpr std::uint64_t kInitialWorldMinute =
    8 * kWorldMinutesPerHour;
inline constexpr std::uint32_t kWorldSecondsPerSimulationSecond = 60;
inline constexpr float kBaseClockCheckpointIntervalSeconds = 30.0F;

enum class WorldTimeOfDay
{
    Day,
    Night,
};

struct WorldClockState
{
    std::uint64_t elapsedWorldMinutes{kInitialWorldMinute};

    friend bool operator==(
        const WorldClockState &,
        const WorldClockState &) = default;
};

struct WorldClockProjection
{
    std::uint64_t day{1};
    std::uint32_t hour{8};
    std::uint32_t minute{};
    WorldTimeOfDay timeOfDay{WorldTimeOfDay::Day};
    std::uint64_t completedDays{};
    std::uint64_t minutesUntilNextDay{kWorldMinutesPerDay -
                                      kInitialWorldMinute};

    friend bool operator==(
        const WorldClockProjection &,
        const WorldClockProjection &) = default;
};

struct WorldClockAdvanceResult
{
    std::uint64_t minutesApplied{};
    std::uint64_t completedDaysBefore{};
    std::uint64_t completedDaysAfter{};
    bool saturated{};

    [[nodiscard]] std::uint64_t crossedDayCount() const noexcept
    {
        return completedDaysAfter - completedDaysBefore;
    }
};

[[nodiscard]] WorldClockProjection projectWorldClock(
    const WorldClockState &state) noexcept;

[[nodiscard]] WorldClockAdvanceResult advanceWorldClock(
    WorldClockState &state,
    std::uint64_t worldMinutes) noexcept;

[[nodiscard]] const char *worldTimeOfDayName(
    WorldTimeOfDay timeOfDay) noexcept;
