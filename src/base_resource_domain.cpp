#include "base_resource_domain.h"

#include <array>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
struct ResourceReserve
{
    BaseResourceKind kind{};
    std::uint32_t current{};
    std::uint32_t dailyDemand{};
};

BaseResourcePlan planFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, error, std::move(message), revision, {}};
}

BaseOperationalProjection projectBaseOperationsImpl(
    const BaseResourceState &state,
    const BaseOperationsDefinition &definition,
    BaseResourceBundle dailyDemand) noexcept
{
    const std::array<ResourceReserve, 4> resources{{
        {BaseResourceKind::Food, state.pool.food, dailyDemand.food},
        {BaseResourceKind::Hygiene, state.pool.hygiene,
         dailyDemand.hygiene},
        {BaseResourceKind::Morale, state.pool.morale,
         dailyDemand.morale},
        {BaseResourceKind::Security, state.pool.security,
         dailyDemand.security}}};
    const ResourceReserve *limiting = &resources.front();
    for (const ResourceReserve &resource : resources)
    {
        if (resource.dailyDemand == 0U)
        {
            continue;
        }
        if (limiting->dailyDemand == 0U)
        {
            limiting = &resource;
            continue;
        }
        const std::uint64_t candidate =
            static_cast<std::uint64_t>(resource.current) *
            limiting->dailyDemand;
        const std::uint64_t currentMinimum =
            static_cast<std::uint64_t>(limiting->current) *
            resource.dailyDemand;
        if (candidate < currentMinimum)
        {
            limiting = &resource;
        }
    }

    BaseOperationalProjection projection;
    projection.limitingResource = limiting->kind;
    projection.reserveDays = BaseResourceBundle{
        dailyDemand.food == 0U
            ? kMaximumBaseResource
            : state.pool.food / dailyDemand.food,
        dailyDemand.hygiene == 0U
            ? kMaximumBaseResource
            : state.pool.hygiene / dailyDemand.hygiene,
        dailyDemand.morale == 0U
            ? kMaximumBaseResource
            : state.pool.morale / dailyDemand.morale,
        dailyDemand.security == 0U
            ? kMaximumBaseResource
            : state.pool.security / dailyDemand.security};
    projection.tier = projectBaseResourceTier(
        limiting->current,
        limiting->dailyDemand,
        definition);
    return projection;
}

bool hasChildren(const ProfileState &profile, AssetInstanceId ownerId) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (stored != nullptr &&
            stored->container.kind == ProfileContainerKind::AssetCompartment &&
            stored->container.ownerAssetId == ownerId)
        {
            return true;
        }
    }
    return false;
}

bool fits(std::uint32_t current, std::uint32_t addition) noexcept
{
    return current <= kMaximumBaseResource &&
           addition <= kMaximumBaseResource - current;
}

void add(BaseResourceBundle &target, const BaseResourceBundle &value) noexcept
{
    target.food += value.food;
    target.hygiene += value.hygiene;
    target.morale += value.morale;
    target.security += value.security;
}

