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
    const ContentRegistry &content);

[[nodiscard]] InventoryReceipt executeOpenRaidSelfRecovery(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);
