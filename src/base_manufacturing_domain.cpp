#include "base_manufacturing_domain.h"

#include "base_construction_domain.h"
#include "base_morale_domain.h"
#include "base_site_feature_domain.h"
#include "base_workforce_domain.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <span>
#include <utility>

namespace
{
BaseManufacturingStartPlan startFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, error, std::move(message), revision};
}

BaseManufacturingReturnPlan returnFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, error, std::move(message), revision};
}

BaseManufacturingReceipt receiptFailure(
    DomainErrorCode error,
    std::string message,
    const ProfileState &profile)
{
    return {false, false, error, std::move(message), profile.revision};
}

bool hasChildren(
    const ProfileState &profile,
    AssetInstanceId parent) noexcept
{
    return std::any_of(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [parent](const auto &entry)
        {
            const auto *stored =
                std::get_if<StoredAssetLocation>(&entry.second.location);
            return stored != nullptr &&
                stored->container.kind ==
                    ProfileContainerKind::AssetCompartment &&
                stored->container.ownerAssetId == parent;
        });
}

bool prepareReturns(
    ProfileState &candidate,
    const ContentRegistry &content,
    const std::vector<AssetInstanceId> &assetIds,
    std::vector<BaseManufacturingReturnSelection> &returns)
{
    for (AssetInstanceId assetId : assetIds)
    {
        AssetRecord *asset = candidate.assets.findMutable(assetId);
        if (asset == nullptr)
        {
            return false;
        }
        const ItemDefinition &definition = content.item(asset->definitionId);
        const auto origin = findFirstProfileFit(
            candidate,
            content,
            ProfileContainerId::stash(),
            definition,
            asset->orientation,
            assetId);
        if (!origin.has_value())
        {
            return false;
        }
        StoredAssetLocation destination{
            ProfileContainerId::stash(), *origin};
        asset->location = destination;
        returns.push_back({assetId, destination});
    }
    return true;
}

bool validContext(
    const ProfileState &profile,
    const CommandContext &context,
    BaseManufacturingReceipt &failure)
{
    if (context.transactionId.empty())
    {
        failure = receiptFailure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID is empty",
            profile);
        return false;
    }
    if (context.expectedRevision != profile.revision)
    {
        failure = receiptFailure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile);
        return false;
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        failure = receiptFailure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile);
        return false;
    }
    return true;
}
}

BaseManufacturingProjection projectBaseManufacturing(
    const ProfileState &profile) noexcept
{
    BaseManufacturingProjection projection;
    if (!profile.baseManufacturing.activeOrder.has_value())
    {
        return projection;
    }
    const BaseManufacturingOrder &order =
        *profile.baseManufacturing.activeOrder;
    projection.orderPresent = true;
    projection.outputReady = order.outputReady;
    projection.recipeDefinitionId = order.recipeDefinitionId;
    projection.jobId = order.jobId;
    projection.committedWorkers = order.outputReady
        ? 0U
        : order.committedWorkers;
    std::uint64_t progressWorldMinute = profile.worldClock.elapsedWorldMinutes;
    if (const std::optional<std::uint64_t> reserveStarted =
            baseFacilityReserveStartedWorldMinute(
                profile,
                BaseFacilityDefinitionId{"base_facility.workshop"});
        reserveStarted.has_value())
    {
        progressWorldMinute = *reserveStarted;
    }
    if (!order.outputReady &&
        order.completionWorldMinute > progressWorldMinute)
    {
        projection.remainingMinutes = order.completionWorldMinute -
            progressWorldMinute;
    }
    if (order.outputReady)
    {
        projection.outputAssetId = order.outputAssetId;
    }
    return projection;
}

