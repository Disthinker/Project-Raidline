#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "raid_corpse_search.h"
#include "raid_lifecycle.h"
#include "raid_optics_legacy_fixture.h"
#include "raid_visibility_fixture.h"
#include "save_repository.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace
{
const auto &content = publishedContentRegistry();
const MapDefinitionId riverside{"map.raid.riverside"};

DeployReceipt deploy(ProfileState &profile, const ContentRegistry &registry, std::uint64_t seed)
{
    return executeDeploy(profile, registry, {"river", "home", seed, riverside}, {profile.revision, "start"});
}

Vec2 center(ContentRect r)
{
    return {r.position.x + r.size.x * 0.5F, r.position.y + r.size.y * 0.5F};
}

void checkOuterPath(const PendingRaidSnapshot &raid, Vec2 goal, const MapDefinition &map)
{
    const Vec2 start{raid.playerSpawn.x + 25, raid.playerSpawn.y + 25};
    const float side = start.x < map.worldSize.x * 0.5F ? 30.0F : map.worldSize.x - 30.0F;
    const std::array<Vec2, 5> path{start, Vec2{side, start.y}, Vec2{side, 30}, Vec2{goal.x, 30}, goal};
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const auto a = path[i - 1], b = path[i];
        for (const auto &wall : raid.spatialLayout.ballisticBlockers)
        {
            // Sweep an entire 50x50 character along every segment, including its endpoints.
            EXPECT_FALSE(raidSpaceSegmentIntersectsBlockerInterior(a, b,
                {{wall.position.x - 25, wall.position.y - 25}, {wall.size.x + 50, wall.size.y + 50}}));
        }
        const float dx = b.x - a.x, dy = b.y - a.y, length2 = dx * dx + dy * dy;
        for (const auto &enemy : raid.enemies)
        {
            if (enemy.sourceEnemyId.empty()) continue;
            const auto point = center({enemy.position, enemy.size});
            const float t = length2 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length2, 0.0F, 1.0F) : 0.0F;
            EXPECT_GT(std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy)), 400.0F);
        }
    }
}
}