std::uint64_t saturatedMultiply(
    std::uint64_t left,
    std::uint64_t right) noexcept
{
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

std::uint32_t consumeCycles(
    std::uint32_t &current,
    std::uint32_t demand,
    std::uint64_t cycles) noexcept
{
    const std::uint64_t priorDemand = saturatedMultiply(
        demand,
        cycles - 1U);
    const std::uint64_t availableBeforeLatest =
        priorDemand >= current ? 0U : current - priorDemand;
    const std::uint32_t latestShortfall =
        availableBeforeLatest >= demand
            ? 0U
            : demand - static_cast<std::uint32_t>(availableBeforeLatest);
    const std::uint64_t totalDemand = saturatedMultiply(demand, cycles);
    current = totalDemand >= current
        ? 0U
        : current - static_cast<std::uint32_t>(totalDemand);
    return latestShortfall;
}

std::uint64_t shortageCycles(
    std::uint64_t available,
    std::uint64_t demand,
    std::uint64_t cycles) noexcept
{
    if (demand == 0U)
    {
        return 0U;
    }
    const std::uint64_t fullyCovered = std::min(cycles, available / demand);
    return cycles - fullyCovered;
}

void saturatedAdd(std::uint64_t &target, std::uint64_t value) noexcept
{
    target = value > std::numeric_limits<std::uint64_t>::max() - target
        ? std::numeric_limits<std::uint64_t>::max()
        : target + value;
}

std::uint32_t &resourceValue(
    BaseResourceBundle &bundle,
    BaseSupplyCategory category) noexcept
{
    switch (category)
    {
    case BaseSupplyCategory::Food:
        return bundle.food;
    case BaseSupplyCategory::Medical:
        return bundle.hygiene;
    case BaseSupplyCategory::Recreation:
        return bundle.morale;
    case BaseSupplyCategory::Security:
        return bundle.security;
    }
    return bundle.food;
}

const std::uint32_t &resourceValue(
    const BaseResourceBundle &bundle,
    BaseSupplyCategory category) noexcept
{
    switch (category)
    {
    case BaseSupplyCategory::Food:
        return bundle.food;
    case BaseSupplyCategory::Medical:
        return bundle.hygiene;
    case BaseSupplyCategory::Recreation:
        return bundle.morale;
    case BaseSupplyCategory::Security:
        return bundle.security;
    }
    return bundle.food;
}

std::size_t categoryIndex(BaseSupplyCategory category) noexcept
{
    switch (category)
    {
    case BaseSupplyCategory::Food:
        return 0U;
    case BaseSupplyCategory::Medical:
        return 1U;
    case BaseSupplyCategory::Recreation:
        return 2U;
    case BaseSupplyCategory::Security:
        return 3U;
    }
    return 0U;
}

bool ownsBaseAccessibleDefinition(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == definitionId &&
            assetIsBaseAccessible(profile, asset.instanceId))
        {
            return true;
        }
    }
    return false;
}
}

BaseOperationalTier projectBaseResourceTier(
    std::uint32_t current,
    std::uint32_t dailyDemand,
    const BaseOperationsDefinition &definition) noexcept
{
    if (dailyDemand == 0U)
    {
        return BaseOperationalTier::Supported;
    }
    if (current < dailyDemand)
    {
        return BaseOperationalTier::Critical;
    }
    if (current < dailyDemand * definition.strainedBelowReserveDays)
    {
        return BaseOperationalTier::Strained;
    }
    if (current >= dailyDemand * definition.supportedAtReserveDays)
    {
        return BaseOperationalTier::Supported;
    }
    return BaseOperationalTier::Stable;
}

BaseOperationalProjection projectBaseOperations(
    const BaseResourceState &state,
    const BaseOperationsDefinition &definition,
    BaseResourceBundle dailyDemand) noexcept
{
    return projectBaseOperationsImpl(state, definition, dailyDemand);
}

BaseResourcePlan queryBaseResourceContribution(
    const ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command)
{
    if (profile.pendingRaid.has_value())
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "Base allocation is unavailable during a Raid",
            profile.revision);
    }
    const AssetRecord *asset = profile.assets.find(command.assetId);
    if (asset == nullptr)
    {
        return planFailure(
            DomainErrorCode::MissingAsset,
            "allocation item does not exist",
            profile.revision);
    }
    if (!assetIsBaseAccessible(profile, asset->instanceId))
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "only Base-accessible personal assets can be contributed",
            profile.revision);
    }
    if (hasChildren(profile, asset->instanceId))
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "a non-empty container cannot be contributed",
            profile.revision);
    }

    const ItemDefinition &definition = content.item(asset->definitionId);
    if (!definition.baseContribution.has_value() ||
        definition.baseContribution->empty())
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "item has no Base contribution value",
            profile.revision);
    }
    const BaseResourceBundle unit = *definition.baseContribution;
    const std::uint64_t quantity = asset->quantity;
    const BaseResourceBundle total{
        static_cast<std::uint32_t>(unit.food * quantity),
        static_cast<std::uint32_t>(unit.hygiene * quantity),
        static_cast<std::uint32_t>(unit.morale * quantity),
        static_cast<std::uint32_t>(unit.security * quantity)};
    const BaseResourceBundle &pool = profile.baseResources.pool;
    if (!fits(pool.food, total.food) ||
        !fits(pool.hygiene, total.hygiene) ||
        !fits(pool.morale, total.morale) ||
        !fits(pool.security, total.security))
    {
        return planFailure(
            DomainErrorCode::Capacity,
            "Base resource capacity would be exceeded",
            profile.revision);
    }
    return {true, DomainErrorCode::None, {}, profile.revision, total};
}

