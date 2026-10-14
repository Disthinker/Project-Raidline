#include "game_session.h"
#include "raid_light_cone_performance.h"
#include "raid_visibility_fixture.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>

TEST(RaidLightConeVisibilityTest,
     FarOutdoorCoordinatesKeepValidNightSectorAtEveryHeading)
{
    const auto index = RaidSpaceBlockerIndex::build({25600, 14400}, {});
    ASSERT_TRUE(index);
    for (int n = 0; n < 360; ++n)
    {
        const float angle = n * 3.14159265358979323846F / 180;
        const RaidObservationKey key{outdoorRaidSpaceId(),
                                     {22051, 6031},
                                     {std::cos(angle), std::sin(angle)},
                                     100,
                                     600,
                                     RaidLightEnvironment::NightOutdoor,
                                     1};
        const auto view = buildRaidSpaceConeVisibility(*index, key);
        ASSERT_TRUE(view.validGeometry()) << n;
        EXPECT_TRUE(view.pointVisible({key.observer.x + key.direction.x * 60,
                                       key.observer.y + key.direction.y * 60}));
    }
}

namespace
{
const RaidSpaceDefinitionId room{"raid_space.cone.test"};
RaidVisibilityProjection cone(Vec2 direction = {1, 0}, float angle = 110,
                              float radius = 500,
                              std::vector<BallisticBlocker> walls = {})
{
    const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, walls);
    EXPECT_TRUE(index);
    return buildRaidSpaceConeVisibility(*index,
                                        {room,
                                         {1000, 1000},
                                         direction,
                                         angle,
                                         radius,
                                         RaidLightEnvironment::WeakIndoor,
                                         1});
}
RaidWorldConfig config()
{
    RaidWorldConfig c;
    c.lightConeObservationEnabled = true;
    c.worldSize = {3000, 3000};
    c.playerSpawn = {975, 975};
    c.extractionPoint = {{2800, 2800}, {100, 100}};
    return c;
}
void aim(GameplayWorld &w, Vec2 target, bool locked = false,
         float seconds = 0.05F)
{
    GameplayInput i;
    i.aimWorldPosition = target;
    i.freezeObservationDirection = locked;
    w.update(i, seconds);
}
void settle(GameplayWorld &w, Vec2 target)
{
    for (int n = 0; n < 30; ++n)
        aim(w, target);
}
} // namespace

