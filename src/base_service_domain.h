#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"
#include "profile_state.h"

struct StartGunsmithMaintenanceCommand
{
    AssetInstanceId weaponAssetId{};
};

struct GunsmithMaintenancePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    AssetInstanceId weaponAssetId{};
    std::uint32_t quotedCurrency{};
    std::uint32_t durationMinutes{};
    std::uint64_t completionWorldMinute{};
    std::uint32_t currentDurabilityBeforeCenti{};
    std::uint32_t currentMaximumBeforeCenti{};
    std::uint32_t targetFactoryDurabilityCenti{};
};

struct GunsmithMaintenanceReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseServiceJobId jobId{};
    AssetInstanceId weaponAssetId{};
    std::uint32_t currencyPaid{};
    std::uint64_t completionWorldMinute{};
};

struct GunsmithCollectionPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseServiceJobId jobId{};
    AssetInstanceId weaponAssetId{};
    std::uint64_t minutesRemaining{};
    StoredAssetLocation destination;
    std::uint32_t targetFactoryDurabilityCenti{};
};

struct GunsmithCollectionReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    AssetInstanceId weaponAssetId{};
    StoredAssetLocation destination;
    std::uint32_t restoredCurrentDurabilityCenti{};
    std::uint32_t restoredMaximumDurabilityCenti{};
    bool clearedMalfunction{};
};

[[nodiscard]] GunsmithMaintenancePlan queryGunsmithMaintenance(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartGunsmithMaintenanceCommand &command);

[[nodiscard]] GunsmithMaintenanceReceipt executeGunsmithMaintenance(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartGunsmithMaintenanceCommand &command,
    const CommandContext &context);

[[nodiscard]] GunsmithCollectionPlan queryGunsmithCollection(
    const ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] GunsmithCollectionReceipt executeGunsmithCollection(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);
