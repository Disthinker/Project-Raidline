#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "lost_raid_domain.h"
#include "raid_lifecycle.h"
#include "recovery_task_domain.h"
#include "self_recovery_domain.h"

namespace
{
AssetInstanceId firstAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const auto found = std::find_if(
        profile.assets.records().begin(), profile.assets.records().end(),
        [&](const auto &entry)
        { return entry.second.definitionId == definitionId; });
    return found == profile.assets.records().end() ? 0U : found->first;
}

std::string createLoss(
    ProfileState &profile,
    std::string_view suffix,
    MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"})
{
    for (const auto &[definitionId, slot] :
         std::vector<std::pair<ItemDefinitionId, EquipmentSlotKind>>{
             {alpha_content::rifle, EquipmentSlotKind::PrimaryWeapon},
             {alpha_content::backpack, EquipmentSlotKind::Backpack}})
    {
        const InventoryReceipt equipped = executeInventory(
            profile, publishedContentRegistry(),
            InventoryEquipCommand{firstAsset(profile, definitionId), slot},
            CommandContext{profile.revision,
                "self-loss-equip-" + std::string{suffix} + "-" +
                    std::string{definitionId.value()}});
        EXPECT_TRUE(equipped.succeeded) << equipped.message;
    }
    const std::string settlement = "self-loss-settlement-" +
        std::string{suffix};
    const DeployReceipt deployed = executeDeploy(
        profile, publishedContentRegistry(),
        DeployCommand{
            "self-loss-raid-" + std::string{suffix}, settlement, 7301U,
            mapDefinitionId, {}, std::nullopt},
        CommandContext{profile.revision,
            "self-loss-deploy-" + std::string{suffix}});
    EXPECT_TRUE(deployed.succeeded) << deployed.message;
    const RaidSettlementReceipt settled = settlePendingRaid(
        profile, publishedContentRegistry(), settlement,
        RaidResultOutcome::PlayerDead);
    EXPECT_TRUE(settled.succeeded) << settled.message;
    return settlement;
}

DeployReceipt deployRecovery(
    ProfileState &profile,
    const std::string &record,
    MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"})
{
    return executeDeploy(
        profile, publishedContentRegistry(),
        DeployCommand{
            "self-recovery-raid", "self-recovery-settlement", 9403U,
            mapDefinitionId, {}, record},
        CommandContext{profile.revision, "self-recovery-deploy"});
}
}

TEST(SelfRecoveryDomainTest,
     DeployFreezesCacheWithoutDuplicatingLostOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-freeze", publishedContentRegistry());
    const std::string recordId = createLoss(profile, "freeze");
    const std::uint64_t nextId = profile.assets.nextAssetId();

    const DeployReceipt deployed = deployRecovery(profile, recordId);

    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->selfRecovery.has_value());
    const RaidSelfRecoverySnapshot &recovery =
        *profile.pendingRaid->selfRecovery;
    EXPECT_FALSE(recovery.opened);
    EXPECT_EQ(recovery.sourceRecord.recordId, recordId);
    EXPECT_TRUE(profile.lostRaidRecords.contains(recordId));
    EXPECT_EQ(profile.assets.nextAssetId(), nextId + profile.pendingRaid->loot.size());
    for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
    {
        const auto *lost = std::get_if<LostRaidAssetLocation>(
            &profile.assets.find(root.assetId)->location);
        ASSERT_NE(lost, nullptr);
        EXPECT_EQ(lost->recordId, recordId);
        EXPECT_TRUE(std::none_of(
            profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(),
            [&](const RaidLootSnapshot &loot)
            { return loot.assetId == root.assetId; }));
    }
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}

