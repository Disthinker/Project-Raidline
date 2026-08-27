#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "lost_raid_domain.h"
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
        { return entry.second.definitionId == definitionId; });
    return found == profile.assets.records().end() ? 0U : found->first;
}

void equipLoadout(ProfileState &profile)
{
    for (const auto &[definitionId, slot, transaction] :
         std::vector<std::tuple<
             ItemDefinitionId, EquipmentSlotKind, std::string>>{
             {alpha_content::rifle,
              EquipmentSlotKind::PrimaryWeapon,
              "lost-equip-rifle"},
             {alpha_content::chestRig,
              EquipmentSlotKind::ChestRig,
              "lost-equip-rig"},
             {alpha_content::backpack,
              EquipmentSlotKind::Backpack,
              "lost-equip-pack"}})
    {
        const InventoryReceipt receipt = executeInventory(
            profile,
            publishedContentRegistry(),
            InventoryEquipCommand{
                firstAsset(profile, definitionId), slot},
            CommandContext{profile.revision, transaction});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
    }
}

DeployReceipt deploy(
    ProfileState &profile,
    std::string suffix,
    std::uint64_t seed)
{
    return executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "lost-raid-" + suffix,
            "lost-settlement-" + suffix,
            seed,
            MapDefinitionId{"map.v0.test"},
            {}},
        CommandContext{profile.revision, "lost-deploy-" + suffix});
}
}

TEST(LostRaidDomainTest,
     FailureMovesCarriedRootsWithoutFlatteningContainerOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "lost-record-tree", publishedContentRegistry());
    equipLoadout(profile);
    const AssetInstanceId backpack = *equippedAsset(
        profile, EquipmentSlotKind::Backpack);
    const AssetInstanceId medkit = firstAsset(
        profile, alpha_content::medkit);
    ASSERT_NE(medkit, 0U);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            medkit,
            0U,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0U),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "lost-pack-medkit"}).succeeded);
    ASSERT_TRUE(deploy(profile, "first", 7001U).succeeded);
    const AssetInstanceId groundLoot =
        profile.pendingRaid->loot.front().assetId;

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "lost-settlement-first",
        RaidResultOutcome::PlayerDead);

    ASSERT_TRUE(settled.succeeded) << settled.message;
    ASSERT_EQ(profile.lostRaidRecords.size(), 1U);
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    ASSERT_TRUE(profile.lastRaidResult->lostRaidRecordId.has_value());
    const std::string recordId =
        *profile.lastRaidResult->lostRaidRecordId;
    EXPECT_EQ(recordId, "lost-settlement-first");
    EXPECT_EQ(profile.assets.find(groundLoot), nullptr);
    EXPECT_FALSE(equippedAsset(
        profile, EquipmentSlotKind::Backpack).has_value());
    const auto *lostBackpack = std::get_if<LostRaidAssetLocation>(
        &profile.assets.find(backpack)->location);
    ASSERT_NE(lostBackpack, nullptr);
    EXPECT_EQ(lostBackpack->recordId, recordId);
    const auto *storedMedkit = std::get_if<StoredAssetLocation>(
        &profile.assets.find(medkit)->location);
    ASSERT_NE(storedMedkit, nullptr);
    EXPECT_EQ(storedMedkit->container.ownerAssetId, backpack);
    EXPECT_EQ(lostRaidRecordForAsset(profile, medkit), recordId);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);

    const auto projections = queryLostRaidRecords(
        profile, publishedContentRegistry());
    ASSERT_EQ(projections.size(), 1U);
    EXPECT_EQ(projections.front().subsequentRaidSettlementCount, 0U);
    EXPECT_EQ(projections.front().retainedSettlementsRemaining, 3U);
    EXPECT_GE(projections.front().assets.size(), 4U);
}

