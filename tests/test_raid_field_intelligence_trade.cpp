#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <set>

#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "game_session.h"
#include "profile_ui_test_wait.h"
#include "raid_intelligence_domain.h"
#include "raid_lifecycle.h"
#include "raid_visibility_fixture.h"
#include "save_repository.h"
#include "test_temp_directory.h"
#include <nlohmann/json.hpp>

namespace
{
const ContentRegistry &current = publishedContentRegistry();
constexpr auto transport = RaidIntelligenceCategory::Transport;

ProfileState preparedProfile(std::string id)
{
    auto profile = makeNewAlphaProfile(std::move(id), current);
    static_cast<void>(profile.assets.create(current.item(alpha_content::backpack),
        EquippedAssetLocation{EquipmentSlotKind::Backpack}));
    return profile;
}

DeployReceipt deploy(
    ProfileState &profile, const ContentRegistry &content,
    std::uint64_t seed, const MapDefinitionId &map = kFieldIntelligenceSourceMapId,
    RaidIntelligenceLoadout intelligence = {})
{
    return executeDeploy(profile, content,
        {"trade-raid", "trade-return", seed, map, intelligence},
        {profile.revision, "trade-deploy"});
}

AssetInstanceId documentId(const ProfileState &profile)
{
    const auto &loot = profile.pendingRaid->loot;
    const auto document = std::find_if(loot.begin(), loot.end(), [](const auto &item) {
        return item.definitionId == kFieldIntelligenceDocumentId;
    });
    return document == loot.end() ? AssetInstanceId{} : document->assetId;
}

void pickUpDocument(ProfileState &profile)
{
    const auto id = documentId(profile);
    ASSERT_NE(id, AssetInstanceId{});
    const auto pickup = pickupObservedRaidLoot(profile, current, id, {profile.revision, "collect-file"});
    ASSERT_TRUE(pickup.succeeded) << pickup.message;
    ASSERT_TRUE(assetIsCarried(profile, id));
}

ContentRegistry legacyHospitalContent()
{
    auto json = nlohmann::json::parse(publishedContentJson());
    json["content_version"] = kHospitalMedicalReserveContentVersion;
    for (auto &map : json.at("maps"))
        map.erase("field_intelligence_trade_positions");
    std::erase_if(json.at("items").get_ref<nlohmann::json::array_t &>(), [](const auto &item) {
        return item.at("id") == std::string{kFieldIntelligenceDocumentId.value()};
    });
    return ContentRegistry::fromJson(json.dump());
}
}

TEST(RaidFieldIntelligenceTradeTest, DeployReplacesOneResourceAndKeepsOriginalDraws)
{
    const auto legacy = legacyHospitalContent();
    const auto &map = current.map(kFieldIntelligenceSourceMapId);
    std::set<RaidIntelligenceCategory> extraOffers;
    std::set<std::pair<float, float>> positions;
    for (std::uint64_t seed = 1U; seed <= 32U; ++seed)
    {
        SCOPED_TRACE(seed);
        auto before = makeNewAlphaProfile("trade-layout", legacy);
        auto after = makeNewAlphaProfile("trade-layout", current);
        ASSERT_TRUE(deploy(before, legacy, seed).succeeded);
        ASSERT_TRUE(deploy(after, current, seed).succeeded);
        const auto &oldRaid = *before.pendingRaid;
        const auto &newRaid = *after.pendingRaid;
        ASSERT_TRUE(newRaid.fieldIntelligenceTrade);
        ASSERT_FALSE(oldRaid.fieldIntelligenceTrade);
        EXPECT_EQ(newRaid.enemies, oldRaid.enemies);
        EXPECT_EQ(newRaid.freightKeyLootSlotId, oldRaid.freightKeyLootSlotId);
        ASSERT_EQ(newRaid.loot.size(), oldRaid.loot.size());
        bool replaced{};
        for (std::size_t index{}; index < oldRaid.loot.size(); ++index)
        {
            auto expected = oldRaid.loot[index];
            if (!replaced && expected.slotIndex < map.raidLootSlots.size() &&
                map.raidLootSlots[expected.slotIndex].route == "resource")
            {
                expected.definitionId = kFieldIntelligenceDocumentId;
                expected.quantity = 1U;
                replaced = true;
            }
            EXPECT_EQ(newRaid.loot[index], expected);
        }
        EXPECT_TRUE(replaced);
        const auto &trade = *newRaid.fieldIntelligenceTrade;
        extraOffers.insert(trade.extraCategory);
        positions.emplace(trade.position.x, trade.position.y);
    }
    EXPECT_EQ(extraOffers.size(), 2U);
    EXPECT_EQ(positions.size(), 2U);
}

