#include "base_resource_domain.h"

#include <array>
#include <limits>
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
    const BaseOperationsDefinition &definition) noexcept
{
    const std::array<ResourceReserve, 4> resources{{
        {BaseResourceKind::Food, state.pool.food, kBaseDailyDemand.food},
        {BaseResourceKind::Hygiene, state.pool.hygiene,
         kBaseDailyDemand.hygiene},
        {BaseResourceKind::Morale, state.pool.morale,
         kBaseDailyDemand.morale},
        {BaseResourceKind::Security, state.pool.security,
         kBaseDailyDemand.security}}};
    const ResourceReserve *limiting = &resources.front();
    for (const ResourceReserve &resource : resources)
    {
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
        state.pool.food / kBaseDailyDemand.food,
        state.pool.hygiene / kBaseDailyDemand.hygiene,
        state.pool.morale / kBaseDailyDemand.morale,
        state.pool.security / kBaseDailyDemand.security};
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
}

BaseOperationalTier projectBaseResourceTier(
    std::uint32_t current,
    std::uint32_t dailyDemand,
    const BaseOperationsDefinition &definition) noexcept
{
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
    const BaseOperationsDefinition &definition) noexcept
{
    return projectBaseOperationsImpl(state, definition);
}

BaseResourcePlan queryBaseResourceContribution(
    const ProfileState &profile,
    const ContentRegistry &content,
    const ContributeBaseAssetCommand &command)
{
    const AssetRecord *asset = profile.assets.find(command.assetId);
    if (asset == nullptr)
    {
        return planFailure(
            DomainErrorCode::MissingAsset,
            "allocation item does not exist",
            profile.revision);
    }
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    if (stored == nullptr ||
        stored->container != ProfileContainerId::baseIntake())
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "only pending allocation items can be contributed",
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

BaseDailyDemandResult applyBaseDailyDemandThrough(
    BaseResourceState &state,
    std::uint64_t completedWorldDays) noexcept
{
    if (completedWorldDays <= state.resolvedDemandCycleCount)
    {
        return {};
    }
    const std::uint64_t cycles =
        completedWorldDays - state.resolvedDemandCycleCount;
    BaseResourceBundle &pool = state.pool;
    BaseResourceBundle &shortfall = state.lastShortfall;
    shortfall.food = consumeCycles(
        pool.food, kBaseDailyDemand.food, cycles);
    shortfall.hygiene = consumeCycles(
        pool.hygiene, kBaseDailyDemand.hygiene, cycles);
    shortfall.morale = consumeCycles(
        pool.morale, kBaseDailyDemand.morale, cycles);
    shortfall.security = consumeCycles(
        pool.security, kBaseDailyDemand.security, cycles);
    state.resolvedDemandCycleCount = completedWorldDays;
    return {cycles, shortfall};
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
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    if (stored == nullptr ||
        stored->container != ProfileContainerId::baseIntake())
    {
        return {false, DomainErrorCode::IllegalDestination,
                "only pending allocation items can fulfill a Base priority",
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
