#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "inventory_domain.h"
#include "profile_state.h"

enum class HomeRegionSafetyZone
{
    SafeCore,
    TransitionBuffer,
    Perimeter
};

inline constexpr float kHomePerimeterTransitionWidth = 480.0F;
inline constexpr std::uint64_t kHomePerimeterRefreshMinutes =
    5U * kWorldMinutesPerDay;
inline constexpr std::uint64_t kHomePerimeterReturnMinutes = 90U;
inline constexpr std::uint64_t kHomePerimeterRescueMinutes = 240U;
inline constexpr int kHomePerimeterRescueHealth = 35;

struct HomePerimeterGenerationContext
{
    RegionalBaseSiteDefinitionId baseSiteDefinitionId;
    Vec2 worldSize{};
    ContentRect safeCore;
    std::vector<ContentRect> blockers;
};

struct HomePerimeterEnsureReceipt
{
    bool succeeded{};
    bool changed{};
    ProfileRevision revision{};
    std::string message;
};

struct HomePerimeterResultReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    std::uint64_t minutesApplied{};
    ProfileRevision revision{};
    std::string message;
};

[[nodiscard]] HomeRegionSafetyZone queryHomeRegionSafetyZone(
    Vec2 point,
    const ContentRect &safeCore) noexcept;

[[nodiscard]] std::uint64_t homePerimeterCycleIndex(
    const WorldClockState &clock) noexcept;

[[nodiscard]] HomePerimeterEnsureReceipt ensureHomePerimeterSnapshot(
    ProfileState &profile,
    const ContentRegistry &content,
    const HomePerimeterGenerationContext &context,
    const CommandContext &command);

[[nodiscard]] HomePerimeterEnsureReceipt beginHomePerimeterOuting(
    ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId,
    const CommandContext &command);

[[nodiscard]] HomePerimeterResultReceipt completeHomePerimeterOuting(
    ProfileState &profile,
    bool rescued,
    const CommandContext &command);

[[nodiscard]] bool homePerimeterOutingActive(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId) noexcept;