BaseManufacturingStartPlan queryStartBaseManufacturing(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartBaseManufacturingCommand &command)
{
    const bool research = command.recipeDefinitionId == kArmorRepairResearchId;
    const bool armorRepair = command.recipeDefinitionId == kArmorRepairRecipeId;
    if ((research || armorRepair) &&
        !supportsArmorReadinessContent(content.contentVersion()))
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "manufacturing recipe does not exist",
            profile.revision);
    }
    const BaseManufacturingRecipeDefinition *recipe{};
    if (!research)
    {
        try
        {
            recipe = &content.baseManufacturingRecipe(command.recipeDefinitionId);
        }
        catch (...)
        {
            return startFailure(
                DomainErrorCode::IllegalDestination,
                "manufacturing recipe does not exist",
                profile.revision);
        }
    }
    if (research && profile.baseManufacturing.armorRepairRecipeUnlocked)
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "the armor repair recipe is already unlocked",
            profile.revision);
    }
    if (armorRepair && !profile.baseManufacturing.armorRepairRecipeUnlocked)
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "research the armor repair blueprint first",
            profile.revision);
    }
    if (profile.pendingRaid.has_value())
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "manufacturing can only start in Base",
            profile.revision);
    }
    if (profile.baseManufacturing.activeOrder.has_value())
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "the workshop production slot is occupied",
            profile.revision);
    }
    if (!baseFacilityInstalled(
            profile,
            BaseFacilityDefinitionId{"base_facility.workshop"}))
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "the workshop is stored in the facility reserve",
            profile.revision);
    }
    if ((research || armorRepair) && profile.baseConstruction.workshopLevel < 2U)
    {
        return startFailure(
            DomainErrorCode::IllegalDestination,
            "armor repair requires a level 2 workshop",
            profile.revision);
    }
    if ((research || armorRepair) &&
        profile.baseWorkforce.workshopWorker != BaseResidentProfession::Engineering)
    {
        return startFailure(
            DomainErrorCode::Capacity,
            "armor repair requires an assigned engineer",
            profile.revision);
    }
    const std::uint32_t workerCount = research ? 1U : recipe->workerCount;
    if (!profile.baseWorkforce.workshopWorker.has_value() || workerCount != 1U)
    {
        return startFailure(
            DomainErrorCode::Capacity,
            "the workshop has no assigned worker",
            profile.revision);
    }
    const BaseResidentProfession workerProfession =
        *profile.baseWorkforce.workshopWorker;
    const std::size_t workerIndex = baseProfessionIndex(workerProfession);
    if (workerIndex >= kBaseResidentProfessionCount ||
        profile.basePopulation.professionResidents[workerIndex] <=
            profile.basePopulation.injuredByProfession[workerIndex])
    {
        return startFailure(
            DomainErrorCode::Capacity,
            "the assigned workshop worker is not healthy",
            profile.revision);
    }
    const std::uint32_t moraleDuration = applyBaseMoraleDurationPercent(
        research ? kArmorRepairResearchDurationMinutes : recipe->durationMinutes,
        profile.baseMorale.tier,
        content.baseMorale());
    const std::uint32_t adjustedDuration = applyBaseFacilityTaskDuration(
        moraleDuration,
        BaseFacilityStaffingKind::Workshop,
        workerProfession,
        profile.baseConstruction.workshopLevel,
        content.baseWorkforce());
    if (adjustedDuration == 0U)
    {
        return startFailure(
            DomainErrorCode::Capacity,
            "the workshop worker profession is not eligible",
            profile.revision);
    }
    const std::uint32_t siteDurationPercent =
        activeBaseSiteManufacturingDurationPercent(profile, content);
    const std::uint32_t finalDuration =
        applyActiveBaseSiteManufacturingDuration(
            adjustedDuration, profile, content);
    if (profile.nextBaseServiceJobId ==
            std::numeric_limits<BaseServiceJobId>::max() ||
        (!research && profile.assets.nextAssetId() ==
            std::numeric_limits<AssetInstanceId>::max()) ||
        profile.worldClock.elapsedWorldMinutes >
            std::numeric_limits<std::uint64_t>::max() -
                finalDuration)
    {
        return startFailure(
            DomainErrorCode::RevisionOverflow,
            "manufacturing identity or completion time would overflow",
            profile.revision);
    }

    BaseManufacturingStartPlan plan{
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        workerCount,
        workerProfession,
        finalDuration,
        siteDurationPercent};
    static const std::array researchInputs{
        BaseManufacturingInputDefinition{kArmorRepairBlueprintId, 1U}};
    const std::span<const BaseManufacturingInputDefinition> inputs = research
        ? std::span<const BaseManufacturingInputDefinition>{researchInputs}
        : std::span<const BaseManufacturingInputDefinition>{recipe->inputs};
    std::set<AssetInstanceId> selected;
    for (const BaseManufacturingInputDefinition &input : inputs)
    {
        const auto found = std::find_if(
            profile.assets.records().begin(),
            profile.assets.records().end(),
            [&](const auto &entry)
            {
                const AssetRecord &asset = entry.second;
                if (selected.contains(asset.instanceId) ||
                    asset.definitionId != input.itemDefinitionId ||
                    asset.quantity != input.quantity)
                {
                    return false;
                }
                if (research)
                {
                    const auto *location =
                        std::get_if<StoredAssetLocation>(&asset.location);
                    return location != nullptr &&
                        location->container.kind == ProfileContainerKind::Stash;
                }
                return assetIsBaseAccessible(profile, asset.instanceId) &&
                    !hasChildren(profile, asset.instanceId);
            });
        if (found == profile.assets.records().end())
        {
            return startFailure(
                DomainErrorCode::MissingAsset,
                research
                    ? "place the armor repair blueprint in the Stash"
                    : "required manufacturing input is unavailable",
                profile.revision);
        }
        selected.insert(found->first);
        plan.inputs.push_back(
            {found->first, found->second.definitionId});
    }
    return plan;
}

