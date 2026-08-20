#include "maintenance_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
WeaponMaintenancePlan failurePlan(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return WeaponMaintenancePlan{
        false, error, std::move(message), revision};
}

WeaponMaintenanceReceipt failureReceipt(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return WeaponMaintenanceReceipt{
        false, false, error, std::move(message), revision};
}

std::uint32_t roundedMaximumLoss(
    std::uint32_t repair,
    std::uint32_t basisPoints) noexcept
{
    const std::uint64_t numerator =
        static_cast<std::uint64_t>(repair) * basisPoints;
    return static_cast<std::uint32_t>((numerator + 9999U) / 10000U);
}
}

WeaponMaintenancePlan queryWeaponMaintenance(
    const ProfileState &profile,
    const ContentRegistry &content,
    const WeaponMaintenanceCommand &command)
{
    const AssetRecord *kit = profile.assets.find(command.kitAssetId);
    const AssetRecord *weapon = profile.assets.find(command.weaponAssetId);
    if (kit == nullptr || weapon == nullptr)
    {
        return failurePlan(
            DomainErrorCode::MissingAsset,
            "maintenance kit or weapon does not exist",
            profile.revision);
    }
    if (command.kitAssetId == command.weaponAssetId)
    {
        return failurePlan(
            DomainErrorCode::IllegalDestination,
            "maintenance kit cannot repair itself",
            profile.revision);
    }
    if (command.access == MaintenanceAccess::CarriedOnly &&
        (!assetIsCarried(profile, command.kitAssetId) ||
         !assetIsCarried(profile, command.weaponAssetId)))
    {
        return failurePlan(
            DomainErrorCode::IllegalDestination,
            "Raid maintenance requires carried assets",
            profile.revision);
    }

    const ItemDefinition &kitDefinition = content.item(kit->definitionId);
    const ItemDefinition &weaponDefinition = content.item(weapon->definitionId);
    if (!kitDefinition.weaponMaintenance.has_value() ||
        !weaponDefinition.weaponCondition.has_value() ||
        kit->remainingCharges == 0)
    {
        return failurePlan(
            DomainErrorCode::InvalidQuantity,
            "maintenance capability or capacity is unavailable",
            profile.revision);
    }
    if (weapon->currentDurability >= weapon->currentMaximumDurability &&
        weapon->weaponMalfunction == WeaponMalfunctionType::None)
    {
        return failurePlan(
            DomainErrorCode::InvalidQuantity,
            "weapon does not require maintenance",
            profile.revision);
    }

    const WeaponMaintenanceDefinition &maintenance =
        *kitDefinition.weaponMaintenance;
    const std::uint32_t factoryMaximum =
        weaponDefinition.weaponCondition->maximumDurabilityCenti;
    const std::uint32_t floorMaximum = factoryMaximum / 5U;
    const std::uint32_t lossBasisPoints =
        command.location == MaintenanceLocation::Raid
            ? maintenance.raidMaximumLossBasisPoints
            : 0U;
    const std::uint32_t capacity = std::min(
        kit->remainingCharges,
        maintenance.capacityCenti);
    std::uint32_t repair = std::min(
        capacity,
        weapon->currentMaximumDurability - weapon->currentDurability);
    std::uint32_t newMaximum = weapon->currentMaximumDurability;
    while (repair > 0)
    {
        const std::uint32_t loss = roundedMaximumLoss(
            repair, lossBasisPoints);
        newMaximum = std::max(
            floorMaximum,
            weapon->currentMaximumDurability -
                std::min(loss, weapon->currentMaximumDurability));
        if (weapon->currentDurability + repair <= newMaximum)
        {
            break;
        }
        --repair;
    }
    const bool clearsFault =
        weapon->weaponMalfunction != WeaponMalfunctionType::None;
    if (repair == 0 && !clearsFault)
    {
        return failurePlan(
            DomainErrorCode::InvalidQuantity,
            "maintenance would produce no result",
            profile.revision);
    }
    const std::uint32_t consumedCapacity = repair > 0 ? repair : 1U;
    if (consumedCapacity > capacity)
    {
        return failurePlan(
            DomainErrorCode::InvalidQuantity,
            "maintenance kit has no usable capacity",
            profile.revision);
    }

    return WeaponMaintenancePlan{
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        repair,
        consumedCapacity,
        weapon->currentMaximumDurability,
        newMaximum,
        command.location == MaintenanceLocation::Raid
            ? maintenance.raidActionDurationMs
            : 0U,
        clearsFault};
}

WeaponMaintenanceReceipt executeWeaponMaintenance(
    ProfileState &profile,
    const ContentRegistry &content,
    const WeaponMaintenanceCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failureReceipt(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return WeaponMaintenanceReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failureReceipt(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failureReceipt(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }
    const WeaponMaintenancePlan plan = queryWeaponMaintenance(
        profile, content, command);
    if (!plan.canCommit)
    {
        return failureReceipt(plan.error, plan.message, profile.revision);
    }

    ProfileState candidate = profile;
    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    AssetRecord *kit = candidate.assets.findMutable(command.kitAssetId);
    weapon->currentMaximumDurability = plan.currentMaximumAfterCenti;
    weapon->currentDurability = std::min(
        plan.currentMaximumAfterCenti,
        weapon->currentDurability + plan.restoredDurabilityCenti);
    const bool cleared =
        weapon->weaponMalfunction != WeaponMalfunctionType::None;
    weapon->weaponMalfunction = WeaponMalfunctionType::None;
    kit->remainingCharges -= plan.consumedCapacityCenti;
    if (kit->remainingCharges == 0)
    {
        static_cast<void>(candidate.assets.erase(command.kitAssetId));
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return failureReceipt(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    return WeaponMaintenanceReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.restoredDurabilityCenti,
        plan.consumedCapacityCenti,
        plan.currentMaximumBeforeCenti,
        plan.currentMaximumAfterCenti,
        cleared};
}