BaseResourceReceipt executeBaseResourceContribution(
    ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID must not be empty", profile.revision, {}};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision, {}};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision, {}};
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return {false, false, DomainErrorCode::RevisionOverflow,
                "profile revision cannot advance", profile.revision, {}};
    }

    const BaseResourcePlan plan =
        queryBaseResourceContribution(profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message,
                profile.revision, {}};
    }

    ProfileState candidate = profile;
    add(candidate.baseResources.pool, plan.contribution);
    static_cast<void>(candidate.assets.erase(command.assetId));
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                validation.message, profile.revision, {}};
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {},
            profile.revision, plan.contribution};
}

BaseSupplyReadinessProjection projectBaseSupplyReadiness(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseResourceBundle dailyDemand)
{
    BaseSupplyReadinessProjection projection;
    projection.assignedDefinitionCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(
            profile.baseSupplyPolicy.assignments.size(),
            std::numeric_limits<std::uint32_t>::max()));
    projection.pool = profile.baseResources.pool;
    projection.dailyDemand = dailyDemand;

    std::set<AssetInstanceId> ownersWithChildren;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        const auto *stored = std::get_if<StoredAssetLocation>(
            &asset.location);
        if (stored != nullptr &&
            stored->container.kind ==
                ProfileContainerKind::AssetCompartment)
        {
            ownersWithChildren.insert(stored->container.ownerAssetId);
        }
    }

    std::set<ItemDefinitionId> ownedAssignedDefinitions;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (!assetIsBaseAccessible(profile, asset.instanceId))
            continue;

        ++projection.baseAccessibleStacks;
        saturatedAdd(
            projection.baseAccessibleUnits,
            static_cast<std::uint64_t>(asset.quantity));

        const auto assignment = profile.baseSupplyPolicy.assignments.find(
            asset.definitionId);
        if (assignment == profile.baseSupplyPolicy.assignments.end() ||
            ownersWithChildren.contains(asset.instanceId))
        {
            continue;
        }
        const ItemDefinition *definition{};
        try
        {
            definition = &content.item(asset.definitionId);
        }
        catch (...)
        {
            continue;
        }
        const std::uint32_t perUnit = baseSupplyContribution(
            *definition, assignment->second);
        if (perUnit == 0U)
            continue;

        ownedAssignedDefinitions.insert(asset.definitionId);
        std::uint32_t &available = resourceValue(
            projection.authorizedContribution, assignment->second);
        const std::uint64_t contribution = saturatedMultiply(
            asset.quantity, perUnit);
        available = contribution >=
                std::numeric_limits<std::uint32_t>::max() - available
            ? std::numeric_limits<std::uint32_t>::max()
            : available + static_cast<std::uint32_t>(contribution);
    }
    projection.ownedAssignedDefinitionCount =
        static_cast<std::uint32_t>(std::min<std::size_t>(
            ownedAssignedDefinitions.size(),
            std::numeric_limits<std::uint32_t>::max()));

    for (BaseSupplyCategory category : {
             BaseSupplyCategory::Food,
             BaseSupplyCategory::Medical,
             BaseSupplyCategory::Recreation,
             BaseSupplyCategory::Security})
    {
        const std::uint64_t available =
            static_cast<std::uint64_t>(resourceValue(
                projection.pool, category)) +
            resourceValue(projection.authorizedContribution, category);
        const std::uint32_t demand = resourceValue(
            projection.dailyDemand, category);
        resourceValue(projection.projectedShortfall, category) =
            available >= demand
            ? 0U
            : demand - static_cast<std::uint32_t>(available);
    }
    return projection;
}

std::uint32_t baseSupplyContribution(
    const ItemDefinition &definition,
    BaseSupplyCategory category) noexcept
{
    if (!definition.baseContribution.has_value())
    {
        return 0U;
    }
    return resourceValue(*definition.baseContribution, category);
}

