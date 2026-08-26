#include "base_workforce_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
BaseWorkforcePlan failure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message)
{
    return {false, error, std::move(message), profile.revision,
            profile.baseWorkforce};
}

std::optional<BaseResidentProfession> &slot(
    BaseWorkforceState &state,
    BaseFacilityStaffingKind facility) noexcept
{
    return facility == BaseFacilityStaffingKind::Workshop
        ? state.workshopWorker
        : state.medicalWorker;
}

const std::optional<BaseResidentProfession> &slot(
    const BaseWorkforceState &state,
    BaseFacilityStaffingKind facility) noexcept
{
    return facility == BaseFacilityStaffingKind::Workshop
        ? state.workshopWorker
        : state.medicalWorker;
}

BaseResidentProfession preferredProfession(
    BaseFacilityStaffingKind facility) noexcept
{
    return facility == BaseFacilityStaffingKind::Workshop
        ? BaseResidentProfession::Engineering
        : BaseResidentProfession::Medical;
}

BaseProfessionCounts assignedCounts(
    const BaseWorkforceState &state) noexcept
{
    BaseProfessionCounts counts{};
    for (const auto &worker : {state.workshopWorker, state.medicalWorker})
    {
        if (worker.has_value() &&
            baseProfessionIndex(*worker) < counts.size())
        {
            ++counts[baseProfessionIndex(*worker)];
        }
    }
    return counts;
}

BaseProfessionCounts availableCounts(
    const ProfileState &profile,
    const BaseWorkforceState &state) noexcept
{
    const BaseProfessionCounts assigned = assignedCounts(state);
    BaseProfessionCounts available{};
    for (std::size_t index = 0; index < available.size(); ++index)
    {
        const std::uint32_t residents =
            profile.basePopulation.professionResidents[index];
        const std::uint32_t injured =
            profile.basePopulation.injuredByProfession[index];
        const std::uint32_t healthy = residents > injured
            ? residents - injured
            : 0U;
        available[index] = healthy > assigned[index]
            ? healthy - assigned[index]
            : 0U;
    }
    return available;
}

bool tryFill(
    const ProfileState &profile,
    BaseWorkforceState &state,
    BaseFacilityStaffingKind facility) noexcept
{
    if (slot(state, facility).has_value())
    {
        return true;
    }
    BaseProfessionCounts available = availableCounts(profile, state);
    const BaseResidentProfession preferred = preferredProfession(facility);
    if (available[baseProfessionIndex(preferred)] > 0U)
    {
        slot(state, facility) = preferred;
        return true;
    }
    if (available[baseProfessionIndex(BaseResidentProfession::General)] > 0U)
    {
        slot(state, facility) = BaseResidentProfession::General;
        return true;
    }
    return false;
}

bool activeFacilityJob(
    const ProfileState &profile,
    BaseFacilityStaffingKind facility) noexcept
{
    if (facility == BaseFacilityStaffingKind::Workshop)
    {
        return profile.baseManufacturing.activeOrder.has_value() &&
            !profile.baseManufacturing.activeOrder->outputReady;
    }
    return profile.residentMedical.activeTreatment.has_value();
}

BaseWorkforceReceipt receiptFailure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message)
{
    return {false, false, error, std::move(message), profile.revision,
            profile.baseWorkforce};
}

template <typename Query>
BaseWorkforceReceipt executePlan(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context,
    Query &&query)
{
    if (context.transactionId.empty())
    {
        return receiptFailure(
            profile,
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty");
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision,
                profile.baseWorkforce};
    }
    if (context.expectedRevision != profile.revision)
    {
        return receiptFailure(
            profile,
            DomainErrorCode::StaleRevision,
            "profile revision is stale");
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return receiptFailure(
            profile,
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance");
    }
    const BaseWorkforcePlan plan = query(profile);
    if (!plan.canCommit)
    {
        return receiptFailure(profile, plan.error, plan.message);
    }
    ProfileState candidate = profile;
    candidate.baseWorkforce = plan.resultingState;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            profile,
            DomainErrorCode::InvalidProfile,
            validation.message);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            profile.baseWorkforce};
}
}