TEST(RiversideArmedScavengerDomainTest, AllPairsHaveContinuousBypassAndTradeCoverAndReplaceOnlyTwoOrdinaryRolls)
{
    const auto previous = contentBeforeRiversideArmedScavengers();
    std::set<std::string> pairs, deployments;
    const auto &map = content.map(riverside);
    for (std::uint64_t seed = 1; seed <= 32; ++seed)
    {
        SCOPED_TRACE(seed);
        auto profile = makeNewAlphaProfile("river-domain", content);
        auto old = makeNewAlphaProfile("river-domain", previous);
        auto result = deploy(profile, content, seed);
        ASSERT_TRUE(result.succeeded) << result.message;
        result = deploy(old, previous, seed);
        ASSERT_TRUE(result.succeeded) << result.message;
        const auto &raid = *profile.pendingRaid;
        const auto &before = *old.pendingRaid;
        pairs.insert(raid.spawnExtractionPairId);
        deployments.emplace(raid.enemyDeploymentId.value());
        ASSERT_EQ(raid.spatialLayout, before.spatialLayout);
        ASSERT_EQ(raid.armedScavengers.size(), 2U);
        ASSERT_EQ(raid.enemies.size(), before.enemies.size());
        EXPECT_TRUE(std::equal(raid.enemies.begin() + 2, raid.enemies.end(), before.enemies.begin() + 2));
        ASSERT_EQ(raid.loot.size(), before.loot.size());
        unsigned replaced{};
        for (std::size_t i{}; i < raid.loot.size(); ++i)
        {
            if (!raid.loot[i].corpseContainerAssetId) EXPECT_EQ(raid.loot[i], before.loot[i]);
            else
            {
                EXPECT_FALSE(raid.loot[i].requiresHighRisk);
                EXPECT_EQ(raid.loot[i].slotIndex, before.loot[i].slotIndex);
                EXPECT_EQ(raid.loot[i].definitionId, ItemDefinitionId{"item.weapon.pistol_basic"});
                EXPECT_EQ(profile.assets.find(before.loot[i].assetId), nullptr);
                ++replaced;
            }
        }
        EXPECT_EQ(replaced, 2U);
        ASSERT_TRUE(raid.fieldIntelligenceTrade && raid.rescue);
        const auto market = raid.fieldIntelligenceTrade->position;
        const Vec2 westApproach{30.0F, market.y};
        checkOuterPath(raid, westApproach, map);
        for (const auto &wall : raid.spatialLayout.ballisticBlockers)
            EXPECT_FALSE(raidSpaceSegmentIntersectsBlockerInterior(westApproach, market,
                {{wall.position.x - 25, wall.position.y - 25}, {wall.size.x + 50, wall.size.y + 50}}));
        for (std::size_t i{}; i < 2U; ++i)
        {
            const auto &actor = raid.enemies[i];
            const auto origin = center({actor.position, actor.size});
            const float dy = market.y - origin.y;
            if (std::abs(dy) > 400.0F) continue;
            const float dx = std::sqrt(400.0F * 400.0F - dy * dy);
            const float entry = std::max(westApproach.x, origin.x - dx);
            const float exit = std::min(market.x, origin.x + dx);
            if (entry > exit) continue;
            // The entire in-range portion lies in a single rectangular blocker's convex shadow.
            EXPECT_TRUE(std::any_of(raid.spatialLayout.ballisticBlockers.begin(), raid.spatialLayout.ballisticBlockers.end(),
                [&](ContentRect r) { return raidSpaceSegmentIntersectsBlockerInterior(origin, {entry,market.y}, {r.position,r.size}) &&
                    raidSpaceSegmentIntersectsBlockerInterior(origin, {exit,market.y}, {r.position,r.size}); }));
        }
        checkOuterPath(raid, center(raid.extractionPoint), map);
        checkOuterPath(raid, center(raid.rescue->transferPoint), map);
    }
    EXPECT_EQ(pairs.size(), 3U);
    EXPECT_EQ(deployments.size(), 3U);
}

