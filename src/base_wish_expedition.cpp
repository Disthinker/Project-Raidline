#include "base_wish_expedition.h"

#include <algorithm>
#include <limits>
#include <set>

bool isBaseWishActive(const BasePriorityState &state, const BaseWishInstanceId &id) noexcept
{
    return id.cycleIndex == state.cycleIndex && std::any_of(
        state.wishes.begin(), state.wishes.end(), [&id](const auto &wish) {
            return wish.definitionId == id.definitionId && !wish.fulfilled;
        });
}

BaseWishFocusPlan queryBaseWishFocus(const ProfileState &profile,
    const std::optional<BaseWishInstanceId> &focus)
{
    if (profile.pendingRaid)
        return {false, DomainErrorCode::IllegalDestination, "Wish focus can only change in Base"};
    if (focus && !isBaseWishActive(profile.basePriority, *focus))
        return {false, DomainErrorCode::IllegalDestination, "Wish focus is no longer active"};
    return {true, DomainErrorCode::None, {}};
}

BasePriorityReceipt executeBaseWishFocus(ProfileState &profile, const ContentRegistry &content,
    const std::optional<BaseWishInstanceId> &focus, const CommandContext &context)
{
    BasePriorityReceipt receipt;
    receipt.revision = profile.revision;
    if (context.transactionId.empty())
    {
        receipt.error = DomainErrorCode::InvalidTransaction;
        receipt.message = "transaction ID must not be empty";
        return receipt;
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        receipt.succeeded = receipt.alreadyCommitted = true;
        return receipt;
    }
    if (context.expectedRevision != profile.revision)
    {
        receipt.error = DomainErrorCode::StaleRevision;
        receipt.message = "profile revision is stale";
        return receipt;
    }
    const auto plan = queryBaseWishFocus(profile, focus);
    if (!plan.canCommit)
    {
        receipt.error = plan.error;
        receipt.message = plan.message;
        return receipt;
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        receipt.error = DomainErrorCode::RevisionOverflow;
        receipt.message = "profile revision cannot advance";
        return receipt;
    }
    ProfileState candidate = profile;
    candidate.basePriority.focus = focus;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto valid = validateProfileState(candidate, content);
    if (!valid.valid)
    {
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = valid.message;
        return receipt;
    }
    profile = std::move(candidate);
    receipt.succeeded = true;
    receipt.revision = profile.revision;
    return receipt;
}

std::optional<BaseWishExpeditionSnapshot> freezeBaseWishFocus(
    const ProfileState &profile, const ContentRegistry &content)
{
    if (!profile.basePriority.focus ||
        !isBaseWishActive(profile.basePriority, *profile.basePriority.focus))
        return std::nullopt;
    const auto &definition = content.basePriority(profile.basePriority.focus->definitionId);
    return BaseWishExpeditionSnapshot{*profile.basePriority.focus, definition.category,
        definition.requiredContribution, "wish-assessment-v1:" + content.contentVersion()};
}

bool validBaseWishSnapshot(const BaseWishExpeditionSnapshot &snapshot,
    const ContentRegistry &content) noexcept
{
    try
    {
        const auto &definition = content.basePriority(snapshot.wish.definitionId);
        if (snapshot.assessmentVersion == "wish-assessment-v1:" + content.contentVersion() &&
            (snapshot.category != definition.category ||
             snapshot.requiredContribution != definition.requiredContribution)) return false;
        return static_cast<unsigned>(snapshot.category) <= static_cast<unsigned>(BaseSupplyCategory::Security) &&
            snapshot.requiredContribution > 0U && snapshot.requiredContribution <= 100000U &&
            snapshot.assessmentVersion.starts_with("wish-assessment-v1:") &&
            snapshot.assessmentVersion.size() <= 160U;
    }
    catch (...) { return false; }
}

bool itemContributesToBaseWish(const ContentRegistry &content,
    const ItemDefinitionId &id, BaseSupplyCategory category)
{
    return baseSupplyContribution(content.item(id), category) > 0U;
}

