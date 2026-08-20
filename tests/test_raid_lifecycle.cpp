#include <gtest/gtest.h>

#include <algorithm>
#include <tuple>

#include "alpha_content_ids.h"
#include "raid_lifecycle.h"

namespace
{
AssetInstanceId firstAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const auto found = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&definitionId](const auto &entry)
        {
            return entry.second.definitionId == definitionId;
        });
    return found == profile.assets.records().end() ? 0 : found->first;
}

void equipAlphaLoadout(ProfileState &profile)
{
    for (const auto &[definitionId, slot, transaction] :
         std::vector<std::tuple<ItemDefinitionId, EquipmentSlotKind, std::string>>{
             {alpha_content::rifle, EquipmentSlotKind::PrimaryWeapon, "equip-rifle"},
             {alpha_content::chestRig, EquipmentSlotKind::ChestRig, "equip-chest"},
             {alpha_content::backpack, EquipmentSlotKind::Backpack, "equip-backpack"}})
    {
        const InventoryReceipt receipt = executeInventory(
            profile,
            publishedContentRegistry(),
            InventoryEquipCommand{firstAsset(profile, definitionId), slot},
            CommandContext{profile.revision, transaction});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
    }
}

DeployReceipt deploy(ProfileState &profile, std::uint64_t seed = 9917)
{
    return executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "raid-alpha-test",
            "settlement-alpha-test",
            seed,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "deploy-alpha-test"});
}
}

TEST(RaidLifecycleTest, DeployCreatesDeterministicFiniteSnapshot)
{
    ProfileState first = makeNewAlphaProfile(
        "raid-lifecycle-first",
        publishedContentRegistry());
    ProfileState second = first;
    second.profileId = "raid-lifecycle-second";
    equipAlphaLoadout(first);
    equipAlphaLoadout(second);

    ASSERT_TRUE(deploy(first).succeeded);
    ASSERT_TRUE(deploy(second).succeeded);
    ASSERT_TRUE(first.pendingRaid.has_value());
    ASSERT_TRUE(second.pendingRaid.has_value());
    EXPECT_EQ(first.pendingRaid->spawnExtractionPairId,
              second.pendingRaid->spawnExtractionPairId);
    EXPECT_EQ(first.pendingRaid->enemyDeploymentId,
              second.pendingRaid->enemyDeploymentId);
    EXPECT_EQ(first.pendingRaid->loot.size(), second.pendingRaid->loot.size());
    EXPECT_GE(first.pendingRaid->loot.size(), 6U);
    EXPECT_LE(first.pendingRaid->loot.size(), 9U);
    EXPECT_GE(first.pendingRaid->enemies.size(), 4U);
    EXPECT_LE(first.pendingRaid->enemies.size(), 6U);
    EXPECT_TRUE(validateProfileState(first, publishedContentRegistry()).valid);
}

TEST(RaidLifecycleTest, ExtractionRetainsCarriedAndPickedAssetsExactlyOnce)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-success",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    ASSERT_TRUE(deploy(profile).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const AssetInstanceId loot = profile.pendingRaid->loot.front().assetId;
    ASSERT_TRUE(pickupRaidLoot(
        profile,
        publishedContentRegistry(),
        loot,
        CommandContext{profile.revision, "pickup-first-loot"}).succeeded);
    ASSERT_TRUE(assetIsCarried(profile, loot));

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_NE(profile.assets.find(loot), nullptr);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RaidSettlementReceipt repeated = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RaidLifecycleTest, PickupMayMergeAwayHistoricalSnapshotAsset)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-merge-loot",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const AssetInstanceId backpack = *equippedAsset(
        profile,
        EquipmentSlotKind::Backpack);
    const AssetInstanceId ammunition =
        firstAsset(profile, alpha_content::ammunition);
    profile.assets.findMutable(ammunition)->quantity = 55;
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            ammunition,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "store-ammunition"}).succeeded);
    ASSERT_TRUE(deploy(profile, 8821).succeeded);

    const AssetInstanceId loot = profile.pendingRaid->loot.front().assetId;
    AssetRecord *lootAsset = profile.assets.findMutable(loot);
    ASSERT_NE(lootAsset, nullptr);
    lootAsset->definitionId = alpha_content::ammunition;
    lootAsset->quantity = 5;
    lootAsset->reliefBatchId.reset();
    ASSERT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);

    const InventoryReceipt pickedUp = pickupRaidLoot(
        profile,
        publishedContentRegistry(),
        loot,
        CommandContext{profile.revision, "merge-picked-ammunition"});

    ASSERT_TRUE(pickedUp.succeeded) << pickedUp.message;
    EXPECT_EQ(profile.assets.find(ammunition)->quantity, 60U);
    EXPECT_EQ(profile.assets.find(loot), nullptr);
    EXPECT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);
}

TEST(RaidLifecycleTest, DeathRemovesAllRaidAssetsAndResetsHealth)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-failure",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    profile.currentHealth = 25;
    ASSERT_TRUE(deploy(profile, 4471).succeeded);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.currentHealth, 100);
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::ChestRig).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Backpack).has_value());
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        EXPECT_FALSE(std::holds_alternative<RaidGroundAssetLocation>(asset.location));
    }
}

TEST(RaidLifecycleTest, LegacyPendingRaidRollbackKeepsEntryLoadout)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-rollback",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const auto rifle = equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    ASSERT_TRUE(rifle.has_value());
    profile.currentHealth = 70;
    ASSERT_TRUE(deploy(profile, 4481).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const std::vector<RaidLootSnapshot> generatedLoot =
        profile.pendingRaid->loot;

    const RaidRollbackReceipt rolledBack = rollbackPendingRaidToBase(
        profile,
        publishedContentRegistry());

    ASSERT_TRUE(rolledBack.succeeded) << rolledBack.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.currentHealth, 70);
    EXPECT_EQ(equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon), rifle);
    EXPECT_FALSE(profile.lastRaidResult.has_value());
    for (const RaidLootSnapshot &loot : generatedLoot)
    {
        EXPECT_EQ(profile.assets.find(loot.assetId), nullptr);
    }
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}
