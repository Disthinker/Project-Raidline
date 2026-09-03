#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "alpha_content_ids.h"
#include "base_wish_expedition.h"
#include "game_session.h"

namespace {
const auto &content = publishedContentRegistry();
ProfileState fresh() { return makeNewAlphaProfile("wish-expedition", content); }
BaseWishInstanceId firstWish(const ProfileState &profile) {
    return {profile.basePriority.cycleIndex, profile.basePriority.wishes.front().definitionId};
}
void focusFirst(ProfileState &profile) {
    const auto receipt = executeBaseWishFocus(profile, content, firstWish(profile),
        {profile.revision, "focus:first"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}
AssetInstanceId add(ProfileState &profile, const ItemDefinitionId &id,
    ProfileContainerId container = ProfileContainerId::stash(), std::uint32_t quantity = 1U) {
    const auto &definition = content.item(id);
    const auto fit = findFirstProfileFit(profile, content, container, definition, ItemOrientation::Degrees0);
    if (!fit) { ADD_FAILURE() << "test inventory has no fit"; return 0; }
    return profile.assets.create(definition, StoredAssetLocation{container, *fit}, quantity);
}
ItemDefinitionId usefulItem(BaseSupplyCategory category) {
    switch (category) {
    case BaseSupplyCategory::Food: return ItemDefinitionId{"item.loot.canned_meal"};
    case BaseSupplyCategory::Medical: return ItemDefinitionId{"item.loot.first_aid_stock"};
    case BaseSupplyCategory::Recreation: return ItemDefinitionId{"item.loot.compact_game_set"};
    default: return ItemDefinitionId{"item.loot.precision_components"};
    }
}
AssetInstanceId equipBackpack(ProfileState &profile) {
    auto found = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
    const auto id = found->first;
    EXPECT_TRUE(executeInventory(profile, content, InventoryEquipCommand{id, EquipmentSlotKind::Backpack},
        {profile.revision, "equip:bag"}).succeeded);
    return id;
}
DeployReceipt deploy(ProfileState &profile, bool frontier = false) {
    return executeDeploy(profile, content,
        {"wish-raid", "wish-settlement", 73419U, MapDefinitionId{frontier ? "map.raid.frontier_exchange" : "map.v0.test"}},
        {profile.revision, "wish:deploy"});
}
SaveLoadResult roundTrip(const ProfileState &profile, unsigned schema = 44U) {
    return deserializeProfileEnvelope(serializeProfileEnvelope(profile, content.contentVersion(), schema), content);
}
}

TEST(BaseWishExpeditionTest, SelectionCancellationStaleAndDuplicateAreAtomic) {
    auto profile = fresh();
    const auto before = profileStateFingerprint(profile);
    EXPECT_TRUE(queryBaseWishFocus(profile, firstWish(profile)).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_FALSE(executeBaseWishFocus(profile, content, firstWish(profile), {profile.revision + 1U, "stale"}).succeeded);
    EXPECT_FALSE(executeBaseWishFocus(profile, content, firstWish(profile), {profile.revision, ""}).succeeded);
    auto wrong = firstWish(profile); ++wrong.cycleIndex;
    EXPECT_FALSE(executeBaseWishFocus(profile, content, wrong, {profile.revision, "wrong"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    focusFirst(profile);
    const auto focused = profileStateFingerprint(profile);
    EXPECT_TRUE(executeBaseWishFocus(profile, content, std::nullopt, {0, "focus:first"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), focused);
    EXPECT_TRUE(executeBaseWishFocus(profile, content, std::nullopt, {profile.revision, "cancel"}).succeeded);
    EXPECT_FALSE(profile.basePriority.focus);
}

TEST(BaseWishExpeditionTest, CompletionAndCycleRolloverClearOnlyFocus) {
    auto profile = fresh(); focusFirst(profile);
    const auto id = *profile.basePriority.focus;
    const auto &wish = content.basePriority(id.definitionId);
    const auto item = usefulItem(wish.category);
    std::vector<AssetInstanceId> ids;
    for (unsigned total{}; total < wish.requiredContribution; total += baseSupplyContribution(content.item(item), wish.category))
        ids.push_back(add(profile, item));
    ASSERT_TRUE(executeBasePrioritySubmission(profile, content, {id.definitionId, ids},
        {profile.revision, "complete"}).succeeded);
    EXPECT_FALSE(profile.basePriority.focus);
    EXPECT_FALSE(queryBaseWishFocus(profile, id).canCommit);
    profile = fresh(); focusFirst(profile);
    const auto assetCount = profile.assets.records().size();
    profile.worldClock.elapsedWorldMinutes += content.basePriorityCycleMinutes();
    EXPECT_TRUE(synchronizeBasePriorityThrough(profile, content).changed);
    EXPECT_FALSE(profile.basePriority.focus);
    EXPECT_EQ(profile.assets.records().size(), assetCount);
}

TEST(BaseWishExpeditionTest, UnknownIsPermissionGatedAndQueryDoesNotMutate) {
    auto profile = fresh(); focusFirst(profile);
    for (const auto &map : content.maps()) {
        const auto before = profileStateFingerprint(profile);
        const auto result = projectBaseWishExpeditionRelevance(profile, content, map.id);
        EXPECT_EQ(result.relevance, BaseWishRelevance::Unknown);
        EXPECT_FALSE(result.informed);
        EXPECT_EQ(profileStateFingerprint(profile), before);
        profile.raidIntelligence.counts[map.id][raidIntelligenceCategoryIndex(RaidIntelligenceCategory::Resource)] = 1U;
        const auto informedBefore = profileStateFingerprint(profile);
        EXPECT_TRUE(projectBaseWishExpeditionRelevance(profile, content, map.id).informed);
        EXPECT_EQ(profileStateFingerprint(profile), informedBefore);
    }
}

TEST(BaseWishExpeditionTest, SourceWeightBandsIncludeNoKnownSource) {
    auto profile = fresh(); focusFirst(profile);
    const auto category = content.basePriority(firstWish(profile).definitionId).category;
    const auto item = usefulItem(category);
    const MapDefinitionId mapId{"map.v0.test"};
    profile.raidIntelligence.counts[mapId][1] = 1U;
    for (const auto &[weight, expected] : std::vector<std::pair<unsigned, BaseWishRelevance>>{
        {0, BaseWishRelevance::None}, {10, BaseWishRelevance::Low},
        {30, BaseWishRelevance::Medium}, {80, BaseWishRelevance::High}}) {
        auto json = nlohmann::json::parse(publishedContentJson());
        // Use every existing table so public map/interior/high-risk edges are all exercised.
        for (auto &table : json.at("loot_tables")) {
            table["entries"] = nlohmann::json::array();
            if (weight) table["entries"].push_back({{"item", item.value()}, {"weight", weight},
                {"minimum_quantity", 1}, {"maximum_quantity", 1}});
            table["entries"].push_back({{"item", alpha_content::rifle.value()}, {"weight", 100U-weight},
                {"minimum_quantity", 1}, {"maximum_quantity", 1}});
        }
        const auto fixture = ContentRegistry::fromJson(json.dump());
        EXPECT_EQ(projectBaseWishExpeditionRelevance(profile, fixture, mapId).relevance, expected);
    }
}

TEST(BaseWishExpeditionTest, SameSeedFocusDoesNotChangeWorldLootOrEnemies) {
    auto plain = fresh(); auto focused = plain; focusFirst(focused);
    ASSERT_TRUE(deploy(plain, true).succeeded);
    const auto receipt = deploy(focused, true); ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(focused.pendingRaid->wishFocus);
    auto normalized = focused;
    normalized.revision = plain.revision;
    normalized.committedTransactions = plain.committedTransactions;
    normalized.basePriority.focus.reset();
    normalized.pendingRaid->wishFocus.reset();
    normalized.pendingRaid->travel.startingBasePriority.focus.reset();
    EXPECT_EQ(serializeProfileEnvelope(normalized, content.contentVersion()),
        serializeProfileEnvelope(plain, content.contentVersion()));
    const auto before = profileStateFingerprint(focused);
    EXPECT_FALSE(executeBaseWishFocus(focused, content, std::nullopt, {focused.revision, "mid-raid"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(focused), before);
}

TEST(BaseWishExpeditionTest, TacticalHintRequiresFrozenResourcePermissionAndIgnoresActualLoot) {
    auto profile = fresh(); focusFirst(profile); ASSERT_TRUE(deploy(profile, true).succeeded);
    auto &raid = *profile.pendingRaid;
    EXPECT_TRUE(projectBaseWishResourceHints(raid, content).empty());
    raid.intelligence.set(RaidIntelligenceCategory::Resource, true);
    const auto hints = projectBaseWishResourceHints(raid, content);
    ASSERT_FALSE(hints.empty());
    raid.loot.clear();
    EXPECT_EQ(projectBaseWishResourceHints(raid, content), hints);
    raid.wishFocus.reset();
    EXPECT_TRUE(projectBaseWishResourceHints(raid, content).empty());
}

TEST(BaseWishExpeditionTest, ReturnCountsCarriedStacksOnceAndNeverCountsStash) {
    auto profile = fresh(); focusFirst(profile); const auto frozen = *freezeBaseWishFocus(profile, content);
    const auto bag = equipBackpack(profile);
    const auto item = usefulItem(frozen.category);
    const auto carried = add(profile, item, ProfileContainerId::compartment(bag, 0), 2U);
    add(profile, item);
    const auto before = profileStateFingerprint(profile);
    const auto result = summarizeBaseWishReturn(profile, content, frozen, true);
    EXPECT_EQ(result.itemCount, 2U);
    EXPECT_EQ(result.contribution, 2U * baseSupplyContribution(content.item(item), frozen.category));
    EXPECT_FALSE(result.expired);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_NE(profile.assets.find(carried), nullptr);
    EXPECT_EQ(summarizeBaseWishReturn(profile, content, frozen, false).contribution, 0U);
}

TEST(BaseWishExpeditionTest, ExtractedReturnPersistsAndDuplicateSettlementIsNoOp) {
    auto profile = fresh(); focusFirst(profile);
    const auto frozen = *freezeBaseWishFocus(profile, content);
    const auto bag = equipBackpack(profile);
    const auto item = add(profile, usefulItem(frozen.category), ProfileContainerId::compartment(bag, 0));
    const auto location = profile.assets.find(item)->location;
    ASSERT_TRUE(deploy(profile).succeeded);
    auto loaded = roundTrip(profile); ASSERT_TRUE(loaded.profile) << loaded.message;
    profile = *loaded.profile;
    const auto receipt = settlePendingRaid(profile, content, "wish-settlement", RaidResultOutcome::Extracted);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.lastRaidResult->wishReturn);
    EXPECT_EQ(profile.lastRaidResult->wishReturn->itemCount, 1U);
    EXPECT_EQ(profile.assets.find(item)->location, location);
    const auto hash = profileStateFingerprint(profile);
    EXPECT_TRUE(settlePendingRaid(profile, content, "wish-settlement", RaidResultOutcome::Extracted).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), hash);
    loaded = roundTrip(profile); ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), hash);
}

TEST(BaseWishExpeditionTest, FailureReturnsZeroAndExpiryDoesNotDeleteCarriedItems) {
    for (auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit}) {
        auto profile = fresh(); focusFirst(profile); ASSERT_TRUE(deploy(profile).succeeded);
        ASSERT_TRUE(settlePendingRaid(profile, content, "wish-settlement", outcome).succeeded);
        ASSERT_TRUE(profile.lastRaidResult->wishReturn);
        EXPECT_EQ(profile.lastRaidResult->wishReturn->contribution, 0U);
        EXPECT_EQ(profile.lastRaidResult->wishReturn->itemCount, 0U);
        EXPECT_TRUE(roundTrip(profile).profile);
    }
    auto profile = fresh(); focusFirst(profile);
    const auto bag = equipBackpack(profile);
    const auto item = add(profile, usefulItem(content.basePriority(firstWish(profile).definitionId).category),
        ProfileContainerId::compartment(bag, 0));
    ASSERT_TRUE(deploy(profile).succeeded);
    profile.worldClock.elapsedWorldMinutes += content.basePriorityCycleMinutes();
    static_cast<void>(synchronizeBasePriorityThrough(profile, content));
    ASSERT_TRUE(settlePendingRaid(profile, content, "wish-settlement", RaidResultOutcome::Extracted).succeeded);
    EXPECT_TRUE(profile.lastRaidResult->wishReturn->expired);
    EXPECT_NE(profile.assets.find(item), nullptr);
}

TEST(BaseWishExpeditionTest, Schema44RoundTripsAnd43MigratesWithoutInventedFocus) {
    auto profile = fresh(); focusFirst(profile);
    auto loaded = roundTrip(profile); ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(*loaded.profile));
    auto legacy = roundTrip(profile, 43U); ASSERT_TRUE(legacy.profile) << legacy.message;
    EXPECT_FALSE(legacy.profile->basePriority.focus);
    ASSERT_TRUE(deploy(profile).succeeded);
    loaded = roundTrip(profile); ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(*loaded.profile));
    legacy = roundTrip(profile, 43U); ASSERT_TRUE(legacy.profile) << legacy.message;
    EXPECT_FALSE(legacy.profile->pendingRaid->wishFocus);
    EXPECT_FALSE(legacy.profile->pendingRaid->travel.startingBasePriority.focus);
    const auto originalFocus = profile.pendingRaid->travel.startingBasePriority.focus;
    ASSERT_TRUE(rollbackPendingRaidToBase(*loaded.profile, content).succeeded);
    EXPECT_EQ(loaded.profile->basePriority.focus, originalFocus);
}

TEST(BaseWishExpeditionTest, InvalidFocusAndFrozenReferencesAreRejected) {
    auto profile = fresh(); focusFirst(profile);
    ++profile.basePriority.focus->cycleIndex;
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    EXPECT_FALSE(roundTrip(profile).profile);
    profile = fresh(); focusFirst(profile); ASSERT_TRUE(deploy(profile).succeeded);
    profile.pendingRaid->wishFocus->wish.definitionId = BasePriorityDefinitionId{"base_priority.unknown"};
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    EXPECT_FALSE(roundTrip(profile).profile);
}

TEST(BaseWishExpeditionTest, SaveFailureLeavesSessionUnchangedAndFocusSurvivesReopen) {
    const auto path = std::filesystem::temp_directory_path() / ("raidline-wish-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{path};
    GameSession session; session.configurePersistence(path);
    ASSERT_TRUE(session.startNewProfile("wish-save"));
    const auto focus = firstWish(session.profile());
    ASSERT_TRUE(session.executeBaseWishFocus(focus, "save-focus").succeeded);
    GameSession reopened; reopened.configurePersistence(path); ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(reopened.profile().basePriority.focus, std::optional<BaseWishInstanceId>{focus});
    const auto before = profileStateFingerprint(session.profile());
    const auto blocked = path / "not-a-directory";
    { std::ofstream file(blocked); file << "blocked"; }
    session.configurePersistence(blocked);
    EXPECT_FALSE(session.executeBaseWishFocus(std::nullopt, "save-fails").succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}
