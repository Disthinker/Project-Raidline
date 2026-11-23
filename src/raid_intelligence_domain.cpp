#include "raid_intelligence_domain.h"
#include "alpha_content_ids.h"

#include <algorithm>
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

bool isCarriedFieldDocument(const ProfileState &profile, AssetInstanceId id)
{
    const auto *asset = profile.assets.find(id);
    return asset && asset->definitionId == kFieldIntelligenceDocumentId &&
        asset->quantity == 1U && !asset->reliefBatchId &&
        std::holds_alternative<StoredAssetLocation>(asset->location) &&
        assetIsCarried(profile, id);
}

const RaidFieldIntelligenceTrade *fieldTrade(const ProfileState &profile)
{
    if (!profile.pendingRaid ||
        profile.pendingRaid->mapDefinitionId != kFieldIntelligenceSourceMapId ||
        !profile.pendingRaid->fieldIntelligenceTrade)
        return nullptr;
    return &*profile.pendingRaid->fieldIntelligenceTrade;
}

struct FieldBarterOffer
{
    ItemDefinitionId input;
    ItemDefinitionId output;
    std::uint32_t quantity;
};

const std::array<FieldBarterOffer, 2> kFieldBarterOffers{{
    {alpha_content::lootElectronics, ItemDefinitionId{"item.ammunition.5_45x39_enhanced"}, 12U},
    {alpha_content::lootScrap, alpha_content::noiseDecoy, 1U}
}};
}

RaidFieldCacheReadPlan queryRaidFieldCacheClue(const ProfileState &profile, AssetInstanceId documentId)
{
    RaidFieldCacheReadPlan plan{false, "FIELD CACHE CLUE UNAVAILABLE", profile.revision};
    if (!fieldTrade(profile) || !profile.pendingRaid->fieldClueCache ||
        profile.revision == std::numeric_limits<ProfileRevision>::max() ||
        !isCarriedFieldDocument(profile, documentId))
        return plan;
    const auto &raid = *profile.pendingRaid;
    const auto &cache = *raid.fieldClueCache;
    if (cache.decodedDocumentId)
        return plan;
    const bool pickedUpHere = std::any_of(raid.loot.begin(), raid.loot.end(), [&](const RaidLootSnapshot &loot) {
        return loot.assetId == documentId && loot.definitionId == kFieldIntelligenceDocumentId &&
            !loot.sceneContainerAssetId && !loot.corpseContainerAssetId && loot.collected;
    });
    const bool cacheUnopened = std::any_of(raid.loot.begin(), raid.loot.end(), [&](const RaidLootSnapshot &loot) {
        return loot.assetId == cache.rewardAssetId && !loot.collected;
    });
    if (!pickedUpHere || !cacheUnopened)
        return plan;
    return {true, {}, profile.revision};
}

bool executeRaidFieldCacheClue(ProfileState &profile, AssetInstanceId documentId, ProfileRevision expectedRevision)
{
    if (expectedRevision != profile.revision || !queryRaidFieldCacheClue(profile, documentId).canCommit)
        return false;
    if (!profile.assets.erase(documentId))
        return false;
    profile.pendingRaid->fieldClueCache->decodedDocumentId = documentId;
    ++profile.revision;
    return true;
}

