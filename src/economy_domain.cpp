#include "economy_domain.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <vector>

#include "alpha_content_ids.h"
#include "lost_raid_domain.h"

namespace
{
EconomyReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision)
{
    return EconomyReceipt{
        false, false, error, std::move(message), revision, 0};
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

struct RaidCapabilityInventory
{
    bool weapon{};
    bool magazine{};
    std::uint64_t ammunition{};
};

RaidCapabilityInventory raidCapabilityInventory(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    RaidCapabilityInventory result;
    std::vector<ItemDefinitionId> compatibleMagazineDefinitions;
    std::vector<ItemDefinitionId> ownedMagazineDefinitions;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (std::holds_alternative<RaidGroundAssetLocation>(asset.location) ||
            lostRaidRecordForAsset(profile, id).has_value())
        {
            continue;
        }

        const ItemDefinition &definition = content.item(asset.definitionId);
        if (definition.category == ItemCategory::Weapon &&
            definition.compatibleMagazineDefinitionId.has_value())
        {
            result.weapon = true;
            compatibleMagazineDefinitions.push_back(
                *definition.compatibleMagazineDefinitionId);
        }
        if (definition.category == ItemCategory::Magazine)
        {
            ownedMagazineDefinitions.push_back(definition.definitionId);
        }
        if (definition.category == ItemCategory::Ammunition)
        {
            result.ammunition += asset.quantity;
        }
        for (const MagazineRoundRecord &round : asset.magazineRounds)
        {
            if (round.definitionId == alpha_content::ammunition)
            {
                ++result.ammunition;
            }
        }
        if (asset.chamberedRound.has_value() &&
            asset.chamberedRound->definitionId == alpha_content::ammunition)
        {
            ++result.ammunition;
        }
    }
    result.magazine = std::any_of(
        ownedMagazineDefinitions.begin(),
        ownedMagazineDefinitions.end(),
        [&compatibleMagazineDefinitions](const ItemDefinitionId &owned)
        {
            return std::find(
                       compatibleMagazineDefinitions.begin(),
                       compatibleMagazineDefinitions.end(),
                       owned) != compatibleMagazineDefinitions.end();
        });
    return result;
}

bool createFirstFit(
    ProfileState &profile,
    const ContentRegistry &content,
    const ItemDefinition &definition,
    std::uint32_t quantity,
    const std::optional<std::string> &reliefBatchId)
{
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    if (!origin.has_value())
    {
        return false;
    }
    static_cast<void>(profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        quantity,
        reliefBatchId));
    return true;
}

EconomyReceipt applyPurchase(
    ProfileState &candidate,
    const ContentRegistry &content,
    const PurchaseCommand &command)
{
    const ItemDefinition *definition{};
    try
    {
        definition = &content.item(command.definitionId);
    }
    catch (...)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "supply definition does not exist",
            candidate.revision);
    }
    if (command.quantity == 0 || definition->marketBuyPrice == 0)
    {
        return failure(
            DomainErrorCode::InvalidQuantity,
            "item is not available from fixed supply",
            candidate.revision);
    }

    const std::uint64_t cost =
        static_cast<std::uint64_t>(definition->marketBuyPrice) *
        command.quantity;
    if (cost > candidate.currency)
    {
        return failure(
            DomainErrorCode::Capacity,
            "currency is insufficient",
            candidate.revision);
    }

    std::uint32_t remaining = command.quantity;
    while (remaining > 0)
    {
        const std::uint32_t stack =
            std::min(remaining, definition->maxStackSize);
        if (!createFirstFit(
                candidate,
                content,
                *definition,
                stack,
                std::nullopt))
        {
            return failure(
                DomainErrorCode::Capacity,
                "Stash cannot hold the purchase",
                candidate.revision);
        }
        remaining -= stack;
    }
    candidate.currency -= static_cast<std::uint32_t>(cost);
    return EconomyReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        candidate.revision,
        -static_cast<std::int64_t>(cost)};
}

