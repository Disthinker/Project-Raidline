#include "base_resident_medical_domain.h"

#include <algorithm>
#include <limits>

#include "base_construction_domain.h"
#include "base_workforce_domain.h"

namespace
{
bool hasChildren(const ProfileState &profile, AssetInstanceId ownerId) noexcept
{
    return std::any_of(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [ownerId](const auto &entry)
        {
            const auto *stored = std::get_if<StoredAssetLocation>(
                &entry.second.location);
            return stored != nullptr &&
                stored->container.kind ==
                    ProfileContainerKind::AssetCompartment &&
                stored->container.ownerAssetId == ownerId;
        });
}

ResidentTreatmentPlan failure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message,
    const ResidentMedicalDefinition &definition)
{
    return {
        false,
        error,
        std::move(message),
        profile.revision,
        definition.requiredContribution,
        0U,
        definition.durationMinutes,
        BaseResidentProfession::General,
        BaseResidentProfession::General,
        {}};
}

BaseResidentProfession nextInjuredProfession(
    const BasePopulationState &population) noexcept
{
    for (BaseResidentProfession profession : {
             BaseResidentProfession::Medical,
             BaseResidentProfession::Engineering,
             BaseResidentProfession::Combat,
             BaseResidentProfession::General})
    {
        if (population.injuredByProfession[
                baseProfessionIndex(profession)] > 0U)
        {
            return profession;
        }
    }
    return BaseResidentProfession::General;
}
}

BaseResidentMedicalProjection projectBaseResidentMedical(
    const ProfileState &profile) noexcept
{
    const std::uint32_t healthy =
        profile.basePopulation.ordinaryResidents >
                profile.basePopulation.injuredResidents
            ? profile.basePopulation.ordinaryResidents -
                  profile.basePopulation.injuredResidents
            : 0U;
    std::uint64_t remaining{};
    if (profile.residentMedical.activeTreatment.has_value() &&
        profile.residentMedical.activeTreatment->completionWorldMinute >
            profile.worldClock.elapsedWorldMinutes)
    {
        remaining = profile.residentMedical.activeTreatment
            ->completionWorldMinute - profile.worldClock.elapsedWorldMinutes;
    }
    return {
        profile.basePopulation.ordinaryResidents,
        profile.basePopulation.injuredResidents,
        healthy,
        profile.residentMedical.activeTreatment.has_value(),
        remaining};
}

ResidentTreatmentPlan queryStartResidentTreatment(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartResidentTreatmentCommand &)
{
    const ResidentMedicalDefinition &definition = content.residentMedical();
    if (profile.pendingRaid.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "resident treatment is unavailable during a Raid",
            definition);
    }
    if (profile.residentMedical.activeTreatment.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "resident treatment is already active",
            definition);
    }
    if (!baseFacilityInstalled(
            profile,
            BaseFacilityDefinitionId{"base_facility.medical"}))
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "the medical facility is stored in the facility reserve",
            definition);
    }
    if (profile.basePopulation.injuredResidents == 0U)
    {
        return failure(
            profile,
            DomainErrorCode::InvalidQuantity,
            "no injured resident requires treatment",
            definition);
    }
    if (!profile.baseWorkforce.medicalWorker.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            "the medical facility has no assigned worker",
            definition);
    }
    const BaseResidentProfession workerProfession =
        *profile.baseWorkforce.medicalWorker;
    const std::size_t workerIndex = baseProfessionIndex(workerProfession);
    if (workerIndex >= kBaseResidentProfessionCount ||
        profile.basePopulation.professionResidents[workerIndex] <=
            profile.basePopulation.injuredByProfession[workerIndex])
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            "the assigned medical worker is not healthy",
            definition);
    }
    const BaseResidentProfession patientProfession =
        nextInjuredProfession(profile.basePopulation);
    const std::uint32_t adjustedDuration = applyBaseFacilityTaskDuration(
        definition.durationMinutes,
        BaseFacilityStaffingKind::Medical,
        workerProfession,
        profile.baseConstruction.medicalLevel,
        content.baseWorkforce());
    if (adjustedDuration == 0U)
    {
        return failure(
            profile,
            DomainErrorCode::Capacity,
            "the medical worker profession is not eligible",
            definition);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max() ||
        profile.nextBaseServiceJobId ==
            std::numeric_limits<BaseServiceJobId>::max() ||
        profile.worldClock.elapsedWorldMinutes >
            std::numeric_limits<std::uint64_t>::max() -
                adjustedDuration)
    {
        return failure(
            profile,
            DomainErrorCode::RevisionOverflow,
            "resident treatment state cannot advance",
            definition);
    }

    struct Candidate
    {
        AssetInstanceId assetId{};
        ItemDefinitionId definitionId;
        std::uint32_t availableQuantity{};
        std::uint32_t contributionPerUnit{};
    };
    std::vector<Candidate> candidates;
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        const auto assignment = profile.baseSupplyPolicy.assignments.find(
            asset.definitionId);
        if (assignment == profile.baseSupplyPolicy.assignments.end() ||
            assignment->second != BaseSupplyCategory::Medical ||
            !assetIsBaseAccessible(profile, assetId) ||
            hasChildren(profile, assetId))
        {
            continue;
        }
        const ItemDefinition &item = content.item(asset.definitionId);
        const std::uint32_t contribution = baseSupplyContribution(
            item, BaseSupplyCategory::Medical);
        if (contribution != 0U)
        {
            candidates.push_back(Candidate{
                assetId, asset.definitionId, asset.quantity, contribution});
        }
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate &left, const Candidate &right)
        {
            if (left.contributionPerUnit != right.contributionPerUnit)
            {
                return left.contributionPerUnit > right.contributionPerUnit;
            }
            return left.assetId < right.assetId;
        });

    ResidentTreatmentPlan plan{
        false,
        DomainErrorCode::Capacity,
        "insufficient authorized medical supplies",
        profile.revision,
        definition.requiredContribution,
        0U,
        adjustedDuration,
        patientProfession,
        workerProfession,
        {}};
    std::uint32_t remaining = definition.requiredContribution;
    for (const Candidate &candidate : candidates)
    {
        if (remaining == 0U)
        {
            break;
        }
        const std::uint32_t wanted =
            remaining / candidate.contributionPerUnit +
            (remaining % candidate.contributionPerUnit == 0U ? 0U : 1U);
        const std::uint32_t quantity = std::min(
            wanted, candidate.availableQuantity);
        if (quantity == 0U)
        {
            continue;
        }
        const std::uint32_t contribution =
            quantity * candidate.contributionPerUnit;
        plan.supplies.push_back(ResidentMedicalSupplySelection{
            candidate.assetId,
            candidate.definitionId,
            quantity,
            contribution});
        plan.plannedContribution += contribution;
        remaining = contribution >= remaining ? 0U : remaining - contribution;
    }
    if (remaining == 0U)
    {
        plan.canCommit = true;
        plan.error = DomainErrorCode::None;
        plan.message.clear();
    }
    return plan;
}

