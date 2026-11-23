#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "game_session.h"
#include "profile_ui_test_wait.h"
#include "raid_intelligence_domain.h"
#include "raid_lifecycle.h"
#include "raid_optics_legacy_fixture.h"
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
    const auto tradeStage = contentBeforeRiversideArmedScavengers();
    const auto &map = tradeStage.map(kFieldIntelligenceSourceMapId);
    std::set<RaidIntelligenceCategory> extraOffers;
    std::set<std::pair<float, float>> positions;
    for (std::uint64_t seed = 1U; seed <= 32U; ++seed)
    {
        SCOPED_TRACE(seed);
        auto before = makeNewAlphaProfile("trade-layout", legacy);
        auto after = makeNewAlphaProfile("trade-layout", tradeStage);
        ASSERT_TRUE(deploy(before, legacy, seed).succeeded);
        ASSERT_TRUE(deploy(after, tradeStage, seed).succeeded);
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
    auto prepareTrade = [](ProfileState &state, const ContentRegistry &registry)
    {
        ASSERT_TRUE(deploy(state, registry, 42U).succeeded);
        ASSERT_NO_FATAL_FAILURE(pickUpDocument(state));
        ASSERT_TRUE(executeRaidFieldDocumentSale(state, documentId(state)));
        ASSERT_TRUE(executeRaidFieldIntelligencePurchase(state, registry, 1U));
    };
    auto previous = profile;
    const auto tradeStage = contentBeforeRiversideArmedScavengers();
    ASSERT_NO_FATAL_FAILURE(prepareTrade(previous, tradeStage));
    ASSERT_NO_FATAL_FAILURE(prepareTrade(profile, current));
    const auto pending = serializeProfileEnvelope(profile, current.contentVersion());
    const auto schema69Trade = serializeProfileEnvelope(previous, kFieldIntelligenceTradeContentVersion);
    const auto migratedTrade = deserializeProfileEnvelope(schema69Trade, current);
    ASSERT_TRUE(migratedTrade.profile && migratedTrade.sourceEnvelope) << migratedTrade.message;
    EXPECT_EQ(migratedTrade.sourceEnvelope->schemaVersion, 69U);
    EXPECT_EQ(serializeProfileEnvelope(*migratedTrade.profile, current.contentVersion()),
              serializeProfileEnvelope(previous, current.contentVersion()));
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

namespace
{
const ItemDefinitionId enhancedRounds{"item.ammunition.5_45x39_enhanced"};
const ItemDefinitionId rifleMagazine{"item.magazine.5_45x39_30"};

class RiversideBarterSessionTest : public testing::Test
{
protected:
    raidline::test::TemporaryDirectory directory;
    GameSession session;
    AssetInstanceId magazine{}, rig{}, stashScrap{};
    Vec2 contact{};

    static AssetInstanceId add(ProfileState &profile, const ItemDefinitionId &definitionId, ProfileContainerId container)
    {
        const auto &definition = current.item(definitionId);
        const auto fit = findFirstProfileFit(profile, current, container, definition, ItemOrientation::Degrees0);
        if (!fit)
            throw std::runtime_error("no room for barter fixture material");
        return profile.assets.create(definition, StoredAssetLocation{container, *fit});
    }

    void deployRiverside(bool carryMaterials = true)
    {
        auto profile = preparedProfile("riverside-material-trade");
        profile.tutorial = TutorialProgress::Complete;
        const auto bag = ProfileContainerId::compartment(*equippedAsset(profile, EquipmentSlotKind::Backpack), 0U);
        const auto stash = ProfileContainerId::stash();
        stashScrap = add(profile, alpha_content::lootScrap, stash);
        magazine = add(profile, rifleMagazine, stash);
        const auto found = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
            [](const auto &entry) { return entry.second.definitionId == alpha_content::chestRig; });
        ASSERT_NE(found, profile.assets.records().end());
        rig = found->first;
        if (carryMaterials)
        {
            // The second copy of each input makes the exhausted-offer check independent of stock.
            for (int copy = 0; copy < 2; ++copy)
            {
                add(profile, alpha_content::lootElectronics, bag);
                add(profile, alpha_content::lootScrap, bag);
            }
        }
        directory.claim("raidline-barter-", testing::UnitTest::GetInstance()->current_test_info()->name());
        ASSERT_TRUE(SaveRepository(directory.path()).save(profile, current.contentVersion()).succeeded);
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
        ASSERT_TRUE(session.deployAlpha(42U, kFieldIntelligenceSourceMapId)) << session.persistenceMessage();
        ASSERT_TRUE(session.profile().pendingRaid->fieldIntelligenceTrade);
        contact = session.profile().pendingRaid->fieldIntelligenceTrade->position;
        // Keep live actors available for the counterparty threat check, outside the trade radius.
        for (const auto &enemy : session.world().enemies())
            ASSERT_TRUE(const_cast<Enemy &>(enemy).setPosition({contact.x + 440.0F, contact.y}));
        ASSERT_NO_FATAL_FAILURE(placeAt(contact));
        ASSERT_EQ(session.fieldIntelligenceTradeStatus(), RaidFieldTradeAccess::Available);
    }

    void placeAt(Vec2 center)
    {
        auto &player = const_cast<Player &>(session.world().player());
        const auto half = player.size() * 0.5F;
        ASSERT_TRUE(player.setPosition({center.x - half, center.y - half}));
    }

    static std::vector<AssetInstanceId> carried(const ProfileState &profile, const ItemDefinitionId &type)
    {
        std::vector<AssetInstanceId> result;
        for (const auto &[id, asset] : profile.assets.records())
            if (asset.definitionId == type && assetIsCarried(profile, id))
                result.push_back(id);
        return result;
    }

    void exchange(std::size_t slot, const ItemDefinitionId &inputType, const ItemDefinitionId &outputType,
                  unsigned quantity, AssetInstanceId &received)
    {
        const auto plan = session.queryFieldBarter(slot);
        ASSERT_TRUE(plan.canCommit) << plan.message;
        ASSERT_TRUE(plan.inputAssetId);
        ASSERT_TRUE(plan.destination);
        EXPECT_EQ(plan.inputDefinitionId, inputType);
        EXPECT_EQ(plan.outputDefinitionId, outputType);
        EXPECT_EQ(plan.outputQuantity, quantity);
        ASSERT_TRUE(carried(session.profile(), outputType).empty());
        const auto beforeCurrency = session.profile().currency;
        const auto beforeBalance = session.profile().pendingRaid->fieldIntelligenceTrade->balance;
        const auto revision = session.profile().revision;
        ASSERT_TRUE(session.exchangeFieldBarter(slot, *plan.inputAssetId, plan.revision));
        EXPECT_GT(session.profile().revision, revision);
        EXPECT_EQ(session.profile().assets.find(*plan.inputAssetId), nullptr);
        ASSERT_EQ(carried(session.profile(), inputType).size(), 1U);
        const auto output = carried(session.profile(), outputType);
        ASSERT_EQ(output.size(), 1U);
        received = output.front();
        ASSERT_NE(received, *plan.inputAssetId);
        EXPECT_EQ(session.profile().assets.find(received)->quantity, quantity);
        EXPECT_EQ(std::get<StoredAssetLocation>(session.profile().assets.find(received)->location), *plan.destination);
        EXPECT_EQ(session.profile().currency, beforeCurrency);
        EXPECT_EQ(session.profile().pendingRaid->fieldIntelligenceTrade->balance, beforeBalance);
        EXPECT_TRUE(session.profile().pendingRaid->fieldIntelligenceTrade->bartered[slot]);
        EXPECT_TRUE(session.queryFieldBarter(slot).exchanged);
        const auto state = profileStateFingerprint(session.profile());
        EXPECT_FALSE(session.exchangeFieldBarter(slot, *plan.inputAssetId, plan.revision));
        EXPECT_FALSE(session.exchangeFieldBarter(slot, carried(session.profile(), inputType).front(), session.profile().revision));
        EXPECT_EQ(profileStateFingerprint(session.profile()), state);
    }
};
}