BaseSupplyAssignmentPlan queryBaseSupplyAssignment(
    const ProfileState &profile,
    const ContentRegistry &content,
    const SetBaseSupplyAssignmentCommand &command)
{
    const auto failure = [&profile](
        DomainErrorCode error,
        std::string message)
    {
        return BaseSupplyAssignmentPlan{
            false, error, std::move(message), profile.revision, std::nullopt};
    };
    if (profile.pendingRaid.has_value())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "Base supply policy is unavailable during a Raid");
    }
    const auto existing = profile.baseSupplyPolicy.assignments.find(
        command.itemDefinitionId);
    if (!command.category.has_value())
    {
        if (existing == profile.baseSupplyPolicy.assignments.end())
        {
            return failure(
                DomainErrorCode::IllegalDestination,
                "item definition is not assigned to Base supply");
        }
        return {true, DomainErrorCode::None, {}, profile.revision,
                std::nullopt};
    }
    const ItemDefinition *definition{};
    try
    {
        definition = &content.item(command.itemDefinitionId);
    }
    catch (...)
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            "Base supply item definition is unknown");
    }
    if (baseSupplyContribution(*definition, *command.category) == 0U)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "item cannot supply the selected Base category");
    }
    if (!ownsBaseAccessibleDefinition(profile, command.itemDefinitionId))
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "no owned Base-accessible item matches this supply rule");
    }
    if (existing != profile.baseSupplyPolicy.assignments.end() &&
        existing->second == *command.category)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "item definition is already assigned to this Base category");
    }
    return {true, DomainErrorCode::None, {}, profile.revision,
            command.category};
}

BaseSupplyAssignmentReceipt executeBaseSupplyAssignment(
    ProfileState &profile,
    const ContentRegistry &content,
    const SetBaseSupplyAssignmentCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID must not be empty", profile.revision};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision,
                command.category};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision};
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return {false, false, DomainErrorCode::RevisionOverflow,
                "profile revision cannot advance", profile.revision};
    }
    const BaseSupplyAssignmentPlan plan = queryBaseSupplyAssignment(
        profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision};
    }
    ProfileState candidate = profile;
    if (command.category.has_value())
    {
        candidate.baseSupplyPolicy.assignments.insert_or_assign(
            command.itemDefinitionId, *command.category);
    }
    else
    {
        candidate.baseSupplyPolicy.assignments.erase(command.itemDefinitionId);
    }
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
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            command.category};
}

BaseDailyDemandResult applyBaseDailyDemandThrough(
    BaseResourceState &state,
    std::uint64_t completedWorldDays,
    BaseResourceBundle dailyDemand) noexcept
{
    if (completedWorldDays <= state.resolvedDemandCycleCount)
    {
        return {};
    }
    const std::uint64_t cycles =
        completedWorldDays - state.resolvedDemandCycleCount;
    BaseResourceBundle &pool = state.pool;
    BaseResourceBundle &shortfall = state.lastShortfall;
    const std::uint64_t shortageCount = std::max({
        shortageCycles(pool.food, dailyDemand.food, cycles),
        shortageCycles(pool.hygiene, dailyDemand.hygiene, cycles),
        shortageCycles(pool.morale, dailyDemand.morale, cycles),
        shortageCycles(pool.security, dailyDemand.security, cycles)});
    shortfall.food = consumeCycles(
        pool.food, dailyDemand.food, cycles);
    shortfall.hygiene = consumeCycles(
        pool.hygiene, dailyDemand.hygiene, cycles);
    shortfall.morale = consumeCycles(
        pool.morale, dailyDemand.morale, cycles);
    shortfall.security = consumeCycles(
        pool.security, dailyDemand.security, cycles);
    state.resolvedDemandCycleCount = completedWorldDays;
    return {cycles, shortfall, shortageCount};
}