RaidFieldBarterPlan queryRaidFieldBarter(
    const ProfileState &profile, const ContentRegistry &content, std::size_t index,
    std::optional<AssetInstanceId> selected)
{
    RaidFieldBarterPlan plan;
    plan.revision = profile.revision;
    if (index >= kFieldBarterOffers.size())
    {
        plan.message = "field trader is unavailable";
        return plan;
    }
    const auto &offer = kFieldBarterOffers[index];
    plan.inputDefinitionId = offer.input;
    plan.outputDefinitionId = offer.output;
    plan.outputQuantity = offer.quantity;
    const auto *trade = fieldTrade(profile);
    if (!trade || !supportsFieldBarterContent(content.contentVersion()) ||
        profile.revision == std::numeric_limits<ProfileRevision>::max() ||
        profile.assets.nextAssetId() == std::numeric_limits<AssetInstanceId>::max())
    {
        plan.message = "field trader is unavailable";
        return plan;
    }
    plan.exchanged = trade->bartered[index];
    if (plan.exchanged)
    {
        plan.message = "barter already claimed this Raid";
        return plan;
    }
    const auto &output = content.item(offer.output);
    // Both published outputs occupy one cell. Check the released material's own
    // carried grid, including its pocket rules, before taking anything.
    for (const auto &[id, asset] : profile.assets.records())
    {
        if ((selected && id != *selected) || asset.definitionId != offer.input ||
            asset.quantity != 1U || asset.reliefBatchId || !assetIsCarried(profile, id))
            continue;
        const auto *source = std::get_if<StoredAssetLocation>(&asset.location);
        if (!source) continue;
        plan.inputAssetId = id;
        if (profilePlacementFits(profile, content, source->container, source->origin,
                output, ItemOrientation::Degrees0, id))
        {
            plan.destination = *source;
            plan.canCommit = true;
            return plan;
        }
    }
    plan.message = plan.inputAssetId ? "no carried space for barter output" : "carry the required barter material";
    return plan;
}

bool executeRaidFieldBarter(
    ProfileState &profile, const ContentRegistry &content, std::size_t index,
    AssetInstanceId input, ProfileRevision expectedRevision)
{
    if (expectedRevision != profile.revision)
        return false;
    const auto plan = queryRaidFieldBarter(profile, content, index, input);
    if (!plan.canCommit)
        return false;
    ProfileState candidate = profile;
    if (!candidate.assets.erase(input))
        return false;
    static_cast<void>(candidate.assets.create(content.item(plan.outputDefinitionId),
        *plan.destination, plan.outputQuantity));
    candidate.pendingRaid->fieldIntelligenceTrade->bartered[index] = true;
    ++candidate.revision;
    if (!validateProfileState(candidate, content).valid)
        return false;
    profile = std::move(candidate);
    return true;
}

std::optional<RaidFieldTradeView> queryRaidFieldTrade(
    const ProfileState &profile, const ContentRegistry &content)
{
    const auto *trade = fieldTrade(profile);
    if (!trade)
        return std::nullopt;
    const auto &intelligence = profile.pendingRaid->intelligence;
    const auto &riverside = content.map(kFieldIntelligenceSourceMapId).operationBriefing;
    const auto &frontier = content.map(kFieldIntelligenceTargetMapId).operationBriefing;
    const auto transport = RaidIntelligenceCategory::Transport;
    RaidFieldTradeView view{
        trade->balance,
        trade->fileSold,
        std::nullopt,
        {{
            {kFieldIntelligenceSourceMapId, transport, riverside.price(transport),
                intelligence.has(transport)},
            {kFieldIntelligenceTargetMapId, transport, frontier.price(transport),
                trade->frontierTransportPurchased},
            {kFieldIntelligenceSourceMapId, trade->extraCategory,
                riverside.price(trade->extraCategory), intelligence.has(trade->extraCategory)}
        }}
    };
    if (!trade->fileSold)
    {
        const auto &records = profile.assets.records();
        const auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) {
            return isCarriedFieldDocument(profile, entry.first);
        });
        if (found != records.end())
            view.sellableFile = found->first;
    }
    return view;
}

bool executeRaidFieldDocumentSale(ProfileState &profile, AssetInstanceId id)
{
    const auto *trade = fieldTrade(profile);
    if (!trade || trade->fileSold || !isCarriedFieldDocument(profile, id) ||
        profile.revision == std::numeric_limits<ProfileRevision>::max() ||
        profile.currency > std::numeric_limits<std::uint32_t>::max() - kFieldIntelligenceSalePrice)
        return false;

    // The definition is a single childless document. Erasing its stable ID
    // leaves its collected loot snapshot as the original deployment record.
    if (!profile.assets.erase(id))
        return false;
    auto &committed = *profile.pendingRaid->fieldIntelligenceTrade;
    committed.fileSold = true;
    committed.balance = kFieldIntelligenceSalePrice;
    ++profile.revision;
    return true;
}

