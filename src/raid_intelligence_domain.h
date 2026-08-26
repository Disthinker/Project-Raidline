#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"
#include "raid_intelligence_types.h"

struct RaidIntelligencePurchaseCommand
{
    MapDefinitionId mapDefinitionId;
    RaidIntelligenceCategory category{RaidIntelligenceCategory::Transport};
};

struct RaidIntelligencePurchasePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t price{};
    std::uint32_t ownedBefore{};
};

struct RaidIntelligencePurchaseReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t currencyPaid{};
    std::uint32_t ownedAfter{};
};

struct RaidInteriorIntelligencePurchaseCommand
{
    RaidSpaceDefinitionId interiorId;
};

struct RaidInteriorIntelligencePurchasePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t price{};
};

struct RaidInteriorIntelligencePurchaseReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t currencyPaid{};
};

[[nodiscard]] RaidIntelligencePurchasePlan queryRaidIntelligencePurchase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidIntelligencePurchaseCommand &command);

[[nodiscard]] RaidIntelligencePurchaseReceipt executeRaidIntelligencePurchase(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidIntelligencePurchaseCommand &command,
    const CommandContext &context);

[[nodiscard]] RaidInteriorIntelligencePurchasePlan
queryRaidInteriorIntelligencePurchase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidInteriorIntelligencePurchaseCommand &command);

[[nodiscard]] RaidInteriorIntelligencePurchaseReceipt
executeRaidInteriorIntelligencePurchase(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidInteriorIntelligencePurchaseCommand &command,
    const CommandContext &context);
