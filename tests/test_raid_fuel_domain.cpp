#include "raid_lifecycle.h"
#include "raid_optics_legacy_fixture.h"
#include "raid_visibility_fixture.h"

#include <algorithm>
#include <set>
#include <gtest/gtest.h>

namespace
{
const auto &content = publishedContentRegistry();
const ItemDefinitionId backpackId{"item.container.backpack_expedition"};

DeployReceipt deploy(ProfileState &profile, const ContentRegistry &registry,
    const MapDefinitionId &map = kFreightCrateMapId, std::uint64_t seed = 1U)
{
    return executeDeploy(profile, registry, {"fuel-raid", "fuel-settlement", seed, map},
        {profile.revision, "deploy-for-fuel"});
}

struct FuelFixture : testing::Test
{
    ProfileState profile = makeNewAlphaProfile("fuel-route-contract", content);
    AssetInstanceId bag{};
    ProfileContainerId carried;

    void SetUp() override
    {
        bag = profile.assets.create(content.item(backpackId),
            EquippedAssetLocation{EquipmentSlotKind::Backpack});
        carried = ProfileContainerId::compartment(bag, 0);
    }

    AssetInstanceId placeFuel(const ProfileContainerId &container)
    {
        const auto &definition = content.item(kRaidFuelCanId);
        const auto cell = findFirstProfileFit(profile, content, container,
            definition, ItemOrientation::Degrees0);
        EXPECT_TRUE(cell);
        return cell ? profile.assets.create(definition, StoredAssetLocation{container, *cell}) : 0;
    }

    RaidSettlementReceipt settle(RaidResultOutcome outcome,
        std::optional<RaidFuelExtractionCompletion> fuel = std::nullopt)
    {
        return settlePendingRaid(profile, content, "fuel-settlement", outcome, fuel);
    }
};
}

TEST(RaidFuelDomainTest, ThirdServiceBayRollIsTheOnlyNewFrontierDrop)
{
    const auto freightContent = contentBeforeFuelExtraction();
    std::set<std::pair<std::string, std::string>> combinations;
    for (const auto seed : {6U, 8U, 9U, 3U, 23U, 12U, 2U, 5U, 1U})
    {
        SCOPED_TRACE(seed);
        auto old = makeNewAlphaProfile("fuel-seed-contract", freightContent);
        auto current = makeNewAlphaProfile("fuel-seed-contract", content);
        const auto oldReceipt = deploy(old, freightContent, kFreightCrateMapId, seed);
        ASSERT_TRUE(oldReceipt.succeeded) << oldReceipt.message;
        const auto receipt = deploy(current, content, kFreightCrateMapId, seed);
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        ASSERT_TRUE(old.pendingRaid && current.pendingRaid);
        const auto &raid = *current.pendingRaid;
        ASSERT_TRUE(raid.highRiskCrisis && raid.sealedFreightCrate && raid.fuelExtractionLootAssetId);
        ASSERT_TRUE(old.pendingRaid->sealedFreightCrate);
        EXPECT_FALSE(old.pendingRaid->fuelExtractionLootAssetId);
        EXPECT_EQ(raid.spatialLayout, old.pendingRaid->spatialLayout);
        EXPECT_EQ(raid.enemies, old.pendingRaid->enemies);
        EXPECT_EQ(raid.highRiskCrisis, old.pendingRaid->highRiskCrisis);
        EXPECT_EQ(current.assets.nextAssetId(), old.assets.nextAssetId());
        EXPECT_EQ(raid.sealedFreightCrate->containerAssetId,
            old.pendingRaid->sealedFreightCrate->containerAssetId);
        combinations.emplace(raid.spawnExtractionPairId, raid.highRiskCrisis->definitionId);

        auto expected = old.pendingRaid->loot;
        const auto source = std::find_if(expected.begin(), expected.end(), [](const auto &loot) {
            return loot.spaceId == kFreightCrateSpaceId && loot.position.x == 830.0F &&
                loot.position.y == 250.0F;
        });
        ASSERT_NE(source, expected.end());
        EXPECT_EQ(source->assetId, *raid.fuelExtractionLootAssetId);
        EXPECT_EQ(source->sceneContainerAssetId, 0U);
        source->definitionId = kRaidFuelCanId;
        source->quantity = 1U;
        EXPECT_EQ(raid.loot, expected);
        EXPECT_TRUE(validateProfileState(old, content).valid);

        auto malformed = current;
        malformed.pendingRaid->fuelExtractionLootAssetId = raid.sealedFreightCrate->containerAssetId;
        EXPECT_FALSE(validateProfileState(malformed, content).valid);
        malformed.pendingRaid->fuelExtractionLootAssetId.reset();
        EXPECT_FALSE(validateProfileState(malformed, content).valid);
    }
    EXPECT_EQ(combinations.size(), 9U);
}

TEST(RaidFuelDomainTest, FuelHasFixedDimensionsAndIsAbsentFromCatalogGrants)
{
    const auto &fuel = content.item(kRaidFuelCanId);
    EXPECT_EQ(fuel.inventoryWidthCells, 2);
    EXPECT_EQ(fuel.inventoryHeightCells, 2);
    EXPECT_EQ(fuel.unitWeightGrams, 3000U);
    EXPECT_EQ(fuel.maxStackSize, 1U);
    EXPECT_FALSE(fuel.marketBuyPrice);
    EXPECT_FALSE(fuel.marketRecyclePrice);
    auto profile = makeNewPublishedProfile("fuel-catalog-contract", content);
    ASSERT_TRUE(grantDeveloperWarehouseCatalog(profile, content).succeeded);
    EXPECT_TRUE(std::none_of(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == kRaidFuelCanId; }));
}