bool executeRaidFieldIntelligencePurchase(
    ProfileState &profile, const ContentRegistry &content, std::size_t offerIndex)
{
    const auto view = queryRaidFieldTrade(profile, content);
    if (!view || offerIndex >= view->offers.size() ||
        profile.revision == std::numeric_limits<ProfileRevision>::max())
        return false;
    const auto &offer = view->offers[offerIndex];
    if (offer.purchased || offer.price == 0U || offer.price > view->balance)
        return false;

    auto &raid = *profile.pendingRaid;
    auto &trade = *raid.fieldIntelligenceTrade;
    if (offer.mapId == kFieldIntelligenceTargetMapId)
    {
        if (profile.raidIntelligence.count(offer.mapId, offer.category) >=
            kRaidIntelligenceArchiveLimit)
            return false;
        trade.frontierTransportPurchased = true;
    }
    else
    {
        raid.intelligence.set(offer.category, true);
        if (offer.category == RaidIntelligenceCategory::Transport)
            trade.transportPurchased = true;
        else
            trade.extraPurchased = true;
    }
    trade.balance -= offer.price;
    ++profile.revision;
    return true;
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
    catch (const std::exception &)
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
    if (owned >= kRaidIntelligenceArchiveLimit)
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

RaidInteriorIntelligencePurchasePlan queryRaidInteriorIntelligencePurchase(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidInteriorIntelligencePurchaseCommand &command)
{
    const auto failure = [&](DomainErrorCode error, std::string message)
    {
        return RaidInteriorIntelligencePurchasePlan{
            false, error, std::move(message), profile.revision, 0U};
    };
    if (profile.pendingRaid.has_value())
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "interior intelligence cannot be purchased during a Raid");
    }
    const RaidInteriorDefinition *interior{};
    try
    {
        interior = &content.raidInterior(command.interiorId);
    }
    catch (const std::exception &)
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "interior intelligence location does not exist");
    }
    if (profile.raidInteriorIntelligence.knows(command.interiorId))
    {
        return failure(
            DomainErrorCode::IllegalDestination,
            "interior layout is already permanently known");
    }
    if (profile.currency < interior->intelligencePrice)
    {
        return RaidInteriorIntelligencePurchasePlan{
            false,
            DomainErrorCode::InvalidQuantity,
            "currency is insufficient for interior intelligence",
            profile.revision,
            interior->intelligencePrice};
    }
    return RaidInteriorIntelligencePurchasePlan{
        true,
        DomainErrorCode::None,
        {},
        profile.revision,
        interior->intelligencePrice};
}

RaidInteriorIntelligencePurchaseReceipt executeRaidInteriorIntelligencePurchase(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidInteriorIntelligencePurchaseCommand &command,
    const CommandContext &context)
{
    const auto failure = [&](DomainErrorCode error, std::string message)
    {
        return RaidInteriorIntelligencePurchaseReceipt{
            false,
            false,
            error,
            std::move(message),
            profile.revision,
            0U};
    };
    if (context.transactionId.empty())
    {
        return failure(
            DomainErrorCode::InvalidTransaction,
            "transaction ID must not be empty");
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return RaidInteriorIntelligencePurchaseReceipt{
            true,
            true,
            DomainErrorCode::None,
            {},
            profile.revision,
            0U};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(
            DomainErrorCode::StaleRevision,
            "profile revision is stale");
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(
            DomainErrorCode::RevisionOverflow,
            "profile revision cannot advance");
    }

    const RaidInteriorIntelligencePurchasePlan plan =
        queryRaidInteriorIntelligencePurchase(profile, content, command);
    if (!plan.canCommit)
    {
        return failure(plan.error, plan.message);
    }

    ProfileState candidate = profile;
    candidate.currency -= plan.price;
    candidate.raidInteriorIntelligence.knownLayouts.insert(command.interiorId);
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return failure(DomainErrorCode::InvalidProfile, validation.message);
    }

    profile = std::move(candidate);
    return RaidInteriorIntelligencePurchaseReceipt{
        true,
        false,
        DomainErrorCode::None,
        {},
        profile.revision,
        plan.price};
}