BaseDailyDemandResult applyBaseDailyDemandWithSupplyThrough(
    ProfileState &profile,
    const ContentRegistry &content,
    std::uint64_t completedWorldDays,
    BaseResourceBundle dailyDemand) noexcept
{
    BaseResourceState &state = profile.baseResources;
    if (completedWorldDays <= state.resolvedDemandCycleCount)
    {
        return {};
    }
    if (profile.pendingRaid.has_value())
    {
        return applyBaseDailyDemandThrough(
            state, completedWorldDays, dailyDemand);
    }
    const std::uint64_t cycles =
        completedWorldDays - state.resolvedDemandCycleCount;
    std::array<std::uint64_t, 4> contribution{};
    std::vector<AssetInstanceId> eraseIds;

    for (BaseSupplyCategory category : {
             BaseSupplyCategory::Food,
             BaseSupplyCategory::Medical,
             BaseSupplyCategory::Recreation,
             BaseSupplyCategory::Security})
    {
        const std::uint32_t demand = resourceValue(dailyDemand, category);
        const std::uint64_t totalDemand = saturatedMultiply(demand, cycles);
        const std::uint64_t current = resourceValue(state.pool, category);
        std::uint64_t deficit = totalDemand > current
            ? totalDemand - current
            : 0U;
        if (deficit == 0U)
        {
            continue;
        }

        for (const auto &[assetId, record] : profile.assets.records())
        {
            if (deficit == 0U)
            {
                break;
            }
            const auto assignment =
                profile.baseSupplyPolicy.assignments.find(
                    record.definitionId);
            if (assignment == profile.baseSupplyPolicy.assignments.end() ||
                assignment->second != category ||
                !assetIsBaseAccessible(profile, assetId) ||
                hasChildren(profile, assetId))
            {
                continue;
            }
            const ItemDefinition *definition{};
            try
            {
                definition = &content.item(record.definitionId);
            }
            catch (...)
            {
                continue;
            }
            const std::uint32_t perUnit = baseSupplyContribution(
                *definition, category);
            if (perUnit == 0U)
            {
                continue;
            }
            const std::uint64_t wanted =
                deficit / perUnit + (deficit % perUnit == 0U ? 0U : 1U);
            const std::uint32_t consumed = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(record.quantity, wanted));
            AssetRecord *asset = profile.assets.findMutable(assetId);
            asset->quantity -= consumed;
            if (asset->quantity == 0U)
            {
                eraseIds.push_back(assetId);
            }
            const std::uint64_t supplied =
                static_cast<std::uint64_t>(consumed) * perUnit;
            std::uint64_t &accumulated = contribution[categoryIndex(category)];
            accumulated = supplied >
                    std::numeric_limits<std::uint64_t>::max() - accumulated
                ? std::numeric_limits<std::uint64_t>::max()
                : accumulated + supplied;
            deficit = supplied >= deficit ? 0U : deficit - supplied;
        }
        for (AssetInstanceId assetId : eraseIds)
        {
            static_cast<void>(profile.assets.erase(assetId));
        }
        eraseIds.clear();
    }

    BaseResourceBundle latestShortfall;
    std::uint64_t shortageCount{};
    for (BaseSupplyCategory category : {
             BaseSupplyCategory::Food,
             BaseSupplyCategory::Medical,
             BaseSupplyCategory::Recreation,
             BaseSupplyCategory::Security})
    {
        const std::uint64_t demand = resourceValue(dailyDemand, category);
        const std::uint64_t totalDemand = saturatedMultiply(demand, cycles);
        const std::uint64_t baseAvailable =
            resourceValue(state.pool, category);
        const std::uint64_t supplied =
            contribution[categoryIndex(category)];
        const std::uint64_t available = supplied >
                std::numeric_limits<std::uint64_t>::max() - baseAvailable
            ? std::numeric_limits<std::uint64_t>::max()
            : baseAvailable + supplied;
        shortageCount = std::max(
            shortageCount,
            shortageCycles(available, demand, cycles));
        const std::uint64_t previousDemand =
            saturatedMultiply(demand, cycles - 1U);
        const std::uint64_t beforeLatest = available > previousDemand
            ? available - previousDemand
            : 0U;
        resourceValue(latestShortfall, category) = beforeLatest >= demand
            ? 0U
            : static_cast<std::uint32_t>(demand - beforeLatest);
        const std::uint64_t remaining = available > totalDemand
            ? available - totalDemand
            : 0U;
        resourceValue(state.pool, category) = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, kMaximumBaseResource));
    }
    state.lastShortfall = latestShortfall;
    state.resolvedDemandCycleCount = completedWorldDays;
    return {cycles, latestShortfall, shortageCount};
}

BasePrioritySyncResult synchronizeBasePriorityThrough(
    ProfileState &profile,
    const ContentRegistry &content)
{
    const std::uint64_t cycleMinutes = content.basePriorityCycleMinutes();
    const auto &definitions = content.basePriorities();
    if (cycleMinutes == 0U || definitions.empty())
    {
        throw std::logic_error{"Base priority content is empty"};
    }
    const std::uint64_t currentCycle =
        profile.worldClock.elapsedWorldMinutes <= kInitialWorldMinute
            ? 0U
            : (profile.worldClock.elapsedWorldMinutes -
               kInitialWorldMinute) / cycleMinutes;
    BasePriorityState &state = profile.basePriority;
    if (!state.definitionId.valid())
    {
        state.cycleIndex = currentCycle;
        state.definitionId = definitions[
            currentCycle % definitions.size()].id;
        state.fulfilled = false;
        return {true, 0U, 0U};
    }
    if (currentCycle <= state.cycleIndex)
    {
        return {};
    }
    const std::uint64_t advanced = currentCycle - state.cycleIndex;
    const std::uint64_t missed = state.fulfilled
        ? advanced - 1U
        : advanced;
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    state.missedCycleCount = missed > maximum - state.missedCycleCount
        ? maximum
        : state.missedCycleCount + missed;
    saturatedAdd(profile.baseMorale.pendingMissedWishCount, missed);
    state.cycleIndex = currentCycle;
    state.definitionId = definitions[
        currentCycle % definitions.size()].id;
    state.fulfilled = false;
    return {true, advanced, missed};
}

