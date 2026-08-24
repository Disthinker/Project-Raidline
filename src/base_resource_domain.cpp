#include "base_resource_domain.h"

#include <limits>

namespace
{
BaseResourcePlan planFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, error, std::move(message), revision, {}};
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

std::uint32_t consume(
    std::uint32_t &current,
    std::uint32_t demand) noexcept
{
    const std::uint32_t shortfall = demand > current ? demand - current : 0U;
    current = demand > current ? 0U : current - demand;
    return shortfall;
}
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

void applyBaseActivityDemand(ProfileState &profile) noexcept
{
    BaseResourceBundle &pool = profile.baseResources.pool;
    BaseResourceBundle &shortfall = profile.baseResources.lastShortfall;
    shortfall.food = consume(pool.food, kBaseActivityDemand.food);
    shortfall.hygiene = consume(pool.hygiene, kBaseActivityDemand.hygiene);
    shortfall.morale = consume(pool.morale, kBaseActivityDemand.morale);
    shortfall.security = consume(pool.security, kBaseActivityDemand.security);
    if (profile.baseResources.resolvedRaidCount !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++profile.baseResources.resolvedRaidCount;
    }
}
