#include "base_engineering_training_domain.h"

#include "base_construction_domain.h"
#include "base_workforce_domain.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace
{
const BaseFacilityDefinitionId kWorkshop{"base_facility.workshop"};
constexpr std::size_t kGeneral = baseProfessionIndex(BaseResidentProfession::General);
constexpr std::size_t kEngineering = baseProfessionIndex(BaseResidentProfession::Engineering);
constexpr std::string_view kNoMentor = "engineering training requires an assigned healthy engineer";
constexpr std::string_view kStoredWorkshop = "the workshop is stored in the facility reserve";

bool hasMentor(const ProfileState &profile) noexcept
{
    return profile.baseWorkforce.workshopWorker == BaseResidentProfession::Engineering &&
        profile.basePopulation.professionResidents[kEngineering] >
            profile.basePopulation.injuredByProfession[kEngineering];
}

template <typename Plan>
Plan failure(const ProfileState &profile, DomainErrorCode error, std::string_view message)
{
    return {false, error, std::string{message}, profile.revision};
}

BaseManufacturingReceipt receiptFailure(
    const ProfileState &profile, DomainErrorCode error, std::string_view message)
{
    return {false, false, error, std::string{message}, profile.revision};
}

std::optional<BaseManufacturingReceipt> checkTransaction(
    const ProfileState &profile, const CommandContext &context)
{
    if (context.transactionId.empty())
        return receiptFailure(profile, DomainErrorCode::InvalidTransaction, "transaction ID is empty");
    if (profile.committedTransactions.contains(context.transactionId))
        return BaseManufacturingReceipt{true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return receiptFailure(profile, DomainErrorCode::StaleRevision, "profile revision is stale");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return receiptFailure(profile, DomainErrorCode::RevisionOverflow, "profile revision cannot advance");
    return std::nullopt;
}

BaseManufacturingReceipt commitTrainingOrder(
    ProfileState &profile, ProfileState candidate, const ContentRegistry &content,
    const CommandContext &context, BaseServiceJobId jobId)
{
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return receiptFailure(profile, DomainErrorCode::InvalidProfile, validation.message);
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision, jobId};
}

bool trainingInputsMatch(const ProfileState &profile, const BaseEngineeringTrainingOrder &order)
{
    std::array<std::uint32_t, kBaseEngineeringTrainingInputs.size()> totals{};
    std::set<AssetInstanceId> seen;
    for (const AssetInstanceId assetId : order.inputAssetIds)
    {
        const AssetRecord *asset = profile.assets.find(assetId);
        if (asset == nullptr || asset->quantity == 0U || !seen.insert(assetId).second ||
            asset->location != AssetLocation{BaseServiceAssetLocation{order.jobId}})
            return false;

        const auto found = std::find_if(
            kBaseEngineeringTrainingInputs.begin(), kBaseEngineeringTrainingInputs.end(),
            [&](const BaseManufacturingInputDefinition &input)
            {
                return input.itemDefinitionId == asset->definitionId;
            });
        if (found == kBaseEngineeringTrainingInputs.end())
            return false;
        const auto index = static_cast<std::size_t>(found - kBaseEngineeringTrainingInputs.begin());
        if (asset->quantity > found->quantity - totals[index])
            return false;
        totals[index] += asset->quantity;
    }
    for (std::size_t index{}; index < totals.size(); ++index)
    {
        if (totals[index] != kBaseEngineeringTrainingInputs[index].quantity)
            return false;
    }
    return true;
}
}

BaseEngineeringTrainingProjection projectBaseEngineeringTraining(const ProfileState &profile) noexcept
{
    if (!profile.activeEngineeringTraining)
        return {};
    const auto &order = *profile.activeEngineeringTraining;
    std::string_view reason;
    if (!baseFacilityInstalled(profile, kWorkshop))
        reason = kStoredWorkshop;
    else if (!hasMentor(profile))
        reason = kNoMentor;
    return {true, !reason.empty(), order.jobId, order.remainingMinutes, reason};
}

