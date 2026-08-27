#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "raid_lifecycle.h"
#include "recovery_task_domain.h"

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

void equipRecoveryLoadout(ProfileState &profile)
{
    for (const auto &[definitionId, slot, transaction] :
         std::vector<std::tuple<
             ItemDefinitionId, EquipmentSlotKind, std::string>>{
             {alpha_content::rifle,
              EquipmentSlotKind::PrimaryWeapon,
              "recovery-equip-rifle"},
             {alpha_content::chestRig,
              EquipmentSlotKind::ChestRig,
              "recovery-equip-rig"},
             {alpha_content::backpack,
              EquipmentSlotKind::Backpack,
              "recovery-equip-pack"}})
    {
        ASSERT_TRUE(executeInventory(
            profile,
            publishedContentRegistry(),
            InventoryEquipCommand{
                firstAsset(profile, definitionId), slot},
            CommandContext{profile.revision, transaction}).succeeded);
    }
}

std::string createLostRecord(ProfileState &profile, std::string suffix)
{
    equipRecoveryLoadout(profile);
    const std::string settlement = "recovery-settlement-" + suffix;
    const DeployReceipt deployed = executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "recovery-raid-" + suffix,
            settlement,
            8100U,
            MapDefinitionId{"map.v0.test"},
            {}},
        CommandContext{
            profile.revision, "recovery-deploy-" + suffix});
    if (!deployed.succeeded)
    {
        ADD_FAILURE() << deployed.message;
        return {};
    }
    const RaidSettlementReceipt settled = settlePendingRaid(
        profile,
        publishedContentRegistry(),
        settlement,
        RaidResultOutcome::PlayerDead);
    if (!settled.succeeded)
    {
        ADD_FAILURE() << settled.message;
        return {};
    }
    return settlement;
}
}