TEST(RiversideArmedScavengerDomainTest, AbsoluteFireAndReloadCarryOriginalMagazinesThroughSaveRevealAndExtraction)
{
    auto profile = makeNewAlphaProfile("river-rounds", content);
    const auto bag = profile.assets.create(content.item(alpha_content::backpack), EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const auto carried = ProfileContainerId::compartment(bag, 0);
    const auto grid = profileContainerSize(profile, content, carried);
    std::vector<AssetInstanceId> packed;
    for (int y{}; y < grid.height; ++y)
        for (int x{}; x < grid.width; ++x)
            packed.push_back(profile.assets.create(content.item(alpha_content::medkit), StoredAssetLocation{carried, {x,y}}));
    const auto result = deploy(profile, content, 11);
    ASSERT_TRUE(result.succeeded) << result.message;
    const auto actor = profile.pendingRaid->armedScavengers.front();
    const auto other = profile.pendingRaid->armedScavengers.back();
    auto sync = [&](int shots, bool reloaded) { return syncRaidArmedScavengerAmmo(profile, content, actor.sourceEnemyId, shots, reloaded); };
    ASSERT_TRUE(sync(2, false));
    EXPECT_EQ(profile.assets.find(actor.firstMagazineId)->magazineRounds.size(), 3U);
    EXPECT_FALSE(sync(1, false));
    EXPECT_FALSE(sync(2, false));
    ASSERT_TRUE(sync(6, false));
    EXPECT_FALSE(profile.assets.find(actor.pistolAssetId)->chamberedRound);
    ASSERT_TRUE(sync(6, true));
    EXPECT_EQ(profile.assets.find(actor.secondMagazineId)->magazineRounds.size(), 5U);
    EXPECT_FALSE(sync(6, false));
    ASSERT_TRUE(sync(8, true));
    auto restored = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(restored.profile) << restored.message;
    profile = std::move(*restored.profile);
    EXPECT_FALSE(sync(8, true));
    EXPECT_EQ(profile.assets.find(actor.secondMagazineId)->magazineRounds.size(), 3U);
    EXPECT_EQ(profile.assets.find(other.secondMagazineId)->magazineRounds.size(), 6U);
    const RaidCorpseDeathFact death{actor.sourceEnemyId, outdoorRaidSpaceId(), {400, 350}};
    ASSERT_TRUE(revealRaidCorpseInSimulation(profile, content, death));
    ASSERT_TRUE(revealRaidCorpseInSimulation(profile, content, {actor.sourceEnemyId, death.spaceId, {300, 300}}));
    EXPECT_FALSE(sync(12, true));
    RaidSceneContainerAccess access{actor.corpseAssetId, death.spaceId, death.position};
    access.observation = observeAssetContractPoint(profile, death.spaceId, death.position);
    access.currentObservation = access.observation->key();
    EXPECT_FALSE(findFirstProfileFit(profile, content, carried, content.item(ItemDefinitionId{"item.weapon.pistol_basic"}), ItemOrientation::Degrees0));
    const InventoryMoveCommand take{actor.pistolAssetId, 0U, {carried, {0,0}}};
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeInventory(profile,content,take,{profile.revision,"no-space"},&access).succeeded);
    EXPECT_EQ(before, profileStateFingerprint(profile));
    const auto corpseStorage = ProfileContainerId::compartment(actor.corpseAssetId, 0);
    for (std::size_t i{}; i < 2; ++i)
    {
        const auto destination = findFirstProfileFit(profile, content, corpseStorage, content.item(alpha_content::medkit), ItemOrientation::Degrees0);
        ASSERT_TRUE(destination);
        const auto moved = executeInventory(profile, content, InventoryMoveCommand{packed[i], 0, {corpseStorage, *destination}},
            {profile.revision, "make-space-" + std::to_string(i)}, &access);
        ASSERT_TRUE(moved.succeeded) << moved.message;
    }
    const auto receipt = executeInventory(profile, content, take, {profile.revision,"take"}, &access);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const auto settled = settlePendingRaid(profile, content, "home", RaidResultOutcome::Extracted);
    ASSERT_TRUE(settled.succeeded) << settled.message;
    ASSERT_TRUE(settlePendingRaid(profile, content, "home", RaidResultOutcome::Extracted).alreadyCommitted);
    EXPECT_TRUE(assetIsCarried(profile, actor.pistolAssetId));
    EXPECT_TRUE(profile.assets.find(actor.pistolAssetId)->chamberedRound);
    EXPECT_EQ(profile.assets.find(actor.secondMagazineId)->magazineRounds.size(), 3U);
    EXPECT_FALSE(profile.assets.find(actor.firstMagazineId));
    EXPECT_FALSE(profile.assets.find(other.pistolAssetId));
    EXPECT_FALSE(profile.assets.find(actor.corpseAssetId));
    const auto next = executeDeploy(profile, content, {"next","next-home",2,riverside},{profile.revision,"next-start"});
    ASSERT_TRUE(next.succeeded) << next.message;
    EXPECT_TRUE(assetIsCarried(profile, actor.pistolAssetId));
}

