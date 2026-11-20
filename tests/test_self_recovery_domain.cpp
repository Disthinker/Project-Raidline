#include "raid_visibility_fixture.h"
#include "raid_optics_legacy_fixture.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <set>

#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "lost_raid_domain.h"
#include "maintenance_domain.h"
#include "raid_lifecycle.h"
#include "recovery_task_domain.h"
#include "self_recovery_domain.h"
#include "save_repository.h"
#include "weapon_ammo_domain.h"

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

DeployReceipt deployPursuit(ProfileState &profile, const ContentRegistry &content,
                           const std::string &record, std::uint64_t seed = 9403U)
{
    return executeDeploy(
        profile, content, DeployCommand{
            "pursuit-raid", "pursuit-settlement", seed, kFieldIntelligenceSourceMapId, {}, record},
        CommandContext{profile.revision, "pursuit-deploy"});
}
}

TEST(SelfRecoveryDomainTest, LostWeaponPursuitSelectsOneOriginalGunAndPreservesOrdinaryRolls)
{
    // Recovery still uses the ordinary enemy and loot rolls from before the armed encounter.
    const auto previous = contentBeforeRiversideArmedScavengers();
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("pursuit-stable", content);
    const std::string record = createLoss(profile, "pursuit-stable", kFieldIntelligenceSourceMapId);
    const auto primary = firstAsset(profile, alpha_content::rifle);
    const auto second = profile.assets.create(content.item(alpha_content::rifle),
        LostRaidAssetLocation{record, EquipmentSlotKind::SecondaryWeapon});
    const auto selection = [&]() { return selectRaidPursuitWeapon(profile.assets, content,
        profile.lostRaidRecords.at(record)); };
    EXPECT_FALSE(selection());
    profile.lostRaidRecords.at(record).subsequentRaidSettlementCount = 1U;
    EXPECT_EQ(selection(), primary);
    auto secondaryOnly = profile;
    ASSERT_TRUE(secondaryOnly.assets.erase(primary));
    EXPECT_EQ(selectRaidPursuitWeapon(secondaryOnly.assets, content, secondaryOnly.lostRaidRecords.at(record)), second);

    std::set<std::string> pairIds;
    std::set<std::string> deploymentIds;
    for (std::uint64_t seed = 1; seed <= 48U; ++seed)
    {
        auto branch = profile;
        auto ordinary = profile;
        const auto deployed = deployPursuit(branch, content, record, seed);
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(branch.pendingRaid && branch.pendingRaid->selfRecovery);
        const auto &raid = *branch.pendingRaid;
        const auto &recovery = *raid.selfRecovery;
        ASSERT_TRUE(recovery.pursuit) << seed;
        EXPECT_TRUE(raidPursuitGeometryValid(content.map(kFieldIntelligenceSourceMapId), raid, *recovery.pursuit));
        EXPECT_EQ(recovery.roots.size(), 1U);
        EXPECT_EQ(recovery.roots.front().assetId, primary);
        EXPECT_EQ(branch.lostRaidRecords.at(record), profile.lostRaidRecords.at(record));
        EXPECT_EQ(branch.assets.find(primary)->location, profile.assets.find(primary)->location);
        EXPECT_EQ(branch.assets.find(second)->location, profile.assets.find(second)->location);
        EXPECT_FALSE(recovery.pursuit->deathPosition);
        EXPECT_FALSE(recovery.pursuit->claimed);
        EXPECT_FALSE(queryOpenRaidSelfRecovery(branch, content).canCommit);
        EXPECT_TRUE(raid.armedScavengers.empty());
        const auto original = executeDeploy(ordinary, previous,
            {"comparison", "comparison-return", seed, kFieldIntelligenceSourceMapId},
            {ordinary.revision, "comparison-deploy"});
        ASSERT_TRUE(original.succeeded) << original.message;
        EXPECT_EQ(raid.enemies, ordinary.pendingRaid->enemies);
        auto ordinaryLoot = ordinary.pendingRaid->loot;
        for (std::size_t index{}; index < ordinaryLoot.size(); ++index)
        {
            auto &item = ordinaryLoot[index];
            if (item.definitionId == kFreightCrateKeyId || item.definitionId == kFieldIntelligenceDocumentId)
            {
                item.definitionId = raid.loot.at(index).definitionId;
                item.quantity = raid.loot.at(index).quantity;
            }
        }
        EXPECT_EQ(raid.loot, ordinaryLoot);
        EXPECT_EQ(raid.spatialLayout, ordinary.pendingRaid->spatialLayout);
        EXPECT_FALSE(raid.fieldIntelligenceTrade);
        EXPECT_TRUE(ordinary.pendingRaid->fieldIntelligenceTrade);
        pairIds.insert(raid.spawnExtractionPairId);
        deploymentIds.emplace(raid.enemyDeploymentId.value());
    }
    EXPECT_EQ(pairIds.size(), 3U);
    EXPECT_EQ(deploymentIds.size(), 3U);
}