TEST_F(RiversideBarterSessionTest, TradesSurviveOrdinaryExtractionAndSupplyARealMagazineAndNextRaidThrow)
{
    ASSERT_NO_FATAL_FAILURE(deployRiverside());
    const auto previous = session.queryFieldBarter(1U);
    ASSERT_TRUE(previous.canCommit && previous.inputAssetId);
    AssetInstanceId rounds{}, decoy{};
    ASSERT_NO_FATAL_FAILURE(exchange(0U, alpha_content::lootElectronics, enhancedRounds, 12U, rounds));
    const auto changed = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.exchangeFieldBarter(1U, *previous.inputAssetId, previous.revision));
    EXPECT_EQ(profileStateFingerprint(session.profile()), changed);
    ASSERT_NO_FATAL_FAILURE(exchange(1U, alpha_content::lootScrap, alpha_content::noiseDecoy, 1U, decoy));
    const auto oldRoundsLocation = session.profile().assets.find(rounds)->location;
    const auto oldDecoyLocation = session.profile().assets.find(decoy)->location;
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(session.world().extractionPoint().bounds().position));
    for (int frame = 0; frame < 50 && session.profile().pendingRaid; ++frame)
        session.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.profile().assets.find(rounds));
    ASSERT_TRUE(reopened.profile().assets.find(decoy));
    EXPECT_EQ(reopened.profile().assets.find(rounds)->location, oldRoundsLocation);
    EXPECT_EQ(reopened.profile().assets.find(decoy)->location, oldDecoyLocation);
    const auto loaded = reopened.executeProfileWeaponAmmo(LoadMagazineCommand{magazine, rounds, 12U}, "barter-load-magazine");
    ASSERT_TRUE(loaded.succeeded) << loaded.message;
    const auto *filled = reopened.profile().assets.find(magazine);
    ASSERT_NE(filled, nullptr);
    ASSERT_EQ(filled->magazineRounds.size(), 12U);
    EXPECT_TRUE(std::all_of(filled->magazineRounds.begin(), filled->magazineRounds.end(),
        [](const auto &round) { return round.definitionId == enhancedRounds; }));
    ASSERT_TRUE(reopened.executeProfileInventory(InventoryEquipCommand{rig, EquipmentSlotKind::ChestRig},
        "barter-equip-rig").succeeded);
    const auto pocket = ProfileContainerId::compartment(rig, 0);
    const auto fit = findFirstProfileFit(reopened.profile(), current, pocket, current.item(alpha_content::noiseDecoy),
        ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    ASSERT_TRUE(reopened.executeProfileInventory(InventoryMoveCommand{decoy, 0, {pocket, *fit}},
        "barter-carry-decoy").succeeded);
    ASSERT_TRUE(waitForProfileUiDurability(reopened));
    ASSERT_TRUE(reopened.deployAlpha(42U, kFieldIntelligenceTargetMapId)) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.beginNoiseDecoySelection());
    const auto spawn = reopened.world().player().position();
    const auto thrown = reopened.throwNoiseDecoy(decoy, {spawn.x + 220, spawn.y + 20},
        {reopened.profile().revision, "barter-next-raid-throw"});
    ASSERT_TRUE(thrown.succeeded) << thrown.message;
    EXPECT_TRUE(reopened.world().noiseDecoy().flight());
    EXPECT_FALSE(reopened.profile().assets.find(decoy));
}

