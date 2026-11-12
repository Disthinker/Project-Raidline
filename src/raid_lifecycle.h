#pragma once

#include <functional>
#include <span>
#include <string>

#include "inventory_domain.h"

struct RaidReloadWorldAccess;

[[nodiscard]] bool hasFixedRaidResourceOptic(
    const ContentRegistry &content,
    const PendingRaidSnapshot &raid,
    const RaidResourcePointSnapshot &point,
    std::uint32_t localSlot) noexcept;

enum class RaidLifecycleError
{
    None,
    InvalidCommand,
    StaleRevision,
    RaidAlreadyPending,
    MissingPendingRaid,
    InvalidProfile,
    Capacity,
    InsufficientIntelligence,
    RevisionOverflow
};

struct DeployCommand
{
    std::string raidId;
    std::string settlementId;
    std::uint64_t seed{};
    MapDefinitionId mapDefinitionId;
    RaidIntelligenceLoadout intelligence;
    std::optional<std::string> selfRecoveryRecordId;
    std::optional<RegionalOutpostDefinitionId> outpostRestorationId;
    std::optional<RegionalBaseSiteDefinitionId> baseSiteClearanceId;
    std::optional<RegionalBaseSiteDefinitionId> basePerimeterSweepId;
};

enum class RaidDeploymentProgressStage
{
    Validating,
    PreparingSnapshot,
    GeneratingLayout,
    FreezingLoot,
    ValidatingSnapshot,
    BuildingWorld,
    SavingProfile,
    Complete
};

struct RaidDeploymentProgress
{
    float completion{};
    RaidDeploymentProgressStage stage{
        RaidDeploymentProgressStage::Validating};
};

using RaidDeploymentProgressCallback =
    std::function<void(const RaidDeploymentProgress &)>;

inline void reportRaidDeploymentProgress(
    const RaidDeploymentProgressCallback &callback,
    float completion,
    RaidDeploymentProgressStage stage) noexcept
{
    if (!callback)
        return;
    try
    {
        callback({completion, stage});
    }
    catch (...)
    {
        // Presentation progress is observational and must never affect the
        // atomic Deploy transaction or turn a committed Raid into a failure.
    }
}

struct DeployReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    RaidLifecycleError error{RaidLifecycleError::None};
    std::string message;
    ProfileRevision revision{};
};

struct RaidSettlementReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    RaidLifecycleError error{RaidLifecycleError::None};
    std::string message;
    ProfileRevision revision{};
    RaidResultOutcome outcome{RaidResultOutcome::PlayerDead};
};

struct RaidFuelExtractionCompletion
{
    std::string_view raidId;
    AssetInstanceId fuelAssetId{};
};

struct RaidRollbackReceipt
{
    bool succeeded{};
    RaidLifecycleError error{RaidLifecycleError::None};
    std::string message;
    ProfileRevision revision{};
};

struct RaidTravelPreview
{
    bool reachable{};
    std::uint32_t outboundMinutes{};
    std::uint32_t returnMinutes{};
    std::uint32_t failureRegroupMinutes{};
    WorldClockProjection departure;
    WorldClockProjection arrival;
    WorldClockProjection extractedReturn;
    WorldClockProjection failureReturn;
    std::vector<RegionRouteDefinitionId> routeIds;
    bool usesOnlineOutpost{};
};

[[nodiscard]] RaidTravelPreview queryRaidTravel(
    const ProfileState &profile,
    const ContentRegistry &content,
    const MapDefinition &map) noexcept;

[[nodiscard]] DeployReceipt executeDeploy(
    ProfileState &profile,
    const ContentRegistry &content,
    const DeployCommand &command,
    const CommandContext &context,
    const RaidDeploymentProgressCallback &progress = {});

[[nodiscard]] RaidSettlementReceipt settlePendingRaid(
    ProfileState &profile,
    const ContentRegistry &content,
    std::string_view settlementId,
    RaidResultOutcome outcome,
    std::optional<RaidFuelExtractionCompletion> fuel = std::nullopt);

[[nodiscard]] std::optional<AssetInstanceId> findCarriedRaidFuel(const ProfileState &profile);
[[nodiscard]] bool isCarriedRaidFuel(const ProfileState &profile, AssetInstanceId assetId);

// Compatibility recovery for saves written by builds that persisted a
// pending Raid. New deployments keep the exact pre-Raid Profile on disk.
[[nodiscard]] RaidRollbackReceipt rollbackPendingRaidToBase(
    ProfileState &profile,
    const ContentRegistry &content);

inline constexpr float kRaidBackpackInteractionRange = 72.0F;

[[nodiscard]] constexpr Rect raidBackpackGroundBounds(Vec2 position) noexcept
{
    return {{position.x - 12.0F, position.y - 12.0F}, {24.0F, 24.0F}};
}

[[nodiscard]] bool raidBackpackPlacementValid(
    const PendingRaidSnapshot &raid,
    const MapDefinition &map,
    const RaidDroppedBackpackLocation &location) noexcept;

[[nodiscard]] InventoryReceipt dropRaidBackpack(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId assetId,
    Vec2 landing,
    const CommandContext &context,
    const RaidReloadWorldAccess &access,
    std::span<const AssetInstanceId> lockedAssets = {});

[[nodiscard]] InventoryReceipt pickupRaidBackpack(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId assetId,
    const CommandContext &context,
    const RaidReloadWorldAccess &access);

[[nodiscard]] InventoryReceipt pickupRaidLoot(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId assetId,
    const CommandContext &context,
    const RaidVisibilityProjection *observation = nullptr,
    const RaidObservationKey *currentObservation = nullptr,
    const RaidReloadWorldAccess *reloadAccess = nullptr);