TEST(SelfRecoveryDomainTest, LostWeaponPursuitIneligibleRecordsUseExistingCache)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("pursuit-fallback", content);
    const std::string record = createLoss(profile, "pursuit-fallback", kFieldIntelligenceSourceMapId);
    for (const bool fromQuit : {false, true})
    {
        auto branch = profile;
        if (fromQuit)
        {
            auto &source = branch.lostRaidRecords.at(record);
            source.outcome = RaidResultOutcome::ActiveQuit;
            source.subsequentRaidSettlementCount = 2U;
        }
        const auto deployed = deployPursuit(branch, content, record);
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(branch.pendingRaid && branch.pendingRaid->selfRecovery);
        EXPECT_FALSE(branch.pendingRaid->selfRecovery->pursuit);
        EXPECT_EQ(branch.pendingRaid->selfRecovery->roots.size(), 2U);
    }
}

TEST(SelfRecoveryDomainTest, LostWeaponPursuitWithNoUniqueCarrierUsesExistingCache)
{
    auto content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("pursuit-no-carrier", content);
    const auto record = createLoss(profile, "pursuit-no-carrier", kFieldIntelligenceSourceMapId);
    ++profile.lostRaidRecords.at(record).subsequentRaidSettlementCount;
    for (const auto &id : content.map(kFieldIntelligenceSourceMapId).raidEnemyDeploymentIds)
    {
        auto &enemies = const_cast<EnemyDeploymentDefinition &>(content.enemyDeployment(id)).enemies;
        ASSERT_FALSE(enemies.empty());
        for (auto &enemy : enemies) enemy.position = enemies.front().position;
    }
    const auto deployed = deployPursuit(profile, content, record);
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->selfRecovery);
    EXPECT_FALSE(profile.pendingRaid->selfRecovery->pursuit);
    EXPECT_EQ(profile.pendingRaid->selfRecovery->roots.size(), 2U);
}

TEST(SelfRecoveryDomainTest, UntakenPursuitExitAgesOriginalRecordAndExpiresAtThree)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("pursuit-age", content);
    const std::string record = createLoss(profile, "pursuit-age", kFieldIntelligenceSourceMapId);
    const auto original = firstAsset(profile, alpha_content::rifle);
    for (const std::uint32_t age : {1U, 3U})
    {
        auto branch = profile;
        branch.lostRaidRecords.at(record).subsequentRaidSettlementCount = age;
        const auto deployed = deployPursuit(branch, content, record);
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(branch.pendingRaid->selfRecovery->pursuit);
        ASSERT_TRUE(revealRaidLostWeaponPursuitCorpse(branch, content,
            branch.pendingRaid->selfRecovery->pursuit->fallbackSearchPosition));
        const auto rescue = settlePendingRaid(branch, content, "pursuit-settlement", RaidResultOutcome::Extracted);
        ASSERT_TRUE(rescue.succeeded) << rescue.message;
        if (age == 3U)
        {
            EXPECT_FALSE(branch.lostRaidRecords.contains(record));
            EXPECT_EQ(branch.assets.find(original), nullptr);
        }
        else
        {
            EXPECT_EQ(branch.lostRaidRecords.at(record).subsequentRaidSettlementCount, age + 1U);
            EXPECT_EQ(lostRaidRecordForAsset(branch, original), record);
        }
        EXPECT_TRUE(settlePendingRaid(branch, content, "pursuit-settlement",
            RaidResultOutcome::Extracted).alreadyCommitted);
    }
}