TEST_F(RiversideBarterSessionTest, StashOnlyStockCannotBeOfferedAtTheContact)
{
    ASSERT_NO_FATAL_FAILURE(deployRiverside(false));
    ASSERT_TRUE(carried(session.profile(), alpha_content::lootScrap).empty());
    ASSERT_TRUE(session.profile().assets.find(stashScrap));
    const auto before = profileStateFingerprint(session.profile());
    const auto plan = session.queryFieldBarter(1U);
    EXPECT_FALSE(plan.canCommit);
    EXPECT_FALSE(session.exchangeFieldBarter(1U, stashScrap, session.profile().revision));
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().pendingRaid->fieldIntelligenceTrade->bartered[1]);
}

TEST_F(RiversideBarterSessionTest, ReadsOnlyTheLocalDocumentAndSearchesTheOriginalCacheIntoAnExtractedPack)
{
    ASSERT_NO_FATAL_FAILURE(deployRiverside(false));
    ASSERT_NE(session.fieldClueCacheLoot(), nullptr);
    const auto reward = *session.fieldClueCacheLoot();
    const auto fileId = documentId(session.profile());
    const auto &frozen = session.profile().pendingRaid->loot;
    const auto it = std::find_if(frozen.begin(), frozen.end(), [&](const auto &loot) { return loot.assetId == fileId; });
    ASSERT_NE(it, frozen.end());
    auto interactAt = [&](Vec2 point, bool press) {
        ASSERT_NO_FATAL_FAILURE(placeAt(point));
        GameplayInput input;
        input.aimWorldPosition = {point.x + 80.0F, point.y};
        input.interactJustPressed = press;
        session.update(input, 0.0F);
    };
    ASSERT_NO_FATAL_FAILURE(interactAt(it->position, true));
    ASSERT_TRUE(assetIsCarried(session.profile(), fileId));
    const auto quoted = session.queryFieldCacheClue(fileId);
    ASSERT_TRUE(quoted.canCommit) << quoted.message;
    const auto before = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.readFieldCacheClue(fileId, quoted.revision - 1));
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    const auto initialCurrency = session.profile().currency;
    ASSERT_TRUE(session.readFieldCacheClue(fileId, quoted.revision));
    EXPECT_EQ(session.profile().pendingRaid->fieldClueCache->decodedDocumentId, fileId);
    EXPECT_EQ(session.profile().assets.find(fileId), nullptr);
    EXPECT_FALSE(session.readFieldCacheClue(fileId, session.profile().revision));
    ASSERT_NO_FATAL_FAILURE(placeAt(contact));
    EXPECT_FALSE(session.sellFieldIntelligenceDocument(fileId));
    EXPECT_EQ(session.profile().pendingRaid->fieldIntelligenceTrade->balance, 0U);
    EXPECT_EQ(session.profile().currency, initialCurrency);

    ASSERT_NO_FATAL_FAILURE(interactAt(reward.position, false));
    ASSERT_TRUE(session.fieldClueCacheInRange());
    const auto count = session.profile().assets.records().size();
    ASSERT_NO_FATAL_FAILURE(interactAt(reward.position, true));
    ASSERT_TRUE(assetIsCarried(session.profile(), reward.assetId)) << session.persistenceMessage();
    ASSERT_TRUE(session.fieldClueCacheLoot()->collected);
    EXPECT_EQ(session.profile().assets.records().size(), count);
    EXPECT_EQ(session.profile().assets.find(reward.assetId)->quantity, reward.quantity);
    EXPECT_FALSE(session.searchFieldCache());

    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(session.world().extractionPoint().bounds().position));
    for (int frame = 0; frame < 50 && session.profile().pendingRaid; ++frame)
        session.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    const auto disk = SaveRepository(directory.path()).load(current);
    ASSERT_TRUE(disk.profile && !disk.profile->pendingRaid) << disk.message;
    ASSERT_TRUE(disk.profile->lastRaidResult);
    EXPECT_EQ(disk.profile->lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_TRUE(assetIsCarried(*disk.profile, reward.assetId));
    EXPECT_EQ(disk.profile->assets.find(reward.assetId)->definitionId, reward.definitionId);
    EXPECT_EQ(disk.profile->assets.find(reward.assetId)->quantity, reward.quantity);
    EXPECT_EQ(disk.profile->assets.find(fileId), nullptr);
}