const char *baseResidentProfessionName(
    BaseResidentProfession profession) noexcept
{
    switch (profession)
    {
    case BaseResidentProfession::General:
        return "General";
    case BaseResidentProfession::Medical:
        return "Medical";
    case BaseResidentProfession::Engineering:
        return "Engineering";
    case BaseResidentProfession::Combat:
        return "Combat";
    }
    return "Unknown";
}

std::uint32_t healthyBaseResidents(
    const BasePopulationState &population) noexcept
{
    return population.ordinaryResidents > population.injuredResidents
        ? population.ordinaryResidents - population.injuredResidents
        : 0U;
}

std::uint32_t availableBaseWorkers(const ProfileState &profile) noexcept
{
    const BaseWorkforceProjection projection = projectBaseWorkforce(profile);
    return projection.availableResidents;
}

bool baseFacilityAcceptsProfession(
    BaseFacilityStaffingKind facility,
    BaseResidentProfession profession) noexcept
{
    return profession == BaseResidentProfession::General ||
        (facility == BaseFacilityStaffingKind::Workshop &&
         profession == BaseResidentProfession::Engineering) ||
        (facility == BaseFacilityStaffingKind::Medical &&
         profession == BaseResidentProfession::Medical);
}

std::uint32_t applyBaseFacilityTaskDuration(
    std::uint32_t baseDurationMinutes,
    BaseFacilityStaffingKind facility,
    BaseResidentProfession profession,
    std::uint32_t facilityLevel,
    const BaseWorkforceDefinition &definition) noexcept
{
    if (!baseFacilityAcceptsProfession(facility, profession))
    {
        return 0U;
    }
    std::uint64_t duration = baseDurationMinutes;
    if (profession == BaseResidentProfession::General)
    {
        duration = (duration * definition.generalFallbackDurationPercent +
                    99U) /
            100U;
    }
    const std::uint32_t facilityPercent =
        facility == BaseFacilityStaffingKind::Workshop
        ? definition.workshopLevel2DurationPercent
        : definition.medicalLevel2DurationPercent;
    if (facilityLevel >= 2U)
    {
        duration = (duration * facilityPercent + 99U) / 100U;
    }
    return duration > std::numeric_limits<std::uint32_t>::max()
        ? 0U
        : static_cast<std::uint32_t>(duration);
}

BaseWorkforceProjection projectBaseWorkforce(
    const ProfileState &profile) noexcept
{
    BaseWorkforceProjection projection;
    projection.residentsByProfession =
        profile.basePopulation.professionResidents;
    projection.injuredByProfession =
        profile.basePopulation.injuredByProfession;
    projection.availableByProfession = availableCounts(
        profile, profile.baseWorkforce);
    projection.workshopWorker = profile.baseWorkforce.workshopWorker;
    projection.medicalWorker = profile.baseWorkforce.medicalWorker;
    projection.healthyResidents = healthyBaseResidents(
        profile.basePopulation);
    const BaseProfessionCounts assigned = assignedCounts(profile.baseWorkforce);
    for (std::size_t index = 0; index < kBaseResidentProfessionCount; ++index)
    {
        projection.availableResidents +=
            projection.availableByProfession[index];
        projection.assignedResidents += assigned[index];
    }
    const std::uint32_t constructionWorkers =
        profile.baseConstruction.activeProject.has_value()
        ? profile.baseConstruction.activeProject->committedWorkers
        : 0U;
    projection.availableResidents =
        projection.availableResidents > constructionWorkers
        ? projection.availableResidents - constructionWorkers
        : 0U;
    return projection;
}