BaseManufacturingReceipt executeStartBaseManufacturing(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartBaseManufacturingCommand &command,
    const CommandContext &context)
{
    if (!context.transactionId.empty() &&
        profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    BaseManufacturingReceipt failure;
    if (!validContext(profile, context, failure))
    {
        return failure;
    }
    const BaseManufacturingStartPlan plan = queryStartBaseManufacturing(
        profile, content, command);
    if (!plan.canCommit)
    {
        return receiptFailure(plan.error, plan.message, profile);
    }

    ProfileState candidate = profile;
    const BaseServiceJobId jobId = candidate.nextBaseServiceJobId++;
    std::vector<AssetInstanceId> inputIds;
    for (const BaseManufacturingInputSelection &selection : plan.inputs)
    {
        AssetRecord *asset = candidate.assets.findMutable(selection.assetId);
        if (asset == nullptr)
        {
            return receiptFailure(
                DomainErrorCode::MissingAsset,
                "manufacturing input disappeared",
                profile);
        }
        asset->location = BaseServiceAssetLocation{jobId};
        inputIds.push_back(asset->instanceId);
    }
    std::optional<AssetInstanceId> outputId;
    if (command.recipeDefinitionId != kArmorRepairResearchId)
    {
        const BaseManufacturingRecipeDefinition &recipe =
            content.baseManufacturingRecipe(command.recipeDefinitionId);
        try
        {
            outputId = candidate.assets.create(
                content.item(recipe.outputItemDefinitionId),
                BaseServiceAssetLocation{jobId},
                recipe.outputQuantity);
        }
        catch (...)
        {
            return receiptFailure(
                DomainErrorCode::RevisionOverflow,
                "manufacturing output identity cannot be reserved",
                profile);
        }
    }
    candidate.baseManufacturing.activeOrder = BaseManufacturingOrder{
        jobId,
        command.recipeDefinitionId,
        plan.workerCount,
        plan.workerProfession,
        candidate.worldClock.elapsedWorldMinutes,
        candidate.worldClock.elapsedWorldMinutes + plan.durationMinutes,
        std::move(inputIds),
        outputId.value_or(0),
        false};
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            jobId, outputId};
}

BaseManufacturingReturnPlan queryCancelBaseManufacturing(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    if (profile.pendingRaid.has_value())
    {
        return returnFailure(
            DomainErrorCode::IllegalDestination,
            "manufacturing can only be cancelled in Base",
            profile.revision);
    }
    if (!profile.baseManufacturing.activeOrder.has_value() ||
        profile.baseManufacturing.activeOrder->outputReady)
    {
        return returnFailure(
            DomainErrorCode::IllegalDestination,
            "no processing manufacturing order can be cancelled",
            profile.revision);
    }
    ProfileState candidate = profile;
    std::vector<BaseManufacturingReturnSelection> returns;
    if (!prepareReturns(
            candidate,
            content,
            profile.baseManufacturing.activeOrder->inputAssetIds,
            returns))
    {
        return returnFailure(
            DomainErrorCode::Capacity,
            "Stash cannot receive all reserved manufacturing inputs",
            profile.revision);
    }
    return {true, DomainErrorCode::None, {}, profile.revision,
            std::move(returns)};
}