BasePriorityPlan queryBasePrioritySubmission(
    const ProfileState &profile,
    const ContentRegistry &content,
    const SubmitBasePriorityCommand &command)
{
    if (profile.pendingRaid.has_value())
    {
        return {false, DomainErrorCode::IllegalDestination,
                "Base allocation is unavailable during a Raid",
                profile.revision, 0, {}};
    }
    const BasePriorityState &state = profile.basePriority;
    if (!state.definitionId.valid())
    {
        return {false, DomainErrorCode::InvalidProfile,
                "Base priority is not initialized", profile.revision, 0, {}};
    }
    if (state.fulfilled)
    {
        return {false, DomainErrorCode::IllegalDestination,
                "Base priority is already fulfilled", profile.revision, 0, {}};
    }
    const BasePriorityDefinition *priority{};
    try
    {
        priority = &content.basePriority(state.definitionId);
    }
    catch (...)
    {
        return {false, DomainErrorCode::InvalidProfile,
                "Base priority definition is unknown", profile.revision, 0, {}};
    }
    const AssetRecord *asset = profile.assets.find(command.assetId);
    if (asset == nullptr)
    {
        return {false, DomainErrorCode::MissingAsset,
                "priority item does not exist", profile.revision, 0, {}};
    }
    if (!assetIsBaseAccessible(profile, asset->instanceId))
    {
        return {false, DomainErrorCode::IllegalDestination,
                "only Base-accessible personal assets can fulfill a Base priority",
                profile.revision, 0, {}};
    }
    if (hasChildren(profile, asset->instanceId))
    {
        return {false, DomainErrorCode::IllegalDestination,
                "a non-empty container cannot fulfill a Base priority",
                profile.revision, 0, {}};
    }
    if (asset->definitionId != priority->requiredItemDefinitionId ||
        asset->quantity < priority->requiredQuantity)
    {
        return {false, DomainErrorCode::IllegalDestination,
                "selected item does not match the current Base priority",
                profile.revision, 0, {}};
    }
    const BaseResourceBundle &pool = profile.baseResources.pool;
    const BaseResourceBundle &reward = priority->resourceReward;
    if (!fits(pool.food, reward.food) ||
        !fits(pool.hygiene, reward.hygiene) ||
        !fits(pool.morale, reward.morale) ||
        !fits(pool.security, reward.security))
    {
        return {false, DomainErrorCode::Capacity,
                "Base priority reward would exceed resource capacity",
                profile.revision, 0, {}};
    }
    return {true, DomainErrorCode::None, {}, profile.revision,
            priority->requiredQuantity, reward};
}

BasePriorityReceipt executeBasePrioritySubmission(
    ProfileState &profile,
    const ContentRegistry &content,
    const SubmitBasePriorityCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID must not be empty", profile.revision, {}};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision, {}};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision, {}};
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return {false, false, DomainErrorCode::RevisionOverflow,
                "profile revision cannot advance", profile.revision, {}};
    }
    const BasePriorityPlan plan = queryBasePrioritySubmission(
        profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message,
                profile.revision, {}};
    }

    ProfileState candidate = profile;
    AssetRecord *asset = candidate.assets.findMutable(command.assetId);
    if (asset->quantity == plan.consumedQuantity)
    {
        static_cast<void>(candidate.assets.erase(command.assetId));
    }
    else
    {
        asset->quantity -= plan.consumedQuantity;
    }
    add(candidate.baseResources.pool, plan.reward);
    candidate.basePriority.fulfilled = true;
    saturatedAdd(candidate.baseMorale.pendingFulfilledWishCount, 1U);
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                validation.message, profile.revision, {}};
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {},
            profile.revision, plan.reward};
}
