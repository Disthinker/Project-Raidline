#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>

#include "base_world.h"
#include "economy_domain.h"
#include "game_session.h"
#include "save_repository.h"
#include "weapon_ammo_domain.h"
#include "weapon_component_domain.h"

namespace
{
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};
const ItemDefinitionId suppressor{"item.component.suppressor_5_45"};
const auto &content() { return publishedContentRegistry(); }

WeaponUseDefinition weapon(bool quiet)
{
    auto definition = *content().item(rifle).weaponUse;
    if (quiet) definition.acoustics = content().item(suppressor).weaponComponent->acoustics;
    return definition;
}

AssetInstanceId find(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}

void fire(WorldShootingRuntime &shooting)
{
    GameplayInput input;
    input.fireJustPressed = true;
    input.firePressed = true;
    input.aimWorldPosition = Vec2{1000, 1800};
    EnemyRoster<> targets;
    shooting.beginFrame(0.016F);
    shooting.updateAim(input, {1000, 1000}, {0, 1}, {6000, 6000}, 0.016F);
    static_cast<void>(shooting.advanceShots(input, 0.016F, {1000, 1000}, 52,
        false, false, {6000, 6000}, targets, {}));
}

std::optional<BaseDefenseSnapshot> arena()
{
    BaseDefenseSnapshot seed;
    seed.eventId = "suppressor-defense";
    seed.siegeSequence = 1;
    seed.seed = 12345;
    seed.frozenPopulation = 8;
    seed.frozenMoraleTier = 1;
    seed.frozenSiteThreat = 1;
    seed.siteDefinitionId = "regional_base_site.greyline_yard";
    seed.layoutIdentity = "suppressor-arena";
    seed.worldSize = {6000, 6000};
    seed.safeCore = {{2300, 2300}, {1200, 1000}};
    seed.playerPosition = {4000, 4000};
    return BaseDefenseRuntime::prepare(seed, {},
        content().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
}
}

TEST(WeaponSuppressorRuntimeTest, SuccessfulShotFreezesAcousticsAcrossAWeaponChange)
{
    WorldShootingRuntime shooting;
    shooting.configureWeapon(weapon(true));
    fire(shooting);
    ASSERT_TRUE(shooting.shotFiredLastUpdate());
    EXPECT_EQ(shooting.shotAcousticsLastUpdate(), weapon(true).acoustics);
    shooting.configureWeapon(weapon(false));
    EXPECT_EQ(shooting.shotAcousticsLastUpdate(), weapon(true).acoustics);
    shooting.beginFrame(0.016F);
    EXPECT_FALSE(shooting.shotFiredLastUpdate());
    EXPECT_EQ(shooting.shotAcousticsLastUpdate(), WeaponAcousticsDefinition{});
    shooting.clearSpatialTransientPresentation();
    EXPECT_EQ(shooting.shotAcousticsLastUpdate(), WeaponAcousticsDefinition{});
}

TEST(WeaponSuppressorRuntimeTest, BlockedInputCannotPublishAQuietShotFact)
{
    WorldShootingRuntime shooting;
    shooting.configureWeapon(weapon(true));
    GameplayInput input;
    input.firePressed = true;
    EnemyRoster<> targets;
    shooting.beginFrame(0.016F);
    shooting.updateAim(input, {1000, 1000}, {1, 0}, {6000, 6000}, 0.016F);
    static_cast<void>(shooting.advanceShots(input, 0.016F, {1000, 1000}, 52,
        false, true, {6000, 6000}, targets, {}));
    EXPECT_FALSE(shooting.shotFiredLastUpdate());
    EXPECT_EQ(shooting.shotAcousticsLastUpdate(), WeaponAcousticsDefinition{});
}

TEST(WeaponSuppressorRuntimeTest, RestoringAnOldCheckpointKeepsTheCurrentStaticCapabilityWithoutReplayingSound)
{
    WorldShootingRuntime previous;
    previous.configureWeapon(weapon(false));
    const auto checkpoint = previous.checkpoint();
    WorldShootingRuntime restored;
    restored.configureWeapon(weapon(true));
    fire(restored);
    ASSERT_TRUE(restored.shotFiredLastUpdate());
    ASSERT_TRUE(restored.restoreCheckpoint(checkpoint));
    EXPECT_FALSE(restored.shotFiredLastUpdate());
    EXPECT_EQ(restored.shotAcousticsLastUpdate(), WeaponAcousticsDefinition{});
    fire(restored);
    ASSERT_TRUE(restored.shotFiredLastUpdate());
    EXPECT_EQ(restored.shotAcousticsLastUpdate(), weapon(true).acoustics);
}