TEST_F(FuelFixture, OnlyBoundCarriedFuelIsPaidAndSettlementIsIdempotent)
{
    const auto stored = placeFuel(ProfileContainerId::stash());
    const auto carriedFirst = placeFuel(carried);
    const auto bound = placeFuel(carried);
    ASSERT_TRUE(stored && carriedFirst && bound);
    const auto receipt = deploy(profile, content);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.pendingRaid->fuelExtractionLootAssetId);
    const auto uncollected = *profile.pendingRaid->fuelExtractionLootAssetId;
    EXPECT_FALSE(isCarriedRaidFuel(profile, uncollected));
    EXPECT_FALSE(isCarriedRaidFuel(profile, stored));
    EXPECT_EQ(findCarriedRaidFuel(profile), carriedFirst);

    const auto before = profileStateFingerprint(profile);
    for (const auto rejected : {RaidFuelExtractionCompletion{"other-raid", bound},
             RaidFuelExtractionCompletion{"fuel-raid", stored},
             RaidFuelExtractionCompletion{"fuel-raid", uncollected},
             RaidFuelExtractionCompletion{"fuel-raid", bag}})
    {
        EXPECT_FALSE(settle(RaidResultOutcome::Extracted, rejected).succeeded);
        EXPECT_EQ(profileStateFingerprint(profile), before);
    }
    const RaidFuelExtractionCompletion completion{"fuel-raid", bound};
    EXPECT_FALSE(settle(RaidResultOutcome::PlayerDead, completion).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const auto paid = settle(RaidResultOutcome::Extracted, completion);
    ASSERT_TRUE(paid.succeeded) << paid.message;
    EXPECT_FALSE(profile.assets.find(bound));
    EXPECT_FALSE(profile.assets.find(uncollected));
    EXPECT_TRUE(profile.assets.find(stored));
    EXPECT_TRUE(isCarriedRaidFuel(profile, carriedFirst));
    ASSERT_TRUE(profile.lastRaidResult);
    EXPECT_TRUE(profile.lastRaidResult->fuelExtractionPaid);
    const auto &returned = profile.lastRaidResult->returnedItemDefinitionIds;
    EXPECT_EQ(std::count(returned.begin(), returned.end(), kRaidFuelCanId), 1);
    const auto paidState = profileStateFingerprint(profile);
    EXPECT_TRUE(settle(RaidResultOutcome::Extracted, completion).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), paidState);
}

TEST_F(FuelFixture, OtherMapCannotChargeEvenWithCarriedFuel)
{
    const auto fuel = placeFuel(carried);
    ASSERT_NE(fuel, 0U);
    const auto receipt = deploy(profile, content, kFreightKeySourceMapId);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_FALSE(profile.pendingRaid->fuelExtractionLootAssetId);

    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(settle(RaidResultOutcome::Extracted, RaidFuelExtractionCompletion{"fuel-raid", fuel}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    ASSERT_TRUE(settle(RaidResultOutcome::Extracted).succeeded);
    EXPECT_TRUE(isCarriedRaidFuel(profile, fuel));
    ASSERT_TRUE(profile.lastRaidResult);
    EXPECT_FALSE(profile.lastRaidResult->fuelExtractionPaid);
}

TEST_F(FuelFixture, FoundFuelUsesNormalCarryLossAndRecovery)
{
    const auto receipt = deploy(profile, content);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_TRUE(profile.pendingRaid->fuelExtractionLootAssetId);
    const auto fuel = *profile.pendingRaid->fuelExtractionLootAssetId;
    ASSERT_TRUE(pickupObservedRaidLoot(profile, content, fuel, {profile.revision, "take-field-fuel"}).succeeded);
    ASSERT_TRUE(isCarriedRaidFuel(profile, fuel));

    auto restored = profile;
    const auto rollback = rollbackPendingRaidToBase(restored, content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    EXPECT_FALSE(restored.pendingRaid);
    EXPECT_FALSE(restored.assets.find(fuel));

    auto extracted = profile;
    const auto freeExit = settlePendingRaid(extracted, content, "fuel-settlement", RaidResultOutcome::Extracted);
    ASSERT_TRUE(freeExit.succeeded) << freeExit.message;
    EXPECT_TRUE(isCarriedRaidFuel(extracted, fuel));
    ASSERT_TRUE(extracted.lastRaidResult);
    EXPECT_FALSE(extracted.lastRaidResult->fuelExtractionPaid);

    const auto death = settle(RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(death.succeeded) << death.message;
    ASSERT_TRUE(profile.lastRaidResult && profile.lastRaidResult->lostRaidRecordId);
    EXPECT_FALSE(profile.lastRaidResult->fuelExtractionPaid);
    EXPECT_TRUE(profile.assets.find(fuel));
    EXPECT_FALSE(isCarriedRaidFuel(profile, fuel));

    const auto recovered = executeDeploy(profile, content,
        {"fuel-recovery-raid", "fuel-recovery-result", 1U, kFreightCrateMapId, {},
            profile.lastRaidResult->lostRaidRecordId},
        {profile.revision, "deploy-fuel-recovery"});
    ASSERT_TRUE(recovered.succeeded) << recovered.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->selfRecovery);
    EXPECT_FALSE(profile.pendingRaid->fuelExtractionLootAssetId);
    EXPECT_TRUE(std::none_of(profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(),
        [](const auto &loot) { return loot.definitionId == kRaidFuelCanId; }));
    ASSERT_TRUE(openObservedRaidSelfRecovery(profile, content, {profile.revision, "open-fuel-cache"}).succeeded);
    ASSERT_TRUE(pickupObservedRaidLoot(profile, content, bag, {profile.revision, "recover-fuel-bag"}).succeeded);
    EXPECT_TRUE(isCarriedRaidFuel(profile, fuel));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
