#include "base_emergency_power_domain.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
const BaseFacilityDefinitionId kWorkshopFacilityId{"base_facility.workshop"};
const BaseFacilityDefinitionId kMedicalFacilityId{"base_facility.medical"};

bool isStashedFuel(const AssetRecord *asset) noexcept
{
    if (asset == nullptr || asset->definitionId != kRaidFuelCanId || asset->quantity != 1U)
        return false;
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    return stored != nullptr && stored->container.kind == ProfileContainerKind::Stash;
}

struct PowerJob
{
    BaseServiceJobId id{};
    std::uint64_t started{};
    std::uint64_t completion{};
    std::uint32_t bonus{};
};

PowerJob powerJob(const ProfileState &profile, BaseEmergencyPowerTarget target) noexcept
{
    if (target == BaseEmergencyPowerTarget::Workshop &&
        baseFacilityInstalled(profile, kWorkshopFacilityId))
    {
        const auto &order = profile.baseManufacturing.activeOrder;
        if (order && !order->outputReady && order->recipeDefinitionId != kArmorRepairResearchId)
            return {order->jobId, order->startedWorldMinute, order->completionWorldMinute,
                    order->emergencyPowerBonusMinutes};
    }
    if (target == BaseEmergencyPowerTarget::ResidentMedical &&
        baseFacilityInstalled(profile, kMedicalFacilityId))
    {
        const auto &treatment = profile.residentMedical.activeTreatment;
        if (treatment)
            return {treatment->jobId, treatment->startedWorldMinute, treatment->completionWorldMinute,
                    treatment->emergencyPowerBonusMinutes};
    }
    return {};
}

BaseEmergencyPowerPlan failure(DomainErrorCode error, const char *message)
{
    return {false, error, message};
}

void resolveBaseWork(ProfileState &profile, const ContentRegistry &content,
                     std::uint32_t powerThreat, BaseWorldWorkAdvanceResult &result)
{
    const BaseDailyDemandResult demand = synchronizeBaseDailySystemsThrough(profile, content).demand;
    if (demand.cyclesResolved != 0U)
    {
        result.demand.cyclesResolved += demand.cyclesResolved;
        result.demand.latestShortfall = demand.latestShortfall;
        result.demand.shortageCycleCount += demand.shortageCycleCount;
    }
    if (powerThreat != 0U)
        addBaseSiteThreat(profile, powerThreat);
    auto construction = applyBaseConstructionThrough(profile, content);
    if (construction.completed)
        result.construction = std::move(construction);
    auto manufacturing = applyBaseManufacturingThrough(profile, content);
    if (manufacturing.completed)
        result.manufacturing = std::move(manufacturing);
    auto treatment = applyResidentTreatmentThrough(profile);
    if (treatment.completed)
        result.residentTreatment = std::move(treatment);
}

std::uint64_t nextNormalCompletion(
    const ProfileState &profile, std::uint64_t cursor, std::uint64_t end) noexcept
{
    const auto limit = [&](std::uint64_t completion, std::uint32_t bonus)
    {
        const std::uint64_t effective = completion - std::min<std::uint64_t>(completion, bonus);
        if (effective > cursor)
            end = std::min(end, effective);
    };
    if (const auto &project = profile.baseConstruction.activeProject)
        limit(project->completionWorldMinute, 0U);
    if (const auto &order = profile.baseManufacturing.activeOrder;
        order && !order->outputReady && !profile.pendingRaid &&
        baseFacilityInstalled(profile, kWorkshopFacilityId))
        limit(order->completionWorldMinute, order->emergencyPowerBonusMinutes);
    if (const auto &treatment = profile.residentMedical.activeTreatment;
        treatment && baseFacilityInstalled(profile, kMedicalFacilityId))
        limit(treatment->completionWorldMinute, treatment->emergencyPowerBonusMinutes);
    return end;
}

std::uint32_t applyPowerInterval(ProfileState &profile, std::uint64_t end, std::uint32_t minutes,
                                 std::uint32_t bonusMinutes, bool jobComplete) noexcept
{
    BaseEmergencyPowerState &power = *profile.emergencyPower;
    auto &bonus = power.target == BaseEmergencyPowerTarget::Workshop
        ? profile.baseManufacturing.activeOrder->emergencyPowerBonusMinutes
        : profile.residentMedical.activeTreatment->emergencyPowerBonusMinutes;
    bonus += bonusMinutes;
    const std::uint32_t consumedBefore = kBaseEmergencyPowerBatchMinutes - power.remainingMinutes;
    power.remainingMinutes -= minutes;
    power.advancedThroughWorldMinute = end;
    const std::uint32_t threat =
        (consumedBefore + minutes) / kBaseEmergencyPowerThreatPeriodMinutes -
        consumedBefore / kBaseEmergencyPowerThreatPeriodMinutes;
    if (power.remainingMinutes == 0U)
        profile.emergencyPower.reset();
    else if (jobComplete)
        pauseBaseEmergencyPower(profile);
    return threat;
}
} // namespace