TEST(LostRaidDomainTest,
     ExistingRecordAgesOnTerminalSettlementsAndExpiresOnFourth)
{
    ProfileState profile = makeNewAlphaProfile(
        "lost-record-aging", publishedContentRegistry());
    equipLoadout(profile);
    const AssetInstanceId lostRifle = *equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    ASSERT_TRUE(deploy(profile, "origin", 7100U).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "lost-settlement-origin",
        RaidResultOutcome::ActiveQuit).succeeded);
    ASSERT_NE(profile.assets.find(lostRifle), nullptr);

    for (std::uint32_t index = 1U; index <= 3U; ++index)
    {
        const std::string suffix = "age-" + std::to_string(index);
        ASSERT_TRUE(deploy(profile, suffix, 7100U + index).succeeded);
        const std::string settlement = "lost-settlement-" + suffix;
        const RaidSettlementReceipt settled = settlePendingRaid(
            profile,
            publishedContentRegistry(),
            settlement,
            RaidResultOutcome::Extracted);
        ASSERT_TRUE(settled.succeeded) << settled.message;
        ASSERT_TRUE(profile.lostRaidRecords.contains(
            "lost-settlement-origin"));
        EXPECT_EQ(
            profile.lostRaidRecords.at("lost-settlement-origin")
                .subsequentRaidSettlementCount,
            index);
        EXPECT_NE(profile.assets.find(lostRifle), nullptr);

        const std::uint64_t fingerprint = profileStateFingerprint(profile);
        const RaidSettlementReceipt repeated = settlePendingRaid(
            profile,
            publishedContentRegistry(),
            settlement,
            RaidResultOutcome::Extracted);
        EXPECT_TRUE(repeated.succeeded);
        EXPECT_TRUE(repeated.alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    }

    const LostRaidAgingPreview warning = queryLostRaidAging(profile);
    EXPECT_EQ(warning.recordsExpiringOnNextSettlement, 1U);
    EXPECT_GT(warning.assetInstancesExpiringOnNextSettlement, 0U);

    ASSERT_TRUE(deploy(profile, "expiry", 7200U).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "lost-settlement-expiry",
        RaidResultOutcome::Extracted).succeeded);
    EXPECT_FALSE(profile.lostRaidRecords.contains(
        "lost-settlement-origin"));
    EXPECT_EQ(profile.assets.find(lostRifle), nullptr);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(LostRaidDomainTest,
     GenericInventoryCommandsCannotBypassLostAssetOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "lost-record-inaccessible", publishedContentRegistry());
    equipLoadout(profile);
    const AssetInstanceId lostRifle = *equippedAsset(
        profile, EquipmentSlotKind::PrimaryWeapon);
    ASSERT_TRUE(deploy(profile, "inaccessible", 7250U).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "lost-settlement-inaccessible",
        RaidResultOutcome::PlayerDead).succeeded);
    ASSERT_TRUE(lostRaidRecordForAsset(profile, lostRifle).has_value());

    const std::uint64_t before = profileStateFingerprint(profile);
    const InventoryReceipt equip = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            lostRifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "lost-illegal-equip"});
    EXPECT_FALSE(equip.succeeded);
    EXPECT_EQ(equip.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const InventoryReceipt move = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            lostRifle,
            0U,
            StoredAssetLocation{
                ProfileContainerId::stash(), GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "lost-illegal-move"});
    EXPECT_FALSE(move.succeeded);
    EXPECT_EQ(move.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(LostRaidDomainTest, AbnormalRollbackDoesNotAgeExistingRecord)
{
    ProfileState profile = makeNewAlphaProfile(
        "lost-record-rollback", publishedContentRegistry());
    equipLoadout(profile);
    ASSERT_TRUE(deploy(profile, "origin", 7300U).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "lost-settlement-origin",
        RaidResultOutcome::PlayerDead).succeeded);
    ASSERT_TRUE(deploy(profile, "rollback", 7301U).succeeded);

    const std::uint64_t before = profileStateFingerprint(profile);
    ASSERT_TRUE(rollbackPendingRaidToBase(
        profile, publishedContentRegistry()).succeeded);

    EXPECT_EQ(
        profile.lostRaidRecords.at("lost-settlement-origin")
            .subsequentRaidSettlementCount,
        0U);
    EXPECT_NE(profileStateFingerprint(profile), before);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(LostRaidDomainTest, NakedFailureDoesNotCreateEmptyRecord)
{
    ProfileState profile = makeNewAlphaProfile(
        "lost-record-empty", publishedContentRegistry());
    ASSERT_TRUE(deploy(profile, "empty", 7400U).succeeded);

    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "lost-settlement-empty",
        RaidResultOutcome::PlayerDead).succeeded);

    EXPECT_TRUE(profile.lostRaidRecords.empty());
    ASSERT_TRUE(profile.lastRaidResult.has_value());
    EXPECT_FALSE(profile.lastRaidResult->lostRaidRecordId.has_value());
}