TEST(RaidLightConeVisibilityTest,
     FrontSideRearRangeAndNoNearOmnidirectionalBubble)
{
    const auto v = cone();
    ASSERT_TRUE(v.validGeometry());
    EXPECT_TRUE(v.pointVisible({1200, 1000}));
    EXPECT_FALSE(v.pointVisible({999, 1000}));
    EXPECT_FALSE(v.pointVisible({1000, 1100}));
    EXPECT_FALSE(v.pointVisible({1501, 1000}));
    EXPECT_TRUE(v.boundsVisible({{990, 980}, {60, 40}}));
    EXPECT_FALSE(v.boundsVisible({{940, 980}, {40, 40}}));
    const auto day = cone({1, 0}, 180, 1000);
    EXPECT_TRUE(day.pointVisible({1001, 1500}));
    EXPECT_FALSE(day.pointVisible({999, 1500}));
}
TEST(RaidLightConeVisibilityTest, SectorWrapAroundNegativePiKeepsBothFrontEdges)
{
    const auto v = cone({-1, 0});
    ASSERT_TRUE(v.validGeometry());
    EXPECT_TRUE(v.pointVisible({800, 1100}));
    EXPECT_TRUE(v.pointVisible({800, 900}));
    EXPECT_FALSE(v.pointVisible({1100, 1000}));
    EXPECT_FALSE(v.pointVisible({800, 1400}));
}
TEST(RaidLightConeVisibilityTest,
     ThinWallAndLargeWallWithDistantCenterStillOcclude)
{
    auto v = cone({1, 0}, 180, 1000, {{1, {{1300, 0}, {0.5F, 3000}}}});
    ASSERT_TRUE(v.validGeometry());
    EXPECT_FALSE(v.pointVisible({1600, 1000}));
    EXPECT_TRUE(v.boundsVisible({{1290, 990}, {40, 30}}));
    EXPECT_FALSE(v.boundsVisible({{1310, 990}, {40, 30}}));
    v = cone({1, 0}, 110, 500, {{1, {{1300, 0}, {10, 3000}}}});
    EXPECT_FALSE(v.pointVisible({1450, 1000}));
}
TEST(RaidLightConeVisibilityTest,
     InvalidDirectionDistanceAndMalformedSectorFailClosed)
{
    for (const auto d : std::vector<Vec2>{{NAN, 0}, {0, 0}, {2, 0}})
        EXPECT_FALSE(cone(d).valid);
    EXPECT_FALSE(cone({1, 0}, -1).valid);
    EXPECT_FALSE(cone({1, 0}, 181).valid);
    EXPECT_FALSE(cone({1, 0}, 100, -1).valid);
    EXPECT_FALSE(cone({1, 0}, 100, NAN).valid);
    auto v = cone();
    v.boundary[1] = {900, 1000};
    EXPECT_FALSE(v.validGeometry());
    EXPECT_FALSE(v.matches(v.key()));
    EXPECT_FALSE(v.boundsVisible({{1000, 1000}, {10, 10}}));
}
TEST(RaidLightConeVisibilityTest,
     StationaryAimAndCoincidentReticleUseActualWeaponDirection)
{
    GameplayWorld w{config()};
    settle(w, {1500, 1000});
    const auto old = w.raidObservation().key();
    const auto builds = w.indoorObservationBuildCount();
    static_cast<void>(w.raidObservation());
    EXPECT_EQ(w.indoorObservationBuildCount(), builds);
    settle(w, {500, 1000});
    const auto current = w.raidObservation().key();
    EXPECT_FALSE(current == old);
    EXPECT_TRUE(w.raidObservation().pointVisible({800, 1000}));
    EXPECT_FALSE(w.raidObservation().pointVisible({1200, 1000}));
    EXPECT_FLOAT_EQ(current.direction.x, w.weaponAimDirection().x);
    EXPECT_FLOAT_EQ(current.direction.y, w.weaponAimDirection().y);
    settle(w, {1000, 1000});
    const auto atCenter = w.raidObservationKey();
    EXPECT_TRUE(std::isfinite(atCenter.direction.x));
    EXPECT_TRUE(std::isfinite(atCenter.direction.y));
    EXPECT_GT(atCenter.direction.x * atCenter.direction.x +
                  atCenter.direction.y * atCenter.direction.y,
              0.99F);
}
TEST(RaidLightConeVisibilityTest,
     DayNightTransitionUsesHalfSimulationSecondAndPauseDoesNotAdvance)
{
    GameplayWorld w{config()};
    aim(w, {1500, 1000}, false, 0);
    EXPECT_FLOAT_EQ(w.raidObservationKey().angleDegrees, 180);
    w.setRaidDaylight(false);
    aim(w, {1500, 1000}, false, 0.25F);
    EXPECT_FLOAT_EQ(w.raidObservationKey().angleDegrees, 140);
    EXPECT_FLOAT_EQ(w.raidObservationKey().maximumDistance, 800);
    const auto paused = w.raidObservation().key();
    aim(w, {1500, 1000}, false, 0);
    EXPECT_TRUE(w.raidObservation().key() == paused);
    aim(w, {1500, 1000}, false, 0.25F);
    EXPECT_FLOAT_EQ(w.raidObservationKey().angleDegrees, 100);
    EXPECT_FLOAT_EQ(w.raidObservationKey().maximumDistance, 600);
    EXPECT_FALSE(w.raidObservation().matches(paused));
}
TEST(RaidLightConeVisibilityTest, MenuLocksHeadingWhileMovementAndLightContinue)
{
    GameplayWorld w{config()};
    settle(w, {1500, 1000});
    const auto old = w.raidObservationKey();
    GameplayInput i;
    i.inventoryOpen = true;
    i.moveDown = true;
    i.aimWorldPosition = Vec2{500, 1000};
    w.setRaidDaylight(false);
    w.update(i, 0.25F);
    const auto current = w.raidObservationKey();
    EXPECT_FLOAT_EQ(current.direction.x, old.direction.x);
    EXPECT_FLOAT_EQ(current.direction.y, old.direction.y);
    EXPECT_GT(current.observer.y, old.observer.y);
    EXPECT_FLOAT_EQ(current.angleDegrees, 140);
    EXPECT_FALSE(w.raidObservation().matches(old));
    aim(w, {500, 1000}, false);
    EXPECT_FLOAT_EQ(w.raidObservationKey().direction.x,
                    w.weaponAimDirection().x);
}
TEST(RaidLightConeVisibilityTest,
     SpaceTransferImmediatelyUsesConfiguredIndoorProfileAndPreservesPortal)
{
    auto c = config();
    RaidInteriorWorldConfig interior;
    interior.id = room;
    interior.displayName = "测试室";
    interior.worldSize = {1000, 800};
    interior.interiorSpawn = {300, 300};
    interior.exteriorEntrance = {{950, 950}, {100, 100}};
    interior.exteriorReturn = {975, 975};
    interior.interiorExit = {{270, 270}, {140, 140}};
    c.interiors.push_back(interior);
    GameplayWorld w{c};
    w.setRaidDaylight(false);
    GameplayInput enter;
    enter.interactJustPressed = true;
    w.update(enter, 0);
    EXPECT_EQ(w.activeRaidSpaceId(), room);
    EXPECT_FLOAT_EQ(w.raidObservationKey().angleDegrees, 110);
    EXPECT_FLOAT_EQ(w.raidObservationKey().maximumDistance, 500);
    const auto old = w.raidObservation();
    w.update(enter, 0);
    EXPECT_TRUE(w.inOutdoorRaidSpace());
    EXPECT_FLOAT_EQ(w.raidObservationKey().angleDegrees, 100);
    EXPECT_FALSE(w.raidObservation().matches(old.key()));
}
TEST(RaidLightConeVisibilityTest,
     CurrentContextRejectsOldHeadingLightAndMissingOutdoorProofAtomically)
{
    const auto &content = publishedContentRegistry();
    auto p = makeNewAlphaProfile("cone-domain", content);
    p.assets.create(
        content.item(ItemDefinitionId{"item.container.backpack_expedition"}),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    ASSERT_TRUE(executeDeploy(p, content,
                              {"raid", "settle", 9917,
                               MapDefinitionId{"map.raid.frontier_exchange"}},
                              {p.revision, "deploy"})
                    .succeeded);
    const auto loot =
        std::find_if(p.pendingRaid->loot.begin(), p.pendingRaid->loot.end(),
                     [](const auto &l)
                     {
                         return l.spaceId == outdoorRaidSpaceId() &&
                                !l.corpseContainerAssetId &&
                                !l.requiresHighRisk;
                     });
    ASSERT_NE(loot, p.pendingRaid->loot.end());
    const auto id = loot->assetId;
    const auto view =
        observeAssetContractPoint(p, loot->spaceId, loot->position);
    auto key = view.key();
    const auto hash = profileStateFingerprint(p);
    EXPECT_FALSE(
        pickupRaidLoot(p, content, id, {p.revision, "missing"}).succeeded);
    key.direction = {-1, 0};
    EXPECT_FALSE(pickupRaidLoot(p, content, id, {p.revision, "stale-direction"},
                                &view, &key)
                     .succeeded);
    key = view.key();
    key.angleDegrees = 100;
    key.maximumDistance = 600;
    EXPECT_FALSE(
        pickupRaidLoot(p, content, id, {p.revision, "stale-light"}, &view, &key)
            .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
    key = view.key();
    EXPECT_TRUE(
        pickupRaidLoot(p, content, id, {p.revision, "visible"}, &view, &key)
            .succeeded);
}
TEST(RaidLightConeVisibilityTest, CorpseBehindConeCannotGrantInventoryAccess)
{
    const auto &content = publishedContentRegistry();
    auto p = makeNewAlphaProfile("cone-corpse", content);
    ASSERT_TRUE(executeDeploy(p, content,
                              {"raid", "settle", 9917,
                               MapDefinitionId{"map.raid.frontier_exchange"}},
                              {p.revision, "deploy"})
                    .succeeded);
    AssetInstanceId id{};
    for (const auto &[i, a] : p.assets.records())
        if (std::holds_alternative<RaidCorpseContainerAssetLocation>(
                a.location))
        {
            id = i;
            break;
        }
    ASSERT_NE(id, 0U);
    const auto location =
        std::get<RaidCorpseContainerAssetLocation>(p.assets.find(id)->location);
    ASSERT_TRUE(revealRaidCorpse(
        p, content, {location.sourceEnemyId, location.spaceId, {1000, 1000}}));
    const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, {});
    RaidSceneContainerAccess access{id, location.spaceId, {1060, 1000}};
    access.observation =
        buildRaidSpaceConeVisibility(*index, {location.spaceId,
                                              access.playerCenter,
                                              {1, 0},
                                              180,
                                              1000,
                                              RaidLightEnvironment::DayOutdoor,
                                              1});
    access.currentObservation = access.observation->key();
    EXPECT_FALSE(raidSceneContainerAccessible(p, access));
    access.observation =
        buildRaidSpaceConeVisibility(*index, {location.spaceId,
                                              access.playerCenter,
                                              {-1, 0},
                                              180,
                                              1000,
                                              RaidLightEnvironment::DayOutdoor,
                                              1});
    access.currentObservation = access.observation->key();
    EXPECT_TRUE(raidSceneContainerAccessible(p, access));
}

TEST(RaidLightConeVisibilityTest,
     SceneCommandRejectsOldDirectionAndNewlyHiddenProofWithZeroMutation)
{
    const auto &content = publishedContentRegistry();
    auto p = makeNewAlphaProfile("cone-box", content);
    const auto bag = p.assets.create(
        content.item(ItemDefinitionId{"item.container.backpack_expedition"}),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    ASSERT_TRUE(executeDeploy(p, content,
                              {"raid", "settle", 9917,
                               MapDefinitionId{"map.raid.frontier_exchange"}},
                              {p.revision, "deploy"})
                    .succeeded);
    AssetInstanceId box{};
    for (const auto &[id, a] : p.assets.records())
        if (std::holds_alternative<RaidSceneContainerAssetLocation>(a.location))
        {
            box = id;
            break;
        }
    ASSERT_NE(box, 0U);
    const auto loc =
        std::get<RaidSceneContainerAssetLocation>(p.assets.find(box)->location);
    const auto item =
        assetsInContainer(p, ProfileContainerId::compartment(box, 0))
            .front()
            ->instanceId;
    const auto index = RaidSpaceBlockerIndex::build({2400, 1440}, {});
    RaidSceneContainerAccess access{
        box, loc.spaceId, {loc.position.x - 60, loc.position.y}};
    auto key = RaidObservationKey{loc.spaceId,
                                  access.playerCenter,
                                  {1, 0},
                                  110,
                                  500,
                                  RaidLightEnvironment::WeakIndoor,
                                  1};
    access.observation = buildRaidSpaceConeVisibility(*index, key);
    access.currentObservation = key;
    const InventoryCommand command = InventoryMoveCommand{
        item, 0, {ProfileContainerId::compartment(bag, 0), {0, 0}}};
    ASSERT_TRUE(queryInventory(p, content, command, &access).canCommit);
    const auto hash = profileStateFingerprint(p);
    key.direction = {-1, 0};
    access.currentObservation = key;
    EXPECT_FALSE(executeInventory(p, content, command,
                                  {p.revision, "stale-facing"}, &access)
                     .succeeded);
    access.observation = buildRaidSpaceConeVisibility(*index, key);
    EXPECT_FALSE(
        executeInventory(p, content, command, {p.revision, "behind"}, &access)
            .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
}
TEST(RaidLightConeVisibilityTest,
     HiddenActorsStillSimulateAndBlindShotsAreNotFiltered)
{
    auto c = config();
    c.initialEnemies = {{{800, 975}, {50, 50}, 100}};
    GameplayWorld front{c}, back{c};
    settle(front, {1500, 1000});
    settle(back, {500, 1000});
    EXPECT_FALSE(
        front.raidEnemyObservable(front.enemies().front().combatTargetId()));
    EXPECT_EQ(front.enemies().front().health(),
              back.enemies().front().health());
    EXPECT_FLOAT_EQ(front.enemies().front().position().x,
                    back.enemies().front().position().x);
    EXPECT_EQ(front.player().health(), back.player().health());
}
TEST(RaidLightConeVisibilityTest,
     DistantWallCountDoesNotIncreaseLocalSolvingWork)
{
    std::vector<BallisticBlocker> walls{{1, {{1200, 700}, {8, 600}}}};
    auto index = RaidSpaceBlockerIndex::build({25600, 14400}, walls);
    RaidObservationWork before, after;
    const RaidObservationKey key{outdoorRaidSpaceId(),
                                 {1000, 1000},
                                 {1, 0},
                                 180,
                                 1000,
                                 RaidLightEnvironment::DayOutdoor,
                                 1};
    static_cast<void>(buildRaidSpaceConeVisibility(*index, key, &before));
    for (int n = 0; n < 5000; ++n)
        walls.push_back(
            {static_cast<std::uint64_t>(n + 2),
             {{5000.0F + (n % 200) * 70, 5000.0F + (n / 200) * 70}, {12, 12}}});
    index = RaidSpaceBlockerIndex::build({25600, 14400}, walls);
    static_cast<void>(buildRaidSpaceConeVisibility(*index, key, &after));
    EXPECT_EQ(before.localBlockers, after.localBlockers);
    EXPECT_EQ(before.rays, after.rays);
    EXPECT_EQ(before.blockerTests, after.blockerTests);
}

TEST(RaidLightConeVisibilityTest,
     LethalHeadFeedbackRetainsSourceAndDisappearsWhenFacingAway)
{
    auto c = config();
    c.playerSpawn = {975, 980};
    c.initialEnemies = {EnemySpawn{{1100, 1000}, {50, 50}, 4}};
    GameplayWorld w{c};
    auto weapon = *itemDefinition(ItemId::Rifle).weaponUse;
    weapon.baseDamage = 4;
    auto handling = deriveWeaponHandling(weapon);
    handling.minimumSpreadDegrees = handling.maximumSpreadDegrees = 0;
    w.configureWeaponFire(weapon, handling, false);
    GameplayInput i;
    i.aimWorldPosition = Vec2{1125, 1005};
    i.aimWorldBounds = Rect{{}, c.worldSize};
    w.update(i, 0);
    i.fireJustPressed = true;
    w.update(i, 0.001F);
    ASSERT_TRUE(w.shotFiredLastUpdate());
    i.fireJustPressed = false;
    for (int n = 0; n < 100 && w.hitResultsLastUpdate().empty(); ++n)
        w.update(i, 0.001F);
    ASSERT_FALSE(w.hitResultsLastUpdate().empty());
    const auto hit = w.hitResultsLastUpdate().front();
    EXPECT_TRUE(hit.targetKilled);
    EXPECT_EQ(hit.semantic, HitSemantic::Headshot);
    ASSERT_TRUE(w.hitFeedbackPresentation().semanticSource);
    EXPECT_EQ(w.hitFeedbackPresentation().semanticSource->targetId,
              hit.targetId);
    EXPECT_GT(w.observableHitFeedback().remainingSeconds, 0);
    i.aimWorldPosition = Vec2{500, 1005};
    w.update(i, 0);
    EXPECT_GT(w.hitFeedbackPresentation().remainingSeconds, 0);
    EXPECT_FLOAT_EQ(w.observableHitFeedback().remainingSeconds, 0);
}
TEST(RaidLightConeVisibilityTest,
     BlindShotWhileObservationLockedStillHitsHiddenActor)
{
    auto c = config();
    c.initialEnemies = {EnemySpawn{{800, 975}, {50, 50}, 100}};
    GameplayWorld w{c};
    auto weapon = *itemDefinition(ItemId::Rifle).weaponUse;
    auto handling = deriveWeaponHandling(weapon);
    handling.minimumSpreadDegrees = handling.maximumSpreadDegrees = 0;
    w.configureWeaponFire(weapon, handling, false);
    aim(w, {1500, 1000}, false, 0);
    const auto id = w.enemies().front().combatTargetId();
    ASSERT_FALSE(w.raidEnemyObservable(id));
    GameplayInput i;
    i.freezeObservationDirection = true;
    i.aimWorldPosition = Vec2{825, 1000};
    w.update(i, 0);
    i.fireJustPressed = true;
    w.update(i, 0.001F);
    i.fireJustPressed = false;
    for (int n = 0; n < 200 && w.hitResultsLastUpdate().empty(); ++n)
        w.update(i, 0.001F);
    ASSERT_FALSE(w.hitResultsLastUpdate().empty());
    EXPECT_EQ(w.hitResultsLastUpdate().front().targetId, id);
    EXPECT_GT(w.hitResultsLastUpdate().front().damageApplied, 0);
    EXPECT_FALSE(w.raidEnemyObservable(id));
}

TEST(RaidLightConeVisibilityTest,
     SessionReadsOnlyAuthoritativeClockAtSixAndEighteen)
{
    GameSession s;
    ASSERT_TRUE(s.startNewProfile("cone-clock"));
    auto &profile = const_cast<ProfileState &>(s.profile());
    ASSERT_TRUE(
        s.deployAlpha(9917, MapDefinitionId{"map.raid.frontier_exchange"}));
    s.update({}, 0);
    profile.worldClock.elapsedWorldMinutes = 5 * 60 + 59;
    s.update({}, 0.5F);
    EXPECT_FLOAT_EQ(s.world().raidObservationKey().angleDegrees, 100);
    profile.worldClock.elapsedWorldMinutes = 6 * 60;
    s.update({}, 0.25F);
    EXPECT_FLOAT_EQ(s.world().raidObservationKey().angleDegrees, 140);
    s.update({}, 0.25F);
    EXPECT_FLOAT_EQ(s.world().raidObservationKey().angleDegrees, 180);
    profile.worldClock.elapsedWorldMinutes = 18 * 60;
    s.update({}, 0.5F);
    EXPECT_FLOAT_EQ(s.world().raidObservationKey().angleDegrees, 100);
}

TEST(RaidLightConeVisibilityTest,
     CachedActorEligibilityRefreshesOnPoseFacingLightAndDeath)
{
    auto c = config();
    c.initialEnemies = {EnemySpawn{{1100, 975}, {50, 50}, 100}};
    GameplayWorld w{c};
    aim(w, {1500, 1000}, false, 0);
    const auto id = w.enemies().front().combatTargetId();
    ASSERT_TRUE(w.raidEnemyObservable(id));
    const auto solves = w.enemyObservationSolveCount();
    for (int n = 0; n < 5; ++n)
        EXPECT_TRUE(w.raidEnemyObservable(id));
    EXPECT_EQ(w.enemyObservationSolveCount(), solves);
    auto &actor = const_cast<Enemy &>(w.enemies().front());
    ASSERT_TRUE(actor.setPosition({800, 975}));
    EXPECT_FALSE(w.raidEnemyObservable(id));
    EXPECT_EQ(w.enemyObservationSolveCount(), solves + 1);
    GameplayInput i;
    i.aimWorldPosition = Vec2{500, 1000};
    i.aimWorldBounds = Rect{{}, c.worldSize};
    w.update(i, 0);
    EXPECT_TRUE(w.raidEnemyObservable(id));
    w.setRaidDaylight(false);
    w.update(i, 0.5F);
    EXPECT_FLOAT_EQ(w.raidObservationKey().maximumDistance, 600);
    static_cast<void>(actor.takeDamage(actor.health()));
    EXPECT_FALSE(w.raidEnemyObservable(id));
}

TEST(RaidLightConeVisibilityTest,
     FrameCacheNeverSurvivesExplicitRosterReplacement)
{
    auto c = config();
    c.developerRange = true;
    c.worldSize = {4000, 4000};
    c.initialEnemies = {EnemySpawn{{1100, 975}, {50, 50}, 100}};
    GameplayWorld w{c};
    aim(w, {1500, 1000}, false, 0);
    const auto old = w.enemies().front().combatTargetId();
    ASSERT_TRUE(w.raidEnemyObservable(old));
    const auto &definition = publishedContentRegistry().enemyCombatDefinition(
        EnemyCombatDefinitionId{"enemy.infected.basic"});
    ASSERT_TRUE(w.resetDeveloperRangeEnemies(definition, 0, 5));
    EXPECT_FALSE(w.raidEnemyObservable(old));
}

TEST(RaidLightConeVisibilityTest,
     ActualFourConsumerActorQueriesStayWithinBudget)
{
    for (int count : {32, 100})
    {
        auto c = config();
        c.playerSpawn = {984, 984};
        for (int n = 0; n < 48; ++n)
        {
            const float a = n * 6.28318530718F / 48;
            c.ballisticBlockers.push_back(
                {static_cast<std::uint64_t>(n + 1),
                 {{1000 + std::cos(a) * 300, 1000 + std::sin(a) * 300},
                  {5, 40}}});
        }
        for (int n = 0; n < count; ++n)
        {
            const float a = n * 6.28318530718F / count,
                        r = 100 + (n % 7) * 90.0F;
            c.initialEnemies.push_back(EnemySpawn{
                {1000 + std::cos(a) * r - 25, 1000 + std::sin(a) * r - 25},
                {50, 50},
                100});
        }
        GameplayWorld w{c};
        std::vector<double> samples;
        volatile int seen{};
        for (int frame = 0; frame < 160; ++frame)
        {
            const float a = frame * 0.09F;
            const Vec2 center{1000 + 3 * std::sin(a), 1000 + 3 * std::cos(a)};
            ASSERT_TRUE(const_cast<Player &>(w.player())
                            .setPosition({center.x - 16, center.y - 16}));
            GameplayInput i;
            i.aimWorldPosition = Vec2{center.x + std::cos(a) * 400,
                                      center.y + std::sin(a) * 400};
            i.aimWorldBounds = Rect{{}, c.worldSize};
            w.update(i, 0);
            const auto solves = w.enemyObservationSolveCount();
            const auto start = std::chrono::steady_clock::now();
            static_cast<void>(w.raidObservation());
            for (int consumer = 0; consumer < 4; ++consumer)
                for (const auto &enemy : w.enemies())
                    seen =
                        seen +
                        (w.raidEnemyObservable(enemy.combatTargetId()) ? 1 : 0);
            const auto elapsed = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            EXPECT_EQ(w.enemyObservationSolveCount() - solves,
                      static_cast<std::uint64_t>(count));
            if (frame >= 10)
                samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        const auto p95 = samples[142];
        EXPECT_LE(p95, 3.0) << count;
        if (const char *dir = std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR"))
        {
            std::filesystem::create_directories(dir);
            std::ofstream output{std::filesystem::path{dir} /
                                     "actual-consumer-performance.csv",
                                 std::ios::app};
            output << count << ",4,150," << p95 << ',' << samples.back() << ','
                   << w.enemyObservationSolveCount() << '\n';
        }
    }
}

TEST(RaidLightConeVisibilityTest,
     PublishedFiveSpacesAndDenseLocalGeometryStayWithinObservationBudget)
{
    for (const auto map : {MapDefinitionId{"map.raid.frontier_exchange"},
                           MapDefinitionId{"map.raid.hospital_district"}})
    {
        GameSession session;
        ASSERT_TRUE(session.startNewProfile("cone-perf"));
        ASSERT_TRUE(session.deployAlpha(9917, map));
        const auto &raid = *session.profile().pendingRaid;
        std::vector<BallisticBlocker> walls;
        for (const auto &r : raid.spatialLayout.ballisticBlockers)
            walls.push_back({static_cast<std::uint64_t>(walls.size() + 1),
                             {r.position, r.size}});
        const auto worldSize = publishedContentRegistry().map(map).worldSize;
        const auto index = RaidSpaceBlockerIndex::build(worldSize, walls);
        ASSERT_TRUE(index);
        const auto p = session.world().player().position();
        const float half = session.world().player().size() / 2;
        measureCone(*index, outdoorRaidSpaceId(), {p.x + half, p.y + half},
                    RaidLightEnvironment::DayOutdoor,
                    std::string{map.value()} + ".day");
        measureCone(*index, outdoorRaidSpaceId(), {p.x + half, p.y + half},
                    RaidLightEnvironment::NightOutdoor,
                    std::string{map.value()} + ".night");
        for (const auto &interior : raid.interiors)
        {
            walls.clear();
            for (const auto &r : interior.ballisticBlockers)
                walls.push_back({static_cast<std::uint64_t>(walls.size() + 1),
                                 {r.position, r.size}});
            const auto roomIndex =
                RaidSpaceBlockerIndex::build(interior.worldSize, walls);
            ASSERT_TRUE(roomIndex);
            measureCone(*roomIndex, interior.id,
                        {interior.interiorSpawn.x + half,
                         interior.interiorSpawn.y + half},
                        RaidLightEnvironment::WeakIndoor,
                        std::string{interior.id.value()});
        }
    }
    std::vector<BallisticBlocker> dense;
    for (int n = 0; n < 48; ++n)
    {
        const float a = n * 6.28318530718F / 48;
        dense.push_back(
            {static_cast<std::uint64_t>(n + 1),
             {{1000 + std::cos(a) * 300, 1000 + std::sin(a) * 300}, {5, 40}}});
    }
    const auto index = RaidSpaceBlockerIndex::build({3000, 3000}, dense);
    ASSERT_TRUE(index);
    measureCone(*index, room, {1000, 1000}, RaidLightEnvironment::DayOutdoor,
                "dense-local-48");
}
