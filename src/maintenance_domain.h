#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"
#include "profile_state.h"

enum class MaintenanceAccess
{
    AnyOwned,
    CarriedOnly
};

enum class MaintenanceLocation
{
    Base,
    Raid
};

struct WeaponMaintenanceCommand
{
    AssetInstanceId kitAssetId{};
    AssetInstanceId weaponAssetId{};
    MaintenanceAccess access{MaintenanceAccess::AnyOwned};
    MaintenanceLocation location{MaintenanceLocation::Base};
};

struct WeaponMaintenancePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t restoredDurabilityCenti{};
    std::uint32_t consumedCapacityCenti{};
    std::uint32_t currentMaximumBeforeCenti{};
    std::uint32_t currentMaximumAfterCenti{};
    std::uint32_t actionDurationMs{};
    bool clearsMalfunction{};
};

struct WeaponMaintenanceReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t restoredDurabilityCenti{};
    std::uint32_t consumedCapacityCenti{};
    std::uint32_t currentMaximumBeforeCenti{};
    std::uint32_t currentMaximumAfterCenti{};
    bool clearedMalfunction{};
};

[[nodiscard]] WeaponMaintenancePlan queryWeaponMaintenance(
    const ProfileState &profile,
    const ContentRegistry &content,
    const WeaponMaintenanceCommand &command);

[[nodiscard]] WeaponMaintenanceReceipt executeWeaponMaintenance(
    ProfileState &profile,
    const ContentRegistry &content,
    const WeaponMaintenanceCommand &command,
    const CommandContext &context);

struct ArmorMaintenanceCommand
{
    AssetInstanceId kitAssetId{};
    AssetInstanceId armorAssetId{};
    MaintenanceAccess access{MaintenanceAccess::AnyOwned};
    MaintenanceLocation location{MaintenanceLocation::Base};
};

struct ArmorMaintenancePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t restoredDurability{};
    std::uint32_t consumedCapacityCenti{};
    std::uint32_t currentMaximumBefore{};
    std::uint32_t currentMaximumAfter{};
    std::uint32_t actionDurationMs{};
};

struct ArmorMaintenanceReceipt
{
    bool succeeded{};
    bool idempotent{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t restoredDurability{};
    std::uint32_t consumedCapacityCenti{};
    std::uint32_t currentMaximumBefore{};
    std::uint32_t currentMaximumAfter{};
};

[[nodiscard]] ArmorMaintenancePlan queryArmorMaintenance(
    const ProfileState &profile,
    const ContentRegistry &content,
    const ArmorMaintenanceCommand &command);

[[nodiscard]] ArmorMaintenanceReceipt executeArmorMaintenance(
    ProfileState &profile,
    const ContentRegistry &content,
    const ArmorMaintenanceCommand &command,
    const CommandContext &context);