TEST(SelfRecoveryDomainTest, CurrentObservationRequiredForQueryAndFinalOpen)
{
    const auto &content=publishedContentRegistry();
    auto profile=makeNewAlphaProfile("self-cone",content);
    const auto record=createLoss(profile,"cone"); ASSERT_TRUE(deployRecovery(profile,record).succeeded);
    const auto point=profile.pendingRaid->selfRecovery->cachePosition;
    const auto view=observeAssetContractPoint(profile,outdoorRaidSpaceId(),point);
    auto key=view.key(); const auto hash=profileStateFingerprint(profile);
    EXPECT_FALSE(queryOpenRaidSelfRecovery(profile,content).canCommit);
    EXPECT_FALSE(executeOpenRaidSelfRecovery(profile,content,{profile.revision,"missing"}).succeeded);
    key.direction={-1,0};
    EXPECT_FALSE(executeOpenRaidSelfRecovery(profile,content,{profile.revision,"old-heading"},&view,&key).succeeded);
    key=view.key(); key.observer.x+=100;
    EXPECT_FALSE(executeOpenRaidSelfRecovery(profile,content,{profile.revision,"old-position"},&view,&key).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile),hash);
    key=view.key(); EXPECT_TRUE(queryOpenRaidSelfRecovery(profile,content,&view,&key).canCommit);
    EXPECT_TRUE(executeOpenRaidSelfRecovery(profile,content,{profile.revision,"current"},&view,&key).succeeded);
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