ResidentTreatmentReceipt executeStartResidentTreatment(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartResidentTreatmentCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID must not be empty", profile.revision};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision};
    }
    const ResidentTreatmentPlan plan = queryStartResidentTreatment(
        profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision};
    }

    ProfileState candidate = profile;
    for (const ResidentMedicalSupplySelection &selection : plan.supplies)
    {
        AssetRecord *asset = candidate.assets.findMutable(selection.assetId);
        if (asset == nullptr || asset->quantity < selection.quantity)
        {
            return {false, false, DomainErrorCode::MissingAsset,
                    "resident treatment supply changed", profile.revision};
        }
        asset->quantity -= selection.quantity;
        if (asset->quantity == 0U)
        {
            static_cast<void>(candidate.assets.erase(selection.assetId));
        }
    }
    const BaseServiceJobId jobId = candidate.nextBaseServiceJobId++;
    candidate.residentMedical.activeTreatment = ActiveResidentTreatment{
        jobId,
        candidate.worldClock.elapsedWorldMinutes,
        candidate.worldClock.elapsedWorldMinutes + plan.durationMinutes,
        plan.plannedContribution,
        plan.patientProfession,
        plan.workerProfession};
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                validation.message, profile.revision};
    }
    profile = std::move(candidate);
    return {
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        jobId,
        plan.plannedContribution,
        profile.residentMedical.activeTreatment->completionWorldMinute};
}

ResidentTreatmentAdvanceResult applyResidentTreatmentThrough(
    ProfileState &profile) noexcept
{
    if (!profile.residentMedical.activeTreatment.has_value() ||
        profile.residentMedical.activeTreatment->completionWorldMinute >
            profile.worldClock.elapsedWorldMinutes)
    {
        return {};
    }
    const BaseServiceJobId jobId =
        profile.residentMedical.activeTreatment->jobId;
    if (profile.basePopulation.injuredResidents > 0U)
    {
        --profile.basePopulation.injuredResidents;
    }
    const std::size_t professionIndex = baseProfessionIndex(
        profile.residentMedical.activeTreatment->patientProfession);
    if (professionIndex < kBaseResidentProfessionCount &&
        profile.basePopulation.injuredByProfession[professionIndex] > 0U)
    {
        --profile.basePopulation.injuredByProfession[professionIndex];
    }
    profile.residentMedical.activeTreatment.reset();
    return {true, jobId, profile.basePopulation.injuredResidents};
}
