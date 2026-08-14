#pragma once

#include <variant>

#include "inventory_domain.h"

struct PurchaseCommand
{
    ItemDefinitionId definitionId;
    std::uint32_t quantity{1};
};

struct RecycleCommand
{
    AssetInstanceId instanceId{};
};

struct ClaimReliefCommand
{
    std::string batchId;
};

using EconomyCommand = std::variant<
    PurchaseCommand,
    RecycleCommand,
    ClaimReliefCommand>;

struct EconomyReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::int64_t currencyDelta{};
};

[[nodiscard]] bool hasMinimumRaidCapability(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;

[[nodiscard]] bool isReliefEligible(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;

[[nodiscard]] EconomyReceipt executeEconomy(
    ProfileState &profile,
    const ContentRegistry &content,
    const EconomyCommand &command,
    const CommandContext &context);