TEST(SelfRecoveryDomainTest, HospitalLossSupportsBothExclusiveRecoveryPathsAfterReload)
{
    const auto &content = publishedContentRegistry();
    const MapDefinitionId hospital{"map.raid.hospital_district"};
    auto profile = makeNewAlphaProfile("hospital-loss-recovery", content);
    const auto record = createLoss(profile, "hospital", hospital);
    const auto rifle = firstAsset(profile, alpha_content::rifle);
    const auto savedLoss = serializeProfileEnvelope(profile, content.contentVersion());
    for (const bool npc : {false, true})
    {
        auto loaded = deserializeProfileEnvelope(savedLoss, content);
        ASSERT_TRUE(loaded.profile) << loaded.message;
        auto state = std::move(*loaded.profile);
        if (npc)
        {
            const auto quote = queryStartRecoveryTask(state, content, record);
            ASSERT_TRUE(quote.canCommit) << quote.message;
            EXPECT_EQ(quote.serviceFee, content.map(hospital).recovery.serviceFee);
            ASSERT_TRUE(executeStartRecoveryTask(state, content, record,
                CommandContext{state.revision,"hospital-npc-start"}).succeeded);
            const auto fingerprint = profileStateFingerprint(state);
            EXPECT_FALSE(deployRecovery(state, record, hospital).succeeded);
            EXPECT_EQ(profileStateFingerprint(state), fingerprint);
            auto saved = deserializeProfileEnvelope(serializeProfileEnvelope(state, content.contentVersion()), content);
            ASSERT_TRUE(saved.profile) << saved.message;
            state = std::move(*saved.profile);
            state.worldClock.elapsedWorldMinutes = state.recoveryTask->completionWorldMinute;
            ASSERT_TRUE(applyRecoveryTaskThrough(state).becameReady);
            const auto receipt = executeCollectRecoveryTask(state, content,
                CommandContext{state.revision,"hospital-npc-collect"});
            ASSERT_TRUE(receipt.succeeded) << receipt.message;
            const auto after = profileStateFingerprint(state);
            EXPECT_TRUE(executeCollectRecoveryTask(state, content,
                CommandContext{state.revision,"hospital-npc-collect"}).alreadyCommitted);
            EXPECT_EQ(profileStateFingerprint(state), after);
            EXPECT_FALSE(state.recoveryTask);
        }
        else
        {
            ASSERT_TRUE(deployRecovery(state, record, hospital).succeeded);
            ASSERT_TRUE(state.pendingRaid->selfRecovery);
            ASSERT_NE(findRaidAnchorPlacement(state.pendingRaid->spatialLayout, kRaidAnchorSelfRecovery), nullptr);
            EXPECT_FALSE(queryStartRecoveryTask(state, content, record).canCommit);
            ASSERT_TRUE(openObservedRaidSelfRecovery(state, content,
                CommandContext{state.revision,"hospital-cache-open"}).succeeded);
            ASSERT_TRUE(pickupObservedRaidLoot(state, content, rifle,
                CommandContext{state.revision,"hospital-cache-pick"}).succeeded);
            const auto carried = state.assets.find(rifle)->location;
            ASSERT_TRUE(settlePendingRaid(state, content,"self-recovery-settlement",
                RaidResultOutcome::Extracted).succeeded);
            ASSERT_NE(state.assets.find(rifle), nullptr);
            EXPECT_EQ(state.assets.find(rifle)->location, carried);
        }
        EXPECT_FALSE(state.lostRaidRecords.contains(record));
        EXPECT_TRUE(validateProfileState(state, content).valid);
        auto finalLoad = deserializeProfileEnvelope(serializeProfileEnvelope(state, content.contentVersion()),content);
        ASSERT_TRUE(finalLoad.profile) << finalLoad.message;
        EXPECT_EQ(profileStateFingerprint(*finalLoad.profile), profileStateFingerprint(state));
    }
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

    const InventoryReceipt opened = openObservedRaidSelfRecovery(
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
    const InventoryReceipt replay = openObservedRaidSelfRecovery(
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
    ASSERT_TRUE(openObservedRaidSelfRecovery(
        profile, publishedContentRegistry(),
        CommandContext{profile.revision, "self-recovery-success-open"})
                    .succeeded);
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId backpack = firstAsset(profile, alpha_content::backpack);
    ASSERT_TRUE(pickupObservedRaidLoot(
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
    ASSERT_TRUE(openObservedRaidSelfRecovery(
        failed, publishedContentRegistry(),
        CommandContext{failed.revision, "self-recovery-fail-open"})
                    .succeeded);
    const AssetInstanceId rifle = firstAsset(failed, alpha_content::rifle);
    ASSERT_TRUE(pickupObservedRaidLoot(
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
    ASSERT_TRUE(openObservedRaidSelfRecovery(
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

namespace
{
class PursuitCorpseInventoryTest : public testing::Test
{
protected:
    void SetUp() override
    {
        const auto &weapon = content.item(ItemDefinitionId{"item.weapon.rifle_5_45_service"});
        const auto &magazine = content.item(ItemDefinitionId{"item.magazine.5_45x39_30"});
        const auto &optic = content.item(ItemDefinitionId{"item.component.optic_1_5x_5_45"});
        rifle = profile.assets.create(weapon, EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
        installedMag = profile.assets.create(magazine, InstalledMagazineLocation{rifle});
        opticId = profile.assets.create(optic, InstalledWeaponComponentLocation{rifle});
        profile.assets.findMutable(rifle)->currentDurability -= 2000U;
        profile.assets.findMutable(rifle)->chamberedRound =
            MagazineRoundRecord{ItemDefinitionId{"item.ammunition.5_45x39_enhanced"}, std::nullopt};
        profile.assets.findMutable(installedMag)->magazineRounds = {
            {ItemDefinitionId{"item.ammunition.5_45x39_standard"}, std::nullopt},
            {ItemDefinitionId{"item.ammunition.5_45x39_enhanced"}, std::nullopt},
            {ItemDefinitionId{"item.ammunition.5_45x39_expanding"}, std::nullopt}};

        otherLost = firstAsset(profile, alpha_content::pistol);
        ASSERT_TRUE(executeInventory(profile, content,
            InventoryEquipCommand{otherLost, EquipmentSlotKind::Sidearm},
            {profile.revision, "equip-original-sidearm"}).succeeded);
        const auto firstRaid = executeDeploy(profile, content,
            {"pursuit-original", recordId, 7301U, kFieldIntelligenceSourceMapId, {}, std::nullopt},
            {profile.revision, "deploy-original"});
        ASSERT_TRUE(firstRaid.succeeded) << firstRaid.message;
        ASSERT_TRUE(settlePendingRaid(profile, content, recordId, RaidResultOutcome::PlayerDead).succeeded);
        profile.lostRaidRecords.at(recordId).subsequentRaidSettlementCount = 1U;
        const auto reserveRifle = firstAsset(profile, alpha_content::rifle);
        ASSERT_TRUE(executeInventory(profile, content,
            InventoryEquipCommand{reserveRifle, EquipmentSlotKind::SecondaryWeapon},
            {profile.revision, "equip-reserve-rifle"}).succeeded);
        backpack = firstAsset(profile, alpha_content::backpack);
        ASSERT_TRUE(executeInventory(profile, content,
            InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
            {profile.revision, "equip-backpack"}).succeeded);
        retainedMag = profile.assets.create(magazine,
            RetainedRaidMagazineLocation{rifle, RaidReloadMagazineRole::OldMagazine});

        const auto deployed = deployRecovery(profile, recordId, kFieldIntelligenceSourceMapId);
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->selfRecovery &&
            profile.pendingRaid->selfRecovery->pursuit);
        const auto &pursuit = *profile.pendingRaid->selfRecovery->pursuit;
        corpse = pursuit.corpseAssetId;
        point = pursuit.fallbackSearchPosition;
    }

    RaidSceneContainerAccess observeAt(Vec2 observer) const
    {
        const auto &space = outdoorRaidSpaceId();
        auto view = observeAssetContractPoint(profile, space, observer);
        const auto key = view.key();
        return {corpse, space, observer, std::move(view), key};
    }

    static InventoryEquipCommand equipRecovered(AssetInstanceId id)
    {
        return {id, EquipmentSlotKind::PrimaryWeapon};
    }

    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("pursuit-corpse-inventory", content);
    const std::string recordId = "pursuit-original-settlement";
    AssetInstanceId rifle{}, installedMag{}, retainedMag{}, opticId{}, otherLost{}, corpse{}, backpack{};
    Vec2 point{};
};
}

TEST_F(PursuitCorpseInventoryTest, DeathRevealsReadOnlyWeaponAndRequiresCurrentNearbyClearObservation)
{
    const auto access = observeAt(point);
    auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(raidSceneContainerAccessible(profile, access));
    EXPECT_FALSE(queryInventory(profile, content, equipRecovered(rifle), &access).canCommit);
    EXPECT_FALSE(executeInventory(profile, content, equipRecovered(rifle),
        {profile.revision, "unseen"}, &access).succeeded);
    EXPECT_FALSE(revealRaidLostWeaponPursuitCorpse(profile, content, {-1.0F, 0.0F}));
    EXPECT_FALSE(revealRaidLostWeaponPursuitCorpse(profile, content,
        {std::numeric_limits<float>::quiet_NaN(), point.y}));
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);

    ASSERT_TRUE(revealRaidLostWeaponPursuitCorpse(profile, content, point));
    const auto *shown = raidPursuitCorpseWeapon(profile, corpse);
    ASSERT_NE(shown, nullptr);
    EXPECT_EQ(shown->instanceId, rifle);
    EXPECT_EQ(lostRaidRecordForAsset(profile, rifle), recordId);
    EXPECT_TRUE(assetsInContainer(profile, ProfileContainerId::compartment(corpse, 0)).empty());
    EXPECT_EQ(profile.pendingRaid->selfRecovery->pursuit->deathPosition->x, point.x);
    fingerprint = profileStateFingerprint(profile);
    EXPECT_TRUE(revealRaidLostWeaponPursuitCorpse(profile, content, point));
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_FALSE(queryOpenRaidSelfRecovery(profile, content,
        &*access.observation, &*access.currentObservation).canCommit);
    EXPECT_FALSE(queryInventory(profile, content, equipRecovered(rifle)).canCommit);

    const auto far = observeAt({point.x - 73.0F, point.y});
    EXPECT_FALSE(queryInventory(profile, content, equipRecovered(rifle), &far).canCommit);
    auto stale = observeAt({point.x - 24.0F, point.y});
    stale.currentObservation->geometryRevision += 1;
    EXPECT_FALSE(queryInventory(profile, content, equipRecovered(rifle), &stale).canCommit);
    auto walled = profile;
    const auto near = observeAt({point.x - 60.0F, point.y});
    walled.pendingRaid->spatialLayout.ballisticBlockers.push_back(
        {{point.x - 45.0F, point.y - 30.0F}, {10.0F, 60.0F}});
    EXPECT_FALSE(raidSceneContainerAccessible(walled, near));
    EXPECT_FALSE(queryInventory(walled, content, equipRecovered(rifle), &near).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST_F(PursuitCorpseInventoryTest, LegalEquipMovesOriginalTreeAndPreservesOtherLostRoots)
{
    ASSERT_TRUE(revealRaidLostWeaponPursuitCorpse(profile, content, point));
    const auto access = observeAt(point);
    const auto original = *profile.assets.find(rifle);
    const auto orderedRounds = profile.assets.find(installedMag)->magazineRounds;
    const auto originalAge = profile.lostRaidRecords.at(recordId).subsequentRaidSettlementCount;
    const auto originalHighWater = profile.assets.nextAssetId();
    const InventoryCommand occupied = InventoryEquipCommand{rifle, EquipmentSlotKind::SecondaryWeapon};
    const InventoryCommand wrongSlot = InventoryEquipCommand{rifle, EquipmentSlotKind::Sidearm};
    const InventoryCommand wrongRoot = InventoryEquipCommand{otherLost, EquipmentSlotKind::Sidearm};
    const InventoryCommand deposit = InventoryMoveCommand{
        *equippedAsset(profile, EquipmentSlotKind::SecondaryWeapon), 1,
        {ProfileContainerId::compartment(corpse, 0), {0, 0}}};
    const auto fingerprint = profileStateFingerprint(profile);
    for (const auto *command : {&occupied, &wrongSlot, &wrongRoot, &deposit})
    {
        EXPECT_FALSE(queryInventory(profile, content, *command, &access).canCommit);
        EXPECT_FALSE(executeInventory(profile, content, *command,
            {profile.revision, "reject-intrusion"}, &access).succeeded);
        EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    }
    EXPECT_FALSE(queryWeaponAmmo(profile, content, UnloadMagazineCommand{installedMag}).canCommit);
    EXPECT_FALSE(executeWeaponAmmo(profile, content, UnloadMagazineCommand{installedMag},
        {profile.revision, "unload-before-take"}).succeeded);
    EXPECT_FALSE(queryFireWeapon(profile, content, FireWeaponCommand{rifle}).canCommit);
    EXPECT_FALSE(executeFireWeapon(profile, content, FireWeaponCommand{rifle},
        {profile.revision, "fire-before-take"}).succeeded);
    EXPECT_FALSE(queryWeaponMaintenance(profile, content,
        {firstAsset(profile, alpha_content::weaponMaintenanceKit), rifle,
            MaintenanceAccess::CarriedOnly, MaintenanceLocation::Raid}).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);

    const InventoryCommand take = equipRecovered(rifle);
    const CommandContext operation{profile.revision, "pursuit-claim-weapon"};
    const auto preview = queryInventory(profile, content, take, &access);
    ASSERT_TRUE(preview.canCommit) << preview.message;
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    const auto accepted = executeInventory(profile, content, take, operation, &access);
    ASSERT_TRUE(accepted.succeeded) << accepted.message;
    EXPECT_EQ(profile.revision, operation.expectedRevision + 1);
    EXPECT_EQ(profile.assets.nextAssetId(), originalHighWater);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::PrimaryWeapon), rifle);
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, original.currentDurability);
    EXPECT_EQ(profile.assets.find(rifle)->currentMaximumDurability, original.currentMaximumDurability);
    EXPECT_EQ(profile.assets.find(rifle)->chamberedRound, original.chamberedRound);
    EXPECT_EQ(installedMagazine(profile, rifle), installedMag);
    EXPECT_EQ(profile.assets.find(installedMag)->magazineRounds, orderedRounds);
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(profile.assets.find(opticId)->location).weaponAssetId, rifle);
    EXPECT_TRUE(assetIsCarried(profile, retainedMag));
    EXPECT_EQ(lostRaidRecordForAsset(profile, otherLost), recordId);
    EXPECT_EQ(profile.lostRaidRecords.at(recordId).subsequentRaidSettlementCount, originalAge);
    EXPECT_TRUE(profile.pendingRaid->selfRecovery->pursuit->claimed);
    EXPECT_EQ(raidPursuitCorpseWeapon(profile, corpse), nullptr);
    const auto validity = validateProfileState(profile, content);
    EXPECT_TRUE(validity.valid) << validity.message;

    const auto after = profileStateFingerprint(profile);
    const WeaponAmmoCommand unloadIntoCorpse = UnloadMagazineCommand{
        installedMag, ProfileContainerId::compartment(corpse, 0)};
    const auto rejectLostOrCorpse = [&](const WeaponAmmoCommand &command, const char *operationId)
    {
        EXPECT_EQ(queryWeaponAmmo(profile, content, command).error, DomainErrorCode::IllegalDestination);
        EXPECT_EQ(executeWeaponAmmo(profile, content, command,
            {profile.revision, operationId}).error, DomainErrorCode::IllegalDestination);
    };
    rejectLostOrCorpse(unloadIntoCorpse, "unload-into-corpse");
    rejectLostOrCorpse(ChamberWeaponCommand{otherLost}, "chamber-other-loss-after-take");
    EXPECT_TRUE(executeInventory(profile, content, take, operation, &access).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), after);
}

TEST_F(PursuitCorpseInventoryTest, OverlappingBackpackDropCannotSwapALostWeapon)
{
    ASSERT_TRUE(revealRaidLostWeaponPursuitCorpse(profile, content, point));
    const auto access = observeAt(point);
    const auto pack = ProfileContainerId::compartment(backpack, 0);
    const auto obstruction = profile.assets.create(content.item(alpha_content::lootCola),
        StoredAssetLocation{pack, {0, 0}});
    const InventoryCommand take = InventoryMoveCommand{rifle, 0, {pack, {0, 0}}};
    const auto before = profileStateFingerprint(profile);
    EXPECT_EQ(queryInventory(profile, content, take, &access).error, DomainErrorCode::Capacity);
    EXPECT_EQ(executeInventory(profile, content, take, {profile.revision, "blocked"}, &access).error,
        DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    ASSERT_TRUE(profile.assets.erase(obstruction));
    const auto receipt = executeInventory(profile, content, take, {profile.revision, "room-made"}, &access);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(assetIsCarried(profile, rifle));
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(rifle)->location).container, pack);
    EXPECT_EQ(lostRaidRecordForAsset(profile, otherLost), recordId);
    EXPECT_TRUE(assetIsCarried(profile, retainedMag));
}

TEST_F(PursuitCorpseInventoryTest, ClaimedWeaponSettlementFollowsCarriedOwnership)
{
    const auto source = profile.lostRaidRecords.at(recordId);
    ASSERT_TRUE(revealRaidLostWeaponPursuitCorpse(profile, content, point));
    const auto access = observeAt(point);
    const auto take = executeInventory(profile, content, equipRecovered(rifle),
        {profile.revision, "settlement-claim"}, &access);
    ASSERT_TRUE(take.succeeded) << take.message;
    const auto rifleAtClaim = *profile.assets.find(rifle);
    const auto magazineAtClaim = *profile.assets.find(installedMag);
    const auto settlementId = profile.pendingRaid->settlementId;
    for (const auto outcome : {RaidResultOutcome::Extracted, RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        auto branch = profile;
        const auto result = settlePendingRaid(branch, content, settlementId, outcome);
        ASSERT_TRUE(result.succeeded) << result.message;
        EXPECT_EQ(branch.assets.find(corpse), nullptr);
        EXPECT_EQ(lostRaidRecordForAsset(branch, otherLost), recordId);
        EXPECT_EQ(branch.lostRaidRecords.at(recordId).subsequentRaidSettlementCount,
            source.subsequentRaidSettlementCount + 1U);
        ASSERT_NE(branch.assets.find(rifle), nullptr);
        EXPECT_EQ(branch.assets.find(rifle)->currentDurability, rifleAtClaim.currentDurability);
        EXPECT_EQ(branch.assets.find(rifle)->chamberedRound, rifleAtClaim.chamberedRound);
        EXPECT_EQ(installedMagazine(branch, rifle), installedMag);
        EXPECT_EQ(branch.assets.find(installedMag)->magazineRounds, magazineAtClaim.magazineRounds);
        if (outcome == RaidResultOutcome::Extracted)
        {
            EXPECT_EQ(equippedAsset(branch, EquipmentSlotKind::PrimaryWeapon), rifle);
            EXPECT_TRUE(assetIsCarried(branch, opticId));
            EXPECT_FALSE(branch.lastRaidResult->lostRaidRecordId);
        }
        else
        {
            EXPECT_EQ(branch.lastRaidResult->lostRaidRecordId, settlementId);
            EXPECT_EQ(lostRaidRecordForAsset(branch, rifle), settlementId);
            EXPECT_EQ(lostRaidRecordForAsset(branch, opticId), settlementId);
            EXPECT_EQ(branch.lostRaidRecords.at(settlementId).outcome, outcome);
        }
        const auto settled = profileStateFingerprint(branch);
        EXPECT_TRUE(settlePendingRaid(branch, content, settlementId, outcome).alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(branch), settled);
    }
}

TEST_F(PursuitCorpseInventoryTest, AbnormalRollbackRestoresSoleSourceAndExactAssets)
{
    // Recreate a legitimate one-root record, then deploy and claim its sole root.
    ASSERT_TRUE(rollbackPendingRaidToBase(profile, content).succeeded);
    ASSERT_TRUE(profile.assets.erase(otherLost));
    const auto source = profile.lostRaidRecords.at(recordId);
    const auto original = *profile.assets.find(rifle);
    const auto oldHighWater = profile.assets.nextAssetId();
    const auto deployed = deployPursuit(profile, content, recordId);
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->selfRecovery && profile.pendingRaid->selfRecovery->pursuit);
    corpse = profile.pendingRaid->selfRecovery->pursuit->corpseAssetId;
    point = profile.pendingRaid->selfRecovery->pursuit->fallbackSearchPosition;
    ASSERT_TRUE(revealRaidLostWeaponPursuitCorpse(profile, content, point));
    const auto access = observeAt(point);
    const auto take = executeInventory(profile, content, equipRecovered(rifle),
        {profile.revision, "rollback-claim"}, &access);
    ASSERT_TRUE(take.succeeded) << take.message;
    EXPECT_FALSE(profile.lostRaidRecords.contains(recordId));
    auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    profile = std::move(*loaded.profile);
    const auto rollback = rollbackPendingRaidToBase(profile, content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    EXPECT_FALSE(profile.pendingRaid);
    EXPECT_EQ(profile.lostRaidRecords.at(recordId), source);
    EXPECT_EQ(lostRaidRecordForAsset(profile, rifle), recordId);
    EXPECT_EQ(lostRaidRecordForAsset(profile, installedMag), recordId);
    EXPECT_EQ(lostRaidRecordForAsset(profile, retainedMag), recordId);
    EXPECT_EQ(lostRaidRecordForAsset(profile, opticId), recordId);
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, original.currentDurability);
    EXPECT_EQ(profile.assets.find(rifle)->chamberedRound, original.chamberedRound);
    EXPECT_EQ(profile.assets.find(corpse), nullptr);
    EXPECT_EQ(profile.assets.nextAssetId(), oldHighWater);
}