TEST(RaidFieldIntelligenceTradeTest, OneSaleAndCrossMapPurchaseSettleOnlyOnce)
{
    auto profile = preparedProfile("trade-success");
    const auto initialFunds = profile.currency;
    ASSERT_TRUE(deploy(profile, current, 42U).succeeded);
    const auto id = documentId(profile);
    ASSERT_NE(id, AssetInstanceId{});
    EXPECT_FALSE(executeRaidFieldDocumentSale(profile, id));
    EXPECT_FALSE(executeRaidFieldIntelligencePurchase(profile, current, 1U));
    ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
    const auto before = queryRaidFieldTrade(profile, current);
    ASSERT_TRUE(before);
    EXPECT_EQ(before->sellableFile, id);
    EXPECT_EQ(before->offers[0].price, 36U);
    EXPECT_EQ(before->offers[1].price, 58U);
    ASSERT_TRUE(executeRaidFieldDocumentSale(profile, id));
    EXPECT_FALSE(executeRaidFieldDocumentSale(profile, id));
    EXPECT_EQ(profile.assets.find(id), nullptr);
    ASSERT_TRUE(executeRaidFieldIntelligencePurchase(profile, current, 1U));
    const auto revision = profile.revision;
    EXPECT_FALSE(executeRaidFieldIntelligencePurchase(profile, current, 1U));
    EXPECT_FALSE(executeRaidFieldIntelligencePurchase(profile, current, 0U));
    EXPECT_EQ(profile.revision, revision);
    EXPECT_EQ(profile.pendingRaid->fieldIntelligenceTrade->balance, 14U);
    EXPECT_EQ(profile.currency, initialFunds);
    EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 0U);
    EXPECT_TRUE(validateProfileState(profile, current).valid);
    const auto settled = settlePendingRaid(profile, current, "trade-return", RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(profile.currency, initialFunds + 14U);
    ASSERT_TRUE(profile.lastRaidResult);
    EXPECT_EQ(profile.lastRaidResult->currencyDelta, 14);
    EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 1U);
    const auto repeated = settlePendingRaid(profile, current, "trade-return", RaidResultOutcome::Extracted);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profile.currency, initialFunds + 14U);
    EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 1U);

    RaidIntelligenceLoadout choice;
    choice.set(transport, true);
    const auto next = executeDeploy(profile, current,
        {"frontier-raid", "frontier-return", 51U, kFieldIntelligenceTargetMapId, choice},
        {profile.revision, "frontier-deploy"});
    ASSERT_TRUE(next.succeeded) << next.message;
    EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 0U);
    EXPECT_TRUE(profile.pendingRaid->intelligence.has(transport));
    EXPECT_FALSE(profile.pendingRaid->fieldIntelligenceTrade);
}

TEST(RaidFieldIntelligenceTradeTest, LocalPurchaseAppliesOnlyToItsRaid)
{
    auto profile = preparedProfile("trade-local");
    const auto initialFunds = profile.currency;
    ASSERT_TRUE(deploy(profile, current, 41U).succeeded);
    ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
    ASSERT_TRUE(executeRaidFieldDocumentSale(profile, documentId(profile)));
    ASSERT_TRUE(executeRaidFieldIntelligencePurchase(profile, current, 0U));
    EXPECT_TRUE(profile.pendingRaid->intelligence.has(transport));
    EXPECT_FALSE(executeRaidFieldIntelligencePurchase(profile, current, 0U));
    EXPECT_EQ(profile.pendingRaid->fieldIntelligenceTrade->balance, 36U);
    EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceSourceMapId, transport), 0U);
    const auto settled = settlePendingRaid(profile, current, "trade-return", RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(profile.currency, initialFunds + 36U);
    EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceSourceMapId, transport), 0U);
}

