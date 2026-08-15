#pragma once

#include <string>

#include "inventory_domain.h"

enum class RaidLifecycleError
{
    None,
    InvalidCommand,
    StaleRevision,
    RaidAlreadyPending,
    MissingPendingRaid,
    InvalidProfile,
    Capacity,
    RevisionOverflow
};

struct DeployCommand
{
    std::string raidId;
    std::string settlementId;
    std::uint64_t seed{};
    MapDefinitionId mapDefinitionId;
};

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

[[nodiscard]] DeployReceipt executeDeploy(
    ProfileState &profile,
    const ContentRegistry &content,
    const DeployCommand &command,
    const CommandContext &context);

[[nodiscard]] RaidSettlementReceipt settlePendingRaid(
    ProfileState &profile,
    const ContentRegistry &content,
    std::string_view settlementId,
    RaidResultOutcome outcome);

[[nodiscard]] InventoryReceipt pickupRaidLoot(
    ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId assetId,
    const CommandContext &context);