std::uint64_t remainingBaseEmergencyPowerWork(
    std::uint64_t completionWorldMinute, std::uint32_t bonusMinutes,
    std::uint64_t worldMinute) noexcept
{
    const std::uint64_t remaining = completionWorldMinute > worldMinute
        ? completionWorldMinute - worldMinute : 0U;
    return remaining - std::min<std::uint64_t>(remaining, bonusMinutes);
}

std::optional<BaseServiceJobId> baseEmergencyPowerJob(
    const ProfileState &profile, BaseEmergencyPowerTarget target) noexcept
{
    const PowerJob job = powerJob(profile, target);
    const std::uint64_t now = profile.worldClock.elapsedWorldMinutes;
    if (job.id == 0U || job.started > now ||
        remainingBaseEmergencyPowerWork(job.completion, job.bonus, now) == 0U)
        return std::nullopt;
    return job.id;
}

std::optional<AssetInstanceId> baseEmergencyPowerFuel(const ProfileState &profile) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (isStashedFuel(&asset))
            return id;
    }
    return std::nullopt;
}

BaseEmergencyPowerPlan queryBaseEmergencyPower(
    const ProfileState &profile, const BaseEmergencyPowerCommand &command)
{
    if (profile.pendingRaid || profile.activeBaseDefense)
        return failure(DomainErrorCode::IllegalDestination,
                       "generator cannot be operated during an active activity");
    if (!profile.homeFounding.established || !baseFacilityInstalled(profile, kBaseGeneratorFacilityId))
        return failure(DomainErrorCode::IllegalDestination, "generator is not installed");

    return std::visit([&](const auto &typed) -> BaseEmergencyPowerPlan
    {
        using Command = std::decay_t<decltype(typed)>;
        const auto &power = profile.emergencyPower;
        if constexpr (std::is_same_v<Command, StartBaseEmergencyPowerCommand>)
        {
            if (power)
                return failure(DomainErrorCode::IllegalDestination, "resume the remaining fuel batch");
            if (!isStashedFuel(profile.assets.find(typed.fuelAssetId)))
                return failure(DomainErrorCode::MissingAsset, "select a fuel can in the stash");
            if (baseEmergencyPowerJob(profile, typed.target) != typed.jobId)
                return failure(DomainErrorCode::IllegalDestination, "the selected facility has no eligible job");
        }
        else if constexpr (std::is_same_v<Command, PauseBaseEmergencyPowerCommand>)
        {
            if (!power || !power->activeJobId)
                return failure(DomainErrorCode::IllegalDestination, "generator is not running");
        }
        else
        {
            if (!power)
                return failure(DomainErrorCode::MissingAsset, "no fuel batch remains");
            if (power->activeJobId)
                return failure(DomainErrorCode::IllegalDestination, "generator is already running");
            if (baseEmergencyPowerJob(profile, power->target) != typed.jobId)
                return failure(DomainErrorCode::IllegalDestination, "the selected facility has no eligible job");
        }
        return {true, DomainErrorCode::None, {}};
    }, command);
}

BaseEmergencyPowerReceipt executeBaseEmergencyPower(
    ProfileState &profile, const ContentRegistry &content,
    const BaseEmergencyPowerCommand &command, const CommandContext &context)
{
    if (context.transactionId.empty())
        return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID is empty", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return {false, false, DomainErrorCode::StaleRevision, "profile revision is stale", profile.revision};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return {false, false, DomainErrorCode::RevisionOverflow, "profile revision overflow", profile.revision};
    const BaseEmergencyPowerPlan plan = queryBaseEmergencyPower(profile, command);
    if (!plan.canCommit)
        return {false, false, plan.error, plan.message, profile.revision};

    ProfileState candidate = profile;
    const char *message = std::visit([&](const auto &typed) -> const char *
    {
        using Command = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Command, StartBaseEmergencyPowerCommand>)
        {
            static_cast<void>(candidate.assets.erase(typed.fuelAssetId));
            candidate.emergencyPower = BaseEmergencyPowerState{
                typed.target, kBaseEmergencyPowerBatchMinutes, typed.jobId,
                candidate.worldClock.elapsedWorldMinutes};
            return "emergency power started";
        }
        else if constexpr (std::is_same_v<Command, PauseBaseEmergencyPowerCommand>)
        {
            pauseBaseEmergencyPower(candidate);
            return "emergency power paused";
        }
        else
        {
            candidate.emergencyPower->activeJobId = typed.jobId;
            candidate.emergencyPower->advancedThroughWorldMinute = candidate.worldClock.elapsedWorldMinutes;
            return "emergency power resumed";
        }
    }, command);
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return {false, false, DomainErrorCode::InvalidProfile, validation.message, profile.revision};
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, message, profile.revision};
}