BaseWishExpeditionRelevanceProjection projectBaseWishExpeditionRelevance(
    const ProfileState &profile, const ContentRegistry &content, const MapDefinitionId &mapId)
{
    BaseWishExpeditionRelevanceProjection result{mapId};
    if (!profile.basePriority.focus || !isBaseWishActive(profile.basePriority, *profile.basePriority.focus))
    {
        result.reason = "No active wish focus";
        return result;
    }
    // Permission is checked before following the content graph. Unknown carries no hidden answer.
    if (profile.raidIntelligence.count(mapId, RaidIntelligenceCategory::Resource) == 0U)
    {
        result.reason = "Resource relevance unknown - acquire supply intelligence";
        return result;
    }
    const auto &map = content.map(mapId);
    const auto category = content.basePriority(profile.basePriority.focus->definitionId).category;
    std::set<LootTableDefinitionId> tables;
    if (map.proceduralOutdoor.enabled && !map.proceduralOutdoor.resourcePointArchetypes.empty())
    {
        for (const auto &point : map.proceduralOutdoor.resourcePointArchetypes)
            tables.insert(point.lootTableId);
    }
    else tables.insert(map.raidLootTableId);
    if (map.highRisk.enabled)
    {
        tables.insert(map.highRisk.advancedLootTableId);
        for (const auto &crisis : map.highRisk.crises)
            tables.insert(crisis.advancedLootTableId);
    }
    for (const auto &interior : map.interiors) tables.insert(interior.lootTableId);
    // Each published source contributes equally; this is relevance, not a predicted drop count.
    std::uint64_t score{};
    std::uint64_t sourceCount{};
    for (const auto &tableId : tables)
    {
        const auto &table = content.lootTable(tableId);
        std::uint64_t total{}, matching{};
        for (const auto &entry : table.entries)
        {
            total += entry.weight;
            if (itemContributesToBaseWish(content, entry.itemDefinitionId, category)) matching += entry.weight;
        }
        if (total > 0U) { score += matching * 10000U / total; ++sourceCount; }
    }
    const auto ratio = sourceCount == 0U ? 0U : score / sourceCount;
    result.informed = true;
    result.relevance = ratio >= 5000U ? BaseWishRelevance::High : ratio >= 2000U ? BaseWishRelevance::Medium :
        score > 0U ? BaseWishRelevance::Low : BaseWishRelevance::None;
    result.reason = "Published supply sources only - no guaranteed drops";
    return result;
}

BaseWishReturnSummary summarizeBaseWishReturn(const ProfileState &profile,
    const ContentRegistry &content, const BaseWishExpeditionSnapshot &focus, bool extracted)
{
    BaseWishReturnSummary result{focus};
    result.expired = !isBaseWishActive(profile.basePriority, focus.wish);
    if (!extracted) return result;
    // Registry traversal, not recursive container traversal: every unique instance is counted once.
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (!assetIsCarried(profile, id)) continue;
        const auto unit = baseSupplyContribution(content.item(asset.definitionId), focus.category);
        if (unit == 0U) continue;
        result.itemCount += asset.quantity;
        result.contribution += static_cast<std::uint64_t>(unit) * asset.quantity;
    }
    return result;
}

const char *baseWishRelevanceName(BaseWishRelevance relevance) noexcept
{
    switch (relevance)
    {
    case BaseWishRelevance::High: return "WISH RELEVANCE: HIGH";
    case BaseWishRelevance::Medium: return "WISH RELEVANCE: MEDIUM";
    case BaseWishRelevance::Low: return "WISH RELEVANCE: LOW";
    case BaseWishRelevance::None: return "WISH RELEVANCE: NO KNOWN SOURCE";
    default: return "WISH RELEVANCE: UNKNOWN";
    }
}

std::vector<std::string> projectBaseWishResourceHints(
    const PendingRaidSnapshot &raid, const ContentRegistry &content)
{
    std::vector<std::string> result;
    if (!raid.wishFocus || !raid.intelligence.has(RaidIntelligenceCategory::Resource))
        return result;
    for (const auto &point : raid.spatialLayout.resourcePoints)
    {
        const auto &table = content.lootTable(point.lootTableId);
        if (std::any_of(table.entries.begin(), table.entries.end(), [&](const auto &entry) {
            return entry.weight > 0U && itemContributesToBaseWish(
                content, entry.itemDefinitionId, raid.wishFocus->category);
        })) result.push_back(point.instanceId);
    }
    return result;
}