TEST(RiversideArmedScavengerDomainTest, EmptyMagazinesDuplicateDeathsAndFailureNeverProduceAnotherLoadout)
{
    for (auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        auto profile = makeNewAlphaProfile("river-loss", content);
        const auto deployed = deploy(profile, content, 12);
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        const auto actors = profile.pendingRaid->armedScavengers;
        auto forged = profile;
        forged.pendingRaid->armedScavengers[0].secondMagazineId = actors[1].secondMagazineId;
        EXPECT_FALSE(validateProfileState(forged, content).valid);
        forged = profile;
        forged.assets.findMutable(actors[0].secondMagazineId)->magazineRounds.pop_back();
        EXPECT_FALSE(validateProfileState(forged, content).valid);
        for (const auto &actor : actors)
        {
            ASSERT_TRUE(syncRaidArmedScavengerAmmo(profile, content, actor.sourceEnemyId,12,true));
            EXPECT_FALSE(profile.assets.find(actor.pistolAssetId)->chamberedRound);
            EXPECT_TRUE(profile.assets.find(actor.secondMagazineId)->magazineRounds.empty());
            const RaidCorpseDeathFact death{actor.sourceEnemyId,outdoorRaidSpaceId(),{400,350}};
            ASSERT_TRUE(revealRaidCorpseInSimulation(profile,content,death));
            const auto before = profileStateFingerprint(profile);
            EXPECT_TRUE(revealRaidCorpse(profile,content,death));
            EXPECT_EQ(before, profileStateFingerprint(profile));
        }
        const auto settled = settlePendingRaid(profile,content,"home",outcome);
        ASSERT_TRUE(settled.succeeded) << settled.message;
        for (const auto &actor : actors)
            for (const auto id : {actor.pistolAssetId,actor.firstMagazineId,actor.secondMagazineId,actor.corpseAssetId})
                EXPECT_FALSE(profile.assets.find(id));
    }
}

TEST(RiversideArmedScavengerDomainTest, OtherMapsKeepTheirCompletePreviousDeployment)
{
    const auto previous = contentBeforeRiversideArmedScavengers();
    for (const auto &id : {MapDefinitionId{"map.v0.test"}, MapDefinitionId{"map.raid.industrial"},
                           kFreightCrateMapId, kHospitalFloorplanMapId})
    {
        SCOPED_TRACE(std::string{id.value()});
        auto old = makeNewAlphaProfile("off-map", previous);
        auto now = makeNewAlphaProfile("off-map", content);
        const DeployCommand command{"unchanged", "home", 15U, id};
        const auto before = executeDeploy(old, previous, command, {old.revision, "start"});
        ASSERT_TRUE(before.succeeded) << before.message;
        const auto after = executeDeploy(now, content, command, {now.revision, "start"});
        ASSERT_TRUE(after.succeeded) << after.message;
        EXPECT_TRUE(now.pendingRaid->armedScavengers.empty());
        EXPECT_EQ(profileStateFingerprint(now), profileStateFingerprint(old));
    }
}

TEST(RiversideArmedScavengerDomainTest, RecoveryDeployAndCrashRollbackPreserveOriginalAssets)
{
    auto profile = makeNewAlphaProfile("river-recovery", content);
    const auto bag = profile.assets.create(content.item(alpha_content::backpack), EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const auto nextId = profile.assets.nextAssetId();
    const auto deployed = deploy(profile, content, 41);
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    const auto actor = profile.pendingRaid->armedScavengers.front();
    ASSERT_TRUE(syncRaidArmedScavengerAmmo(profile, content, actor.sourceEnemyId, 9, true));
    auto interrupted = profile;
    const auto rollback = rollbackPendingRaidToBase(interrupted, content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    EXPECT_FALSE(interrupted.pendingRaid);
    EXPECT_TRUE(interrupted.assets.find(bag));
    EXPECT_EQ(interrupted.assets.nextAssetId(), nextId);
    EXPECT_FALSE(interrupted.assets.find(actor.pistolAssetId));

    const auto failed = settlePendingRaid(profile, content, "home", RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(failed.succeeded) << failed.message;
    ASSERT_TRUE(profile.lastRaidResult->lostRaidRecordId);
    DeployCommand recovery{"recover", "recover-home", 42, riverside};
    recovery.selfRecoveryRecordId = profile.lastRaidResult->lostRaidRecordId;
    const auto run = executeDeploy(profile, content, recovery, {profile.revision, "recover-start"});
    ASSERT_TRUE(run.succeeded) << run.message;
    EXPECT_TRUE(profile.pendingRaid->selfRecovery);
    EXPECT_TRUE(profile.pendingRaid->armedScavengers.empty());
    EXPECT_TRUE(std::none_of(profile.pendingRaid->enemies.begin(), profile.pendingRaid->enemies.end(),
        [](const auto &enemy) { return enemy.sourceEnemyId.starts_with("riverside-scavenger:"); }));
}