BaseEngineeringTrainingStartPlan queryStartBaseEngineeringTraining(
    const ProfileState &profile, const ContentRegistry &content)
{
    using Plan = BaseEngineeringTrainingStartPlan;
    if (profile.pendingRaid || profile.activeBaseDefense || profile.baseSiege.warningActive)
        return failure<Plan>(profile, DomainErrorCode::IllegalDestination,
            "engineering training can only start in a safe Base");
    if (profile.activeEngineeringTraining || profile.baseManufacturing.activeOrder)
        return failure<Plan>(profile, DomainErrorCode::IllegalDestination,
            "the workshop production slot is occupied");
    if (!baseFacilityInstalled(profile, kWorkshop))
        return failure<Plan>(profile, DomainErrorCode::IllegalDestination, kStoredWorkshop);
    if (!hasMentor(profile))
        return failure<Plan>(profile, DomainErrorCode::Capacity, kNoMentor);

    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    if (workforce.availableResidents == 0U || workforce.availableByProfession[kGeneral] == 0U)
        return failure<Plan>(profile, DomainErrorCode::Capacity,
            "engineering training requires an unassigned healthy General resident");

    if (profile.nextBaseServiceJobId == std::numeric_limits<BaseServiceJobId>::max() ||
        profile.worldClock.elapsedWorldMinutes >
            std::numeric_limits<std::uint64_t>::max() - kBaseEngineeringTrainingMinutes)
        return failure<Plan>(profile, DomainErrorCode::RevisionOverflow,
            "engineering training identity or completion time would overflow");

    Plan plan{true, DomainErrorCode::None, {}, profile.revision};
    AssetInstanceId splits{};
    for (const BaseManufacturingInputDefinition &input : kBaseEngineeringTrainingInputs)
    {
        try
        {
            static_cast<void>(content.item(input.itemDefinitionId));
        }
        catch (...)
        {
            return failure<Plan>(profile, DomainErrorCode::MissingAsset,
                "engineering training materials are unavailable");
        }
        auto remaining = input.quantity;
        for (const auto &[assetId, asset] : profile.assets.records())
        {
            const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
            if (asset.definitionId != input.itemDefinitionId || asset.quantity == 0U ||
                stored == nullptr || stored->container.kind != ProfileContainerKind::Stash)
                continue;

            const auto units = std::min(remaining, asset.quantity);
            plan.inputs.push_back({assetId, asset.definitionId, units});
            if (units < asset.quantity)
                ++splits;
            remaining -= units;
            if (remaining == 0U)
                break;
        }
        if (remaining != 0U)
            return failure<Plan>(profile, DomainErrorCode::MissingAsset,
                "place all engineering training materials in the Stash");
    }
    if (splits > std::numeric_limits<AssetInstanceId>::max() - profile.assets.nextAssetId())
        return failure<Plan>(profile, DomainErrorCode::RevisionOverflow,
            "engineering training input identity cannot be reserved");
    return plan;
}

BaseManufacturingReturnPlan queryCancelBaseEngineeringTraining(
    const ProfileState &profile, const ContentRegistry &content)
{
    using Plan = BaseManufacturingReturnPlan;
    if (profile.pendingRaid || profile.activeBaseDefense || profile.baseSiege.warningActive)
        return failure<Plan>(profile, DomainErrorCode::IllegalDestination,
            "engineering training can only be cancelled in a safe Base");
    if (!profile.activeEngineeringTraining)
        return failure<Plan>(profile, DomainErrorCode::IllegalDestination,
            "there is no engineering training order to cancel");
    const auto &order = *profile.activeEngineeringTraining;
    if (!trainingInputsMatch(profile, order))
        return failure<Plan>(profile, DomainErrorCode::InvalidProfile,
            "reserved engineering training inputs are invalid");

    ProfileState candidate = profile;
    Plan plan{true, DomainErrorCode::None, {}, profile.revision};
    if (!prepareBaseWorkshopReturns(candidate, content, order.inputAssetIds, plan.returns))
        return failure<Plan>(profile, DomainErrorCode::Capacity,
            "Stash cannot receive all reserved engineering training inputs");
    return plan;
}

