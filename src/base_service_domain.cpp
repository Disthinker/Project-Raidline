#include "base_service_domain.h"

#include "base_resource_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
GunsmithMaintenancePlan maintenanceFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return GunsmithMaintenancePlan{
        false, error, std::move(message), revision};
}

GunsmithMaintenanceReceipt maintenanceReceiptFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return GunsmithMaintenanceReceipt{
        false, false, error, std::move(message), revision};
}

GunsmithCollectionPlan collectionFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision,
    std::uint64_t minutesRemaining = 0)
{
    GunsmithCollectionPlan result{
        false, error, std::move(message), revision};
    result.minutesRemaining = minutesRemaining;
    return result;
}

GunsmithCollectionReceipt collectionReceiptFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return GunsmithCollectionReceipt{
        false, false, error, std::move(message), revision};
}

std::uint32_t durabilityPointsRoundedUp(std::uint32_t centi) noexcept
{
    return centi == 0 ? 0U : (centi + 99U) / 100U;
}
}

GunsmithMaintenancePlan queryGunsmithMaintenance(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartGunsmithMaintenanceCommand &command)
{
    if (profile.pendingRaid.has_value())
    {
        return maintenanceFailure(
            DomainErrorCode::IllegalDestination,
            "gunsmith service is unavailable during a Raid",
            profile.revision);
    }
    if (profile.gunsmithMaintenanceJob.has_value())
    {
        return maintenanceFailure(
            DomainErrorCode::IllegalDestination,
            "gunsmith service already has an active job",
            profile.revision);
    }
    const AssetRecord *weapon = profile.assets.find(command.weaponAssetId);
    if (weapon == nullptr)
    {
        return maintenanceFailure(
            DomainErrorCode::MissingAsset,
            "gunsmith weapon does not exist",
            profile.revision);
    }
    const auto *stored = std::get_if<StoredAssetLocation>(&weapon->location);
    if (stored == nullptr ||
        stored->container != ProfileContainerId::stash())
    {
        return maintenanceFailure(
            DomainErrorCode::IllegalDestination,
            "gunsmith weapon must be stored directly in the Stash",
            profile.revision);
    }

    const ItemDefinition &definition = content.item(weapon->definitionId);
    if (definition.category != ItemCategory::Weapon ||
        !definition.weaponCondition.has_value())
    {
        return maintenanceFailure(
            DomainErrorCode::IllegalDestination,
            "asset is not a serviceable weapon",
            profile.revision);
    }
    const std::uint32_t factoryMaximum =
        definition.weaponCondition->maximumDurabilityCenti;
    if (weapon->currentDurability >= factoryMaximum &&
        weapon->currentMaximumDurability >= factoryMaximum &&
        weapon->weaponMalfunction == WeaponMalfunctionType::None)
    {
        return maintenanceFailure(
            DomainErrorCode::InvalidQuantity,
            "weapon already has factory condition",
            profile.revision);
    }

    const GunsmithFullMaintenanceDefinition &service =
        content.gunsmithFullMaintenance();
    const std::uint32_t currentMissing =
        factoryMaximum - weapon->currentDurability;
    const std::uint32_t maximumMissing =
        factoryMaximum - weapon->currentMaximumDurability;
    const std::uint64_t quoted =
        static_cast<std::uint64_t>(service.baseCost) +
        static_cast<std::uint64_t>(durabilityPointsRoundedUp(currentMissing)) *
            service.currentDurabilityCostPerPoint +
        static_cast<std::uint64_t>(durabilityPointsRoundedUp(maximumMissing)) *
            service.maximumDurabilityCostPerPoint;
    if (quoted == 0 || quoted > std::numeric_limits<std::uint32_t>::max())
    {
        return maintenanceFailure(
            DomainErrorCode::InvalidQuantity,
            "gunsmith quote exceeds the supported currency range",
            profile.revision);
    }
    if (quoted > profile.currency)
    {
        return maintenanceFailure(
            DomainErrorCode::InvalidQuantity,
            "currency is insufficient for gunsmith service",
            profile.revision);
    }
    const BaseOperationalProjection operations = projectBaseOperations(
        profile.baseResources,
        content.baseOperations());
    const std::uint64_t adjustedDuration =
        (static_cast<std::uint64_t>(service.durationMinutes) *
             operations.serviceDurationPercent +
         99U) /
        100U;
    if (adjustedDuration == 0U ||
        adjustedDuration > std::numeric_limits<std::uint32_t>::max())
    {
        return maintenanceFailure(
            DomainErrorCode::InvalidQuantity,
            "gunsmith service duration is invalid",
            profile.revision);
    }
    if (profile.nextBaseServiceJobId ==
            std::numeric_limits<BaseServiceJobId>::max() ||
        profile.worldClock.elapsedWorldMinutes >
            std::numeric_limits<std::uint64_t>::max() -
                adjustedDuration)
    {
        return maintenanceFailure(
            DomainErrorCode::RevisionOverflow,
            "gunsmith service timeline cannot advance",
            profile.revision);
    }

    return GunsmithMaintenancePlan{
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        command.weaponAssetId,
        static_cast<std::uint32_t>(quoted),
        static_cast<std::uint32_t>(adjustedDuration),
        operations.serviceDurationPercent,
        operations.tier,
        operations.limitingResource,
        profile.worldClock.elapsedWorldMinutes + adjustedDuration,
        weapon->currentDurability,
        weapon->currentMaximumDurability,
        factoryMaximum};
}