EconomyReceipt applyRecycle(
    ProfileState &candidate,
    const ContentRegistry &content,
    const RecycleCommand &command)
{
    const AssetRecord *asset = candidate.assets.find(command.instanceId);
    if (asset == nullptr)
    {
        return failure(
            DomainErrorCode::MissingAsset,
            "asset does not exist",
            candidate.revision);
    }
    if (lostRaidRecordForAsset(candidate, asset->instanceId).has_value())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "lost Raid assets require a recovery transaction",
            candidate.revision);
    }
    const ItemDefinition &definition = content.item(asset->definitionId);
    if (asset->reliefBatchId.has_value() ||
        definition.marketRecyclePrice == 0 ||
        hasChildren(candidate, asset->instanceId))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "asset cannot be recycled",
            candidate.revision);
    }

    const std::uint64_t value =
        static_cast<std::uint64_t>(definition.marketRecyclePrice) *
        asset->quantity;
    if (value > std::numeric_limits<std::uint32_t>::max() - candidate.currency)
    {
        return failure(
            DomainErrorCode::Capacity,
            "currency would overflow",
            candidate.revision);
    }
    candidate.currency += static_cast<std::uint32_t>(value);
    static_cast<void>(candidate.assets.erase(asset->instanceId));
    return EconomyReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        candidate.revision,
        static_cast<std::int64_t>(value)};
}

EconomyReceipt applyRelief(
    ProfileState &candidate,
    const ContentRegistry &content,
    const ClaimReliefCommand &command)
{
    if (command.batchId.empty() || !isReliefEligible(candidate, content))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "profile is not eligible for relief",
            candidate.revision);
    }

    const auto create =
        [&candidate, &content, &command](
            const ItemDefinitionId &id,
            std::uint32_t quantity = 1)
        {
            return createFirstFit(
                candidate,
                content,
                content.item(id),
                quantity,
                command.batchId);
        };

    if (!create(alpha_content::rifle) ||
        !create(alpha_content::chestRig) ||
        !create(alpha_content::magazine) ||
        !create(alpha_content::magazine) ||
        !create(alpha_content::ammunition, 60) ||
        !create(alpha_content::medkit))
    {
        return failure(
            DomainErrorCode::Capacity,
            "Stash cannot hold the relief batch",
            candidate.revision);
    }

    return EconomyReceipt{
        true, false, DomainErrorCode::None, {}, candidate.revision, 0};
}

EconomyReceipt apply(
    ProfileState &candidate,
    const ContentRegistry &content,
    const EconomyCommand &command)
{
    return std::visit(
        [&candidate, &content](const auto &typed)
        {
            using Command = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Command, PurchaseCommand>)
            {
                return applyPurchase(candidate, content, typed);
            }
            else if constexpr (std::is_same_v<Command, RecycleCommand>)
            {
                return applyRecycle(candidate, content, typed);
            }
            else
            {
                return applyRelief(candidate, content, typed);
            }
        },
        command);
}
}

bool hasMinimumRaidCapability(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    try
    {
        const RaidCapabilityInventory inventory =
            raidCapabilityInventory(profile, content);
        return inventory.weapon && inventory.magazine &&
               inventory.ammunition >= 30;
    }
    catch (...)
    {
        return false;
    }
}

bool isReliefEligible(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    if (hasMinimumRaidCapability(profile, content))
    {
        return false;
    }

    try
    {
        const RaidCapabilityInventory inventory =
            raidCapabilityInventory(profile, content);

        std::uint64_t required{};
        if (!inventory.weapon)
        {
            required += content.item(alpha_content::rifle).marketBuyPrice;
        }
        if (!inventory.magazine)
        {
            required += content.item(alpha_content::magazine).marketBuyPrice;
        }
        if (inventory.ammunition < 30)
        {
            required += (30 - inventory.ammunition) *
                content.item(alpha_content::ammunition).marketBuyPrice;
        }
        return required > profile.currency;
    }
    catch (...)
    {
        return false;
    }
}

EconomyReceipt executeEconomy(
    ProfileState &profile,
    const ContentRegistry &content,
    const EconomyCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty",
            profile.revision);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return EconomyReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision, 0};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale",
            profile.revision);
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance",
            profile.revision);
    }

    ProfileState candidate = profile;
    EconomyReceipt receipt = apply(candidate, content, command);
    if (!receipt.succeeded)
    {
        receipt.revision = profile.revision;
        return receipt;
    }
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return failure(
            DomainErrorCode::InvalidProfile,
            validation.message,
            profile.revision);
    }
    profile = std::move(candidate);
    receipt.revision = profile.revision;
    return receipt;
}
