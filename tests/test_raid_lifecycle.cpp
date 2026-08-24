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
             {alpha_content::pistol, EquipmentSlotKind::Sidearm, "equip-pistol"},
             {alpha_content::helmet, EquipmentSlotKind::Helmet, "equip-helmet"},
             {alpha_content::bodyArmor, EquipmentSlotKind::BodyArmor, "equip-body-armor"},
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

DeployReceipt deploy(
    ProfileState &profile,
    std::uint64_t seed = 9917,
    MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"})
{
    return executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "raid-alpha-test",
            "settlement-alpha-test",
            seed,
            std::move(mapDefinitionId)},
        CommandContext{profile.revision, "deploy-alpha-test"});
}

TEST(RaidLifecycleTest, EveryPublishedRaidMapCreatesItsOwnDeterministicSnapshot)
{
    for (const MapDefinition &map : publishedContentRegistry().maps())
    {
        ProfileState first = makeNewAlphaProfile(
            "multi-map-first", publishedContentRegistry());
        ProfileState second = first;
        second.profileId = "multi-map-second";

        ASSERT_TRUE(deploy(first, 77231, map.id).succeeded) << map.id.value();
        ASSERT_TRUE(deploy(second, 77231, map.id).succeeded) << map.id.value();
        ASSERT_TRUE(first.pendingRaid.has_value());
        ASSERT_TRUE(second.pendingRaid.has_value());
        EXPECT_EQ(first.pendingRaid->mapDefinitionId, map.id);
        EXPECT_EQ(first.pendingRaid->spawnExtractionPairId,
                  second.pendingRaid->spawnExtractionPairId);
        EXPECT_EQ(first.pendingRaid->enemyDeploymentId,
                  second.pendingRaid->enemyDeploymentId);
        ASSERT_EQ(first.pendingRaid->loot.size(), second.pendingRaid->loot.size());
        for (std::size_t index{}; index < first.pendingRaid->loot.size(); ++index)
        {
            EXPECT_EQ(first.pendingRaid->loot[index].assetId,
                      second.pendingRaid->loot[index].assetId);
            EXPECT_EQ(first.pendingRaid->loot[index].slotIndex,
                      second.pendingRaid->loot[index].slotIndex);
            EXPECT_FLOAT_EQ(first.pendingRaid->loot[index].position.x,
                            second.pendingRaid->loot[index].position.x);
            EXPECT_FLOAT_EQ(first.pendingRaid->loot[index].position.y,
                            second.pendingRaid->loot[index].position.y);
        }
    }
}

TEST(RaidLifecycleTest, UnknownRaidMapRejectsWithoutChangingProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "unknown-map", publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(profile);

    const DeployReceipt receipt = deploy(
        profile, 77232, MapDefinitionId{"map.raid.unknown"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, RaidLifecycleError::InvalidCommand);
    EXPECT_EQ(profileStateFingerprint(profile), before);
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
    const auto regularLootCount = [](const PendingRaidSnapshot &raid)
    {
        return static_cast<std::size_t>(
            std::count_if(raid.loot.begin(),
                          raid.loot.end(),
                          [](const RaidLootSnapshot &loot)
                          { return !loot.requiresHighRisk; }));
    };
    EXPECT_GE(regularLootCount(*first.pendingRaid), 6U);
    EXPECT_LE(regularLootCount(*first.pendingRaid), 9U);
    EXPECT_EQ(first.pendingRaid->loot.size() -
                  regularLootCount(*first.pendingRaid),
              2U);
    EXPECT_GE(first.pendingRaid->enemies.size(), 4U);
    EXPECT_LE(first.pendingRaid->enemies.size(), 6U);
    EXPECT_TRUE(validateProfileState(first, publishedContentRegistry()).valid);
}

TEST(RaidLifecycleTest, DeploySnapshotTracksAllThreeWeaponRoots)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-multi-weapon",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const ItemDefinition &rifleDefinition = publishedContentRegistry().item(
        alpha_content::rifle);
    const auto origin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::stash(),
        rifleDefinition,
        ItemOrientation::Degrees0,
        std::nullopt);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId secondRifle = profile.assets.create(
        rifleDefinition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            secondRifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "equip-second-rifle"}).succeeded);
    const AssetInstanceId primary = *equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    const AssetInstanceId sidearm = *equippedAsset(
        profile, EquipmentSlotKind::Sidearm);

    ASSERT_TRUE(deploy(profile, 77125).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const auto &roots = profile.pendingRaid->carriedRootAssetIds;
    EXPECT_NE(std::find(roots.begin(), roots.end(), primary), roots.end());
    EXPECT_NE(std::find(roots.begin(), roots.end(), secondRifle), roots.end());
    EXPECT_NE(std::find(roots.begin(), roots.end(), sidearm), roots.end());

    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::PrimaryWeapon), primary);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::SecondaryWeapon),
        secondRifle);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Sidearm), sidearm);
}

