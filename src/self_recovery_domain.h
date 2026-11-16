#pragma once

#include "inventory_domain.h"

struct RaidSelfRecoveryPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::size_t rootCount{};
};

[[nodiscard]] RaidSelfRecoveryPlan queryOpenRaidSelfRecovery(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidVisibilityProjection *observation = nullptr,
    const RaidObservationKey *currentObservation = nullptr);

[[nodiscard]] InventoryReceipt executeOpenRaidSelfRecovery(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context,
    const RaidVisibilityProjection *observation = nullptr,
    const RaidObservationKey *currentObservation = nullptr);

// Called only after GameplayWorld resolves the bound carrier's first death
// to a searchable position. This publishes the scene, not an asset transfer.
[[nodiscard]] bool revealRaidLostWeaponPursuitCorpse(
    ProfileState &profile, const ContentRegistry &content, Vec2 position) noexcept;

[[nodiscard]] bool raidLostWeaponCorpsePointClear(
    const MapDefinition &map, const PendingRaidSnapshot &raid, Vec2 point) noexcept;
