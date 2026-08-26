#include "raid_intelligence_domain.h"

#include <limits>
#include <utility>

namespace
{
RaidIntelligencePurchasePlan planFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, error, std::move(message), revision};
}

RaidIntelligencePurchaseReceipt receiptFailure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return {false, false, error, std::move(message), revision};
}
}

RaidIntelligencePurchasePlan queryRaidIntelligencePurchase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidIntelligencePurchaseCommand &command)
{
    if (profile.pendingRaid.has_value())
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "Raid intelligence cannot be purchased during a Raid",
            profile.revision);
    }
    if (command.category == RaidIntelligenceCategory::Count)
    {
        return planFailure(
            DomainErrorCode::InvalidQuantity,
            "Raid intelligence category is invalid",
            profile.revision);
    }

    const MapDefinition *map{};
    try
    {
        map = &content.map(command.mapDefinitionId);
    }
    catch (...)
    {
        return planFailure(
            DomainErrorCode::IllegalDestination,
            "Raid intelligence map does not exist",
            profile.revision);
    }

    const std::uint32_t price = map->operationBriefing.price(command.category);
    if (price == 0U)
    {
        return planFailure(
            DomainErrorCode::InvalidQuantity,
            "Raid intelligence is not offered for this map",
            profile.revision);
    }
    const std::uint32_t owned =
        profile.raidIntelligence.count(command.mapDefinitionId, command.category);
    if (owned == std::numeric_limits<std::uint32_t>::max())
    {
        return planFailure(
            DomainErrorCode::Capacity,
            "Raid intelligence archive is full",
            profile.revision);
    }
    return RaidIntelligencePurchasePlan{
        profile.currency >= price,
        profile.currency >= price
            ? DomainErrorCode::None
            : DomainErrorCode::InvalidQuantity,
        profile.currency >= price
            ? std::string{}
            : std::string{"currency is insufficient for Raid intelligence"},
        profile.revision,
        price,
        owned};
}

RaidIntelligencePurchaseReceipt executeRaidIntelligencePurchase(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidIntelligencePurchaseCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return receiptFailure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    }
    if (context.expectedRevision != profile.revision)
    {
        return receiptFailure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return receiptFailure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    const RaidIntelligencePurchasePlan plan =
        queryRaidIntelligencePurchase(profile, content, command);
    if (!plan.canCommit)
    {
        return receiptFailure(plan.error, plan.message, profile.revision);
    }

    ProfileState candidate = profile;
    candidate.currency -= plan.price;
    auto &counts = candidate.raidIntelligence.counts[command.mapDefinitionId];
    const std::size_t index = raidIntelligenceCategoryIndex(command.category);
    ++counts[index];
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;

    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return receiptFailure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }

    profile = std::move(candidate);
    return RaidIntelligencePurchaseReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.price,
        plan.ownedBefore + 1U};
}