GunsmithMaintenanceReceipt executeGunsmithMaintenance(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartGunsmithMaintenanceCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return maintenanceReceiptFailure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return GunsmithMaintenanceReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return maintenanceReceiptFailure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return maintenanceReceiptFailure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    const GunsmithMaintenancePlan plan = queryGunsmithMaintenance(
        profile, content, command);
    if (!plan.canCommit)
    {
        return maintenanceReceiptFailure(
            plan.error, plan.message, profile.revision);
    }

    ProfileState candidate = profile;
    const BaseServiceJobId jobId = candidate.nextBaseServiceJobId++;
    AssetRecord *weapon = candidate.assets.findMutable(command.weaponAssetId);
    const StoredAssetLocation returnLocation =
        std::get<StoredAssetLocation>(weapon->location);
    weapon->location = BaseServiceAssetLocation{jobId};
    candidate.currency -= plan.quotedCurrency;
    candidate.gunsmithMaintenanceJob = GunsmithMaintenanceJob{
        jobId,
        command.weaponAssetId,
        returnLocation.origin,
        candidate.worldClock.elapsedWorldMinutes,
        plan.completionWorldMinute,
        plan.quotedCurrency,
        plan.targetFactoryDurabilityCenti};
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;

    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return maintenanceReceiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    return GunsmithMaintenanceReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        jobId,
        command.weaponAssetId,
        plan.quotedCurrency,
        plan.completionWorldMinute};
}

GunsmithCollectionPlan queryGunsmithCollection(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    if (!profile.gunsmithMaintenanceJob.has_value())
    {
        return collectionFailure(
            DomainErrorCode::MissingAsset,
            "gunsmith service has no active job",
            profile.revision);
    }
    const GunsmithMaintenanceJob &job = *profile.gunsmithMaintenanceJob;
    const AssetRecord *weapon = profile.assets.find(job.weaponAssetId);
    if (weapon == nullptr ||
        weapon->location != AssetLocation{BaseServiceAssetLocation{job.jobId}})
    {
        return collectionFailure(
            DomainErrorCode::InvalidProfile,
            "gunsmith service asset ownership is invalid",
            profile.revision);
    }
    if (profile.worldClock.elapsedWorldMinutes < job.completionWorldMinute)
    {
        return collectionFailure(
            DomainErrorCode::InvalidQuantity,
            "gunsmith service is still in progress",
            profile.revision,
            job.completionWorldMinute -
                profile.worldClock.elapsedWorldMinutes);
    }

    const ItemDefinition &definition = content.item(weapon->definitionId);
    std::optional<GridPosition> destination;
    if (profilePlacementFits(
            profile,
            content,
            ProfileContainerId::stash(),
            job.returnOrigin,
            definition,
            weapon->orientation,
            weapon->instanceId))
    {
        destination = job.returnOrigin;
    }
    else
    {
        destination = findFirstProfileFit(
            profile,
            content,
            ProfileContainerId::stash(),
            definition,
            weapon->orientation,
            weapon->instanceId);
    }
    if (!destination.has_value())
    {
        return collectionFailure(
            DomainErrorCode::Capacity,
            "Stash has no legal space for serviced weapon",
            profile.revision);
    }
    return GunsmithCollectionPlan{
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        job.jobId,
        job.weaponAssetId,
        0,
        StoredAssetLocation{ProfileContainerId::stash(), *destination},
        job.targetFactoryDurabilityCenti};
}

GunsmithCollectionReceipt executeGunsmithCollection(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return collectionReceiptFailure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return GunsmithCollectionReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return collectionReceiptFailure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return collectionReceiptFailure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    const GunsmithCollectionPlan plan = queryGunsmithCollection(
        profile, content);
    if (!plan.canCommit)
    {
        return collectionReceiptFailure(
            plan.error, plan.message, profile.revision);
    }

    ProfileState candidate = profile;
    AssetRecord *weapon = candidate.assets.findMutable(plan.weaponAssetId);
    const std::uint32_t restoredCurrent =
        plan.targetFactoryDurabilityCenti - weapon->currentDurability;
    const std::uint32_t restoredMaximum =
        plan.targetFactoryDurabilityCenti - weapon->currentMaximumDurability;
    const bool cleared =
        weapon->weaponMalfunction != WeaponMalfunctionType::None;
    weapon->currentMaximumDurability = plan.targetFactoryDurabilityCenti;
    weapon->currentDurability = plan.targetFactoryDurabilityCenti;
    weapon->weaponMalfunction = WeaponMalfunctionType::None;
    weapon->location = plan.destination;
    candidate.gunsmithMaintenanceJob.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;

    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return collectionReceiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    return GunsmithCollectionReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.weaponAssetId,
        plan.destination,
        restoredCurrent,
        restoredMaximum,
        cleared};
}