BaseManufacturingReceipt executeCancelBaseManufacturing(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
{
    if (!context.transactionId.empty() &&
        profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    BaseManufacturingReceipt failure;
    if (!validContext(profile, context, failure))
    {
        return failure;
    }
    const BaseManufacturingReturnPlan plan = queryCancelBaseManufacturing(
        profile, content);
    if (!plan.canCommit)
    {
        return receiptFailure(plan.error, plan.message, profile);
    }
    ProfileState candidate = profile;
    const BaseManufacturingOrder order =
        *candidate.baseManufacturing.activeOrder;
    for (const BaseManufacturingReturnSelection &selection : plan.returns)
    {
        AssetRecord *asset = candidate.assets.findMutable(selection.assetId);
        if (asset == nullptr)
        {
            return receiptFailure(
                DomainErrorCode::MissingAsset,
                "reserved manufacturing input disappeared",
                profile);
        }
        asset->location = selection.destination;
    }
    if (order.outputAssetId != 0)
    {
        static_cast<void>(candidate.assets.erase(order.outputAssetId));
    }
    candidate.baseManufacturing.activeOrder.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            order.jobId, std::nullopt};
}

BaseManufacturingReturnPlan queryCollectBaseManufacturing(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    if (profile.pendingRaid.has_value())
    {
        return returnFailure(
            DomainErrorCode::IllegalDestination,
            "manufacturing output can only be collected in Base",
            profile.revision);
    }
    if (!profile.baseManufacturing.activeOrder.has_value() ||
        !profile.baseManufacturing.activeOrder->outputReady)
    {
        return returnFailure(
            DomainErrorCode::IllegalDestination,
            "no manufacturing output is ready",
            profile.revision);
    }
    const AssetInstanceId outputId =
        profile.baseManufacturing.activeOrder->outputAssetId;
    ProfileState candidate = profile;
    std::vector<BaseManufacturingReturnSelection> returns;
    if (!prepareReturns(candidate, content, {outputId}, returns))
    {
        return returnFailure(
            DomainErrorCode::Capacity,
            "Stash has no room for the manufacturing output",
            profile.revision);
    }
    return {true, DomainErrorCode::None, {}, profile.revision,
            std::move(returns)};
}

BaseManufacturingReceipt executeCollectBaseManufacturing(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context)
{
    if (!context.transactionId.empty() &&
        profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    BaseManufacturingReceipt failure;
    if (!validContext(profile, context, failure))
    {
        return failure;
    }
    const BaseManufacturingReturnPlan plan = queryCollectBaseManufacturing(
        profile, content);
    if (!plan.canCommit)
    {
        return receiptFailure(plan.error, plan.message, profile);
    }
    ProfileState candidate = profile;
    const BaseManufacturingOrder order =
        *candidate.baseManufacturing.activeOrder;
    AssetRecord *output = candidate.assets.findMutable(order.outputAssetId);
    if (output == nullptr)
    {
        return receiptFailure(
            DomainErrorCode::MissingAsset,
            "manufacturing output disappeared",
            profile);
    }
    output->location = plan.returns.front().destination;
    candidate.baseManufacturing.activeOrder.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile);
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            order.jobId, order.outputAssetId};
}

BaseManufacturingAdvanceResult applyBaseManufacturingThrough(
    ProfileState &profile,
    const ContentRegistry &content)
{
    if (profile.pendingRaid.has_value() ||
        !profile.baseManufacturing.activeOrder.has_value() ||
        !baseFacilityInstalled(
            profile,
            BaseFacilityDefinitionId{"base_facility.workshop"}))
    {
        return {};
    }
    BaseManufacturingOrder &order =
        *profile.baseManufacturing.activeOrder;
    if (order.outputReady || order.completionWorldMinute >
            profile.worldClock.elapsedWorldMinutes)
    {
        return {};
    }
    const BaseServiceJobId jobId = order.jobId;
    if (order.recipeDefinitionId == kArmorRepairResearchId)
    {
        if (order.inputAssetIds.size() != 1U)
        {
            return {};
        }
        const AssetInstanceId blueprintId = order.inputAssetIds.front();
        const AssetRecord *blueprint = profile.assets.find(blueprintId);
        if (blueprint == nullptr ||
            blueprint->definitionId != kArmorRepairBlueprintId ||
            blueprint->quantity != 1U ||
            blueprint->location != AssetLocation{BaseServiceAssetLocation{jobId}})
        {
            return {};
        }
        static_cast<void>(profile.assets.erase(blueprintId));
        profile.baseManufacturing.armorRepairRecipeUnlocked = true;
        profile.baseManufacturing.activeOrder.reset();
        return {true, false, jobId, std::nullopt, true};
    }
    const AssetInstanceId outputId = order.outputAssetId;
    AssetRecord *output = profile.assets.findMutable(outputId);
    if (output == nullptr)
    {
        return {};
    }
    for (AssetInstanceId inputId : order.inputAssetIds)
    {
        if (profile.assets.find(inputId) == nullptr)
        {
            return {};
        }
    }
    for (AssetInstanceId inputId : order.inputAssetIds)
    {
        static_cast<void>(profile.assets.erase(inputId));
    }
    order.inputAssetIds.clear();
    order.committedWorkers = 0U;
    const ItemDefinition &definition = content.item(output->definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        output->orientation,
        outputId);
    if (origin.has_value())
    {
        output->location = StoredAssetLocation{
            ProfileContainerId::stash(), *origin};
        profile.baseManufacturing.activeOrder.reset();
        return {true, false, jobId, outputId};
    }
    order.outputReady = true;
    return {true, true, jobId, outputId};
}
