#include "base_resident_medical_domain.h"

#include <algorithm>
#include <limits>
#include <tuple>

#include "base_construction_domain.h"
#include "base_emergency_power_domain.h"
#include "base_workforce_domain.h"

namespace
{
const BaseFacilityDefinitionId kMedicalFacility{"base_facility.medical"};
constexpr std::uint32_t kTreatmentCharges{2U};

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
    const BasePopulationState &population,
    const ResidentMedicalReservePolicy &policy) noexcept
{
    for (const BaseResidentProfession profession : policy.professionPriority)
    {
        const auto index = baseProfessionIndex(profession);
        if (index < kBaseResidentProfessionCount && population.injuredByProfession[index] != 0U)
        {
            return profession;
        }
    }
    return BaseResidentProfession::General;
}

bool validMedicalReservePolicy(const ResidentMedicalReservePolicy &policy) noexcept
{
    std::array<bool, kBaseResidentProfessionCount> seen{};
    for (const auto profession : policy.professionPriority)
    {
        const auto index = baseProfessionIndex(profession);
        if (index >= seen.size() || seen[index])
            return false;
        seen[index] = true;
    }
    return true;
}

MedicalReservePlan reserveAccess(const ProfileState &profile, const ContentRegistry &content)
{
    if (!supportsHospitalMedicalReserveContent(content.contentVersion()) || !profile.homeFounding.established)
        return {false, DomainErrorCode::IllegalDestination, "The public medicine cabinet is unavailable"};
    if (profile.pendingRaid || profile.activeBaseDefense)
        return {false, DomainErrorCode::IllegalDestination, "The public medicine cabinet is unavailable during an activity"};
    if (!baseFacilityInstalled(profile, kMedicalFacility))
        return {false, DomainErrorCode::IllegalDestination, "the medical facility is stored in the facility reserve"};
    return {true, DomainErrorCode::None, {}};
}