BaseManufacturingReceipt executeStartBaseEngineeringTraining(
    ProfileState &profile, const ContentRegistry &content, const CommandContext &context)
{
    if (const auto transaction = checkTransaction(profile, context))
        return *transaction;
    const auto plan = queryStartBaseEngineeringTraining(profile, content);
    if (!plan.canCommit)
        return receiptFailure(profile, plan.error, plan.message);

    ProfileState candidate = profile;
    const auto jobId = candidate.nextBaseServiceJobId++;
    std::vector<AssetInstanceId> inputIds;
    const auto reservation = reserveBaseWorkshopInputs(candidate, content, plan.inputs, jobId, inputIds);
    if (reservation != DomainErrorCode::None)
        return receiptFailure(profile, reservation, "engineering training inputs cannot be reserved");

    candidate.activeEngineeringTraining = BaseEngineeringTrainingOrder{
        jobId, kBaseEngineeringTrainingMinutes, candidate.worldClock.elapsedWorldMinutes,
        std::move(inputIds)};
    return commitTrainingOrder(profile, std::move(candidate), content, context, jobId);
}

BaseManufacturingReceipt executeCancelBaseEngineeringTraining(
    ProfileState &profile, const ContentRegistry &content, const CommandContext &context)
{
    if (const auto transaction = checkTransaction(profile, context))
        return *transaction;
    const auto plan = queryCancelBaseEngineeringTraining(profile, content);
    if (!plan.canCommit)
        return receiptFailure(profile, plan.error, plan.message);

    ProfileState candidate = profile;
    const auto jobId = candidate.activeEngineeringTraining->jobId;
    for (const BaseManufacturingReturnSelection &selection : plan.returns)
        candidate.assets.findMutable(selection.assetId)->location = selection.destination;
    candidate.activeEngineeringTraining.reset();
    return commitTrainingOrder(profile, std::move(candidate), content, context, jobId);
}

ProfileValidationResult validateBaseEngineeringTrainingOrder(const ProfileState &profile)
{
    if (!profile.activeEngineeringTraining)
        return {true, {}};

    const auto &order = *profile.activeEngineeringTraining;
    if (order.jobId == 0U || order.jobId >= profile.nextBaseServiceJobId ||
        order.remainingMinutes == 0U || order.remainingMinutes > kBaseEngineeringTrainingMinutes ||
        order.updatedThroughWorldMinute > profile.worldClock.elapsedWorldMinutes ||
        order.inputAssetIds.size() > 3U ||
        !baseFacilityOwned(profile, kWorkshop) || profile.baseManufacturing.activeOrder ||
        profile.baseWorkforce.workshopWorker == BaseResidentProfession::General ||
        !trainingInputsMatch(profile, order))
        return {false, "active engineering training order is invalid"};
    return {true, {}};
}

bool applyBaseEngineeringTrainingThrough(ProfileState &profile, const ContentRegistry &)
{
    if (!profile.activeEngineeringTraining || profile.pendingRaid)
        return false;

    auto &order = *profile.activeEngineeringTraining;
    const auto through = profile.worldClock.elapsedWorldMinutes;
    if (through <= order.updatedThroughWorldMinute)
        return false;
    if (!baseFacilityInstalled(profile, kWorkshop) || !hasMentor(profile))
    {
        pauseBaseEngineeringTrainingThrough(profile);
        return false;
    }
    const auto elapsed = through - order.updatedThroughWorldMinute;
    if (elapsed < order.remainingMinutes)
    {
        order.remainingMinutes -= static_cast<std::uint32_t>(elapsed);
        order.updatedThroughWorldMinute = through;
        return false;
    }

    auto &population = profile.basePopulation;
    if (population.professionResidents[kGeneral] <= population.injuredByProfession[kGeneral] ||
        !validateBaseEngineeringTrainingOrder(profile).valid)
        return false;

    for (const AssetInstanceId assetId : order.inputAssetIds)
        static_cast<void>(profile.assets.erase(assetId));
    --population.professionResidents[kGeneral];
    ++population.professionResidents[kEngineering];
    profile.activeEngineeringTraining.reset();
    return true;
}

void pauseBaseEngineeringTrainingThrough(ProfileState &profile) noexcept
{
    if (profile.activeEngineeringTraining)
        profile.activeEngineeringTraining->updatedThroughWorldMinute =
            profile.worldClock.elapsedWorldMinutes;
}