BaseWorkforcePlan queryAssignBestBaseWorker(
    const ProfileState &profile,
    const BaseFacilityStaffingCommand &command) noexcept
{
    if (profile.pendingRaid.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "Base staffing is unavailable during a Raid");
    }
    if (slot(profile.baseWorkforce, command.facility).has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "the Base facility staffing slot is already filled");
    }
    BaseWorkforceState resulting = profile.baseWorkforce;
    if (!tryFill(profile, resulting, command.facility))
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            "no qualified healthy Base worker is available");
    }
    const std::uint32_t constructionWorkers =
        profile.baseConstruction.activeProject.has_value()
        ? profile.baseConstruction.activeProject->committedWorkers
        : 0U;
    const BaseProfessionCounts available = availableCounts(profile, resulting);
    std::uint32_t remaining{};
    for (std::uint32_t count : available)
    {
        remaining += count;
    }
    if (remaining < constructionWorkers)
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            "construction has reserved the remaining healthy workers");
    }
    return {true, DomainErrorCode::None, {}, profile.revision, resulting};
}

BaseWorkforceReceipt executeAssignBestBaseWorker(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseFacilityStaffingCommand &command,
    const CommandContext &context)
{
    return executePlan(
        profile,
        content,
        context,
        [&](const ProfileState &state)
        {
            return queryAssignBestBaseWorker(state, command);
        });
}

BaseWorkforcePlan queryClearBaseWorker(
    const ProfileState &profile,
    const BaseFacilityStaffingCommand &command) noexcept
{
    if (profile.pendingRaid.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "Base staffing is unavailable during a Raid");
    }
    if (!slot(profile.baseWorkforce, command.facility).has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "the Base facility staffing slot is already empty");
    }
    if (activeFacilityJob(profile, command.facility))
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "an active facility job still requires this worker");
    }
    BaseWorkforceState resulting = profile.baseWorkforce;
    slot(resulting, command.facility).reset();
    return {true, DomainErrorCode::None, {}, profile.revision, resulting};
}

BaseWorkforceReceipt executeClearBaseWorker(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseFacilityStaffingCommand &command,
    const CommandContext &context)
{
    return executePlan(
        profile,
        content,
        context,
        [&](const ProfileState &state)
        {
            return queryClearBaseWorker(state, command);
        });
}

BaseWorkforcePlan queryAutoFillBaseWorkers(
    const ProfileState &profile) noexcept
{
    if (profile.pendingRaid.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "Base staffing is unavailable during a Raid");
    }
    BaseWorkforceState resulting = profile.baseWorkforce;
    const bool workshopWasEmpty = !resulting.workshopWorker.has_value();
    const bool medicalWasEmpty = !resulting.medicalWorker.has_value();
    if (workshopWasEmpty)
    {
        static_cast<void>(tryFill(
            profile, resulting, BaseFacilityStaffingKind::Workshop));
    }
    if (medicalWasEmpty)
    {
        static_cast<void>(tryFill(
            profile, resulting, BaseFacilityStaffingKind::Medical));
    }
    const std::uint32_t constructionWorkers =
        profile.baseConstruction.activeProject.has_value()
        ? profile.baseConstruction.activeProject->committedWorkers
        : 0U;
    BaseProfessionCounts available = availableCounts(profile, resulting);
    std::uint32_t remaining{};
    for (std::uint32_t count : available)
    {
        remaining += count;
    }
    if (remaining < constructionWorkers)
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            "construction has reserved the remaining healthy workers");
    }
    if (resulting == profile.baseWorkforce)
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            workshopWasEmpty || medicalWasEmpty
                ? "no qualified healthy Base worker is available"
                : "all Base facility staffing slots are already filled");
    }
    return {true, DomainErrorCode::None, {}, profile.revision, resulting};
}

BaseWorkforceReceipt executeAutoFillBaseWorkers(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
{
    return executePlan(
        profile,
        content,
        context,
        [](const ProfileState &state)
        {
            return queryAutoFillBaseWorkers(state);
        });
}
