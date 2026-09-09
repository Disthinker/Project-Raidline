#pragma once

#include "inventory_domain.h"

struct BuildFortificationCommand
{
    FortificationDefinitionId definition;
};
struct StoreFortificationCommand
{
    FortificationInstanceId instance;
};
struct RepairFortificationCommand
{
    FortificationInstanceId instance;
};
// Installation deliberately stays unavailable until its collision, damage,
// warning-freeze and restoration consumers are delivered together.
using FortificationCommand =
    std::variant<BuildFortificationCommand, StoreFortificationCommand, RepairFortificationCommand>;

struct FortificationPlan : InventoryPlan
{
    FortificationInstanceId instance;
    std::uint32_t materialCost{};
    std::uint32_t durabilityAfter{};
};
struct FortificationReceipt : InventoryReceipt
{
    FortificationInstanceId instance;
    std::uint32_t materialSpent{};
};

[[nodiscard]] FortificationPlan queryFortificationCommand(const ProfileState &,
                                                          const ContentRegistry &,
                                                          const FortificationCommand &);
[[nodiscard]] FortificationReceipt executeFortificationCommand(ProfileState &,
                                                               const ContentRegistry &,
                                                               const FortificationCommand &,
                                                               const CommandContext &);
[[nodiscard]] ProfileValidationResult validateBaseFortifications(const ProfileState &,
                                                                 const ContentRegistry &);
[[nodiscard]] std::uint64_t baseFortificationFingerprint(const BaseFortificationState &) noexcept;
void storeAllBaseFortifications(BaseFortificationState &) noexcept;