TEST(RecoveryTaskDomainTest,
     StartAtomicallyPaysAndMovesOneWholeRecordIntoExclusiveOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "recovery-start", publishedContentRegistry());
    const std::string recordId = createLostRecord(profile, "start");
    const std::uint32_t currencyBefore = profile.currency;
    const RecoveryTaskQuote quote = queryStartRecoveryTask(
        profile, publishedContentRegistry(), recordId);
    ASSERT_TRUE(quote.canCommit) << quote.message;
    EXPECT_EQ(quote.serviceFee, 60U);
    EXPECT_EQ(quote.durationMinutes, 360U);
    EXPECT_GT(quote.highTendencyAssets, 0U);
    EXPECT_GT(quote.lowTendencyAssets, 0U);

    const RecoveryTaskReceipt started = executeStartRecoveryTask(
        profile,
        publishedContentRegistry(),
        recordId,
        CommandContext{profile.revision, "recovery-start-task"});

    ASSERT_TRUE(started.succeeded) << started.message;
    EXPECT_EQ(profile.currency, currencyBefore - quote.serviceFee);
    EXPECT_FALSE(profile.lostRaidRecords.contains(recordId));
    ASSERT_TRUE(profile.recoveryTask.has_value());
    EXPECT_EQ(profile.recoveryTask->sourceRecord.recordId, recordId);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    for (const auto &[assetId, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        if (recoveryTaskForAsset(profile, assetId).has_value())
        {
            EXPECT_EQ(
                *recoveryTaskForAsset(profile, assetId), started.taskId);
        }
    }

    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const RecoveryTaskReceipt replay = executeStartRecoveryTask(
        profile,
        publishedContentRegistry(),
        recordId,
        CommandContext{profile.revision, "recovery-start-task"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RecoveryTaskDomainTest,
     CancelReturnsTheLockedRecordWithoutRefundOrRetroactiveAging)
{
    ProfileState profile = makeNewAlphaProfile(
        "recovery-cancel", publishedContentRegistry());
    const std::string recordId = createLostRecord(profile, "cancel");
    profile.lostRaidRecords.at(recordId).subsequentRaidSettlementCount = 2U;
    ASSERT_TRUE(executeStartRecoveryTask(
        profile,
        publishedContentRegistry(),
        recordId,
        CommandContext{profile.revision, "recovery-cancel-start"}).succeeded);
    const std::uint32_t paidBalance = profile.currency;
    profile.worldClock.elapsedWorldMinutes += 120U;

    const RecoveryTaskReceipt cancelled = executeCancelRecoveryTask(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "recovery-cancel-task"});

    ASSERT_TRUE(cancelled.succeeded) << cancelled.message;
    EXPECT_EQ(profile.currency, paidBalance);
    EXPECT_FALSE(profile.recoveryTask.has_value());
    ASSERT_TRUE(profile.lostRaidRecords.contains(recordId));
    EXPECT_EQ(
        profile.lostRaidRecords.at(recordId)
            .subsequentRaidSettlementCount,
        2U);
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}

TEST(RecoveryTaskDomainTest,
     WorldTimeMakesFrozenResultReadyAndCollectionEndsTaskExactlyOnce)
{
    ProfileState profile = makeNewAlphaProfile(
        "recovery-collect", publishedContentRegistry());
    const std::string recordId = createLostRecord(profile, "collect");
    ASSERT_TRUE(executeStartRecoveryTask(
        profile,
        publishedContentRegistry(),
        recordId,
        CommandContext{profile.revision, "recovery-collect-start"}).succeeded);
    ASSERT_TRUE(profile.recoveryTask.has_value());
    const RecoveryTaskId taskId = profile.recoveryTask->taskId;
    const std::size_t total = std::count_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&profile, taskId](const auto &entry)
        {
            return recoveryTaskForAsset(profile, entry.first) == taskId;
        });
    profile.worldClock.elapsedWorldMinutes =
        profile.recoveryTask->completionWorldMinute;
    EXPECT_TRUE(applyRecoveryTaskThrough(profile).becameReady);
    EXPECT_FALSE(applyRecoveryTaskThrough(profile).becameReady);

    const RecoveryTaskReceipt collected = executeCollectRecoveryTask(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "recovery-collect-task"});

    ASSERT_TRUE(collected.succeeded) << collected.message;
    EXPECT_EQ(
        collected.recoveredAssetCount + collected.lostAssetCount,
        total);
    EXPECT_FALSE(profile.recoveryTask.has_value());
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const RecoveryTaskReceipt replay = executeCollectRecoveryTask(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "recovery-collect-task"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RecoveryTaskDomainTest,
     CollectionCapacityFailureLeavesTaskAssetsAndProfileUnchanged)
{
    ProfileState profile = makeNewAlphaProfile(
        "recovery-capacity", publishedContentRegistry());
    const std::string recordId = createLostRecord(profile, "capacity");
    ASSERT_TRUE(executeStartRecoveryTask(
        profile,
        publishedContentRegistry(),
        recordId,
        CommandContext{profile.revision, "recovery-capacity-start"})
                    .succeeded);
    ASSERT_TRUE(profile.recoveryTask.has_value());
    const RecoveryTaskId taskId = profile.recoveryTask->taskId;
    const auto root = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [taskId](const auto &entry)
        {
            const auto *location =
                std::get_if<RecoveryTaskAssetLocation>(
                    &entry.second.location);
            return location != nullptr && location->taskId == taskId;
        });
    ASSERT_NE(root, profile.assets.records().end());
    profile.recoveryTask->recoveredAssetIds.insert(root->first);
    const ItemDefinition &filler = publishedContentRegistry().item(
        alpha_content::ammunition);
    while (const auto origin = findFirstProfileFit(
               profile,
               publishedContentRegistry(),
               ProfileContainerId::stash(),
               filler,
               ItemOrientation::Degrees0))
    {
        static_cast<void>(profile.assets.create(
            filler,
            StoredAssetLocation{ProfileContainerId::stash(), *origin},
            1U));
    }
    profile.worldClock.elapsedWorldMinutes =
        profile.recoveryTask->completionWorldMinute;
    ASSERT_TRUE(applyRecoveryTaskThrough(profile).becameReady);
    ASSERT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const RecoveryTaskReceipt collected = executeCollectRecoveryTask(
        profile,
        publishedContentRegistry(),
        CommandContext{profile.revision, "recovery-capacity-collect"});

    EXPECT_FALSE(collected.succeeded);
    EXPECT_EQ(collected.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(RecoveryTaskDomainTest,
     LaterRaidFailureCreatesAnotherRecordWithoutAgingOrReplacingTask)
{
    ProfileState profile = makeNewAlphaProfile(
        "recovery-concurrent-failure", publishedContentRegistry());
    const std::string firstRecord = createLostRecord(profile, "locked");
    ASSERT_TRUE(executeStartRecoveryTask(
        profile,
        publishedContentRegistry(),
        firstRecord,
        CommandContext{profile.revision, "recovery-lock-record"})
                    .succeeded);
    ASSERT_TRUE(profile.recoveryTask.has_value());
    const RecoveryTaskId taskId = profile.recoveryTask->taskId;
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            firstAsset(profile, alpha_content::pistol),
            EquipmentSlotKind::Sidearm},
        CommandContext{profile.revision, "recovery-equip-second-raid"})
                    .succeeded);
    const std::string secondSettlement{"recovery-settlement-second"};
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "recovery-raid-second",
            secondSettlement,
            8200U,
            MapDefinitionId{"map.v0.test"},
            {}},
        CommandContext{profile.revision, "recovery-deploy-second"})
                    .succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        secondSettlement,
        RaidResultOutcome::PlayerDead).succeeded);

    ASSERT_TRUE(profile.recoveryTask.has_value());
    EXPECT_EQ(profile.recoveryTask->taskId, taskId);
    EXPECT_EQ(
        profile.recoveryTask->sourceRecord
            .subsequentRaidSettlementCount,
        0U);
    ASSERT_EQ(profile.lostRaidRecords.size(), 1U);
    EXPECT_TRUE(profile.lostRaidRecords.contains(secondSettlement));
    EXPECT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
}
