#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "inventory_domain.h"

inline constexpr std::uint32_t kMaximumBaseResource = 100;
inline constexpr BaseResourceBundle kBaseDailyDemand{8, 6, 5, 4};

enum class BaseResourceKind
{
    Food,
    Hygiene,
    Morale,
    Security
};

enum class BaseOperationalTier
{
    Critical,
    Strained,
    Stable,
    Supported
};

struct BaseOperationalProjection
{
    BaseOperationalTier tier{BaseOperationalTier::Stable};
    BaseResourceKind limitingResource{BaseResourceKind::Food};
    BaseResourceBundle reserveDays;
};

[[nodiscard]] BaseOperationalTier projectBaseResourceTier(
    std::uint32_t current,
    std::uint32_t dailyDemand,
    const BaseOperationsDefinition &definition) noexcept;

[[nodiscard]] BaseOperationalProjection projectBaseOperations(
    const BaseResourceState &state,
    const BaseOperationsDefinition &definition,
    BaseResourceBundle dailyDemand = kBaseDailyDemand) noexcept;

struct ContributeBaseAssetCommand
{
    AssetInstanceId assetId{};
};

struct BaseResourcePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseResourceBundle contribution;
};

struct BaseResourceReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseResourceBundle contribution;
};

struct SetBaseSupplyAssignmentCommand
{
    ItemDefinitionId itemDefinitionId;
    std::optional<BaseSupplyCategory> category;
};

struct BaseSupplyAssignmentPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::optional<BaseSupplyCategory> category;
};

struct BaseSupplyAssignmentReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::optional<BaseSupplyCategory> category;
};

// Read-only view of the unified owned inventory and the items explicitly
// authorized to cover the next Base need. This projection never reserves or
// consumes assets; the daily settlement remains the only authority that does.
struct BaseSupplyReadinessProjection
{
    std::size_t baseAccessibleStacks{};
    std::uint64_t baseAccessibleUnits{};
    std::uint32_t assignedDefinitionCount{};
    std::uint32_t ownedAssignedDefinitionCount{};
    BaseResourceBundle pool;
    BaseResourceBundle dailyDemand;
    BaseResourceBundle authorizedContribution;
    BaseResourceBundle projectedShortfall;
};

[[nodiscard]] BaseSupplyReadinessProjection projectBaseSupplyReadiness(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseResourceBundle dailyDemand = kBaseDailyDemand);

[[nodiscard]] std::uint32_t baseSupplyContribution(
    const ItemDefinition &definition,
    BaseSupplyCategory category) noexcept;

[[nodiscard]] BaseSupplyAssignmentPlan queryBaseSupplyAssignment(
    const ProfileState &profile,
    const ContentRegistry &content,
    const SetBaseSupplyAssignmentCommand &command);

[[nodiscard]] BaseSupplyAssignmentReceipt executeBaseSupplyAssignment(
    ProfileState &profile,
    const ContentRegistry &content,
    const SetBaseSupplyAssignmentCommand &command,
    const CommandContext &context);

[[nodiscard]] BaseResourcePlan queryBaseResourceContribution(
    const ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command);

[[nodiscard]] BaseResourceReceipt executeBaseResourceContribution(
    ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command,
    const CommandContext &context);

struct BaseDailyDemandResult
{
    std::uint64_t cyclesResolved{};
    BaseResourceBundle latestShortfall;
    std::uint64_t shortageCycleCount{};
};

// Resolves every unprocessed daily boundary through completedWorldDays.
// The implementation is constant-time even when future sleep/travel commands
// advance across many days. Shortages remain non-blocking and never mutate
// assets.
[[nodiscard]] BaseDailyDemandResult applyBaseDailyDemandThrough(
    BaseResourceState &state,
    std::uint64_t completedWorldDays,
    BaseResourceBundle dailyDemand = kBaseDailyDemand) noexcept;

// Resolves daily needs and, only when an assigned need would otherwise be
// short, consumes the minimum whole quantity of explicitly authorized owned
// item definitions. Assets remain in their real Stash/carried location until
// this function commits their consumption. While a Raid is pending no owned
// assets are auto-consumed, so abnormal rollback can restore the exact
// pre-deployment Profile without reconstructing inventory instances.
[[nodiscard]] BaseDailyDemandResult applyBaseDailyDemandWithSupplyThrough(
    ProfileState &profile,
    const ContentRegistry &content,
    std::uint64_t completedWorldDays,
    BaseResourceBundle dailyDemand = kBaseDailyDemand) noexcept;

struct BasePrioritySyncResult
{
    bool changed{};
    std::uint64_t cyclesAdvanced{};
    std::uint64_t newlyMissedCycles{};
};

// Selects the one-to-three requests for the current five-day content cycle,
// freezing the population tier until the next boundary. It can catch up across
// arbitrary world-time jumps without iterating each missed cycle.
[[nodiscard]] BasePrioritySyncResult synchronizeBasePriorityThrough(
    ProfileState &profile,
    const ContentRegistry &content);

struct SubmitBasePriorityCommand
{
    BasePriorityDefinitionId priorityDefinitionId;
    std::vector<AssetInstanceId> assetIds;
};

struct BasePriorityAssetContribution
{
    AssetInstanceId assetId{};
    ItemDefinitionId itemDefinitionId;
    std::uint32_t quantity{};
    std::uint32_t contribution{};
};

struct BasePriorityProjection
{
    BasePriorityDefinitionId definitionId;
    std::string displayName;
    BaseSupplyCategory category{BaseSupplyCategory::Food};
    std::uint32_t requiredContribution{};
    std::string sourceHint;
    bool fulfilled{};
    std::vector<BasePriorityAssetContribution> eligibleAssets;
};

struct BasePriorityPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t totalContribution{};
    std::uint32_t excessContribution{};
    std::vector<BasePriorityAssetContribution> consumedAssets;
};

struct BasePriorityReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BasePriorityDefinitionId priorityDefinitionId;
    std::uint32_t totalContribution{};
    std::uint32_t excessContribution{};
    std::vector<AssetInstanceId> consumedAssetIds;
};

[[nodiscard]] std::vector<BasePriorityProjection> projectBasePriorities(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] std::vector<BasePriorityDefinitionId>
selectBasePriorityDefinitions(
    std::uint64_t cycleIndex,
    std::uint32_t frozenPopulation,
    const ContentRegistry &content);

[[nodiscard]] BasePriorityPlan queryBasePrioritySubmission(
    const ProfileState &profile,
    const ContentRegistry &content,
    const SubmitBasePriorityCommand &command);

[[nodiscard]] BasePriorityReceipt executeBasePrioritySubmission(
    ProfileState &profile,
    const ContentRegistry &content,
    const SubmitBasePriorityCommand &command,
    const CommandContext &context);