std::optional<MedicalReserveReceipt> checkReserveContext(
    const ProfileState &profile, const CommandContext &context)
{
    if (context.transactionId.empty())
        return MedicalReserveReceipt{false, false, DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId))
        return MedicalReserveReceipt{true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return MedicalReserveReceipt{false, false, DomainErrorCode::StaleRevision,
            "profile revision is stale", profile.revision};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return MedicalReserveReceipt{false, false, DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance", profile.revision};
    return std::nullopt;
}

MedicalReserveReceipt commitReserveCommand(
    ProfileState &profile, ProfileState &candidate,
    const ContentRegistry &content, const CommandContext &context)
{
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return {false, false, DomainErrorCode::InvalidProfile, validation.message, profile.revision};
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

void planMedicalReserveCost(const ProfileState &profile, ResidentTreatmentPlan &plan)
{
    const auto assets = assetsInContainer(profile, ProfileContainerId::medicalReserve());
    const AssetRecord *firstAidStock = nullptr;
    std::vector<const AssetRecord *> chargeAssets;
    for (const AssetRecord *asset : assets)
    {
        if (asset->definitionId == kHospitalFirstAidStockId && asset->quantity != 0U)
        {
            if (!firstAidStock || asset->instanceId < firstAidStock->instanceId)
                firstAidStock = asset;
        }
        else if (isMedicalReserveItem(asset->definitionId) && asset->remainingCharges != 0U)
        {
            chargeAssets.push_back(asset);
        }
    }
    if (firstAidStock)
    {
        plan.supplies.push_back({firstAidStock->instanceId, firstAidStock->definitionId, 1U, 0U, 0U});
        plan.canCommit = true;
        return;
    }
    std::sort(chargeAssets.begin(), chargeAssets.end(), [](const AssetRecord *left, const AssetRecord *right) {
        return std::tie(left->remainingCharges, left->instanceId) <
            std::tie(right->remainingCharges, right->instanceId);
    });
    std::uint32_t remaining = kTreatmentCharges;
    for (const auto *asset : chargeAssets)
    {
        const auto used = std::min(remaining, asset->remainingCharges);
        plan.supplies.push_back({asset->instanceId, asset->definitionId, 0U, 0U, used});
        remaining -= used;
        if (remaining == 0U)
        {
            plan.canCommit = true;
            return;
        }
    }
    plan.supplies.clear();
    plan.error = DomainErrorCode::Capacity;
    plan.message = "The public medicine cabinet needs one first aid stock or two medicine uses";
}

void enforceTreatmentQuote(ResidentTreatmentPlan &plan, const StartResidentTreatmentCommand &command)
{
    if (plan.canCommit &&
        ((command.expectedPatient && command.expectedPatient != plan.patientProfession) ||
         (command.expectedSupplies && *command.expectedSupplies != plan.supplies)))
    {
        plan.canCommit = false;
        plan.error = DomainErrorCode::StaleRevision;
        plan.message = "resident treatment supply changed";
    }
}
}

MedicalReservePlan queryMoveMedicalReserveAsset(
    const ProfileState &profile,
    const ContentRegistry &content,
    const MoveMedicalReserveAssetCommand &command)
{
    const auto access = reserveAccess(profile, content);
    if (!access.canCommit)
        return access;
    const auto stash = ProfileContainerId::stash();
    const auto cabinet = ProfileContainerId::medicalReserve();
    const bool deposit = command.source.container == stash && command.destination.container == cabinet;
    const bool withdraw = command.source.container == cabinet && command.destination.container == stash;
    if (!deposit && !withdraw)
        return {false, DomainErrorCode::IllegalDestination, "Move medicine between the warehouse and public cabinet"};

    const AssetRecord *asset = profile.assets.find(command.assetId);
    if (!asset)
        return {false, DomainErrorCode::MissingAsset, "The selected medicine is no longer available"};
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    if (!stored || *stored != command.source)
        return {false, DomainErrorCode::StaleRevision, "The selected medicine has moved"};
    if (!isMedicalReserveItem(asset->definitionId) ||
        (asset->definitionId != kHospitalFirstAidStockId && asset->remainingCharges == 0U))
        return {false, DomainErrorCode::IllegalDestination, "The public medicine cabinet does not accept this item"};

    const auto &definition = content.item(asset->definitionId);
    if (!profilePlacementFits(profile, content, command.destination.container,
            command.destination.origin, definition, asset->orientation))
        return {false, DomainErrorCode::Capacity, deposit
            ? "No space at the selected public cabinet position" : "No space at the selected warehouse position"};
    return {true, DomainErrorCode::None, {}};
}

MedicalReserveReceipt executeMoveMedicalReserveAsset(
    ProfileState &profile,
    const ContentRegistry &content,
    const MoveMedicalReserveAssetCommand &command,
    const CommandContext &context)
{
    if (const auto checked = checkReserveContext(profile, context))
        return *checked;
    const auto plan = queryMoveMedicalReserveAsset(profile, content, command);
    if (!plan.canCommit)
        return {false, false, plan.error, plan.message, profile.revision};

    ProfileState candidate = profile;
    candidate.assets.findMutable(command.assetId)->location = command.destination;
    return commitReserveCommand(profile, candidate, content, context);
}

MedicalReserveReceipt executeSetResidentMedicalReservePolicy(
    ProfileState &profile,
    const ContentRegistry &content,
    const SetResidentMedicalReservePolicyCommand &command,
    const CommandContext &context)
{
    if (const auto checked = checkReserveContext(profile, context))
        return *checked;
    const auto access = reserveAccess(profile, content);
    if (!access.canCommit)
        return {false, false, access.error, access.message, profile.revision};
    if (!validMedicalReservePolicy(command.policy))
        return {false, false, DomainErrorCode::IllegalDestination,
            "The resident treatment priority is invalid", profile.revision};
    if (command.policy == profile.residentMedicalReservePolicy)
        return {true, true, DomainErrorCode::None, {}, profile.revision};

    ProfileState candidate = profile;
    candidate.residentMedicalReservePolicy = command.policy;
    return commitReserveCommand(profile, candidate, content, context);
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
    if (const auto &treatment = profile.residentMedical.activeTreatment)
    {
        std::uint64_t progressWorldMinute = profile.worldClock.elapsedWorldMinutes;
        if (treatment->usesMedicalReserve || treatment->emergencyPowerBonusMinutes != 0U)
        {
            progressWorldMinute = baseFacilityReserveStartedWorldMinute(
                profile, kMedicalFacility).value_or(progressWorldMinute);
        }
        remaining = remainingBaseEmergencyPowerWork(
            treatment->completionWorldMinute, treatment->emergencyPowerBonusMinutes, progressWorldMinute);
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
    const StartResidentTreatmentCommand &command)
{
    const ResidentMedicalDefinition &definition = content.residentMedical();
    const bool usesReserve = supportsHospitalMedicalReserveContent(content.contentVersion());
    if (profile.pendingRaid.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "resident treatment is unavailable during a Raid",
            definition);
    }
    if (usesReserve && (!profile.homeFounding.established || profile.activeBaseDefense))
    {
        return failure(profile, DomainErrorCode::IllegalDestination,
            "The public medicine cabinet is unavailable during an activity", definition);
    }
    if (profile.residentMedical.activeTreatment.has_value())
    {
        return failure(
            profile,
            DomainErrorCode::IllegalDestination,
            "resident treatment is already active",
            definition);
    }
    if (!baseFacilityInstalled(profile, kMedicalFacility))
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
    const auto patientProfession = nextInjuredProfession(profile.basePopulation,
        usesReserve ? profile.residentMedicalReservePolicy : ResidentMedicalReservePolicy{});
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

    if (usesReserve)
    {
        ResidentTreatmentPlan plan;
        plan.revision = profile.revision;
        plan.durationMinutes = adjustedDuration;
        plan.patientProfession = patientProfession;
        plan.workerProfession = workerProfession;
        plan.usesMedicalReserve = true;
        planMedicalReserveCost(profile, plan);
        enforceTreatmentQuote(plan, command);
        return plan;
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
    enforceTreatmentQuote(plan, command);
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
        if (asset == nullptr || asset->quantity < selection.quantity ||
            asset->remainingCharges < selection.charges)
        {
            return {false, false, DomainErrorCode::MissingAsset,
                    "resident treatment supply changed", profile.revision};
        }
        asset->quantity -= selection.quantity;
        asset->remainingCharges -= selection.charges;
        if (asset->quantity == 0U || (selection.charges != 0U && asset->remainingCharges == 0U))
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
        plan.workerProfession,
        0U,
        plan.usesMedicalReserve};
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

bool tryStartAutomaticResidentTreatment(ProfileState &candidate, const ContentRegistry &content)
{
    if (!candidate.residentMedicalReservePolicy.automaticAdmission ||
        candidate.residentMedical.activeTreatment || candidate.pendingRaid ||
        candidate.activeBaseDefense || candidate.baseSiege.warningActive ||
        candidate.homePerimeter.activeOuting ||
        candidate.basePopulation.injuredResidents == 0U || !candidate.baseWorkforce.medicalWorker ||
        !baseFacilityInstalled(candidate, kMedicalFacility) ||
        !supportsHospitalMedicalReserveContent(content.contentVersion()))
        return false;
    const auto receipt = executeStartResidentTreatment(candidate, content, {},
        {candidate.revision, "resident-medical:auto:" + std::to_string(candidate.nextBaseServiceJobId)});
    return receipt.succeeded && !receipt.alreadyCommitted;
}

ResidentTreatmentAdvanceResult applyResidentTreatmentThrough(
    ProfileState &profile) noexcept
{
    const auto &treatment = profile.residentMedical.activeTreatment;
    if (!treatment || ((treatment->usesMedicalReserve || treatment->emergencyPowerBonusMinutes != 0U) &&
                       !baseFacilityInstalled(profile, kMedicalFacility)) ||
        remainingBaseEmergencyPowerWork(treatment->completionWorldMinute,
                                       treatment->emergencyPowerBonusMinutes,
                                       profile.worldClock.elapsedWorldMinutes) != 0U)
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
    pauseBaseEmergencyPowerForFacility(profile, kMedicalFacility);
    return {true, jobId, profile.basePopulation.injuredResidents};
}
