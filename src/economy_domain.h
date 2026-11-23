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

inline const ItemDefinitionId kWeaponSalvageMaterialId{"item.loot.scrap_parts"};
inline constexpr std::uint32_t kWeaponSalvageMaterialQuantity = 1U;

struct SalvageWeaponCommand
{
    AssetInstanceId instanceId{};
};

struct WeaponSalvagePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    std::optional<GridPosition> outputPosition;
    ItemOrientation outputOrientation{ItemOrientation::Degrees0};
};

[[nodiscard]] WeaponSalvagePlan queryWeaponSalvage(
    const ProfileState &, const ContentRegistry &, AssetInstanceId);

struct ClaimReliefCommand
{
    std::string batchId;
};

using EconomyCommand = std::variant<
    PurchaseCommand,
    RecycleCommand,
    SalvageWeaponCommand,
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