TEST(RaidFieldIntelligenceTradeTest, FailedRaidLosesUnsettledBalanceAndArchiveGrant)
{
    for (const auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        auto profile = preparedProfile("trade-loss");
        const auto initialFunds = profile.currency;
        ASSERT_TRUE(deploy(profile, current, 42U).succeeded);
        ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
        ASSERT_TRUE(executeRaidFieldDocumentSale(profile, documentId(profile)));
        ASSERT_TRUE(executeRaidFieldIntelligencePurchase(profile, current, 1U));
        const auto settled = settlePendingRaid(profile, current, "trade-return", outcome);
        ASSERT_TRUE(settled.succeeded) << settled.message;
        EXPECT_EQ(profile.currency, initialFunds);
        EXPECT_EQ(profile.raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 0U);
        EXPECT_EQ(profile.lastRaidResult->currencyDelta, 0);
    }
}

TEST(RaidFieldIntelligenceTradeTest, PreRaidRecoveryAndCurrentAndLegacySaveRoundTrip)
{
    auto profile = preparedProfile("trade-restore");
    const auto preRaid = serializeProfileEnvelope(profile, current.contentVersion());
    ASSERT_TRUE(deploy(profile, current, 42U).succeeded);
    ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
    ASSERT_TRUE(executeRaidFieldDocumentSale(profile, documentId(profile)));
    ASSERT_TRUE(executeRaidFieldIntelligencePurchase(profile, current, 1U));
    const auto pending = serializeProfileEnvelope(profile, current.contentVersion());
    const auto schema69Trade = serializeProfileEnvelope(profile, kFieldIntelligenceTradeContentVersion);
    const auto migratedTrade = deserializeProfileEnvelope(schema69Trade, current);
    ASSERT_TRUE(migratedTrade.profile && migratedTrade.sourceEnvelope) << migratedTrade.message;
    EXPECT_EQ(migratedTrade.sourceEnvelope->schemaVersion, 69U);
    EXPECT_EQ(serializeProfileEnvelope(*migratedTrade.profile, current.contentVersion()), pending);
    auto loaded = deserializeProfileEnvelope(pending, current);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid);
    EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, current.contentVersion()), pending);
    ASSERT_TRUE(rollbackPendingRaidToBase(*loaded.profile, current).succeeded);
    EXPECT_FALSE(loaded.profile->pendingRaid);
    EXPECT_EQ(loaded.profile->raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 0U);
    const auto restored = deserializeProfileEnvelope(preRaid, current);
    ASSERT_TRUE(restored.profile) << restored.message;
    EXPECT_FALSE(restored.profile->pendingRaid);
    EXPECT_EQ(restored.profile->raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 0U);

    const auto legacy = legacyHospitalContent();
    auto old = makeNewAlphaProfile("trade-old-frozen", legacy);
    ASSERT_TRUE(deploy(old, legacy, 42U).succeeded);
    const auto frozen = serializeProfileEnvelope(old, legacy.contentVersion(), 68U);
    const auto migrated = deserializeProfileEnvelope(frozen, current);
    ASSERT_TRUE(migrated.profile) << migrated.message;
    ASSERT_TRUE(migrated.profile->pendingRaid);
    EXPECT_FALSE(migrated.profile->pendingRaid->fieldIntelligenceTrade);
    EXPECT_EQ(migrated.profile->pendingRaid->loot, old.pendingRaid->loot);
}

TEST(RaidFieldIntelligenceTradeTest, FullBalancesRejectWithoutConsumingFileOrFunds)
{
    auto profile = preparedProfile("trade-limits");
    profile.currency = std::numeric_limits<std::uint32_t>::max() - 71U;
    profile.raidIntelligence.counts[kFieldIntelligenceTargetMapId][raidIntelligenceCategoryIndex(transport)] =
        kRaidIntelligenceArchiveLimit;
    ASSERT_TRUE(deploy(profile, current, 42U).succeeded);
    ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
    const auto id = documentId(profile);
    EXPECT_FALSE(executeRaidFieldDocumentSale(profile, id));
    EXPECT_TRUE(assetIsCarried(profile, id));
    --profile.currency;
    ASSERT_TRUE(executeRaidFieldDocumentSale(profile, id));
    EXPECT_FALSE(executeRaidFieldIntelligencePurchase(profile, current, 1U));
    EXPECT_EQ(profile.pendingRaid->fieldIntelligenceTrade->balance, 72U);
    EXPECT_FALSE(profile.pendingRaid->fieldIntelligenceTrade->frontierTransportPurchased);
}