TEST(WeaponSuppressorRuntimeTest, ARealEquippedSessionShotUsesTheInstalledComponent)
{
    const auto root = std::filesystem::temp_directory_path() /
        ("raidline-suppressor-shot-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{root};
    for (bool quiet : {false, true})
    {
        SCOPED_TRACE(quiet ? "suppressed" : "ordinary");
        auto profile = makeNewPublishedProfile("session-acoustics", content());
        const auto gun = find(profile, rifle);
        ASSERT_NE(gun, 0U);
        if (quiet)
        {
            profile.currency = 1000;
            ASSERT_TRUE(executeEconomy(profile, content(), PurchaseCommand{suppressor, 1},
                {profile.revision, "buy"}).succeeded);
            ASSERT_TRUE(executeWeaponComponentChange(profile, content(),
                {gun, WeaponComponentSlot::Muzzle, find(profile, suppressor)},
                {profile.revision, "install"}).succeeded);
        }
        const auto magazine = find(profile, ItemDefinitionId{"item.magazine.5_45x39_30"});
        const auto ammo = find(profile, ItemDefinitionId{"item.ammunition.5_45x39_standard"});
        ASSERT_NE(magazine, 0U);
        ASSERT_NE(ammo, 0U);
        ASSERT_TRUE(executeWeaponAmmo(profile, content(), LoadMagazineCommand{magazine, ammo, 3},
            {profile.revision, "load"}).succeeded);
        ASSERT_TRUE(executeWeaponAmmo(profile, content(),
            InstallMagazineAndChamberCommand{gun, magazine}, {profile.revision, "chamber"}).succeeded);
        ASSERT_TRUE(executeInventory(profile, content(),
            InventoryEquipCommand{gun, EquipmentSlotKind::PrimaryWeapon},
            {profile.revision, "equip"}).succeeded);
        ASSERT_TRUE(SaveRepository(root).save(profile, content().contentVersion()).succeeded);
        GameSession session;
        session.configurePersistence(root);
        ASSERT_TRUE(session.continueProfile());
        ASSERT_TRUE(session.deployAlpha(123, MapDefinitionId{"map.v0.test"}));
        ASSERT_TRUE(session.profile().pendingRaid->encounterGroups.empty());
        // Separate hearing from visual discovery using the real frozen actors.
        // The ordinary test map has no encounter-group propagation.
        auto &player = const_cast<Player &>(session.world().player());
        ASSERT_TRUE(player.setPosition({80, 80}));
        const Vec2 playerCenter{player.position().x + player.size() / 2,
                                player.position().y + player.size() / 2};
        CombatTargetId listener = kInvalidCombatTargetId;
        for (const auto &enemy : session.world().enemies())
        {
            const float range = std::hypot(
                enemy.position().x + enemy.size().x / 2 - playerCenter.x,
                enemy.position().y + enemy.size().y / 2 - playerCenter.y);
            if (range > 520 && range < 850 && !enemy.lastKnownTargetPosition())
            {
                listener = enemy.combatTargetId();
                break;
            }
        }
        ASSERT_NE(listener, kInvalidCombatTargetId);
        GameplayInput input;
        input.aimWorldPosition = Vec2{80, 650};
        input.firePressed = true;
        for (unsigned frame = 0; frame < 90; ++frame)
        {
            session.update(input, 0.016F);
            if (session.world().shotFiredLastUpdate()) break;
        }
        ASSERT_TRUE(session.world().shotFiredLastUpdate());
        EXPECT_EQ(session.world().shotAcousticsLastUpdate(), weapon(quiet).acoustics);
        bool checkedListener{};
        for (const auto &enemy : session.world().enemies())
            if (enemy.combatTargetId() == listener)
            {
                checkedListener = true;
                const auto target = enemy.lastKnownTargetPosition();
                EXPECT_EQ(target.has_value(), !quiet);
                if (target)
                {
                    EXPECT_NEAR(target->x, playerCenter.x, 0.1F);
                    EXPECT_NEAR(target->y, playerCenter.y, 0.1F);
                }
            }
        EXPECT_TRUE(checkedListener);
        const auto after = session.profile().assets.find(gun);
        ASSERT_NE(after, nullptr);
        EXPECT_LT(magazineRoundCount(session.profile(), magazine), 2U);
    }
}

TEST(WeaponSuppressorRuntimeTest, PerimeterActualGunfireKeepsVisualAndHearingRangesSeparate)
{
    for (bool quiet : {false, true})
    {
        SCOPED_TRACE(quiet ? "suppressed" : "ordinary");
        BaseWorld world;
        BaseInput walk;
        walk.moveDown = true;
        for (unsigned frame = 0; frame < 400 &&
            world.playerSafetyZone() != HomeRegionSafetyZone::Perimeter; ++frame)
            static_cast<void>(world.update(walk, 0.1F));
        ASSERT_EQ(world.playerSafetyZone(), HomeRegionSafetyZone::Perimeter);
        const Vec2 pos = world.playerPosition(), size = world.playerSize();
        const Vec2 center{pos.x + size.x / 2, pos.y + size.y / 2};
        const Vec2 enemyPosition{center.x + 1100 - 25, center.y - 25};
        HomePerimeterSiteSnapshot snapshot;
        snapshot.baseSiteDefinitionId = RegionalBaseSiteDefinitionId{world.siteDefinitionId()};
        snapshot.cycleIndex = 4;
        snapshot.seed = 17;
        snapshot.enemies = {{11, enemyPosition, enemyPosition, {50, 50}, 100, 100}};
        world.configureHomePerimeter(&snapshot);
        ASSERT_EQ(world.perimeterEnemies().size(), 1U);
        ASSERT_FALSE(world.perimeterEnemies().front().lastKnownTargetPosition());
        world.configureWeaponFire(weapon(quiet));
        BaseInput shot;
        shot.firePressed = true;
        shot.aimWorldPosition = Vec2{center.x, center.y + 500};
        static_cast<void>(world.update(shot, 0.001F));
        ASSERT_TRUE(world.shotFiredLastUpdate());
        const auto target = world.perimeterEnemies().front().lastKnownTargetPosition();
        EXPECT_EQ(target.has_value(), !quiet);
        if (target)
        {
            EXPECT_NEAR(target->x, center.x, 0.1F);
            EXPECT_NEAR(target->y, center.y, 0.1F);
        }
    }
}

TEST(WeaponSuppressorRuntimeTest, DefenseActualShotHearingDoesNotReplaceItsWaveObjective)
{
    for (bool quiet : {false, true})
    {
        SCOPED_TRACE(quiet ? "suppressed" : "ordinary");
        auto saved = arena();
        ASSERT_TRUE(saved);
        const Vec2 player = saved->playerPosition, size{40, 52};
        const Vec2 center{player.x + 20, player.y + 26};
        const auto &wave = saved->wavePlans.front();
        Enemy enemy{{center.x + 1100 - 16, center.y - 24}, {32, 48}, {},
            wave.enemyMaxHealth, wave.enemyIds.front()};
        saved->enemies = {enemy.checkpoint()};
        saved->spawnedEnemyCount = 1;
        saved->nextSpawnDelay = 1;
        WorldShootingRuntime shooting;
        shooting.configureWeapon(weapon(quiet));
        saved->shooting = shooting.checkpoint();
        BaseDefenseRuntime runtime;
        ASSERT_TRUE(runtime.resume(*saved, {}));
        ASSERT_TRUE(runtime.playerExposed(center));
        GameplayInput input;
        input.firePressed = true;
        input.aimWorldPosition = Vec2{center.x, center.y + 500};
        shooting.beginFrame(0.001F);
        shooting.updateAim(input, center, {0, 1}, saved->worldSize, 0.001F);
        runtime.advance(input, 0.001F, player, size, false, shooting, {});
        ASSERT_TRUE(shooting.shotFiredLastUpdate());
        ASSERT_EQ(runtime.enemies().size(), 1U);
        const auto target = runtime.enemies().front().lastKnownTargetPosition();
        if (quiet)
            EXPECT_FALSE(target && std::hypot(target->x - center.x, target->y - center.y) < 0.1F);
        else
        {
            ASSERT_TRUE(target);
            EXPECT_NEAR(target->x, center.x, 0.1F);
            EXPECT_NEAR(target->y, center.y, 0.1F);
        }
        EXPECT_EQ(runtime.state().killedIds.size(), 0U);
        EXPECT_EQ(runtime.state().breachedIds.size(), 0U);
    }
}

TEST(WeaponSuppressorRuntimeTest, AListenerStillAlertsItsEncounterGroupOutsideTheQuietRadius)
{
    RaidWorldConfig config;
    config.worldSize = {3000, 2000};
    config.playerSpawn = {1000, 1000};
    config.extractionPoint = {{2600, 1500}, {100, 100}};
    EnemySpawn near{{1300, 1000}, {50, 50}, 100};
    near.encounterGroupInstanceId = "encounter:quiet:group";
    EnemySpawn far{{2200, 1000}, {50, 50}, 100};
    far.encounterGroupInstanceId = near.encounterGroupInstanceId;
    EnemySpawn unrelated{{1800, 1000}, {50, 50}, 100};
    unrelated.encounterGroupInstanceId = "encounter:quiet:other";
    config.initialEnemies = {near, far, unrelated};
    GameplayWorld world{config};
    WorldShootingRuntime shooting;
    shooting.configureWeapon(weapon(true));
    fire(shooting);
    ASSERT_TRUE(shooting.shotFiredLastUpdate());
    world.emitPlayerNoise(900 * shooting.shotAcousticsLastUpdate().noiseRadiusMultiplier);
    ASSERT_EQ(world.enemies().size(), 3U);
    EXPECT_EQ(world.enemies()[0].awarenessState(), EnemyAwarenessState::Alerted);
    EXPECT_EQ(world.enemies()[1].awarenessState(), EnemyAwarenessState::Alerted);
    EXPECT_EQ(world.enemies()[2].awarenessState(), EnemyAwarenessState::Unaware);
}