TEST(RaidFieldIntelligenceTradeTest, ClosedCacheRejectsOrdinaryPickupDirectMoveAndFullBagsUntilSpaceIsFreed)
{
    auto profile = preparedProfile("clue-full-pack");
    const auto bag = ProfileContainerId::compartment(*equippedAsset(profile, EquipmentSlotKind::Backpack), 0U);
    const auto &filler = current.item(alpha_content::ammunition);
    const auto &document = current.item(kFieldIntelligenceDocumentId);
    const auto fileCell = findFirstProfileFit(profile, current, bag, document, ItemOrientation::Degrees0);
    ASSERT_TRUE(fileCell);
    const auto previousFile = profile.assets.create(document, StoredAssetLocation{bag, *fileCell});
    ASSERT_TRUE(deploy(profile, current, 42U).succeeded);
    ASSERT_TRUE(profile.pendingRaid->fieldClueCache);
    ASSERT_NO_FATAL_FAILURE(pickUpDocument(profile));
    const auto fileId = documentId(profile);
    EXPECT_FALSE(queryRaidFieldCacheClue(profile, previousFile).canCommit);
    ASSERT_TRUE(queryRaidFieldCacheClue(profile, fileId).canCommit);
    const auto &raid = *profile.pendingRaid;
    const auto rewardId = raid.fieldClueCache->rewardAssetId;
    const auto reward = std::find_if(raid.loot.begin(), raid.loot.end(), [&](const auto &loot) { return loot.assetId == rewardId; });
    ASSERT_NE(reward, raid.loot.end());
    const auto view = observeAssetContractPoint(profile, reward->spaceId, reward->position);
    const auto key = view.key();
    const auto cell = findFirstProfileFit(profile, current, bag, current.item(reward->definitionId), ItemOrientation::Degrees0);
    ASSERT_TRUE(cell);
    const InventoryMoveCommand bypass{rewardId, 0, {bag, *cell}};
    const auto beforeBypass = profileStateFingerprint(profile);
    EXPECT_FALSE(pickupObservedRaidLoot(profile, current, rewardId, {profile.revision, "ordinary"}).succeeded);
    EXPECT_FALSE(queryInventory(profile, current, bypass).canCommit);
    EXPECT_FALSE(executeInventory(profile, current, bypass, {profile.revision, "direct"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), beforeBypass);

    std::vector<AssetInstanceId> padding;
    while (const auto available = findFirstProfileFit(profile, current, bag, filler, ItemOrientation::Degrees0))
        padding.push_back(profile.assets.create(filler, StoredAssetLocation{bag, *available}));
    ASSERT_FALSE(padding.empty());
    const auto full = profileStateFingerprint(profile);
    EXPECT_EQ(collectRaidFieldCache(profile, current, {profile.revision, "full"}, view, key).error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), full);
    ASSERT_FALSE(reward->collected);
    for (const auto id : padding)
        ASSERT_TRUE(profile.assets.erase(id));
    ASSERT_TRUE(collectRaidFieldCache(profile, current, {profile.revision, "discover"}, view, key).succeeded);
    ASSERT_TRUE(assetIsCarried(profile, rewardId));
    const auto discovered = profileStateFingerprint(profile);
    EXPECT_FALSE(executeRaidFieldCacheClue(profile, fileId, profile.revision));
    EXPECT_FALSE(collectRaidFieldCache(profile, current, {profile.revision, "discover-again"}, view, key).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), discovered);
    EXPECT_TRUE(assetIsCarried(profile, fileId));
    EXPECT_TRUE(assetIsCarried(profile, previousFile));
    ASSERT_TRUE(executeRaidFieldDocumentSale(profile, fileId));
    EXPECT_EQ(profile.pendingRaid->fieldIntelligenceTrade->balance, kFieldIntelligenceSalePrice);

    const auto pending = serializeProfileEnvelope(profile, current.contentVersion());
    auto restored = deserializeProfileEnvelope(pending, current);
    ASSERT_TRUE(restored.profile && restored.profile->pendingRaid) << restored.message;
    ASSERT_TRUE(rollbackPendingRaidToBase(*restored.profile, current).succeeded);
    EXPECT_TRUE(assetIsCarried(*restored.profile, previousFile));
    EXPECT_FALSE(restored.profile->assets.find(fileId));
    EXPECT_FALSE(restored.profile->assets.find(rewardId));
}

