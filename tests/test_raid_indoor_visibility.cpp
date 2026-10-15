#include "game_session.h"
#include "raid_lifecycle.h"
#include "raid_space_query.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
const RaidSpaceDefinitionId space{"raid_space.visibility.room"};
RaidVisibilityProjection observe(Vec2 position,
                                 const std::vector<BallisticBlocker> &walls,
                                 Vec2 size = {400, 300})
{
    const auto index = RaidSpaceBlockerIndex::build(size, walls);
    EXPECT_TRUE(index);
    return index ? buildRaidSpaceVisibility(*index, space, position)
                 : RaidVisibilityProjection{};
}
void position(GameSession &session, Vec2 center)
{
    const float half = session.world().player().size() * 0.5F;
    ASSERT_TRUE(const_cast<Player &>(session.world().player())
                    .setPosition({center.x - half, center.y - half}));
}
} // namespace

TEST(RaidIndoorVisibilityTest, ClearRoomIsAllDirectionAndBoundsAreClipped)
{
    const auto view = observe({200, 150}, {});
    ASSERT_TRUE(view.valid);
    EXPECT_TRUE(view.matches(space, {200, 150}));
    for (Vec2 p :
         std::vector<Vec2>{{1, 1}, {399, 1}, {399, 299}, {1, 299}, {200, 150}})
        EXPECT_TRUE(view.pointVisible(p));
    EXPECT_TRUE(view.boundsVisible({{10, 10}, {20, 20}}));
    EXPECT_FALSE(view.pointVisible({-1, 150}));
    EXPECT_FALSE(view.pointVisible({NAN, 0}));
    EXPECT_FALSE(view.boundsVisible({{500, 200}, {20, 20}}));
}
TEST(RaidIndoorVisibilityTest, WallHidesWholeTargetsAndAllowsOnlyExposedPart)
{
    const auto view = observe({80, 150}, {{1, {{190, 60}, {20, 180}}}});
    EXPECT_TRUE(view.pointVisible({100, 150}));
    EXPECT_FALSE(view.pointVisible({260, 150}));
    EXPECT_FALSE(view.boundsVisible({{250, 120}, {50, 50}}));
    EXPECT_TRUE(view.boundsVisible({{175, 100}, {50, 50}}));
    EXPECT_FALSE(view.pointVisible({195, 150}));
    const auto corner = observe({80, 30}, {{1, {{190, 60}, {20, 180}}}});
    EXPECT_TRUE(corner.pointVisible({260, 30}));
}
TEST(RaidIndoorVisibilityTest, ThinFullHeightWallNeverLeaksInLargeHospitalSpace)
{
    const std::vector<BallisticBlocker> walls{{1, {{1200, 0}, {0.5F, 1440}}}};
    const auto view = observe({600, 700}, walls, {2400, 1440});
    ASSERT_TRUE(view.valid);
    for (float y : {100.0F, 300.0F, 700.0F, 1100.0F, 1300.0F})
    {
        EXPECT_FALSE(view.pointVisible({1800, y}));
        EXPECT_FALSE(view.boundsVisible({{1700, y}, {20, 20}}));
    }
}
TEST(RaidIndoorVisibilityTest,
     InvalidObserverAndGeometryFailClosedWithoutOpaqueMemory)
{
    EXPECT_FALSE(observe({NAN, 0}, {}).valid);
    EXPECT_FALSE(observe({-1, 30}, {}).valid);
    const auto blocked = observe({195, 120}, {{1, {{190, 60}, {20, 180}}}});
    EXPECT_FALSE(blocked.valid);
    EXPECT_FALSE(blocked.pointVisible({40, 40}));
    const auto view = observe({50, 100}, {});
    EXPECT_FALSE(view.matches(space, {50, 101}));
    EXPECT_FALSE(
        view.matches(RaidSpaceDefinitionId{"raid_space.other"}, {50, 100}));
    EXPECT_FALSE(view.boundsVisible({{30, 30}, {NAN, 20}}));
}
TEST(RaidIndoorVisibilityTest,
     SharedNarrowGeometryPreservesOriginalLineOfSightContract)
{
    const std::vector<BallisticBlocker> walls{{1, {{90, 40}, {20, 80}}}};
    for (int x = 0; x <= 200; x += 10)
        for (int y = 0; y <= 160; y += 10)
            EXPECT_EQ(
                raidSightSegmentIntersectsRectInterior(
                    {40, 80}, {static_cast<float>(x), static_cast<float>(y)},
                    walls.front().bounds),
                !raidSpaceHasLineOfSight(
                    {40, 80}, {static_cast<float>(x), static_cast<float>(y)},
                    walls));
}
TEST(RaidIndoorVisibilityTest,
     ObservationCacheChangesOnMovementAndSpaceButNotMapProjection)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("visibility-cache"));
    ASSERT_TRUE(session.deployAlpha(
        9917, MapDefinitionId{"map.raid.frontier_exchange"}));
    const auto interior = session.profile().pendingRaid->interiors.front();
    position(session, {interior.exteriorEntrance.position.x +
                           interior.exteriorEntrance.size.x / 2,
                       interior.exteriorEntrance.position.y +
                           interior.exteriorEntrance.size.y / 2});
    GameplayInput enter;
    enter.interactJustPressed = true;
    session.update(enter, 0);
    ASSERT_EQ(session.world().activeRaidSpaceId(), interior.id);
    const auto hash = profileStateFingerprint(session.profile());
    const auto &first = session.world().indoorObservation();
    ASSERT_TRUE(first.valid);
    const auto builds = session.world().indoorObservationBuildCount();
    const auto boundary = first.boundary;
    static_cast<void>(session.world().activeInteriorMapProjection());
    EXPECT_EQ(session.world().indoorObservation().boundary.size(),
              boundary.size());
    EXPECT_EQ(session.world().indoorObservationBuildCount(), builds);
    position(session, {first.observer.x + 1, first.observer.y});
    static_cast<void>(session.world().indoorObservation());
    EXPECT_EQ(session.world().indoorObservationBuildCount(), builds + 1);
    EXPECT_EQ(profileStateFingerprint(session.profile()), hash);
    position(
        session,
        {interior.interiorExit.position.x + interior.interiorExit.size.x / 2,
         interior.interiorExit.position.y + interior.interiorExit.size.y / 2});
    session.update(enter, 0);
    EXPECT_TRUE(session.world().inOutdoorRaidSpace());
    EXPECT_TRUE(session.world().raidObservation().valid);
    EXPECT_EQ(session.world().raidObservation().spaceId,outdoorRaidSpaceId());
}
TEST(RaidIndoorVisibilityTest,
     MissingAndStaleIndoorInventoryObservationRejectWithoutMutation)
{
    const auto &content = publishedContentRegistry();
    auto p = makeNewAlphaProfile("visibility-domain", content);
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
    RaidSceneContainerAccess access{box, loc.spaceId, loc.position,
                                    std::nullopt};
    const InventoryCommand command = InventoryMoveCommand{
        item, 0, {ProfileContainerId::compartment(bag, 0), {0, 0}}};
    const auto hash = profileStateFingerprint(p);
    EXPECT_FALSE(queryInventory(p, content, command, &access).canCommit);
    EXPECT_FALSE(
        executeInventory(p, content, command, {p.revision, "hidden"}, &access)
            .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
    auto clear = observe(loc.position, {}, {1200, 800});
    clear.spaceId = loc.spaceId;
    access.observation = clear;
    access.currentObservation=clear.key();
    access.playerCenter.x += 1;
    EXPECT_FALSE(
        executeInventory(p, content, command, {p.revision, "stale"}, &access)
            .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
    access.playerCenter = loc.position;
    EXPECT_TRUE(queryInventory(p, content, command, &access).canCommit);
}
TEST(RaidIndoorVisibilityTest,
     IndoorGroundPickupNeedsCurrentSpaceAndRangeBeforeAnyCommit)
{
    const auto &content = publishedContentRegistry();
    auto p = makeNewAlphaProfile("visibility-ground", content);
    p.assets.create(
        content.item(ItemDefinitionId{"item.container.backpack_expedition"}),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    ASSERT_TRUE(executeDeploy(p, content,
                              {"raid", "settle", 9917,
                               MapDefinitionId{"map.raid.hospital_district"}},
                              {p.revision, "deploy"})
                    .succeeded);
    const auto loot = std::find_if(
        p.pendingRaid->loot.begin(), p.pendingRaid->loot.end(),
        [](const auto &l) { return l.spaceId != outdoorRaidSpaceId(); });
    ASSERT_NE(loot, p.pendingRaid->loot.end());
    const auto id = loot->assetId;
    const auto pos = loot->position;
    const auto hash = profileStateFingerprint(p);
    EXPECT_FALSE(
        pickupRaidLoot(p, content, id, {p.revision, "missing"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
    auto view = observe(pos, {}, {2400, 1440});
    auto key=view.key();
    EXPECT_FALSE(
        pickupRaidLoot(p, content, id, {p.revision, "wrong-space"}, &view, &key)
            .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
    view.spaceId = loot->spaceId;
    view.observer.x += 100;
    key=view.key();
    EXPECT_FALSE(
        pickupRaidLoot(p, content, id, {p.revision, "far"}, &view, &key).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), hash);
    view.observer = pos;
    key=view.key();
    EXPECT_TRUE(pickupRaidLoot(p, content, id, {p.revision, "visible"}, &view, &key)
                    .succeeded);
}

TEST(RaidIndoorVisibilityTest, MalformedAndZeroAreaObservationCannotGrantAccess)
{
    auto view = observe({80, 150}, {});
    ASSERT_TRUE(view.validGeometry());
    view.boundary.front().x = NAN;
    EXPECT_FALSE(view.pointVisible({100, 150}));
    EXPECT_FALSE(view.boundsVisible({{60, 100}, {100, 100}}));
    EXPECT_FALSE(view.matches(space, {80, 150}));
    view = observe({80, 150}, {});
    view.boundary.assign(3, {80, 150});
    EXPECT_FALSE(view.boundsVisible({{60, 100}, {100, 100}}));
}

TEST(RaidIndoorVisibilityTest,
     ObservationDoesNotPauseOrRemoveHiddenActorsOrChangeSimulation)
{
    RaidWorldConfig config;
    config.worldSize = {400, 300};
    config.playerSpawn = {20, 20};
    config.extractionPoint = {{330, 230}, {60, 50}};
    RaidInteriorWorldConfig room;
    room.id = space;
    room.displayName = "Observation test room";
    room.worldSize = {400, 300};
    room.exteriorEntrance = {{20, 20}, {100, 100}};
    room.exteriorReturn = {30, 30};
    room.interiorSpawn = {55, 125};
    room.interiorExit = {{10, 10}, {100, 50}};
    room.initialEnemies = {EnemySpawn{{250, 120}, {50, 50}, 40}};
    room.ballisticBlockers = {{1, {{190, 60}, {20, 180}}}};
    config.interiors = {room};
    GameplayWorld observed{config}, control{config};
    GameplayInput enter;
    enter.interactJustPressed = true;
    observed.update(enter, 0);
    control.update(enter, 0);
    ASSERT_EQ(observed.activeRaidSpaceId(), space);
    ASSERT_EQ(observed.enemies().size(), 1U);
    const auto id = observed.enemies().front().combatTargetId();
    ASSERT_FALSE(observed.raidEnemyObservable(id));
    const auto before = observed.enemies().front().position();
    const_cast<Enemy &>(observed.enemies().front()).hearTarget({80, 150});
    const_cast<Enemy &>(control.enemies().front()).hearTarget({80, 150});
    for (int i = 0; i < 12; ++i)
    {
        static_cast<void>(observed.indoorObservation());
        observed.update({}, 0.05F);
        control.update({}, 0.05F);
        ASSERT_EQ(observed.enemies().size(), control.enemies().size());
        ASSERT_EQ(observed.enemies().size(), 1U);
        const auto &a = observed.enemies().front(),
                   &b = control.enemies().front();
        EXPECT_EQ(a.combatTargetId(), id);
        EXPECT_FLOAT_EQ(a.position().x, b.position().x);
        EXPECT_FLOAT_EQ(a.position().y, b.position().y);
        EXPECT_EQ(a.health(), b.health());
        EXPECT_EQ(a.awarenessState(), b.awarenessState());
        EXPECT_EQ(a.attackPhase(), b.attackPhase());
        EXPECT_EQ(observed.score(), control.score());
    }
    const auto after = observed.enemies().front().position();
    EXPECT_GT(std::abs(after.x - before.x) + std::abs(after.y - before.y),
              0.1F);
    EXPECT_EQ(observed.ballisticBlockers().size(),
              control.ballisticBlockers().size());
    static_cast<void>(
        const_cast<Enemy &>(observed.enemies().front()).takeDamage(40));
    static_cast<void>(
        const_cast<Enemy &>(control.enemies().front()).takeDamage(40));
    observed.update({}, 0.01F);
    control.update({}, 0.01F);
    EXPECT_TRUE(observed.enemies().empty());
    EXPECT_TRUE(control.enemies().empty());
    EXPECT_EQ(observed.score(), control.score());
}

TEST(RaidIndoorVisibilityTest,
     AllPublishedInteriorsKeepMapRightsFrozenAssetsAndWorkingExits)
{
    for (const auto map : {MapDefinitionId{"map.raid.frontier_exchange"},
                           MapDefinitionId{"map.raid.hospital_district"}})
    {
        GameSession session;
        ASSERT_TRUE(session.startNewProfile("visibility-published"));
        ASSERT_TRUE(session.deployAlpha(9917, map));
        const auto interiors = session.profile().pendingRaid->interiors;
        const auto frozen = session.profile().pendingRaid->loot;
        for (const auto &room : interiors)
        {
            position(session, {room.exteriorEntrance.position.x +
                                   room.exteriorEntrance.size.x / 2,
                               room.exteriorEntrance.position.y +
                                   room.exteriorEntrance.size.y / 2});
            GameplayInput enter;
            enter.interactJustPressed = true;
            session.update(enter, 0);
            ASSERT_EQ(session.world().activeRaidSpaceId(), room.id);
            const auto mapBefore =
                session.world().activeInteriorMapProjection();
            const auto hash = profileStateFingerprint(session.profile());
            const auto &view = session.world().indoorObservation();
            ASSERT_TRUE(view.validGeometry());
            EXPECT_EQ(view.spaceId, room.id);
            const auto &player = session.world().player();
            EXPECT_TRUE(view.boundsVisible(
                {player.position(), {player.size(), player.size()}}));
            const auto mapAfter = session.world().activeInteriorMapProjection();
            ASSERT_TRUE(mapBefore && mapAfter);
            EXPECT_EQ(mapBefore->layoutKnown, mapAfter->layoutKnown);
            EXPECT_FLOAT_EQ(mapBefore->visibleBounds.position.x,
                            mapAfter->visibleBounds.position.x);
            EXPECT_FLOAT_EQ(mapBefore->visibleBounds.size.x,
                            mapAfter->visibleBounds.size.x);
            EXPECT_EQ(profileStateFingerprint(session.profile()), hash);
            EXPECT_EQ(session.profile().pendingRaid->loot, frozen);
            if (const char *dir =
                    std::getenv("RAIDLINE_INDOOR_VISIBILITY_QA_DIR"))
            {
                const auto index = RaidSpaceBlockerIndex::build(
                    room.worldSize, session.world().ballisticBlockers());
                ASSERT_TRUE(index);
                std::size_t candidates{};
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < 20; ++i)
                    EXPECT_TRUE(buildRaidSpaceVisibility(
                                    *index, room.id, view.observer, &candidates)
                                    .validGeometry());
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - start)
                                      .count() /
                                  20;
                std::filesystem::create_directories(dir);
                std::ofstream metrics{
                    std::filesystem::path{dir} /
                                      (std::string{room.id.value()} + "-query-metrics.json")};
                metrics << "{\"space_id\":\"" << room.id.value()
                        << "\",\"blockers\":" << index->blockerCount()
                        << ",\"candidate_tests_per_build\":" << candidates
                        << ",\"boundary_vertices\":" << view.boundary.size()
                        << ",\"mean_build_ms\":" << ms << "}\n";
            }
            position(
                session,
                {room.interiorExit.position.x + room.interiorExit.size.x / 2,
                 room.interiorExit.position.y + room.interiorExit.size.y / 2});
            session.update(enter, 0);
            EXPECT_TRUE(session.world().inOutdoorRaidSpace());
        }
    }
}