TEST(RaidFieldIntelligenceTradeTest, ExtractedFileCanBeRecycledFromBaseStash)
{
    auto profile = preparedProfile("trade-base");
    const auto initialFunds = profile.currency;
    ASSERT_TRUE(deploy(profile, current, 42U).succeeded);
    const auto id = documentId(profile);
    ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
    ASSERT_TRUE(settlePendingRaid(profile, current, "trade-return", RaidResultOutcome::Extracted).succeeded);
    const auto stash = ProfileContainerId::stash();
    const auto cell = findFirstProfileFit(profile, current, stash,
        current.item(kFieldIntelligenceDocumentId), ItemOrientation::Degrees0);
    ASSERT_TRUE(cell);
    ASSERT_TRUE(executeInventory(profile, current, InventoryMoveCommand{id, 0U, {stash, *cell}},
        {profile.revision, "file-stash"}).succeeded);
    const auto receipt = executeEconomy(profile, current, RecycleCommand{id},
        {profile.revision, "file-base-recycle"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.currency, initialFunds + 35U);
    EXPECT_EQ(receipt.currencyDelta, 35);
}

TEST(RaidFieldIntelligenceTradeTest, SettlementSaveFailureCanRetryExactlyOnce)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-trade-", "settlement");
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.startNewProfile("trade-save-retry"));
    const auto pack = std::find_if(session.profile().assets.records().begin(),
        session.profile().assets.records().end(), [](const auto &entry) {
            return entry.second.definitionId == alpha_content::backpack;
        });
    ASSERT_NE(pack, session.profile().assets.records().end());
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{pack->first, EquipmentSlotKind::Backpack}, "trade-equip").succeeded);
    const auto initialFunds = session.profile().currency;
    ASSERT_TRUE(session.deployAlpha(42U, kFieldIntelligenceSourceMapId)) << session.persistenceMessage();
    session.update({}, 0.0F);
    auto &world = session.world();
    for (const auto &enemy : world.enemies())
        static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(std::numeric_limits<int>::max()));
    auto &player = const_cast<Player &>(world.player());
    const float half = player.size() * 0.5F;
    const auto id = documentId(session.profile());
    const auto &loot = session.profile().pendingRaid->loot;
    const auto file = std::find_if(loot.begin(), loot.end(), [id](const auto &item) {
        return item.assetId == id;
    });
    ASSERT_NE(file, loot.end());
    const Vec2 lootPosition = file->position;
    ASSERT_TRUE(player.setPosition({lootPosition.x - half, lootPosition.y - half}));
    GameplayInput interact;
    interact.aimWorldPosition = {lootPosition.x + 60.0F, lootPosition.y};
    interact.interactJustPressed = true;
    session.update(interact, 0.0F);
    ASSERT_TRUE(assetIsCarried(session.profile(), id));
    const auto contact = session.profile().pendingRaid->fieldIntelligenceTrade->position;
    ASSERT_TRUE(player.setPosition({contact.x - half, contact.y - half}));
    EXPECT_EQ(session.fieldIntelligenceTradeStatus(), RaidFieldTradeAccess::Available);
    ASSERT_TRUE(session.sellFieldIntelligenceDocument(id));
    ASSERT_TRUE(session.buyFieldIntelligenceOffer(1U));

    const auto blockedTemp = directory.path() / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(blockedTemp));
    ASSERT_TRUE(player.setPosition(world.extractionPoint().bounds().position));
    for (int frame = 0; frame < 45 && !session.profileUiCommitPending(); ++frame)
        session.update({}, 0.1F);
    ASSERT_TRUE(session.profileUiCommitPending());
    EXPECT_FALSE(waitForProfileUiDurability(session));
    ASSERT_TRUE(session.profile().pendingRaid);
    EXPECT_EQ(session.profile().currency, initialFunds);
    EXPECT_EQ(session.profile().raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 0U);
    ASSERT_TRUE(std::filesystem::remove(blockedTemp));
    ASSERT_TRUE(session.retryProfileUiCommit().accepted) << session.persistenceMessage();
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_EQ(session.profile().currency, initialFunds + 14U);
    EXPECT_EQ(session.profile().raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 1U);
    const auto disk = SaveRepository{directory.path()}.load(current);
    ASSERT_TRUE(disk.profile) << disk.message;
    EXPECT_EQ(disk.profile->currency, initialFunds + 14U);
    EXPECT_EQ(disk.profile->raidIntelligence.count(kFieldIntelligenceTargetMapId, transport), 1U);
}