TEST_F(RiversideBarterSessionTest, LeavingOrThreateningTheContactInvalidatesAQuoteAndQuitLosesTheGoods)
{
    ASSERT_NO_FATAL_FAILURE(deployRiverside());
    const auto quoted = session.queryFieldBarter(0U);
    ASSERT_TRUE(quoted.canCommit && quoted.inputAssetId);
    const auto before = profileStateFingerprint(session.profile());
    ASSERT_NO_FATAL_FAILURE(placeAt({contact.x - 120.0F, contact.y}));
    ASSERT_EQ(session.fieldIntelligenceTradeStatus(), RaidFieldTradeAccess::OutOfRange);
    EXPECT_FALSE(session.exchangeFieldBarter(0U, *quoted.inputAssetId, quoted.revision));
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    ASSERT_NO_FATAL_FAILURE(placeAt(contact));
    ASSERT_FALSE(session.world().enemies().empty());
    auto &enemy = const_cast<Enemy &>(session.world().enemies().front());
    ASSERT_TRUE(enemy.setPosition({contact.x + 24.0F, contact.y}));
    ASSERT_EQ(session.fieldIntelligenceTradeStatus(), RaidFieldTradeAccess::Threatened);
    EXPECT_FALSE(session.exchangeFieldBarter(0U, *quoted.inputAssetId, quoted.revision));
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    ASSERT_TRUE(enemy.setPosition({contact.x + 440.0F, contact.y}));
    ASSERT_EQ(session.fieldIntelligenceTradeStatus(), RaidFieldTradeAccess::Available);
    AssetInstanceId rounds{}, decoy{};
    ASSERT_NO_FATAL_FAILURE(exchange(0U, alpha_content::lootElectronics, enhancedRounds, 12U, rounds));
    ASSERT_NO_FATAL_FAILURE(exchange(1U, alpha_content::lootScrap, alpha_content::noiseDecoy, 1U, decoy));
    ASSERT_TRUE(session.requestActiveQuitAlphaRaid().accepted) << session.persistenceMessage();
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    const auto disk = SaveRepository(directory.path()).load(current);
    ASSERT_TRUE(disk.profile) << disk.message;
    ASSERT_FALSE(disk.profile->pendingRaid);
    ASSERT_TRUE(disk.profile->lastRaidResult);
    EXPECT_EQ(disk.profile->lastRaidResult->outcome, RaidResultOutcome::ActiveQuit);
    EXPECT_TRUE(disk.profile->lastRaidResult->returnedItemDefinitionIds.empty());
    EXPECT_FALSE(assetIsCarried(*disk.profile, rounds));
    EXPECT_FALSE(assetIsCarried(*disk.profile, decoy));
    EXPECT_TRUE(disk.profile->assets.find(stashScrap));
}