void pauseBaseEmergencyPower(ProfileState &profile) noexcept
{
    if (profile.emergencyPower)
        profile.emergencyPower->activeJobId.reset();
}

void pauseBaseEmergencyPowerForFacility(
    ProfileState &profile, const BaseFacilityDefinitionId &facility) noexcept
{
    if (!profile.emergencyPower)
        return;
    const BaseFacilityDefinitionId &target = profile.emergencyPower->target == BaseEmergencyPowerTarget::Workshop
        ? kWorkshopFacilityId : kMedicalFacilityId;
    if (facility == kBaseGeneratorFacilityId || facility == target)
        pauseBaseEmergencyPower(profile);
}

BaseWorldWorkAdvanceResult synchronizeBaseWorkThrough(
    ProfileState &profile, const ContentRegistry &content, std::uint64_t minutesAdvanced)
{
    BaseWorldWorkAdvanceResult result;
    const std::uint64_t through = profile.worldClock.elapsedWorldMinutes;
    const auto &power = profile.emergencyPower;
    const auto &construction = profile.baseConstruction.activeProject;
    const auto &order = profile.baseManufacturing.activeOrder;
    const auto &treatment = profile.residentMedical.activeTreatment;
    const bool hasPowerTimeline = (power && power->activeJobId) ||
        (construction && construction->definitionId == kBaseGeneratorProjectId) ||
        (order && !order->outputReady && order->emergencyPowerBonusMinutes != 0U) ||
        (treatment && treatment->emergencyPowerBonusMinutes != 0U);
    if (!hasPowerTimeline)
    {
        resolveBaseWork(profile, content, 0U, result);
        return result;
    }

    std::uint64_t cursor = through - std::min(through, minutesAdvanced);
    do
    {
        std::uint64_t next = nextNormalCompletion(profile, cursor, through);
        std::uint32_t threat{};
        if (power && power->activeJobId && cursor < through)
        {
            const PowerJob job = powerJob(profile, power->target);
            const std::uint64_t remaining = remainingBaseEmergencyPowerWork(job.completion, job.bonus, cursor);
            if (power->advancedThroughWorldMinute > cursor)
            {
                next = std::min(next, power->advancedThroughWorldMinute);
            }
            else if (!baseFacilityInstalled(profile, kBaseGeneratorFacilityId) ||
                     job.id != *power->activeJobId || job.started > cursor || remaining == 0U)
            {
                pauseBaseEmergencyPower(profile);
            }
            else
            {
                const std::uint64_t minutesUntilComplete = remaining / 2U + remaining % 2U;
                const std::uint64_t minutesUntilMidnight =
                    kWorldMinutesPerDay - cursor % kWorldMinutesPerDay;
                const std::uint32_t consumed = kBaseEmergencyPowerBatchMinutes - power->remainingMinutes;
                const std::uint32_t minutesUntilThreat = kBaseEmergencyPowerThreatPeriodMinutes -
                    consumed % kBaseEmergencyPowerThreatPeriodMinutes;
                const std::uint64_t step = std::min({
                    next - cursor, minutesUntilComplete, minutesUntilMidnight,
                    static_cast<std::uint64_t>(power->remainingMinutes),
                    static_cast<std::uint64_t>(minutesUntilThreat)});
                next = cursor + step;
                threat = applyPowerInterval(
                    profile, next, static_cast<std::uint32_t>(step),
                    static_cast<std::uint32_t>(std::min(step, remaining - step)), step == minutesUntilComplete);
            }
        }
        cursor = next;
        profile.worldClock.elapsedWorldMinutes = cursor;
        resolveBaseWork(profile, content, threat, result);
    } while (cursor < through);
    return result;
}
