#pragma once

#include "inventory_domain.h"

inline constexpr std::uint32_t kBaseSiegeThreatThreshold = 100U;
inline constexpr std::uint32_t kBaseSiegeWarningSeconds = 180U;
inline constexpr std::uint32_t kBaseSiegeRaidThreatUnits = 20U;
inline constexpr std::uint32_t kBaseSiegeSuccessSafeDays = 7U;
inline constexpr std::uint32_t kBaseSiegeFailureSafeDays = 12U;
inline constexpr std::uint32_t kBaseSiegeMinimumResidents = 4U;

enum class BaseThreatTier
{
    Low,
    Elevated,
    Critical,
    Warning
};

struct BaseThreatProjection
{
    BaseThreatTier tier{BaseThreatTier::Low};
    std::uint32_t totalThreatUnits{};
    std::uint32_t raidThreatUnits{};
    std::uint32_t populationThreatUnits{};
    std::uint32_t siteThreatUnits{};
    std::uint64_t safeMinutesRemaining{};
    bool warningActive{};
    std::uint32_t warningRemainingSeconds{};
    bool autoDefensePresetSaved{};
    bool requiresFirstPreset{};
};

struct BaseThreatAdvanceResult
{
    bool changed{};
    std::uint64_t daysResolved{};
    std::uint32_t populationThreatAdded{};
    std::uint32_t siteThreatAdded{};
};

struct BaseAutoDefensePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t requiredSecurity{};
    std::uint32_t availableSecurity{};
    bool projectedSuccess{};
};

struct BaseAutoDefenseReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseSiegeOutcome outcome{BaseSiegeOutcome::None};
    std::uint32_t securitySpent{};
    std::uint32_t populationLost{};
    std::uint64_t safeUntilWorldMinute{};
};

[[nodiscard]] std::uint32_t totalBaseThreat(
    const BaseSiegeState &state) noexcept;
[[nodiscard]] BaseThreatProjection projectBaseThreat(
    const ProfileState &profile) noexcept;
[[nodiscard]] BaseThreatAdvanceResult synchronizeBaseThreatThrough(
    ProfileState &profile,
    const ContentRegistry &content) noexcept;
void applySettledRaidBaseThreat(ProfileState &profile) noexcept;
[[nodiscard]] bool activateBaseSiegeWarningIfEligible(
    ProfileState &profile) noexcept;
[[nodiscard]] bool advanceBaseSiegeWarning(
    ProfileState &profile,
    std::uint32_t elapsedSeconds) noexcept;
[[nodiscard]] BaseAutoDefensePlan queryBaseAutoDefense(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;
[[nodiscard]] BaseAutoDefenseReceipt executeBaseAutoDefense(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);
[[nodiscard]] const char *baseThreatTierName(BaseThreatTier tier) noexcept;
[[nodiscard]] const char *baseSiegeOutcomeName(
    BaseSiegeOutcome outcome) noexcept;