TEST(RaidLifecycleTest, DeployedRootMayMoveWithinCarriedOwnershipTree)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-carried-move",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    const AssetInstanceId rifle = *equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    const AssetInstanceId backpack = *equippedAsset(
        profile, EquipmentSlotKind::Backpack);
    ASSERT_TRUE(deploy(profile, 77126).succeeded);

    const ItemDefinition &rifleDefinition =
        publishedContentRegistry().item(alpha_content::rifle);
    const ProfileContainerId backpackGrid =
        ProfileContainerId::compartment(backpack, 0);
    const auto fit = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        backpackGrid,
        rifleDefinition,
        ItemOrientation::Degrees0,
        rifle);
    ASSERT_TRUE(fit.has_value());

    const InventoryReceipt moved = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            rifle,
            0,
            StoredAssetLocation{backpackGrid, *fit},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "raid-store-equipped-rifle"});
    ASSERT_TRUE(moved.succeeded) << moved.message;
    EXPECT_TRUE(assetIsCarried(profile, rifle));
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);

    const InventoryReceipt reequipped = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            rifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "raid-reequip-rifle"});
    ASSERT_TRUE(reequipped.succeeded) << reequipped.message;
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::SecondaryWeapon),
        rifle);
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
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Heavy, 0, 400, 0, 18000};

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_NE(profile.assets.find(loot), nullptr);
    const auto *returnedLocation = std::get_if<StoredAssetLocation>(
        &profile.assets.find(loot)->location);
    ASSERT_NE(returnedLocation, nullptr);
    EXPECT_EQ(
        returnedLocation->container,
        ProfileContainerId::baseIntake());
    EXPECT_EQ(
        profile.baseResources.pool,
        (BaseResourceBundle{32, 34, 35, 36}));
    EXPECT_EQ(profile.medicalStatus.bleeding, BleedingSeverity::Heavy);
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
    profile.pendingRaid->loot.front().definitionId =
        alpha_content::ammunition;
    profile.pendingRaid->loot.front().quantity = 5;
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

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_EQ(profile.assets.find(ammunition)->quantity, 55U);
    const auto intake = assetsInContainer(
        profile, ProfileContainerId::baseIntake());
    ASSERT_EQ(intake.size(), 1U);
    EXPECT_EQ(intake.front()->definitionId, alpha_content::ammunition);
    EXPECT_EQ(intake.front()->quantity, 5U);
}

TEST(RaidLifecycleTest, DeathRemovesAllRaidAssetsAndResetsHealth)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-failure",
        publishedContentRegistry());
    equipAlphaLoadout(profile);
    profile.currentHealth = 25;
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Light, 22000, 700, 0, 16000};
    ASSERT_TRUE(deploy(profile, 4471).succeeded);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "settlement-alpha-test",
        RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    EXPECT_FALSE(profile.pendingRaid.has_value());
    EXPECT_EQ(profile.currentHealth, 100);
    EXPECT_EQ(profile.medicalStatus, MedicalStatusState{});
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::PrimaryWeapon).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Sidearm).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::ChestRig).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Backpack).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::Helmet).has_value());
    EXPECT_FALSE(equippedAsset(
        profile,
        EquipmentSlotKind::BodyArmor).has_value());
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
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Light, 31000, 900, 0, 17000};
    const MedicalStatusState entryMedical = profile.medicalStatus;
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
    EXPECT_EQ(profile.medicalStatus, entryMedical);
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

TEST(RaidLifecycleTest, PendingAllocationBlocksAnotherDeploy)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-lifecycle-allocation-gate",
        publishedContentRegistry());
    const ItemDefinition &definition = publishedContentRegistry().item(
        alpha_content::lootCola);
    static_cast<void>(profile.assets.create(
        definition,
        StoredAssetLocation{
            ProfileContainerId::baseIntake(), GridPosition{0, 0}},
        1));
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const DeployReceipt receipt = deploy(profile, 9901);

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}