TEST(SelfRecoveryDomainTest,
     FrontierRecoveryCacheUsesItsFrozenGeneratedAnchor)
{
    const MapDefinitionId frontier{"map.raid.frontier_exchange"};
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-frontier-anchor", publishedContentRegistry());
    const std::string recordId = createLoss(
        profile, "frontier-anchor", frontier);

    const DeployReceipt deployed = deployRecovery(
        profile, recordId, frontier);

    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_TRUE(profile.pendingRaid->selfRecovery.has_value());
    const RaidAnchorPlacementSnapshot *anchor = findRaidAnchorPlacement(
        profile.pendingRaid->spatialLayout,
        kRaidAnchorSelfRecovery);
    ASSERT_NE(anchor, nullptr);
    const Vec2 expected{
        anchor->bounds.position.x + anchor->bounds.size.x * 0.5F,
        anchor->bounds.position.y + anchor->bounds.size.y * 0.5F};
    EXPECT_FLOAT_EQ(
        profile.pendingRaid->selfRecovery->cachePosition.x,
        expected.x);
    EXPECT_FLOAT_EQ(
        profile.pendingRaid->selfRecovery->cachePosition.y,
        expected.y);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(SelfRecoveryDomainTest,
     OpenAtomicallyMovesWholeRecordToGroundAndReplayDoesNotDuplicate)
{
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-open", publishedContentRegistry());
    const std::string recordId = createLoss(profile, "open");
    ASSERT_TRUE(deployRecovery(profile, recordId).succeeded);
    const std::size_t roots = profile.pendingRaid->selfRecovery->roots.size();

    const InventoryReceipt opened = executeOpenRaidSelfRecovery(
        profile, publishedContentRegistry(),
        CommandContext{profile.revision, "self-recovery-open"});

    ASSERT_TRUE(opened.succeeded) << opened.message;
    EXPECT_FALSE(profile.lostRaidRecords.contains(recordId));
    EXPECT_TRUE(profile.pendingRaid->selfRecovery->opened);
    EXPECT_EQ(std::count_if(
        profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(),
        [&](const RaidLootSnapshot &loot)
        {
            return std::any_of(
                profile.pendingRaid->selfRecovery->roots.begin(),
                profile.pendingRaid->selfRecovery->roots.end(),
                [&](const RaidSelfRecoveryRootSnapshot &root)
                { return root.assetId == loot.assetId; });
        }), static_cast<std::ptrdiff_t>(roots));
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const InventoryReceipt replay = executeOpenRaidSelfRecovery(
        profile, publishedContentRegistry(),
        CommandContext{profile.revision, "self-recovery-open"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(SelfRecoveryDomainTest,
     SuccessfulExtractionKeepsPickedRootAndDeletesUnclaimedAssetTrees)
{
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-success", publishedContentRegistry());
    const std::string recordId = createLoss(profile, "success");
    ASSERT_TRUE(deployRecovery(profile, recordId).succeeded);
    ASSERT_TRUE(executeOpenRaidSelfRecovery(
        profile, publishedContentRegistry(),
        CommandContext{profile.revision, "self-recovery-success-open"})
                    .succeeded);
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId backpack = firstAsset(profile, alpha_content::backpack);
    ASSERT_TRUE(pickupRaidLoot(
        profile, publishedContentRegistry(), rifle,
        CommandContext{profile.revision, "self-recovery-success-pick"})
                    .succeeded);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile, publishedContentRegistry(), "self-recovery-settlement",
        RaidResultOutcome::Extracted);

    ASSERT_TRUE(settled.succeeded) << settled.message;
    ASSERT_NE(profile.assets.find(rifle), nullptr);
    EXPECT_TRUE(std::holds_alternative<EquippedAssetLocation>(
        profile.assets.find(rifle)->location));
    EXPECT_EQ(profile.assets.find(backpack), nullptr);
    EXPECT_FALSE(profile.lostRaidRecords.contains(recordId));
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}

TEST(SelfRecoveryDomainTest,
     FailedRecoveryCreatesNewLossForPickedGearAndRollbackRestoresOldRecord)
{
    ProfileState failed = makeNewAlphaProfile(
        "self-recovery-fail", publishedContentRegistry());
    const std::string oldRecord = createLoss(failed, "fail");
    ASSERT_TRUE(deployRecovery(failed, oldRecord).succeeded);
    ASSERT_TRUE(executeOpenRaidSelfRecovery(
        failed, publishedContentRegistry(),
        CommandContext{failed.revision, "self-recovery-fail-open"})
                    .succeeded);
    const AssetInstanceId rifle = firstAsset(failed, alpha_content::rifle);
    ASSERT_TRUE(pickupRaidLoot(
        failed, publishedContentRegistry(), rifle,
        CommandContext{failed.revision, "self-recovery-fail-pick"})
                    .succeeded);
    ASSERT_TRUE(settlePendingRaid(
        failed, publishedContentRegistry(), "self-recovery-settlement",
        RaidResultOutcome::PlayerDead).succeeded);
    EXPECT_FALSE(failed.lostRaidRecords.contains(oldRecord));
    ASSERT_TRUE(failed.lostRaidRecords.contains("self-recovery-settlement"));
    EXPECT_EQ(lostRaidRecordForAsset(failed, rifle),
              std::optional<std::string>{"self-recovery-settlement"});

    ProfileState rolledBack = makeNewAlphaProfile(
        "self-recovery-rollback", publishedContentRegistry());
    const std::string restoredRecord = createLoss(rolledBack, "rollback");
    ASSERT_TRUE(deployRecovery(rolledBack, restoredRecord).succeeded);
    ASSERT_TRUE(executeOpenRaidSelfRecovery(
        rolledBack, publishedContentRegistry(),
        CommandContext{rolledBack.revision, "self-recovery-rollback-open"})
                    .succeeded);
    ASSERT_TRUE(rollbackPendingRaidToBase(
        rolledBack, publishedContentRegistry()).succeeded);
    EXPECT_TRUE(rolledBack.lostRaidRecords.contains(restoredRecord));
    EXPECT_FALSE(rolledBack.pendingRaid.has_value());
    EXPECT_TRUE(validateProfileState(
        rolledBack, publishedContentRegistry()).valid);
}

TEST(SelfRecoveryDomainTest,
     WrongMapSelectionRejectsWithoutChangingProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-wrong-map", publishedContentRegistry());
    const std::string recordId = createLoss(profile, "wrong-map");
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const DeployReceipt receipt = executeDeploy(
        profile, publishedContentRegistry(),
        DeployCommand{
            "wrong-map-raid", "wrong-map-settlement", 9123U,
            MapDefinitionId{"map.raid.riverside"}, {}, recordId},
        CommandContext{profile.revision, "wrong-map-deploy"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(SelfRecoveryDomainTest,
     LeavingCacheUnopenedKeepsAndAgesOriginalLostRecord)
{
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-unopened", publishedContentRegistry());
    const std::string recordId = createLoss(profile, "unopened");
    ASSERT_TRUE(deployRecovery(profile, recordId).succeeded);

    const RaidSettlementReceipt settled = settlePendingRaid(
        profile, publishedContentRegistry(), "self-recovery-settlement",
        RaidResultOutcome::Extracted);

    ASSERT_TRUE(settled.succeeded) << settled.message;
    ASSERT_TRUE(profile.lostRaidRecords.contains(recordId));
    EXPECT_EQ(
        profile.lostRaidRecords.at(recordId).subsequentRaidSettlementCount,
        1U);
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}

TEST(SelfRecoveryDomainTest,
     NpcTaskAndDifferentSelfRecoveryRecordCanCoexist)
{
    ProfileState profile = makeNewAlphaProfile(
        "self-recovery-coexist", publishedContentRegistry());
    profile.currency = 1000U;
    const std::string npcRecord = createLoss(profile, "npc-record");

    const EconomyReceipt purchased = executeEconomy(
        profile, publishedContentRegistry(),
        PurchaseCommand{alpha_content::rifle, 1U},
        CommandContext{profile.revision, "self-recovery-buy-second-rifle"});
    ASSERT_TRUE(purchased.succeeded) << purchased.message;
    const auto availableRifle = std::find_if(
        profile.assets.records().begin(), profile.assets.records().end(),
        [&](const auto &entry)
        {
            return entry.second.definitionId == alpha_content::rifle &&
                std::holds_alternative<StoredAssetLocation>(
                    entry.second.location);
        });
    ASSERT_NE(availableRifle, profile.assets.records().end());
    ASSERT_TRUE(executeInventory(
        profile, publishedContentRegistry(),
        InventoryEquipCommand{
            availableRifle->first, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "self-recovery-equip-second-rifle"})
                    .succeeded);
    const std::string selfRecord = "self-loss-settlement-self-record";
    ASSERT_TRUE(executeDeploy(
        profile, publishedContentRegistry(),
        DeployCommand{
            "self-loss-raid-self-record", selfRecord, 7311U,
            MapDefinitionId{"map.v0.test"}, {}, std::nullopt},
        CommandContext{profile.revision, "self-loss-deploy-self-record"})
                    .succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile, publishedContentRegistry(), selfRecord,
        RaidResultOutcome::PlayerDead).succeeded);

    const RecoveryTaskReceipt task = executeStartRecoveryTask(
        profile, publishedContentRegistry(), npcRecord,
        CommandContext{profile.revision, "self-recovery-start-npc-task"});
    ASSERT_TRUE(task.succeeded) << task.message;
    const DeployReceipt deployed = executeDeploy(
        profile, publishedContentRegistry(),
        DeployCommand{
            "self-recovery-coexist-raid", "self-recovery-coexist-settlement",
            9411U, MapDefinitionId{"map.v0.test"}, {}, selfRecord},
        CommandContext{profile.revision, "self-recovery-coexist-deploy"});

    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.recoveryTask.has_value());
    EXPECT_EQ(profile.recoveryTask->sourceRecord.recordId, npcRecord);
    ASSERT_TRUE(profile.pendingRaid->selfRecovery.has_value());
    EXPECT_EQ(profile.pendingRaid->selfRecovery->sourceRecord.recordId,
              selfRecord);
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}
