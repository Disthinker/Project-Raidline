#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include <utility>

#include "content_registry.h"
#include "collision.h"
#include "gameplay_input.h"
#include "gameplay_world.h"

namespace
{
    GameplayInput makeFireInput()
    {
        GameplayInput input{};
        input.fireJustPressed = true;
        input.firePressed = true;
        return input;
    }

    class SequenceLootRandomSource final : public LootRandomSource
    {
    public:
        explicit SequenceLootRandomSource(
            std::vector<std::uint32_t> values)
            : values_{std::move(values)}
        {
        }

        std::uint32_t next(
            std::uint32_t) override
        {
            if (position_ >= values_.size())
            {
                throw std::runtime_error{
                    "Unexpected additional loot random draw"};
            }

            return values_[position_++];
        }

        std::size_t drawCount() const noexcept
        {
            return position_;
        }

    private:
        std::vector<std::uint32_t> values_;
        std::size_t position_{};
    };

    void movePlayerToCabinet(
        GameplayWorld &world)
    {
        GameplayInput moveRight{};
        moveRight.moveRight = true;
        world.update(moveRight, 1.0F);
        ASSERT_TRUE(world.canInteractWithContainer());
    }

    float playerEnemyCenterDistance(
        const GameplayWorld &world)
    {
        const Player &player = world.player();
        const Enemy &enemy = world.enemies().front();
        const Vec2 playerCenter{
            player.position().x + player.size() / 2.0F,
            player.position().y + player.size() / 2.0F};
        const Vec2 enemyCenter{
            enemy.position().x + enemy.size().x / 2.0F,
            enemy.position().y + enemy.size().y / 2.0F};
        return std::hypot(
            playerCenter.x - enemyCenter.x,
            playerCenter.y - enemyCenter.y);
    }

    Rect enemyBounds(const Enemy &enemy)
    {
        return Rect{enemy.position(), enemy.size()};
    }

    bool advanceUntilFirstScratchHits(
        GameplayWorld &world)
    {
        const int initialHealth = world.player().health();
        constexpr float kStep{1.0F / 120.0F};
        for (int frame = 0; frame < 600; ++frame)
        {
            world.update(GameplayInput{}, kStep);
            if (world.player().health() < initialHealth)
            {
                return world.enemies().front().attackType() ==
                       EnemyAttackType::Scratch;
            }
        }
        return false;
    }

    bool retreatAndHoldUntilGrabStarts(
        GameplayWorld &world)
    {
        constexpr float kStep{1.0F / 120.0F};
        for (int frame = 0; frame < 600; ++frame)
        {
            if (world.enemies().front().attackType() ==
                EnemyAttackType::Grab)
            {
                return true;
            }

            GameplayInput retreat{};
            if (playerEnemyCenterDistance(world) < 140.0F)
            {
                retreat.moveDown = true;
            }
            world.update(retreat, kStep);
        }
        return false;
    }

    bool advanceUntilGrabBites(
        GameplayWorld &world)
    {
        const int healthBeforeBite = world.player().health();
        constexpr float kStep{1.0F / 120.0F};
        for (int frame = 0; frame < 360; ++frame)
        {
            world.update(GameplayInput{}, kStep);
            if (world.player().health() <= healthBeforeBite - 2)
            {
                return true;
            }
        }
        return false;
    }

    RaidWorldConfig makeHighRiskWorldConfig(
        std::uint64_t seed = 7U)
    {
        RaidWorldConfig config;
        config.worldSize = Vec2{1280.0F, 720.0F};
        config.playerSpawn = Vec2{600.0F, 320.0F};
        config.extractionPoint =
            ContentRect{Vec2{1000.0F, 560.0F}, Vec2{120.0F, 100.0F}};
        config.initialEnemies.clear();
        config.playerMaximumHealth = 100;
        config.playerCurrentHealth = 100;
        config.deferPlayerDamageResolution = true;
        config.highRisk = HighRiskWorldConfig{
            true,
            0.10F,
            ContentRect{Vec2{560.0F, 280.0F}, Vec2{180.0F, 160.0F}},
            0.50F,
            0.05F,
            0.10F,
            2U,
            3U,
            {
                EnemySpawn{Vec2{20.0F, 20.0F}, Vec2{50.0F, 50.0F}, 12},
                EnemySpawn{Vec2{1210.0F, 20.0F}, Vec2{50.0F, 50.0F}, 12},
                EnemySpawn{Vec2{20.0F, 650.0F}, Vec2{50.0F, 50.0F}, 12},
                EnemySpawn{Vec2{1210.0F, 650.0F}, Vec2{50.0F, 50.0F}, 12}},
            ContentRect{Vec2{590.0F, 310.0F}, Vec2{100.0F, 100.0F}},
            0.20F,
            ContentRect{Vec2{850.0F, 450.0F}, Vec2{250.0F, 180.0F}},
            seed,
            ContentRect{Vec2{800.0F, 40.0F}, Vec2{180.0F, 150.0F}},
            0.25F,
            20000U};
        return config;
    }

    RaidWorldConfig makeInteriorDiscoveryWorldConfig()
    {
        RaidWorldConfig config;
        config.worldSize = Vec2{800.0F, 600.0F};
        config.playerSpawn = Vec2{500.0F, 100.0F};
        config.extractionPoint =
            ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
        config.initialEnemies.clear();
        RaidInteriorWorldConfig interior;
        interior.id = RaidSpaceDefinitionId{"raid_space.test.office"};
        interior.displayName = "Test Office";
        interior.worldSize = Vec2{480.0F, 360.0F};
        interior.exteriorEntrance =
            ContentRect{Vec2{80.0F, 80.0F}, Vec2{100.0F, 100.0F}};
        interior.exteriorReturn = Vec2{200.0F, 100.0F};
        interior.interiorSpawn = Vec2{80.0F, 80.0F};
        interior.interiorExit =
            ContentRect{Vec2{60.0F, 60.0F}, Vec2{120.0F, 120.0F}};
        config.interiors.push_back(std::move(interior));
        return config;
    }
} // namespace

// 初始 Player 位置是 (640, 360)
TEST(GameplayWorldTest, InitialPlayerPosition)
{
    GameplayWorld world;
    const Vec2 position = world.player().position();

    EXPECT_FLOAT_EQ(position.x, 640.0f);
    EXPECT_FLOAT_EQ(position.y, 360.0f);
}

TEST(GameplayWorldTest, InitialPlayerHealthIsFull)
{
    const GameplayWorld world;

    EXPECT_EQ(world.player().health(), 3);
    EXPECT_EQ(world.player().maxHealth(), 3);
    EXPECT_FALSE(world.player().isDead());
}

// 初始逻辑弹道集合为空。
TEST(GameplayWorldTest, InitialLogicalBallisticsEmpty)
{
    GameplayWorld world;
    EXPECT_TRUE(world.logicalBallistics().empty());
}

// Week28 正式 Raid 默认部署三个确定性 Enemy。
TEST(GameplayWorldTest, InitialEnemiesState)
{
    GameplayWorld world;
    ASSERT_EQ(world.enemies().size(), 3u);

    const Enemy &enemy = world.enemies()[0];
    const Vec2 enemyPosition = enemy.position();
    const Vec2 enemySize = enemy.size();

    EXPECT_FLOAT_EQ(enemyPosition.x, 600.0f);
    EXPECT_FLOAT_EQ(enemyPosition.y, 100.0f);
    EXPECT_FLOAT_EQ(enemySize.x, 50.0f);
    EXPECT_FLOAT_EQ(enemySize.y, 50.0f);
    EXPECT_EQ(enemy.health(), 3);
    EXPECT_EQ(enemy.maxHealth(), 3);
    EXPECT_FALSE(enemy.isDead());

    EXPECT_FLOAT_EQ(world.enemies()[1].position().x, 350.0F);
    EXPECT_FLOAT_EQ(world.enemies()[1].position().y, 500.0F);
    EXPECT_FLOAT_EQ(world.enemies()[2].position().x, 930.0F);
    EXPECT_FLOAT_EQ(world.enemies()[2].position().y, 500.0F);
    for (const Enemy &deployedEnemy : world.enemies())
    {
        EXPECT_EQ(
            deployedEnemy.awarenessState(),
            EnemyAwarenessState::Unaware);
    }
}

TEST(GameplayWorldTest, DefaultSquadAcquiresPlayerAndAssignsOneEngager)
{
    GameplayWorld world;

    world.update(GameplayInput{}, 0.0F);
    world.update(GameplayInput{}, 1.0F / 120.0F);

    std::size_t engageCount{};
    for (const Enemy &enemy : world.enemies())
    {
        EXPECT_EQ(
            enemy.awarenessState(),
            EnemyAwarenessState::Alerted);
        if (enemy.tacticalRole() == EnemyTacticalRole::Engage)
        {
            ++engageCount;
        }
    }

    EXPECT_EQ(engageCount, 1U);
}

TEST(GameplayWorldTest, CloseSquadStartsAtMostOneAttackPerSubstep)
{
    GameplayWorld world{
        std::vector<EnemySpawn>{
            EnemySpawn{Vec2{590.0F, 360.0F}},
            EnemySpawn{Vec2{640.0F, 300.0F}},
            EnemySpawn{Vec2{690.0F, 360.0F}}},
        20};

    world.update(GameplayInput{}, 0.0F);
    world.update(GameplayInput{}, 1.0F / 120.0F);

    std::size_t activeAttackCount{};
    for (const Enemy &enemy : world.enemies())
    {
        if (enemy.attackPhase() != EnemyAttackPhase::Idle)
        {
            ++activeAttackCount;
        }
    }

    EXPECT_EQ(activeAttackCount, 1U);
}

TEST(GameplayWorldTest, OverlappingSquadUsesSnapshotSeparation)
{
    GameplayWorld world{
        std::vector<EnemySpawn>{
            EnemySpawn{Vec2{400.0F, 350.0F}},
            EnemySpawn{Vec2{400.0F, 350.0F}},
            EnemySpawn{Vec2{400.0F, 350.0F}}},
        20};

    world.update(GameplayInput{}, 0.0F);
    world.update(GameplayInput{}, 1.0F);

    ASSERT_EQ(world.enemies().size(), 3U);
    EXPECT_LT(
        world.enemies()[0].position().x,
        world.enemies()[2].position().x);
}

TEST(GameplayWorldTest, DistantEnemyRemainsUnawareAndStationary)
{
    GameplayWorld world{
        std::vector<EnemySpawn>{
            EnemySpawn{Vec2{0.0F, 0.0F}}},
        20};
    const Vec2 initialPosition =
        world.enemies().front().position();

    world.update(GameplayInput{}, 1.0F);

    const Enemy &enemy = world.enemies().front();
    EXPECT_EQ(
        enemy.awarenessState(),
        EnemyAwarenessState::Unaware);
    EXPECT_FLOAT_EQ(enemy.position().x, initialPosition.x);
    EXPECT_FLOAT_EQ(enemy.position().y, initialPosition.y);
}

TEST(GameplayWorldTest, InvalidEnemySpawnIsRejected)
{
    EnemySpawn invalidSpawn;
    invalidSpawn.position.x =
        std::numeric_limits<float>::quiet_NaN();

    EXPECT_THROW(
        GameplayWorld(
            std::vector<EnemySpawn>{invalidSpawn},
            3),
        std::invalid_argument);
}

TEST(GameplayWorldTest, InitialScoreIsZero)
{
    const GameplayWorld world;

    EXPECT_EQ(world.score(), 0);
}

TEST(GameplayWorldTest, CustomFirstItemIdSeedsDefaultGroundItems)
{
    GameplayWorld world{ItemInstanceId{50}};

    ASSERT_EQ(world.groundItems().size(), 6U);
    EXPECT_EQ(
        world.groundItems().front().item().instanceId(),
        50U);
    EXPECT_EQ(
        world.groundItems().back().item().instanceId(),
        55U);
    EXPECT_EQ(world.nextItemInstanceId(), 56U);
}

TEST(GameplayWorldTest, ZeroFirstItemIdIsRejected)
{
    EXPECT_THROW(
        GameplayWorld(ItemInstanceId{0}),
        std::invalid_argument);
}

TEST(
    GameplayWorldTest,
    ExhaustedIdSpaceRejectsPartialDropWithoutMutation)
{
    const ItemInstanceId maximumId =
        std::numeric_limits<ItemInstanceId>::max();
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1},
        maximumId};
    ItemInstance ammo{1, ItemId::Ammo9mm, 2};
    ASSERT_TRUE(world.inventory().tryPlace(
        std::move(ammo),
        {0, 0}));

    EXPECT_FALSE(world.dropInventoryItemQuantity(
        1,
        1,
        ItemOrientation::Degrees0));
    EXPECT_EQ(world.inventory().quantityOf(1), 2U);
    EXPECT_TRUE(world.groundItems().empty());
    EXPECT_EQ(world.nextItemInstanceId(), maximumId);
}

TEST(
    GameplayWorldTest,
    ExhaustedIdSpaceRejectsPartialInventoryPlacements)
{
    const ItemInstanceId maximumId =
        std::numeric_limits<ItemInstanceId>::max();
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1},
        maximumId};
    ItemInstance ammo{1, ItemId::Ammo9mm, 2};
    ASSERT_TRUE(world.inventory().tryPlace(
        std::move(ammo),
        {0, 0}));

    EXPECT_FALSE(world.transferInventoryItemQuantity(
        true,
        1,
        1));
    EXPECT_FALSE(world.placeInventoryItemQuantity(
        true,
        true,
        1,
        1,
        {1, 0},
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(1), 2U);
    EXPECT_TRUE(
        world.containerInventory().placedItems().empty());
    EXPECT_EQ(world.nextItemInstanceId(), maximumId);
}

// MoveRight input 更新后，world.player().position().x 变大
TEST(GameplayWorldTest, MoveRightUpdatesPlayerPosition)
{
    GameplayWorld world;
    GameplayInput input{};
    input.moveRight = true;

    world.update(input, 1.0f);

    EXPECT_GT(world.player().position().x, 640.0f);
}

// Fire 冻结一条非实体逻辑弹道。
TEST(GameplayWorldTest, FireCreatesLogicalBallistic)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const LogicalBallisticFlight &flight = world.logicalBallistics()[0];

    const float directionLength = std::hypot(
        flight.direction().x,
        flight.direction().y);
    EXPECT_NEAR(directionLength, 1.0F, 0.000001F);
    EXPECT_LT(flight.direction().y, -0.99F);
    EXPECT_NEAR(
        flight.currentPosition().x,
        656.0F + flight.direction().x * 20.0F,
        0.0001F);
    EXPECT_NEAR(
        flight.currentPosition().y,
        376.0F + flight.direction().y * 20.0F,
        0.0001F);
    EXPECT_FLOAT_EQ(flight.collisionExtent(), 8.0f);
    EXPECT_FLOAT_EQ(flight.speed(), 6000.0F);
    EXPECT_EQ(flight.damage(), 1);
}

TEST(GameplayWorldTest,
     FireFreezesReticleTargetAndRegionInsteadOfInfiniteRayIntent)
{
    const EnemySpawn target{
        Vec2{800.0F, 300.0F},
        Vec2{50.0F, 100.0F},
        20};
    GameplayWorld aimedWorld{{target}, 3};
    GameplayInput aimed = makeFireInput();
    aimed.aimWorldPosition = Vec2{825.0F, 310.0F};
    aimedWorld.update(aimed, 0.0F);

    ASSERT_EQ(aimedWorld.logicalBallistics().size(), 1U);
    ASSERT_TRUE(aimedWorld.logicalBallistics().front().aimIntent());
    EXPECT_EQ(
        aimedWorld.logicalBallistics().front().aimIntent()->targetId,
        aimedWorld.enemies().front().combatTargetId());
    EXPECT_EQ(
        aimedWorld.logicalBallistics().front().aimIntent()->region,
        HitRegion::Head);

    GameplayWorld beforeWorld{{target}, 3};
    GameplayInput before = makeFireInput();
    before.aimWorldPosition = Vec2{760.0F, 310.0F};
    beforeWorld.update(before, 0.0F);
    ASSERT_EQ(beforeWorld.logicalBallistics().size(), 1U);
    EXPECT_FALSE(beforeWorld.logicalBallistics().front().aimIntent());

    GameplayWorld behindWorld{{target}, 3};
    GameplayInput behind = makeFireInput();
    behind.aimWorldPosition = Vec2{880.0F, 310.0F};
    behindWorld.update(behind, 0.0F);
    ASSERT_EQ(behindWorld.logicalBallistics().size(), 1U);
    EXPECT_FALSE(behindWorld.logicalBallistics().front().aimIntent());
}

TEST(GameplayWorldTest, RaidEnemiesReceiveUniqueStableCombatTargetIds)
{
    GameplayWorld world{
        std::vector<EnemySpawn>{
            EnemySpawn{Vec2{700.0F, 300.0F}, Vec2{40.0F, 80.0F}, 3},
            EnemySpawn{Vec2{800.0F, 300.0F}, Vec2{40.0F, 80.0F}, 3}},
        3};

    ASSERT_EQ(world.enemies().size(), 2U);
    EXPECT_NE(
        world.enemies()[0].combatTargetId(),
        kInvalidCombatTargetId);
    EXPECT_NE(
        world.enemies()[1].combatTargetId(),
        kInvalidCombatTargetId);
    EXPECT_NE(
        world.enemies()[0].combatTargetId(),
        world.enemies()[1].combatTargetId());
}

TEST(GameplayWorldTest, FirePublishesShotPresentationWithoutDamageAuthority)
{
    GameplayWorld world;
    world.update(makeFireInput(), 0.0F);
    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    const Vec2 frozenOrigin = world.logicalBallistics().front().origin();
    const Vec2 frozenDirection = world.logicalBallistics().front().direction();
    world.update(GameplayInput{}, 0.005F);

    const std::vector<ShotPresentationSnapshot> snapshots =
        world.shotPresentationSnapshots();

    ASSERT_EQ(snapshots.size(), 1U);
    EXPECT_NE(snapshots[0].shotId, kInvalidShotId);
    EXPECT_NEAR(snapshots[0].start.x, frozenOrigin.x, 0.0001F);
    EXPECT_NEAR(snapshots[0].start.y, frozenOrigin.y, 0.0001F);
    EXPECT_FLOAT_EQ(snapshots[0].direction.x, frozenDirection.x);
    EXPECT_FLOAT_EQ(snapshots[0].direction.y, frozenDirection.y);
    const Vec2 travelled{
        snapshots[0].end.x - snapshots[0].start.x,
        snapshots[0].end.y - snapshots[0].start.y};
    EXPECT_GT(
        travelled.x * frozenDirection.x +
            travelled.y * frozenDirection.y,
        0.0F);
    EXPECT_LE(
        std::hypot(
            snapshots[0].end.x - snapshots[0].start.x,
            snapshots[0].end.y - snapshots[0].start.y),
        48.0F);
}

TEST(GameplayWorldTest, AcceptedShotPublishesMuzzleSmokeAndShakeProjection)
{
    GameplayWorld world;
    world.update(makeFireInput(), 0.0F);

    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    const auto feedback = world.shotFeedbackPresentationSnapshots();
    ASSERT_EQ(feedback.size(), 1U);
    EXPECT_EQ(
        feedback.front().shotId,
        world.logicalBallistics().front().shotId());
    EXPECT_FLOAT_EQ(
        feedback.front().origin.x,
        world.logicalBallistics().front().origin().x);
    EXPECT_FLOAT_EQ(
        feedback.front().origin.y,
        world.logicalBallistics().front().origin().y);
    EXPECT_FLOAT_EQ(
        feedback.front().direction.x,
        world.logicalBallistics().front().direction().x);
    EXPECT_FLOAT_EQ(
        feedback.front().direction.y,
        world.logicalBallistics().front().direction().y);
    EXPECT_FLOAT_EQ(feedback.front().muzzleFlashIntensity, 1.0F);
    EXPECT_GT(feedback.front().smokeOpacity, 0.0F);
    const Vec2 shake = world.normalizedShotScreenShakeOffset();
    EXPECT_LE(std::hypot(shake.x, shake.y), 1.0F);

    world.update(GameplayInput{}, 0.230F);
    EXPECT_TRUE(world.shotFeedbackPresentationSnapshots().empty());
    EXPECT_FLOAT_EQ(world.normalizedShotScreenShakeOffset().x, 0.0F);
    EXPECT_FLOAT_EQ(world.normalizedShotScreenShakeOffset().y, 0.0F);
}

TEST(GameplayWorldTest, BlockedShotDoesNotPublishShotFeedback)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();
    input.sprint = true;
    input.moveRight = true;

    world.update(input, 0.0F);

    EXPECT_FALSE(world.shotFiredLastUpdate());
    EXPECT_TRUE(world.shotFeedbackPresentationSnapshots().empty());
    EXPECT_FLOAT_EQ(world.normalizedShotScreenShakeOffset().x, 0.0F);
    EXPECT_FLOAT_EQ(world.normalizedShotScreenShakeOffset().y, 0.0F);
}

TEST(GameplayWorldTest, SameFrameImpactKeepsShortLivedTracerProjection)
{
    GameplayWorld world{std::vector<EnemySpawn>{}, 3};
    world.update(makeFireInput(), 0.0F);
    ASSERT_EQ(world.logicalBallistics().size(), 1U);

    world.update(GameplayInput{}, 1.0F);

    EXPECT_TRUE(world.logicalBallistics().empty());
    ASSERT_EQ(world.hitResultsLastUpdate().size(), 1U);
    EXPECT_EQ(
        world.hitResultsLastUpdate().front().targetKind,
        HitTargetKind::Ground);
    ASSERT_EQ(world.shotPresentationSnapshots().size(), 1U);

    world.update(GameplayInput{}, 0.060F);
    EXPECT_TRUE(world.shotPresentationSnapshots().empty());
}

TEST(GameplayWorldTest, ConfiguredTracerSpansTravelledStepsAndFlickers)
{
    GameplayWorld world{std::vector<EnemySpawn>{}, 3};
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);

    GameplayInput fire = makeFireInput();
    fire.aimWorldPosition = Vec2{1200.0F, 376.0F};
    world.update(fire, 0.0F);
    world.update(GameplayInput{}, 0.030F);

    const std::vector<ShotPresentationSnapshot> first =
        world.shotPresentationSnapshots();
    ASSERT_EQ(first.size(), 1U);
    EXPECT_NEAR(
        std::hypot(
            first.front().end.x - first.front().start.x,
            first.front().end.y - first.front().start.y),
        30.0F,
        0.001F);

    world.update(GameplayInput{}, 0.010F);
    const std::vector<ShotPresentationSnapshot> second =
        world.shotPresentationSnapshots();
    ASSERT_EQ(second.size(), 1U);
    EXPECT_NE(first.front().tracerOpacity, second.front().tracerOpacity);
    EXPECT_GT(second.front().tracerOpacity, 0.0F);
    EXPECT_LE(second.front().tracerOpacity, 1.0F);
}

TEST(GameplayWorldTest, WeaponReconfigurationPreservesReticlePosition)
{
    GameplayWorld world{std::vector<EnemySpawn>{}, 3};
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    const ItemDefinition &pistol = itemDefinition(ItemId::Pistol);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    ASSERT_TRUE(pistol.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);

    GameplayInput initialize{};
    initialize.aimWorldPosition = Vec2{900.0F, 376.0F};
    world.update(initialize, 0.0F);

    GameplayInput relativeMotion = initialize;
    relativeMotion.aimMotionDelta = Vec2{145.0F, -55.0F};
    world.update(relativeMotion, 1.0F / 60.0F);
    const Vec2 beforeSwitch = world.weaponAimWorldPosition();
    ASSERT_NE(beforeSwitch.x, initialize.aimWorldPosition->x);

    world.configureWeaponFire(*pistol.weaponUse);

    EXPECT_FLOAT_EQ(world.weaponAimWorldPosition().x, beforeSwitch.x);
    EXPECT_FLOAT_EQ(world.weaponAimWorldPosition().y, beforeSwitch.y);

    GameplayInput stationaryPointer = initialize;
    stationaryPointer.aimMotionDelta = Vec2{};
    world.update(stationaryPointer, 1.0F / 60.0F);
    EXPECT_FLOAT_EQ(world.weaponAimWorldPosition().x, beforeSwitch.x);
    EXPECT_FLOAT_EQ(world.weaponAimWorldPosition().y, beforeSwitch.y);

    GameplayInput fire = stationaryPointer;
    fire.fireJustPressed = true;
    fire.firePressed = true;
    world.update(fire, 0.0F);
    ASSERT_TRUE(world.shotFiredLastUpdate());

    world.configureWeaponFire(*rifle.weaponUse);
    world.update(fire, 0.0F);
    EXPECT_TRUE(world.shotFiredLastUpdate());
}

TEST(GameplayWorldTest, SprintBlocksImmediateShotCreation)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();
    input.sprint = true;
    input.moveRight = true;

    world.update(input, 0.0F);

    EXPECT_FALSE(world.shotFiredLastUpdate());
    EXPECT_TRUE(world.logicalBallistics().empty());
}

TEST(GameplayWorldTest, ConfiguredMaximumRangeReducesFrozenShotDamage)
{
    GameplayWorld world{RaidWorldConfig{
        Vec2{2400.0F, 1400.0F},
        Vec2{1200.0F, 700.0F},
        ContentRect{Vec2{2200.0F, 1200.0F}, Vec2{100.0F, 100.0F}},
        {},
        100,
        100,
        false}};
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);

    GameplayInput input = makeFireInput();
    input.aimWorldPosition = Vec2{0.0F, 0.0F};
    world.update(input, 0.0F);

    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    EXPECT_TRUE(world.weaponAimBeyondMaximumRange());
    EXPECT_EQ(world.logicalBallistics().front().damage(), 1);
    EXPECT_FLOAT_EQ(
        world.logicalBallistics().front().speed(),
        rifle.weaponUse->logicalBallisticSpeed);
}

TEST(GameplayWorldTest, ShotFreezesAimAndPublishesWorldImpactOnArrival)
{
    GameplayWorld world{std::vector<EnemySpawn>{}, 3};
    GameplayInput fire = makeFireInput();
    fire.aimWorldPosition = Vec2{900.0F, 376.0F};

    world.update(fire, 0.0F);
    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    const Vec2 frozenImpact =
        world.logicalBallistics()[0].impactPosition();
    EXPECT_FLOAT_EQ(frozenImpact.x, 1280.0F);
    EXPECT_TRUE(std::isfinite(frozenImpact.y));

    GameplayInput retarget{};
    retarget.aimWorldPosition = Vec2{200.0F, 100.0F};
    world.update(retarget, 0.05F);
    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    EXPECT_FLOAT_EQ(
        world.logicalBallistics()[0].impactPosition().x,
        frozenImpact.x);
    EXPECT_FLOAT_EQ(
        world.logicalBallistics()[0].impactPosition().y,
        frozenImpact.y);

    world.update(GameplayInput{}, 0.60F);

    EXPECT_TRUE(world.logicalBallistics().empty());
    ASSERT_EQ(world.hitResultsLastUpdate().size(), 1U);
    const HitResult &impact = world.hitResultsLastUpdate().front();
    EXPECT_EQ(impact.targetKind, HitTargetKind::Ground);
    EXPECT_EQ(impact.damageApplied, 0);
    EXPECT_FLOAT_EQ(impact.position.x, frozenImpact.x);
    EXPECT_FLOAT_EQ(impact.position.y, frozenImpact.y);
    EXPECT_EQ(
        world.particles().size(),
        ParticleBurstConfig{}.particleCount);
}

// 不按 Fire 不生成逻辑弹道。
TEST(GameplayWorldTest, NoFireDoesNotCreateLogicalBallistic)
{
    GameplayWorld world;
    GameplayInput input{};

    world.update(input, 0.0f);

    EXPECT_TRUE(world.logicalBallistics().empty());
}

// 逻辑弹道会随 deltaTime 向上推进。
TEST(GameplayWorldTest, LogicalBallisticMovesAfterSpawn)
{
    GameplayWorld world;

    GameplayInput fire = makeFireInput();
    world.update(fire, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const float initialY =
        world.logicalBallistics()[0].currentPosition().y;

    GameplayInput noInput{};
    world.update(noInput, 0.02f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    EXPECT_LT(
        world.logicalBallistics()[0].currentPosition().y,
        initialY);
}

// 逻辑弹道命中 3 HP Enemy 后被消耗，
// Enemy 扣除 1 HP 但仍然保留。
TEST(
    GameplayWorldTest,
    LogicalBallisticCanDamageMovingEnemyWithoutKillingIt)
{
    GameplayWorld world;

    GameplayInput fire = makeFireInput();
    fire.aimWorldPosition = Vec2{625.0F, 125.0F};
    world.update(fire, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    ASSERT_EQ(world.enemies().size(), 3u);
    EXPECT_EQ(world.enemies()[0].health(), 3);

    GameplayInput noInput{};
    constexpr float kSimulationStep{1.0F / 60.0F};
    constexpr int kMaximumFrames{20};
    int simulatedFrames{0};
    while (!world.logicalBallistics().empty() &&
           simulatedFrames < kMaximumFrames)
    {
        world.update(noInput, kSimulationStep);
        ++simulatedFrames;
    }

    EXPECT_TRUE(world.logicalBallistics().empty());
    EXPECT_LT(simulatedFrames, kMaximumFrames);

    ASSERT_EQ(world.enemies().size(), 3u);
    EXPECT_EQ(world.enemies()[0].health(), 2);
    EXPECT_FALSE(world.enemies()[0].isDead());
    EXPECT_TRUE(world.enemies()[0].isImpactSlowed());
    EXPECT_EQ(world.score(), 0);

    const ParticleBurstConfig impactConfig{};
    ASSERT_EQ(
        world.particles().size(),
        impactConfig.particleCount);
    for (const Particle &particle : world.particles())
    {
        EXPECT_GE(particle.duration(), impactConfig.minLifetime);
        EXPECT_LE(particle.duration(), impactConfig.maxLifetime);
        EXPECT_GE(particle.size(), impactConfig.minSize);
        EXPECT_LE(particle.size(), impactConfig.maxSize);
    }
}

TEST(GameplayWorldTest, FastLogicalBallisticDoesNotTunnelDuringLargeFrame)
{
    GameplayWorld world;
    world.update(makeFireInput(), 0.0F);

    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    ASSERT_EQ(world.enemies().size(), 3U);

    world.update(GameplayInput{}, 0.30F);

    EXPECT_TRUE(world.logicalBallistics().empty());
    ASSERT_EQ(world.enemies().size(), 3U);
    EXPECT_EQ(world.enemies()[0].health(), 2);
    EXPECT_EQ(
        world.particles().size(),
        ParticleBurstConfig{}.particleCount);
}

TEST(GameplayWorldTest, EnemyPursuesPlayerInTwoDimensions)
{
    GameplayWorld world;
    GameplayInput input{};

    ASSERT_EQ(world.enemies().size(), 3u);
    const Vec2 initialPosition = world.enemies()[0].position();
    const Vec2 playerPosition = world.player().position();
    const float initialDistanceSquared =
        std::pow(playerPosition.x - initialPosition.x, 2.0F) +
        std::pow(playerPosition.y - initialPosition.y, 2.0F);

    world.update(input, 1.0f);

    ASSERT_EQ(world.enemies().size(), 3u);
    const Vec2 updatedPosition = world.enemies()[0].position();
    const float updatedDistanceSquared =
        std::pow(playerPosition.x - updatedPosition.x, 2.0F) +
        std::pow(playerPosition.y - updatedPosition.y, 2.0F);

    EXPECT_GT(updatedPosition.x, initialPosition.x);
    EXPECT_GT(updatedPosition.y, initialPosition.y);
    EXPECT_LT(updatedDistanceSquared, initialDistanceSquared);
}

TEST(GameplayWorldTest, EnemyFirstApproachUsesNormalPursuitInsteadOfGrab)
{
    GameplayWorld world;
    world.update(GameplayInput{}, 1.20F);

    ASSERT_EQ(world.enemies().size(), 3u);
    const Enemy &enemy = world.enemies()[0];

    EXPECT_FALSE(enemy.attackType().has_value());
    EXPECT_EQ(enemy.attackPhase(), EnemyAttackPhase::Idle);
    EXPECT_EQ(enemy.movementState(), EnemyMovementState::Normal);
    EXPECT_FLOAT_EQ(enemy.movementSpeed(), 72.0F);
}

// 右朝向射击
TEST(GameplayWorldTest, FireAfterFacingRightMovesBallisticRight)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveRight = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 initialPosition =
        world.logicalBallistics()[0].currentPosition();

    GameplayInput noInput{};
    world.update(noInput, 0.02f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 finalPosition =
        world.logicalBallistics()[0].currentPosition();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_NE(finalPosition.y, initialPosition.y);
}

// 左朝向射击
TEST(GameplayWorldTest, FireAfterFacingLeftMovesBallisticLeft)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveLeft = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 initialPosition =
        world.logicalBallistics()[0].currentPosition();

    GameplayInput noInput{};
    world.update(noInput, 0.02f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 finalPosition =
        world.logicalBallistics()[0].currentPosition();

    EXPECT_NE(finalPosition.y, initialPosition.y);
    EXPECT_LT(finalPosition.x, initialPosition.x);
}

// 下朝向射击
TEST(GameplayWorldTest, FireAfterFacingDownMovesBallisticDown)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveDown = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 initialPosition =
        world.logicalBallistics()[0].currentPosition();

    GameplayInput noInput{};
    world.update(noInput, 0.02f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 finalPosition =
        world.logicalBallistics()[0].currentPosition();

    EXPECT_NE(finalPosition.x, initialPosition.x);
    EXPECT_GT(finalPosition.y, initialPosition.y);
}

// 无移动输入时，使用上一次 facing direction 射击
TEST(GameplayWorldTest, FireWithoutMovementUsesPreviousFacingDirection)
{
    GameplayWorld world;

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 0.0f);

    GameplayInput fire = makeFireInput();
    world.update(fire, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 initialPosition =
        world.logicalBallistics()[0].currentPosition();

    GameplayInput noInput{};
    world.update(noInput, 0.02f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 finalPosition =
        world.logicalBallistics()[0].currentPosition();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_NE(finalPosition.y, initialPosition.y);
}

TEST(GameplayWorldTest, PointerAimControlsFacingAndShotWithoutMovement)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();
    input.aimWorldPosition = Vec2{1000.0F, 376.0F};

    world.update(input, 0.0F);

    ASSERT_EQ(world.logicalBallistics().size(), 1U);
    EXPECT_FLOAT_EQ(world.player().position().x, 640.0F);
    EXPECT_FLOAT_EQ(world.player().position().y, 360.0F);
    EXPECT_FLOAT_EQ(world.player().facingDirection().x, 1.0F);
    EXPECT_FLOAT_EQ(world.player().facingDirection().y, 0.0F);

    const Vec2 initialPosition =
        world.logicalBallistics()[0].currentPosition();
    const Vec2 direction = world.logicalBallistics()[0].direction();
    world.update(GameplayInput{}, 0.01F);
    const Vec2 travelled{
        world.logicalBallistics()[0].currentPosition().x - initialPosition.x,
        world.logicalBallistics()[0].currentPosition().y - initialPosition.y};
    EXPECT_GT(
        travelled.x * direction.x + travelled.y * direction.y,
        0.0F);
}

TEST(GameplayWorldTest, PointerAimDoesNotChangeMovementDirection)
{
    GameplayWorld world;
    GameplayInput input{};
    input.moveLeft = true;
    input.aimWorldPosition = Vec2{1000.0F, 376.0F};

    world.update(input, 0.1F);

    EXPECT_LT(world.player().position().x, 640.0F);
    EXPECT_FLOAT_EQ(world.player().facingDirection().x, 1.0F);
    EXPECT_FLOAT_EQ(world.player().facingDirection().y, 0.0F);
}

TEST(GameplayWorldTest, AimAtPlayerCenterPreservesPreviousFacing)
{
    GameplayWorld world;
    GameplayInput faceRight{};
    faceRight.moveRight = true;
    world.update(faceRight, 0.0F);

    GameplayInput aimAtCenter{};
    aimAtCenter.aimWorldPosition = Vec2{656.0F, 376.0F};
    world.update(aimAtCenter, 0.0F);

    EXPECT_FLOAT_EQ(world.player().facingDirection().x, 1.0F);
    EXPECT_FLOAT_EQ(world.player().facingDirection().y, 0.0F);
}

TEST(GameplayWorldTest, WeaponFeedbackReflectsShotAndRecovers)
{
    GameplayWorld world;
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);
    world.update(makeFireInput(), 0.0F);

    const float recoil = world.weaponVisualRecoilPixels();
    const float firedSpread = world.weaponSpreadDegrees();
    EXPECT_GT(world.weaponSpreadDegrees(), 0.0F);
    EXPECT_GT(recoil, 0.0F);

    world.update(GameplayInput{}, 1.0F);
    EXPECT_LE(world.weaponSpreadDegrees(), firedSpread);
    EXPECT_GE(
        world.weaponSpreadDegrees(),
        world.weaponAccuracyProjection().minimumSpreadDegrees);
    EXPECT_LE(
        world.weaponSpreadDegrees(),
        world.weaponAccuracyProjection().maximumSpreadDegrees);
    EXPECT_LT(world.weaponVisualRecoilPixels(), recoil);
    EXPECT_FLOAT_EQ(world.weaponVisualRecoilPixels(), 0.0F);
}

TEST(GameplayWorldTest, ReticleProjectionMakesAuthoritativeBloomReadable)
{
    GameplayWorld world{std::vector<EnemySpawn>{}, 3};
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);

    GameplayInput rest{};
    rest.aimWorldPosition = Vec2{720.0F, 376.0F};
    world.update(rest, 0.0F);
    const WeaponAccuracyProjection resting =
        world.weaponAccuracyProjection();
    EXPECT_GT(resting.reticleRadius, resting.worldRadius);
    EXPECT_NEAR(resting.reticleRadius - resting.worldRadius, 10.0F, 0.001F);
    EXPECT_GE(resting.reticleRadius, 10.0F);
    EXPECT_LT(resting.reticleRadius, 12.0F + resting.worldRadius);

    GameplayInput movingAndFlicking{};
    movingAndFlicking.moveRight = true;
    movingAndFlicking.aimWorldPosition = Vec2{720.0F, 376.0F};
    movingAndFlicking.aimMotionDelta = Vec2{30.0F, 0.0F};
    world.update(movingAndFlicking, 1.0F / 60.0F);
    const WeaponAccuracyProjection expanded =
        world.weaponAccuracyProjection();
    EXPECT_GT(expanded.reticleRadius, resting.reticleRadius + 35.0F);
    EXPECT_LT(expanded.reticleRadius, resting.reticleRadius + 70.0F);

    GameplayInput movingWithoutMouse{};
    movingWithoutMouse.moveRight = true;
    movingWithoutMouse.aimWorldPosition = Vec2{750.0F, 376.0F};
    movingWithoutMouse.aimMotionDelta = Vec2{};
    world.update(movingWithoutMouse, 1.0F / 60.0F);
    const WeaponAccuracyProjection nextFrame =
        world.weaponAccuracyProjection();
    EXPECT_GT(nextFrame.reticleRadius, resting.reticleRadius + 35.0F);
    EXPECT_LT(
        std::abs(nextFrame.reticleRadius - expanded.reticleRadius),
        10.0F);

    for (int frame = 0; frame < 18; ++frame)
    {
        world.update(movingAndFlicking, 1.0F / 60.0F);
    }
    const WeaponAccuracyProjection sustained =
        world.weaponAccuracyProjection();
    EXPECT_GT(sustained.reticleRadius, resting.reticleRadius + 18.0F);
}

TEST(GameplayWorldTest, SprintingImmediatelyOpensFartherThanWalking)
{
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    GameplayWorld walking{std::vector<EnemySpawn>{}, 3};
    GameplayWorld sprinting{std::vector<EnemySpawn>{}, 3};
    walking.configureWeaponFire(*rifle.weaponUse);
    sprinting.configureWeaponFire(*rifle.weaponUse);

    GameplayInput walk;
    walk.moveRight = true;
    walk.aimWorldPosition = Vec2{900.0F, 376.0F};
    walking.update(walk, 0.0F);

    GameplayInput sprint = walk;
    sprint.sprint = true;
    sprinting.update(sprint, 0.0F);

    const WeaponAccuracyProjection walkProjection =
        walking.weaponAccuracyProjection();
    const WeaponAccuracyProjection sprintProjection =
        sprinting.weaponAccuracyProjection();
    EXPECT_GT(
        walkProjection.currentSpreadDegrees,
        walkProjection.minimumSpreadDegrees);
    EXPECT_GT(
        sprintProjection.currentSpreadDegrees,
        walkProjection.currentSpreadDegrees);
    EXPECT_GT(sprintProjection.reticleRadius, walkProjection.reticleRadius);
}

TEST(GameplayWorldTest, StationaryPointerDoesNotAutomaticallyRecoverAimRecoil)
{
    GameplayWorld world{std::vector<EnemySpawn>{}, 3};
    const ItemDefinition &rifle = itemDefinition(ItemId::Rifle);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    world.configureWeaponFire(*rifle.weaponUse);

    GameplayInput fire = makeFireInput();
    constexpr Vec2 pointer{1000.0F, 376.0F};
    fire.aimWorldPosition = pointer;
    world.update(fire, 0.0F);
    const float originalAimX = world.weaponAimWorldPosition().x;

    GameplayInput stationary{};
    stationary.aimWorldPosition = pointer;
    world.update(stationary, 1.0F);
    const float displacedAimX = world.weaponAimWorldPosition().x;
    ASSERT_GT(displacedAimX, originalAimX + 1.0F);

    world.update(stationary, 1.0F);
    EXPECT_NEAR(world.weaponAimWorldPosition().x, displacedAimX, 0.01F);

    GameplayInput counterMove{};
    counterMove.aimWorldPosition = Vec2{950.0F, 376.0F};
    world.update(counterMove, 0.5F);
    EXPECT_LT(world.weaponAimWorldPosition().x, displacedAimX);
}

// 斜向射击
TEST(GameplayWorldTest, FireAfterDiagonalFacingMovesBallisticDiagonally)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    input.moveUp = true;
    input.moveRight = true;
    world.update(input, 0.0f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 initialPosition =
        world.logicalBallistics()[0].currentPosition();

    GameplayInput noInput{};
    world.update(noInput, 0.02f);

    ASSERT_EQ(world.logicalBallistics().size(), 1u);
    const Vec2 finalPosition =
        world.logicalBallistics()[0].currentPosition();

    EXPECT_GT(finalPosition.x, initialPosition.x);
    EXPECT_LT(finalPosition.y, initialPosition.y);
}

// 连续射击时，第一次可以立即射击
TEST(GameplayWorldTest, HoldingFireCreatesFirstBallisticImmediately)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);

    EXPECT_EQ(world.logicalBallistics().size(), 1u);
}

TEST(GameplayWorldTest, FireEdgeCreatesShotEvenIfReleasedWithinFrame)
{
    GameplayWorld world;
    GameplayInput input{};
    input.fireJustPressed = true;
    input.firePressed = false;

    world.update(input, 0.0F);

    EXPECT_EQ(world.logicalBallistics().size(), 1U);
}

// 按住 Fire 但冷却未结束时，不会再次创建逻辑弹道。
TEST(GameplayWorldTest, HoldingFireDoesNotCreateBallisticBeforeCooldownEnds)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);
    EXPECT_EQ(world.logicalBallistics().size(), 1u);

    input.fireJustPressed = false;
    input.firePressed = true;
    world.update(input, 0.1f);

    EXPECT_FALSE(world.shotFiredLastUpdate());
}

// 按住 Fire 且冷却结束后，可以再次生成逻辑弹道。
TEST(GameplayWorldTest, HoldingFireCreatesAnotherBallisticAfterCooldownEnds)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);
    EXPECT_EQ(world.logicalBallistics().size(), 1u);

    input.fireJustPressed = false;
    input.firePressed = true;
    world.update(input, 0.12f);

    EXPECT_TRUE(world.shotFiredLastUpdate());
}

// 冷却结束后，如果没有按 Fire，不会自动生成逻辑弹道。
TEST(GameplayWorldTest, NoFireDoesNotCreateBallisticAfterCooldownEnds)
{
    GameplayWorld world;
    GameplayInput input = makeFireInput();

    world.update(input, 0.0f);
    EXPECT_EQ(world.logicalBallistics().size(), 1u);

    GameplayInput noInput{};
    world.update(noInput, 0.12f);

    EXPECT_FALSE(world.shotFiredLastUpdate());
}

namespace
{

    constexpr Vec2 kInitialPlayerCenter{
        656.0f,
        376.0f};

    GameplayInput makeInteractInput()
    {
        GameplayInput input{};
        input.interactJustPressed = true;
        return input;
    }

    GameplayWorld makeItemTestWorld(
        std::vector<GroundItemSpawn> spawns,
        InventoryGridSize inventorySize = {10, 6})
    {
        return GameplayWorld{
            3,
            std::move(spawns),
            inventorySize};
    }

} // namespace

TEST(
    GameplayWorldTest,
    InitialInventoryIsEmptyTenBySixGrid)
{
    const GameplayWorld world;

    EXPECT_EQ(
        world.inventory().width(),
        10);
    EXPECT_EQ(
        world.inventory().height(),
        6);
    EXPECT_EQ(
        world.inventory().cellCount(),
        60U);
    EXPECT_TRUE(
        world.inventory()
            .placedItems()
            .empty());
}

TEST(
    GameplayWorldTest,
    InitialGroundItemsHaveStableIds)
{
    const GameplayWorld world;

    ASSERT_EQ(
        world.groundItems().size(),
        6U);

    EXPECT_EQ(
        world.groundItems()[0]
            .item()
            .instanceId(),
        1U);
    EXPECT_EQ(
        world.groundItems()[0]
            .item()
            .definitionId(),
        ItemId::Cola);

    EXPECT_EQ(
        world.groundItems()[1]
            .item()
            .instanceId(),
        2U);
    EXPECT_EQ(
        world.groundItems()[1]
            .item()
            .definitionId(),
        ItemId::Medkit);

    EXPECT_EQ(
        world.groundItems()[2]
            .item()
            .instanceId(),
        3U);
    EXPECT_EQ(
        world.groundItems()[2]
            .item()
            .definitionId(),
        ItemId::Pistol);

    EXPECT_EQ(
        world.groundItems()[3]
            .item()
            .instanceId(),
        4U);
    EXPECT_EQ(
        world.groundItems()[3]
            .item()
            .definitionId(),
        ItemId::Rifle);

    EXPECT_EQ(
        world.groundItems()[4]
            .item()
            .instanceId(),
        5U);
    EXPECT_EQ(
        world.groundItems()[4]
            .item()
            .definitionId(),
        ItemId::Ammo9mm);
    EXPECT_EQ(
        world.groundItems()[4]
            .item()
            .quantity(),
        25U);

    EXPECT_EQ(
        world.groundItems()[5]
            .item()
            .instanceId(),
        6U);
    EXPECT_EQ(
        world.groundItems()[5]
            .item()
            .definitionId(),
        ItemId::Ammo9mm);
    EXPECT_EQ(
        world.groundItems()[5]
            .item()
            .quantity(),
        40U);
    EXPECT_TRUE(
        itemDefinition(ItemId::Ammo9mm)
            .visualAssetsPublished);
}

TEST(
    GameplayWorldTest,
    NoInteractDoesNotPickUpItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                kInitialPlayerCenter,
            },
        });

    world.update(
        GameplayInput{},
        0.0f);

    EXPECT_EQ(
        world.groundItems().size(),
        1U);
    EXPECT_TRUE(
        world.inventory()
            .placedItems()
            .empty());
}

TEST(
    GameplayWorldTest,
    InteractOutsideRangeDoesNotPickUpItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                Vec2{100.0f, 100.0f},
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.groundItems().size(),
        1U);
    EXPECT_TRUE(
        world.inventory()
            .placedItems()
            .empty());
}

TEST(
    GameplayWorldTest,
    InteractInRangeTransfersItemIntoInventory)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Pistol,
                kInitialPlayerCenter,
            },
        });

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    const ItemInstanceId originalId =
        world.groundItems()
            .front()
            .item()
            .instanceId();

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_TRUE(
        world.groundItems().empty());

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    const PlacedItem &placed =
        world.inventory()
            .placedItems()
            .front();

    EXPECT_EQ(
        placed.item.instanceId(),
        originalId);
    EXPECT_EQ(
        placed.item.definitionId(),
        ItemId::Pistol);
    EXPECT_EQ(
        placed.origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        world.inventory()
            .occupantAt({0, 0}),
        std::optional<ItemInstanceId>{
            originalId});
    EXPECT_EQ(
        world.inventory()
            .occupantAt({1, 0}),
        std::optional<ItemInstanceId>{
            originalId});
}

TEST(
    GameplayWorldTest,
    MultipleCandidatesPickNearestItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Medkit,
                Vec2{680.0f, 376.0f},
            },
            {
                ItemId::Cola,
                Vec2{650.0f, 376.0f},
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .front()
            .item
            .definitionId(),
        ItemId::Cola);

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    EXPECT_EQ(
        world.groundItems()
            .front()
            .item()
            .definitionId(),
        ItemId::Medkit);
}

TEST(
    GameplayWorldTest,
    EqualDistanceKeepsEarlierVectorItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                Vec2{644.0f, 376.0f},
            },
            {
                ItemId::Medkit,
                Vec2{668.0f, 376.0f},
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .front()
            .item
            .definitionId(),
        ItemId::Cola);

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    EXPECT_EQ(
        world.groundItems()
            .front()
            .item()
            .definitionId(),
        ItemId::Medkit);
}

TEST(
    GameplayWorldTest,
    OneInteractPicksAtMostOneItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                kInitialPlayerCenter,
            },
            {
                ItemId::Medkit,
                kInitialPlayerCenter,
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);
    EXPECT_EQ(
        world.groundItems().size(),
        1U);
}

TEST(
    GameplayWorldTest,
    NoNewInteractDoesNotPickNextItem)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Cola,
                kInitialPlayerCenter,
            },
            {
                ItemId::Medkit,
                kInitialPlayerCenter,
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    world.update(
        GameplayInput{},
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);
    EXPECT_EQ(
        world.groundItems().size(),
        1U);
}

TEST(
    GameplayWorldTest,
    PickedItemsUseRowMajorFirstFit)
{
    GameplayWorld world =
        makeItemTestWorld({
            {
                ItemId::Pistol,
                kInitialPlayerCenter,
            },
            {
                ItemId::Medkit,
                kInitialPlayerCenter,
            },
        });

    world.update(
        makeInteractInput(),
        0.0f);

    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        2U);

    const PlacedItem &pistol =
        world.inventory()
            .placedItems()[0];

    const PlacedItem &medkit =
        world.inventory()
            .placedItems()[1];

    EXPECT_EQ(
        pistol.item.definitionId(),
        ItemId::Pistol);
    EXPECT_EQ(
        pistol.origin,
        (GridPosition{0, 0}));

    EXPECT_EQ(
        medkit.item.definitionId(),
        ItemId::Medkit);
    EXPECT_EQ(
        medkit.origin,
        (GridPosition{2, 0}));
}

TEST(
    GameplayWorldTest,
    FullInventoryKeepsGroundItemAndInstanceId)
{
    GameplayWorld world =
        makeItemTestWorld(
            {
                {
                    ItemId::Pistol,
                    kInitialPlayerCenter,
                },
                {
                    ItemId::Cola,
                    kInitialPlayerCenter,
                },
            },
            InventoryGridSize{2, 1});

    // Pistol 为 2×1，第一次拾取后填满整个背包。
    world.update(
        makeInteractInput(),
        0.0f);

    ASSERT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);
    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    const ItemInstanceId remainingId =
        world.groundItems()
            .front()
            .item()
            .instanceId();

    ASSERT_TRUE(
        world.groundItems()
            .front()
            .item()
            .valid());

    // 第二次交互时背包已满。
    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .size(),
        1U);

    ASSERT_EQ(
        world.groundItems().size(),
        1U);

    const ItemInstance &remainingItem =
        world.groundItems()
            .front()
            .item();

    EXPECT_TRUE(
        remainingItem.valid());
    EXPECT_EQ(
        remainingItem.instanceId(),
        remainingId);
    EXPECT_EQ(
        remainingItem.definitionId(),
        ItemId::Cola);
}

TEST(
    GameplayWorldTest,
    InventoryCapacityFailureDoesNotChangeOccupiedCells)
{
    GameplayWorld world =
        makeItemTestWorld(
            {
                {
                    ItemId::Pistol,
                    kInitialPlayerCenter,
                },
                {
                    ItemId::Cola,
                    kInitialPlayerCenter,
                },
            },
            InventoryGridSize{2, 1});

    world.update(
        makeInteractInput(),
        0.0f);

    const ItemInstanceId pistolId =
        world.inventory()
            .placedItems()
            .front()
            .item
            .instanceId();

    world.update(
        makeInteractInput(),
        0.0f);

    EXPECT_EQ(
        world.inventory()
            .occupantAt({0, 0}),
        std::optional<ItemInstanceId>{
            pistolId});

    EXPECT_EQ(
        world.inventory()
            .occupantAt({1, 0}),
        std::optional<ItemInstanceId>{
            pistolId});
}

TEST(GameplayWorldTest, OwnsEmptySixBySixContainerInventory)
{
    const GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    EXPECT_EQ(world.containerInventory().width(), 6);
    EXPECT_EQ(world.containerInventory().height(), 6);
    EXPECT_TRUE(
        world.containerInventory()
            .placedItems()
            .empty());
}

TEST(GameplayWorldTest, ContainerInventoryBelongsToStorageCabinet)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    EXPECT_EQ(
        &world.containerInventory(),
        &world.storageCabinet().inventory());
}

TEST(GameplayWorldLootTest, SearchRequiresPlayerInsideCabinetRange)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    SequenceLootRandomSource random{{0, 0, 0}};

    EXPECT_FALSE(world.searchStorageCabinet(random));
    EXPECT_EQ(random.drawCount(), 0U);
    EXPECT_FALSE(world.storageCabinet().isSearched());
    EXPECT_TRUE(world.containerInventory().placedItems().empty());
}

TEST(GameplayWorldLootTest, FirstSearchCreatesDeterministicRowMajorLoot)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};

    ASSERT_TRUE(world.searchStorageCabinet(random));

    EXPECT_TRUE(world.storageCabinet().isSearched());
    EXPECT_EQ(random.drawCount(), 5U);
    ASSERT_EQ(world.containerInventory().placedItems().size(), 2U);

    const PlacedItem &cola =
        world.containerInventory().placedItems()[0];
    const PlacedItem &ammunition =
        world.containerInventory().placedItems()[1];

    EXPECT_EQ(cola.item.instanceId(), 1U);
    EXPECT_EQ(cola.item.definitionId(), ItemId::Cola);
    EXPECT_EQ(cola.item.quantity(), 1U);
    EXPECT_EQ(cola.origin, (GridPosition{0, 0}));

    EXPECT_EQ(ammunition.item.instanceId(), 2U);
    EXPECT_EQ(ammunition.item.definitionId(), ItemId::Ammo9mm);
    EXPECT_EQ(ammunition.item.quantity(), 40U);
    EXPECT_EQ(ammunition.origin, (GridPosition{1, 0}));
}

TEST(GameplayWorldLootTest, ReopeningSearchedCabinetDoesNotReroll)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(random));
    const std::size_t drawCount = random.drawCount();

    EXPECT_TRUE(world.searchStorageCabinet(random));
    EXPECT_EQ(random.drawCount(), drawCount);
    ASSERT_EQ(world.containerInventory().placedItems().size(), 2U);
    EXPECT_EQ(
        world.containerInventory().placedItems()[1].item.quantity(),
        40U);
}

TEST(GameplayWorldLootTest, EmptySearchedCabinetDoesNotReplenish)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(random));
    ASSERT_TRUE(world.containerInventory().remove(1).has_value());
    ASSERT_TRUE(world.containerInventory().remove(2).has_value());
    ASSERT_TRUE(world.containerInventory().placedItems().empty());
    const std::size_t drawCount = random.drawCount();

    EXPECT_TRUE(world.searchStorageCabinet(random));
    EXPECT_EQ(random.drawCount(), drawCount);
    EXPECT_TRUE(world.containerInventory().placedItems().empty());
}

TEST(GameplayWorldLootTest, SearchAdvancesWorldIdOnlyForFinalPlacements)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(random));

    ASSERT_TRUE(world.transferInventoryItemQuantity(
        false,
        2,
        10));
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        world.inventory().placedItems().front().item.instanceId(),
        3U);
    EXPECT_EQ(
        world.inventory().placedItems().front().item.quantity(),
        10U);
}

TEST(GameplayWorldLootTest, IdCollisionLeavesCabinetUnsearchedAndIdSequenceUnchanged)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    ASSERT_TRUE(world.inventory().tryPlace(
        ItemInstance{1, ItemId::Medkit},
        {0, 0}));
    SequenceLootRandomSource firstRandom{
        {0, 99, 0, 99, 20}};

    EXPECT_FALSE(world.searchStorageCabinet(firstRandom));
    EXPECT_FALSE(world.storageCabinet().isSearched());
    EXPECT_TRUE(world.containerInventory().placedItems().empty());
    ASSERT_TRUE(world.inventory().remove(1).has_value());

    SequenceLootRandomSource secondRandom{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(secondRandom));
    ASSERT_FALSE(world.containerInventory().placedItems().empty());
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        1U);
}

TEST(GameplayWorldLootTest, DefaultGroundItemsReserveEarlierWorldIds)
{
    GameplayWorld world;
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};

    ASSERT_TRUE(world.searchStorageCabinet(random));
    ASSERT_EQ(world.groundItems().size(), 6U);
    ASSERT_FALSE(world.containerInventory().placedItems().empty());
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        7U);
}

TEST(GameplayWorldLootTest, InvalidRandomResultLeavesWorldStateUnchanged)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    movePlayerToCabinet(world);
    SequenceLootRandomSource random{{100}};

    EXPECT_THROW(
        static_cast<void>(
            world.searchStorageCabinet(random)),
        std::out_of_range);
    EXPECT_FALSE(world.storageCabinet().isSearched());
    EXPECT_TRUE(world.containerInventory().placedItems().empty());

    SequenceLootRandomSource validRandom{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(world.searchStorageCabinet(validRandom));
    ASSERT_FALSE(world.containerInventory().placedItems().empty());
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        1U);
}

TEST(GameplayWorldTest, PlayerMustApproachCabinetBeforeInteraction)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    EXPECT_FALSE(world.canInteractWithContainer());

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 1.0F);

    EXPECT_TRUE(world.canInteractWithContainer());
}

TEST(GameplayWorldTest, DropsPlayerInventoryItemAtPlayersFeet)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    ItemInstance rifle{501, ItemId::Rifle};

    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(rifle),
            {2, 1}));

    ASSERT_TRUE(world.dropInventoryItem(501));

    EXPECT_TRUE(world.inventory().placedItems().empty());
    ASSERT_EQ(world.groundItems().size(), 1U);

    const GroundItem &dropped = world.groundItems().front();
    EXPECT_EQ(dropped.item().instanceId(), 501U);
    EXPECT_EQ(dropped.item().definitionId(), ItemId::Rifle);
    EXPECT_FLOAT_EQ(dropped.position().x, 656.0f);
    EXPECT_FLOAT_EQ(dropped.position().y, 392.0f);
}

TEST(GameplayWorldTest, MissingDropIdLeavesWorldUnchanged)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    ItemInstance cola{502, ItemId::Cola};

    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    EXPECT_FALSE(world.dropInventoryItem(999));
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        world.inventory()
            .placedItems()
            .front()
            .item.instanceId(),
        502U);
    EXPECT_TRUE(world.groundItems().empty());
}

TEST(GameplayWorldTest, CannotDropDirectlyFromContainerInventory)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};
    ItemInstance medkit{503, ItemId::Medkit};

    ASSERT_TRUE(
        world.containerInventory().tryPlace(
            std::move(medkit),
            {1, 1}));

    EXPECT_FALSE(world.dropInventoryItem(503));
    ASSERT_EQ(
        world.containerInventory().placedItems().size(),
        1U);
    EXPECT_TRUE(world.groundItems().empty());
}

TEST(GameplayWorldTest, DropPositionKeepsItemInsideWorldBounds)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveDown{};
    moveDown.moveDown = true;
    world.update(moveDown, 10.0f);

    ItemInstance cola{504, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(504));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        704.0f);
}

TEST(GameplayWorldTest, DropPositionIgnoresRightFacingDirection)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 0.0f);

    ItemInstance cola{505, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(505));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().x,
        656.0f);
    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        392.0f);
}

TEST(GameplayWorldTest, DropPositionIgnoresDownFacingDirection)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveDown{};
    moveDown.moveDown = true;
    world.update(moveDown, 0.0f);

    ItemInstance cola{506, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(506));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().x,
        656.0f);
    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        392.0f);
}

TEST(GameplayWorldTest, DropPositionIgnoresLeftFacingDirection)
{
    GameplayWorld world{
        3,
        std::vector<GroundItemSpawn>{}};

    GameplayInput moveLeft{};
    moveLeft.moveLeft = true;
    world.update(moveLeft, 0.0f);

    ItemInstance cola{507, ItemId::Cola};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(cola),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(507));
    ASSERT_EQ(world.groundItems().size(), 1U);

    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().x,
        656.0f);
    EXPECT_FLOAT_EQ(
        world.groundItems().front().position().y,
        392.0f);
}

TEST(GameplayWorldTest, DropCommitsRequestedItemOrientation)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{10, 6}};
    ItemInstance rifle{508, ItemId::Rifle};
    ASSERT_TRUE(
        world.inventory().tryPlace(
            std::move(rifle),
            {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(
        508,
        ItemOrientation::Degrees90));

    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(
        world.groundItems().front().item().orientation(),
        ItemOrientation::Degrees90);
    EXPECT_TRUE(world.inventory().placedItems().empty());
}

TEST(GameplayWorldStackTest, PickupMergesWholeGroundStackAtomically)
{
    GameplayWorld world{
        3,
        {{ItemId::Ammo9mm, {640.0F, 360.0F}, 2}},
        InventoryGridSize{1, 1}};
    ItemInstance ammo{700, ItemId::Ammo9mm, 58};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    EXPECT_TRUE(world.groundItems().empty());
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.instanceId(), 700U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 60U);
}

TEST(GameplayWorldStackTest, FailedPickupPreservesGroundAndInventoryQuantities)
{
    GameplayWorld world{
        3,
        {{ItemId::Ammo9mm, {640.0F, 360.0F}, 1}},
        InventoryGridSize{1, 1}};
    ItemInstance ammo{701, ItemId::Ammo9mm, 60};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(world.groundItems().front().item().quantity(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 60U);
}

TEST(GameplayWorldStackTest, PartialContainerTransferConsumesWorldIdOnCommit)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{702, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.transferInventoryItemQuantity(true, 702, 4));

    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 6U);
    ASSERT_EQ(world.containerInventory().placedItems().size(), 1U);
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.instanceId(),
        1U);
    EXPECT_EQ(
        world.containerInventory().placedItems().front().item.quantity(),
        4U);
}

TEST(GameplayWorldStackTest, PickupMergesThenPlacesRemainderWithGroundId)
{
    GameplayWorld world{
        3,
        {{ItemId::Ammo9mm, {640.0F, 360.0F}, 5}},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{703, ItemId::Ammo9mm, 59};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    EXPECT_TRUE(world.groundItems().empty());
    ASSERT_EQ(world.inventory().placedItems().size(), 2U);
    EXPECT_EQ(world.inventory().placedItems()[0].item.instanceId(), 703U);
    EXPECT_EQ(world.inventory().placedItems()[0].item.quantity(), 60U);
    EXPECT_EQ(world.inventory().placedItems()[1].item.instanceId(), 1U);
    EXPECT_EQ(world.inventory().placedItems()[1].item.quantity(), 4U);
    EXPECT_EQ(world.inventory().placedItems()[1].origin, (GridPosition{1, 0}));
}

TEST(GameplayWorldStackTest, DropAndPickupPreserveWholeStackQuantityAndId)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{704, ItemId::Ammo9mm, 25};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.dropInventoryItem(704));
    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(world.groundItems().front().item().instanceId(), 704U);
    EXPECT_EQ(world.groundItems().front().item().quantity(), 25U);

    GameplayInput pickup{};
    pickup.interactJustPressed = true;
    world.update(pickup, 0.0F);

    EXPECT_TRUE(world.groundItems().empty());
    ASSERT_EQ(world.inventory().placedItems().size(), 1U);
    EXPECT_EQ(world.inventory().placedItems().front().item.instanceId(), 704U);
    EXPECT_EQ(world.inventory().placedItems().front().item.quantity(), 25U);
}

TEST(GameplayWorldStackTest, PartialDropCreatesGroundStackAtPlayersFeet)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{705, ItemId::Ammo9mm, 25};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.dropInventoryItemQuantity(
        705,
        5,
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(705), 20U);
    ASSERT_EQ(world.groundItems().size(), 1U);
    EXPECT_EQ(world.groundItems().front().item().instanceId(), 1U);
    EXPECT_EQ(world.groundItems().front().item().quantity(), 5U);
    EXPECT_FLOAT_EQ(world.groundItems().front().position().x, 656.0F);
    EXPECT_FLOAT_EQ(world.groundItems().front().position().y, 392.0F);
}

TEST(GameplayWorldStackTest, QuantityPlacementWorksInsidePlayerInventory)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{3, 1}};
    ItemInstance ammo{706, ItemId::Ammo9mm, 9};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    ASSERT_TRUE(world.placeInventoryItemQuantity(
        true,
        true,
        706,
        1,
        {2, 0},
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(706), 8U);
    EXPECT_EQ(world.inventory().quantityOf(1), 1U);
    EXPECT_EQ(world.inventory().originOf(1),
              (std::optional<GridPosition>{GridPosition{2, 0}}));
}

TEST(GameplayWorldStackTest, FailedQuantityPlacementDoesNotConsumeWorldId)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{3, 1}};
    ItemInstance sourceAmmo{707, ItemId::Ammo9mm, 10};
    ItemInstance fullAmmo{708, ItemId::Ammo9mm, 60};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(sourceAmmo), {0, 0}));
    ASSERT_TRUE(world.inventory().tryPlace(std::move(fullAmmo), {1, 0}));

    EXPECT_FALSE(world.placeInventoryItemQuantity(
        true,
        true,
        707,
        1,
        {1, 0},
        ItemOrientation::Degrees0));
    ASSERT_TRUE(world.placeInventoryItemQuantity(
        true,
        true,
        707,
        1,
        {2, 0},
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(707), 9U);
    EXPECT_EQ(world.inventory().quantityOf(1), 1U);
    EXPECT_EQ(world.inventory().originOf(2), std::nullopt);
}

TEST(GameplayWorldStackTest, PartialDropRejectsConflictingWorldId)
{
    GameplayWorld world{
        3,
        {},
        InventoryGridSize{2, 1}};
    ItemInstance ammo{1, ItemId::Ammo9mm, 10};
    ASSERT_TRUE(world.inventory().tryPlace(std::move(ammo), {0, 0}));

    EXPECT_FALSE(world.dropInventoryItemQuantity(
        1,
        1,
        ItemOrientation::Degrees0));

    EXPECT_EQ(world.inventory().quantityOf(1), 10U);
    EXPECT_TRUE(world.groundItems().empty());
}

namespace
{
    void movePlayerIntoExtractionPoint(
        GameplayWorld &world)
    {
        GameplayInput moveLeft{};
        moveLeft.moveLeft = true;
        world.update(moveLeft, 2.0F);

        GameplayInput moveDown{};
        moveDown.moveDown = true;
        world.update(moveDown, 0.65F);

        ASSERT_EQ(
            world.raidSession().state(),
            RaidSessionState::Extracting);
    }
}

TEST(GameplayWorldRaidTest, StartsActiveRaidWithPublishedExtractionPoint)
{
    const GameplayWorld world;

    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(
        world.raidSession().raidTimeRemaining(),
        180.0F);

    const Rect &bounds =
        world.extractionPoint().bounds();

    EXPECT_FLOAT_EQ(bounds.position.x, 64.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 520.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 176.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 136.0F);
}

TEST(GameplayWorldRaidTest, IndependentInteriorSwitchesActiveSpatialAuthority)
{
    RaidWorldConfig config;
    config.worldSize = Vec2{800.0F, 600.0F};
    config.playerSpawn = Vec2{100.0F, 100.0F};
    config.extractionPoint =
        ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
    config.initialEnemies = {
        EnemySpawn{Vec2{620.0F, 100.0F}, Vec2{50.0F, 50.0F}, 12}};
    RaidInteriorWorldConfig interior;
    interior.id = RaidSpaceDefinitionId{"raid_space.test.office"};
    interior.displayName = "Test Office";
    interior.layoutKnown = true;
    interior.worldSize = Vec2{480.0F, 360.0F};
    interior.exteriorEntrance =
        ContentRect{Vec2{80.0F, 80.0F}, Vec2{100.0F, 100.0F}};
    interior.exteriorReturn = Vec2{190.0F, 100.0F};
    interior.interiorSpawn = Vec2{80.0F, 80.0F};
    interior.interiorExit =
        ContentRect{Vec2{60.0F, 60.0F}, Vec2{120.0F, 120.0F}};
    interior.initialEnemies = {
        EnemySpawn{Vec2{300.0F, 80.0F}, Vec2{50.0F, 50.0F}, 12}};
    interior.ballisticBlockers = {
        BallisticBlocker{1U,
                         Rect{Vec2{220.0F, 160.0F}, Vec2{70.0F, 100.0F}}}};
    config.interiors.push_back(std::move(interior));
    GameplayWorld world{std::move(config)};

    ASSERT_TRUE(world.inOutdoorRaidSpace());
    ASSERT_EQ(world.enemies().size(), 1U);
    const Vec2 outdoorEnemyPosition = world.enemies().front().position();

    GameplayInput enter;
    enter.interactJustPressed = true;
    world.update(enter, 0.0F);

    EXPECT_TRUE(world.spaceTransitionedLastUpdate());
    EXPECT_FALSE(world.inOutdoorRaidSpace());
    EXPECT_EQ(
        world.activeRaidSpaceId(),
        RaidSpaceDefinitionId{"raid_space.test.office"});
    EXPECT_EQ(world.enemies().size(), 1U);
    EXPECT_EQ(world.ballisticBlockers().size(), 1U);
    EXPECT_EQ(world.raidSpaceWorldSize().x, 480.0F);
    const std::optional<RaidInteriorMapProjection> mapProjection =
        world.activeInteriorMapProjection();
    ASSERT_TRUE(mapProjection.has_value());
    EXPECT_EQ(mapProjection->id,
              RaidSpaceDefinitionId{"raid_space.test.office"});
    EXPECT_EQ(mapProjection->blockers.size(), 1U);
    EXPECT_EQ(mapProjection->exit,
              (ContentRect{Vec2{60.0F, 60.0F}, Vec2{120.0F, 120.0F}}));

    const Vec2 interiorEnemyBefore = world.enemies().front().position();
    world.update(GameplayInput{}, 0.25F);
    EXPECT_NE(world.enemies().front().position().x, interiorEnemyBefore.x);

    GameplayInput fire = makeFireInput();
    fire.aimWorldPosition = Vec2{430.0F, 120.0F};
    world.update(fire, 0.0F);
    ASSERT_FALSE(world.logicalBallistics().empty());

    GameplayInput leave;
    leave.interactJustPressed = true;
    world.update(leave, 0.0F);

    EXPECT_TRUE(world.spaceTransitionedLastUpdate());
    EXPECT_TRUE(world.inOutdoorRaidSpace());
    EXPECT_TRUE(world.logicalBallistics().empty());
    ASSERT_EQ(world.enemies().size(), 1U);
    EXPECT_FLOAT_EQ(world.enemies().front().position().x,
                    outdoorEnemyPosition.x);
    EXPECT_FLOAT_EQ(world.enemies().front().position().y,
                    outdoorEnemyPosition.y);
}

TEST(GameplayWorldRaidTest, OutdoorInteriorPortalAppearsOnlyAfterDiscovery)
{
    GameplayWorld world{makeInteriorDiscoveryWorldConfig()};
    const RaidSpaceDefinitionId officeId{"raid_space.test.office"};

    EXPECT_FALSE(world.activeRaidSpacePortal().has_value());
    EXPECT_FALSE(world.tacticalMap().specialLocationVisible(officeId));

    GameplayInput approach;
    approach.moveLeft = true;
    world.update(approach, 1.5F);

    EXPECT_TRUE(world.inOutdoorRaidSpace());
    EXPECT_TRUE(world.activeRaidSpacePortal().has_value());
    EXPECT_TRUE(world.tacticalMap().specialLocationVisible(officeId));
}

TEST(GameplayWorldRaidTest, EnteringPortalDiscoversItBeforeSpaceTransition)
{
    GameplayWorld world{makeInteriorDiscoveryWorldConfig()};
    const RaidSpaceDefinitionId officeId{"raid_space.test.office"};
    GameplayInput approachAndEnter;
    approachAndEnter.moveLeft = true;
    approachAndEnter.interactJustPressed = true;

    world.update(approachAndEnter, 1.5F);

    EXPECT_FALSE(world.inOutdoorRaidSpace());
    EXPECT_TRUE(world.spaceTransitionedLastUpdate());
    EXPECT_TRUE(world.tacticalMap().specialLocationVisible(officeId));
    ASSERT_TRUE(world.activeRaidSpacePortal().has_value());
    EXPECT_EQ(
        *world.activeRaidSpacePortal(),
        (ContentRect{Vec2{60.0F, 60.0F}, Vec2{120.0F, 120.0F}}));
    EXPECT_FALSE(world.activeInteriorMapProjection().has_value());
}

TEST(GameplayWorldRaidTest, OrdinarySurvivorTransferRequiresContinuousHold)
{
    RaidWorldConfig config;
    config.playerSpawn = Vec2{80.0F, 320.0F};
    config.extractionPoint = ContentRect{
        Vec2{1080.0F, 260.0F}, Vec2{140.0F, 140.0F}};
    config.initialEnemies.clear();
    config.rescue = RaidWorldConfig::OrdinarySurvivorRescue{
        ContentRect{Vec2{40.0F, 290.0F}, Vec2{200.0F, 120.0F}},
        2.0F};
    GameplayWorld world{std::move(config)};

    ASSERT_TRUE(world.ordinarySurvivorRescuePoint().has_value());
    EXPECT_TRUE(world.ordinarySurvivorRescueInteractionInRange());
    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueProgress(), 0.0F);

    GameplayInput hold;
    hold.interactPressed = true;
    world.update(hold, 1.0F);
    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueProgress(), 0.5F);
    EXPECT_FALSE(world.ordinarySurvivorRescueReady());

    world.update(GameplayInput{}, 0.1F);
    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueProgress(), 0.0F);

    world.update(hold, 2.0F);
    EXPECT_TRUE(world.ordinarySurvivorRescueReady());
    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueTimeRemaining(), 0.0F);
    world.confirmOrdinarySurvivorRescue();
    EXPECT_FALSE(world.ordinarySurvivorRescueInteractionInRange());
    EXPECT_FALSE(world.ordinarySurvivorRescueReady());
}

TEST(GameplayWorldRaidTest, InventoryOpenCancelsOrdinarySurvivorTransfer)
{
    RaidWorldConfig config;
    config.playerSpawn = Vec2{80.0F, 320.0F};
    config.extractionPoint = ContentRect{
        Vec2{1080.0F, 260.0F}, Vec2{140.0F, 140.0F}};
    config.initialEnemies.clear();
    config.rescue = RaidWorldConfig::OrdinarySurvivorRescue{
        ContentRect{Vec2{40.0F, 290.0F}, Vec2{200.0F, 120.0F}},
        2.0F};
    GameplayWorld world{std::move(config)};
    GameplayInput input;
    input.interactPressed = true;
    world.update(input, 0.75F);
    ASSERT_GT(world.ordinarySurvivorRescueProgress(), 0.0F);

    input.inventoryOpen = true;
    world.update(input, 0.1F);

    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueProgress(), 0.0F);
    EXPECT_FALSE(world.ordinarySurvivorRescueReady());
}

TEST(GameplayWorldRaidTest, ActiveWorldAdvancesRaidClock)
{
    GameplayWorld world;

    world.update(GameplayInput{}, 1.25F);

    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(
        world.raidSession().raidTimeRemaining(),
        178.75F);
}

TEST(GameplayWorldRaidTest, MovingCenterIntoPointStartsExtraction)
{
    GameplayWorld world;

    movePlayerIntoExtractionPoint(world);

    EXPECT_GT(
        world.raidSession().extractionTimeElapsed(),
        0.0F);
    EXPECT_TRUE(
        world.extractionPoint().contains(
            Vec2{
                world.player().position().x +
                    world.player().size() / 2.0F,
                world.player().position().y +
                    world.player().size() / 2.0F}));
}

TEST(GameplayWorldRaidTest, LeavingPointCancelsExtraction)
{
    GameplayWorld world;
    movePlayerIntoExtractionPoint(world);

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 0.5F);

    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(
        world.raidSession().extractionTimeElapsed(),
        0.0F);
}

TEST(GameplayWorldRaidTest, ContinuousStayExtractsAndFreezesGameplay)
{
    GameplayWorld world{
        3,
        {
            {
                ItemId::Cola,
                Vec2{176.0F, 532.0F},
            },
        }};
    movePlayerIntoExtractionPoint(world);

    world.update(GameplayInput{}, 2.5F);

    ASSERT_EQ(
        world.raidSession().state(),
        RaidSessionState::Extracted);
    const Vec2 extractedPosition =
        world.player().position();
    const std::size_t ballisticCount =
        world.logicalBallistics().size();
    ASSERT_EQ(world.groundItems().size(), 1U);

    GameplayInput terminalInput{};
    terminalInput.moveRight = true;
    terminalInput.firePressed = true;
    terminalInput.fireJustPressed = true;
    terminalInput.interactJustPressed = true;
    world.update(terminalInput, 1.0F);

    EXPECT_FLOAT_EQ(
        world.player().position().x,
        extractedPosition.x);
    EXPECT_FLOAT_EQ(
        world.player().position().y,
        extractedPosition.y);
    EXPECT_EQ(world.logicalBallistics().size(), ballisticCount);
    EXPECT_EQ(world.groundItems().size(), 1U);
    EXPECT_TRUE(world.inventory().placedItems().empty());
}

TEST(GameplayWorldRaidTest, PlayerDeathCommandIsStickyAndFreezesWorld)
{
    GameplayWorld world;
    const Vec2 alivePosition =
        world.player().position();

    ASSERT_TRUE(world.markPlayerDead());
    EXPECT_FALSE(world.markPlayerDead());
    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::PlayerDead);
    EXPECT_EQ(world.player().health(), 0);
    EXPECT_TRUE(world.player().isDead());

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    world.update(moveRight, 1.0F);

    EXPECT_FLOAT_EQ(
        world.player().position().x,
        alivePosition.x);
    EXPECT_FLOAT_EQ(
        world.player().position().y,
        alivePosition.y);
}

TEST(GameplayWorldRaidTest, PlayerDamageConnectsHealthToStickyRaidDeath)
{
    GameplayWorld world;

    EXPECT_FALSE(world.damagePlayer(1));
    EXPECT_EQ(world.player().health(), 2);
    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::InRaid);

    EXPECT_TRUE(world.damagePlayer(2));
    EXPECT_EQ(world.player().health(), 0);
    EXPECT_TRUE(world.player().isDead());
    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::PlayerDead);

    EXPECT_FALSE(world.damagePlayer(1));
    EXPECT_EQ(world.player().health(), 0);
}

TEST(GameplayWorldRaidTest, AlphaWorldPublishesDamageInsteadOfGuessingArmor)
{
    GameplayWorld world{RaidWorldConfig{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {EnemySpawn{Vec2{630.0F, 330.0F}, Vec2{50.0F, 50.0F}, 3}},
        100,
        100,
        true}};

    std::vector<PlayerDamageObservation> observations;
    for (int frame = 0; frame < 600 && observations.empty(); ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 60.0F);
        observations = world.takePlayerDamageObservations();
    }

    ASSERT_FALSE(observations.empty());
    EXPECT_EQ(world.player().health(), 100);
    EXPECT_EQ(observations.front().baseDamage, 12);
    EXPECT_EQ(observations.front().region, HitRegion::Torso);
    EXPECT_EQ(observations.front().penetration, 1);
    EXPECT_EQ(observations.front().armorDamage, 2);
}

TEST(GameplayWorldRaidTest, BallisticBlockerBlocksPlayerAndLogicalShot)
{
    const RaidWorldConfig config{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {},
        100,
        100,
        false,
        {BallisticBlocker{
            1,
            Rect{Vec2{660.0F, 300.0F}, Vec2{60.0F, 80.0F}}}}};
    GameplayWorld world{config};

    const Vec2 start = world.player().position();
    GameplayInput move{};
    move.moveRight = true;
    world.update(move, 0.20F);
    EXPECT_FLOAT_EQ(world.player().position().x, 628.0F);
    EXPECT_FLOAT_EQ(world.player().position().y, start.y);

    GameplayWorld shotWorld{config};

    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    fire.aimWorldPosition = Vec2{900.0F, 336.0F};
    shotWorld.update(fire, 0.0F);
    ASSERT_EQ(shotWorld.logicalBallistics().size(), 1U);

    shotWorld.update(GameplayInput{}, 0.10F);
    EXPECT_TRUE(shotWorld.logicalBallistics().empty());
    ASSERT_EQ(shotWorld.hitResultsLastUpdate().size(), 1U);
    EXPECT_EQ(
        shotWorld.hitResultsLastUpdate().front().targetKind,
        HitTargetKind::Obstacle);
    EXPECT_EQ(shotWorld.hitResultsLastUpdate().front().damageApplied, 0);
    const std::vector<ShotPresentationSnapshot> tracer =
        shotWorld.shotPresentationSnapshots();
    ASSERT_EQ(tracer.size(), 1U);
    EXPECT_FLOAT_EQ(
        tracer.front().end.x,
        shotWorld.hitResultsLastUpdate().front().position.x);
    EXPECT_FLOAT_EQ(
        tracer.front().end.y,
        shotWorld.hitResultsLastUpdate().front().position.y);
}

TEST(GameplayWorldRaidTest, BlockedAxisStillAllowsSlidingAlongCover)
{
    GameplayWorld world{RaidWorldConfig{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {},
        100,
        100,
        false,
        {BallisticBlocker{
            1,
            Rect{Vec2{540.0F, 260.0F}, Vec2{60.0F, 180.0F}}}}}};

    const Vec2 start = world.player().position();
    GameplayInput moveAlongLeftWall{};
    moveAlongLeftWall.moveLeft = true;
    moveAlongLeftWall.moveUp = true;
    world.update(moveAlongLeftWall, 0.10F);

    EXPECT_FLOAT_EQ(world.player().position().x, start.x);
    EXPECT_LT(world.player().position().y, start.y);
}

TEST(GameplayWorldRaidTest, VerticalCoverBlocksBothDirectionsAndLongFrames)
{
    GameplayWorld worldMovingUp{RaidWorldConfig{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {},
        100,
        100,
        false,
        {BallisticBlocker{
            1,
            Rect{Vec2{590.0F, 220.0F}, Vec2{80.0F, 40.0F}}}}}};

    GameplayInput moveUp{};
    moveUp.moveUp = true;
    worldMovingUp.update(moveUp, 1.0F);
    EXPECT_FLOAT_EQ(worldMovingUp.player().position().y, 260.0F);

    GameplayWorld worldMovingDown{RaidWorldConfig{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {},
        100,
        100,
        false,
        {BallisticBlocker{
            1,
            Rect{Vec2{590.0F, 420.0F}, Vec2{80.0F, 40.0F}}}}}};

    GameplayInput moveDown{};
    moveDown.moveDown = true;
    worldMovingDown.update(moveDown, 1.0F);
    EXPECT_FLOAT_EQ(worldMovingDown.player().position().y, 388.0F);
}

TEST(GameplayWorldRaidTest, EveryPublishedBlockerStopsMovementOnAllFourSides)
{
    constexpr float playerSize{32.0F};
    constexpr float approachMargin{24.0F};
    for (const MapDefinition &map : publishedContentRegistry().maps())
    {
        for (const BallisticBlockerDefinition &definition :
             map.ballisticBlockers)
        {
            SCOPED_TRACE(
                std::string{map.id.value()} + "/" + definition.id);
            const Rect obstacle{
                definition.bounds.position,
                definition.bounds.size};
            const auto makeWorld = [&](Vec2 spawn)
            {
                return GameplayWorld{RaidWorldConfig{
                    map.worldSize,
                    spawn,
                    map.extractionPoint,
                    {},
                    100,
                    100,
                    false,
                    {BallisticBlocker{1, obstacle}}}};
            };

            const float centeredX = obstacle.position.x +
                (obstacle.size.x - playerSize) * 0.5F;
            const float centeredY = obstacle.position.y +
                (obstacle.size.y - playerSize) * 0.5F;

            GameplayWorld fromAbove = makeWorld(Vec2{
                centeredX,
                obstacle.position.y - playerSize - approachMargin});
            GameplayInput moveDown{};
            moveDown.moveDown = true;
            fromAbove.update(moveDown, 2.0F);
            EXPECT_FLOAT_EQ(
                fromAbove.player().position().y,
                obstacle.position.y - playerSize);

            GameplayWorld fromBelow = makeWorld(Vec2{
                centeredX,
                obstacle.position.y + obstacle.size.y + approachMargin});
            GameplayInput moveUp{};
            moveUp.moveUp = true;
            fromBelow.update(moveUp, 2.0F);
            EXPECT_FLOAT_EQ(
                fromBelow.player().position().y,
                obstacle.position.y + obstacle.size.y);

            GameplayWorld fromLeft = makeWorld(Vec2{
                obstacle.position.x - playerSize - approachMargin,
                centeredY});
            GameplayInput moveRight{};
            moveRight.moveRight = true;
            fromLeft.update(moveRight, 2.0F);
            EXPECT_FLOAT_EQ(
                fromLeft.player().position().x,
                obstacle.position.x - playerSize);

            GameplayWorld fromRight = makeWorld(Vec2{
                obstacle.position.x + obstacle.size.x + approachMargin,
                centeredY});
            GameplayInput moveLeft{};
            moveLeft.moveLeft = true;
            fromRight.update(moveLeft, 2.0F);
            EXPECT_FLOAT_EQ(
                fromRight.player().position().x,
                obstacle.position.x + obstacle.size.x);
        }
    }
}

TEST(GameplayWorldRaidTest, EnemyPursuitRoutesAroundVerticalCoverWithoutOverlap)
{
    GameplayWorld fromAbove{RaidWorldConfig{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {EnemySpawn{Vec2{591.0F, 120.0F}, Vec2{50.0F, 50.0F}, 12}},
        100,
        100,
        false,
        {BallisticBlocker{
            1,
            Rect{Vec2{570.0F, 220.0F}, Vec2{140.0F, 40.0F}}}}}};
    const Rect aboveCover{Vec2{570.0F, 220.0F}, Vec2{140.0F, 40.0F}};
    const float aboveInitialDistance = playerEnemyCenterDistance(fromAbove);
    float aboveMinimumDistance = aboveInitialDistance;
    fromAbove.emitPlayerNoise(1000.0F);
    for (int frame{}; frame < 240; ++frame)
    {
        fromAbove.update(GameplayInput{}, 1.0F / 60.0F);
        ASSERT_EQ(fromAbove.enemies().size(), 1U);
        EXPECT_FALSE(isCollision(
            enemyBounds(fromAbove.enemies().front()),
            aboveCover));
        aboveMinimumDistance = std::min(
            aboveMinimumDistance,
            playerEnemyCenterDistance(fromAbove));
    }
    ASSERT_EQ(fromAbove.enemies().size(), 1U);
    EXPECT_LT(aboveMinimumDistance, aboveInitialDistance - 40.0F);

    GameplayWorld fromBelow{RaidWorldConfig{
        Vec2{1280.0F, 720.0F},
        Vec2{600.0F, 320.0F},
        ContentRect{Vec2{1100.0F, 600.0F}, Vec2{80.0F, 80.0F}},
        {EnemySpawn{Vec2{591.0F, 520.0F}, Vec2{50.0F, 50.0F}, 12}},
        100,
        100,
        false,
        {BallisticBlocker{
            1,
            Rect{Vec2{570.0F, 420.0F}, Vec2{140.0F, 40.0F}}}}}};
    const Rect belowCover{Vec2{570.0F, 420.0F}, Vec2{140.0F, 40.0F}};
    const float belowInitialDistance = playerEnemyCenterDistance(fromBelow);
    float belowMinimumDistance = belowInitialDistance;
    fromBelow.emitPlayerNoise(1000.0F);
    for (int frame{}; frame < 240; ++frame)
    {
        fromBelow.update(GameplayInput{}, 1.0F / 60.0F);
        ASSERT_EQ(fromBelow.enemies().size(), 1U);
        EXPECT_FALSE(isCollision(
            enemyBounds(fromBelow.enemies().front()),
            belowCover));
        belowMinimumDistance = std::min(
            belowMinimumDistance,
            playerEnemyCenterDistance(fromBelow));
    }
    ASSERT_EQ(fromBelow.enemies().size(), 1U);
    EXPECT_LT(belowMinimumDistance, belowInitialDistance - 40.0F);
}

TEST(GameplayWorldRaidTest, EveryPublishedBlockerRoutesEnemyWithoutOverlap)
{
    constexpr float enemySize{50.0F};
    constexpr float playerSize{32.0F};
    constexpr float enemyMargin{24.0F};
    constexpr float playerMargin{80.0F};
    const auto advanceEnemy = [](GameplayWorld &world, const Rect &obstacle)
    {
        const float initialDistance = playerEnemyCenterDistance(world);
        float minimumDistance = initialDistance;
        world.emitPlayerNoise(2000.0F);
        for (int frame{}; frame < 240; ++frame)
        {
            world.update(GameplayInput{}, 1.0F / 60.0F);
            EXPECT_FALSE(isCollision(
                enemyBounds(world.enemies().front()),
                obstacle));
            minimumDistance = std::min(
                minimumDistance,
                playerEnemyCenterDistance(world));
        }
        EXPECT_LT(minimumDistance, initialDistance - 1.0F);
    };

    for (const MapDefinition &map : publishedContentRegistry().maps())
    {
        for (const BallisticBlockerDefinition &definition :
             map.ballisticBlockers)
        {
            SCOPED_TRACE(
                std::string{map.id.value()} + "/" + definition.id);
            const Rect obstacle{
                definition.bounds.position,
                definition.bounds.size};
            const auto makeWorld = [&](Vec2 playerSpawn, Vec2 enemySpawn)
            {
                return GameplayWorld{RaidWorldConfig{
                    map.worldSize,
                    playerSpawn,
                    map.extractionPoint,
                    {EnemySpawn{enemySpawn, Vec2{enemySize, enemySize}, 12}},
                    100,
                    100,
                    false,
                    {BallisticBlocker{1, obstacle}}}};
            };

            const float obstacleCenterX =
                obstacle.position.x + obstacle.size.x * 0.5F;
            const float obstacleCenterY =
                obstacle.position.y + obstacle.size.y * 0.5F;

            GameplayWorld fromAbove = makeWorld(
                Vec2{
                    obstacleCenterX - playerSize * 0.5F,
                    obstacle.position.y + obstacle.size.y + playerMargin},
                Vec2{
                    obstacleCenterX - enemySize * 0.5F,
                    obstacle.position.y - enemySize - enemyMargin});
            advanceEnemy(fromAbove, obstacle);

            GameplayWorld fromBelow = makeWorld(
                Vec2{
                    obstacleCenterX - playerSize * 0.5F,
                    obstacle.position.y - playerSize - playerMargin},
                Vec2{
                    obstacleCenterX - enemySize * 0.5F,
                    obstacle.position.y + obstacle.size.y + enemyMargin});
            advanceEnemy(fromBelow, obstacle);

            GameplayWorld fromLeft = makeWorld(
                Vec2{
                    obstacle.position.x + obstacle.size.x + playerMargin,
                    obstacleCenterY - playerSize * 0.5F},
                Vec2{
                    obstacle.position.x - enemySize - enemyMargin,
                    obstacleCenterY - enemySize * 0.5F});
            advanceEnemy(fromLeft, obstacle);

            GameplayWorld fromRight = makeWorld(
                Vec2{
                    obstacle.position.x - playerSize - playerMargin,
                    obstacleCenterY - playerSize * 0.5F},
                Vec2{
                    obstacle.position.x + obstacle.size.x + enemyMargin,
                    obstacleCenterY - enemySize * 0.5F});
            advanceEnemy(fromRight, obstacle);
        }
    }
}

TEST(GameplayWorldRaidTest, OutdoorCoverBlocksVisualAcquisitionAndMelee)
{
    RaidWorldConfig config;
    config.worldSize = Vec2{800.0F, 600.0F};
    config.playerSpawn = Vec2{269.0F, 250.0F};
    config.extractionPoint =
        ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
    config.initialEnemies = {
        EnemySpawn{Vec2{330.0F, 241.0F}, Vec2{50.0F, 50.0F}, 12}};
    config.playerMaximumHealth = 100;
    config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    config.ballisticBlockers = {
        BallisticBlocker{
            1,
            Rect{Vec2{310.0F, 100.0F}, Vec2{10.0F, 350.0F}}}};
    GameplayWorld world{config};
    const Vec2 initialEnemyPosition = world.enemies().front().position();

    for (int frame{}; frame < 120; ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 60.0F);
    }

    EXPECT_EQ(
        world.enemies().front().awarenessState(),
        EnemyAwarenessState::Unaware);
    EXPECT_FLOAT_EQ(
        world.enemies().front().position().x,
        initialEnemyPosition.x);
    EXPECT_FLOAT_EQ(
        world.enemies().front().position().y,
        initialEnemyPosition.y);

    world.emitPlayerNoise(1000.0F);
    for (int frame{}; frame < 60; ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 120.0F);
    }

    EXPECT_TRUE(world.takePlayerDamageObservations().empty());
    EXPECT_EQ(world.player().health(), 100);
}

TEST(GameplayWorldRaidTest, AudibleEnemyRoutesAroundOutdoorCover)
{
    RaidWorldConfig config;
    config.worldSize = Vec2{800.0F, 600.0F};
    config.playerSpawn = Vec2{250.0F, 250.0F};
    config.extractionPoint =
        ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
    config.initialEnemies = {
        EnemySpawn{Vec2{355.0F, 241.0F}, Vec2{50.0F, 50.0F}, 12}};
    config.playerMaximumHealth = 100;
    config.playerCurrentHealth = 100;
    const Rect cover{Vec2{300.0F, 180.0F}, Vec2{20.0F, 170.0F}};
    config.ballisticBlockers = {BallisticBlocker{1, cover}};
    GameplayWorld world{config};
    const float initialDistance = playerEnemyCenterDistance(world);
    float minimumDistance = initialDistance;

    world.emitPlayerNoise(1000.0F);
    for (int frame{}; frame < 300; ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 60.0F);
        EXPECT_FALSE(isCollision(
            enemyBounds(world.enemies().front()),
            cover));
        minimumDistance = std::min(
            minimumDistance,
            playerEnemyCenterDistance(world));
    }

    EXPECT_LT(minimumDistance, initialDistance - 30.0F);
    EXPECT_NE(
        world.enemies().front().awarenessState(),
        EnemyAwarenessState::Unaware);
}

TEST(GameplayWorldRaidTest, AudibleEnemyPursuesPlayerFlushAgainstCover)
{
    RaidWorldConfig config;
    config.worldSize = Vec2{800.0F, 600.0F};
    config.playerSpawn = Vec2{278.0F, 250.0F};
    config.extractionPoint =
        ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
    config.initialEnemies = {
        EnemySpawn{Vec2{360.0F, 241.0F}, Vec2{50.0F, 50.0F}, 12}};
    config.playerMaximumHealth = 100;
    config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    const Rect cover{Vec2{310.0F, 150.0F}, Vec2{20.0F, 200.0F}};
    config.ballisticBlockers = {BallisticBlocker{1, cover}};
    GameplayWorld world{config};
    const Vec2 initialEnemyPosition = world.enemies().front().position();
    const float initialDistance = playerEnemyCenterDistance(world);
    float minimumDistance = initialDistance;

    world.emitPlayerNoise(1000.0F);
    ASSERT_EQ(
        world.enemies().front().awarenessState(),
        EnemyAwarenessState::Alerted);
    for (int frame{}; frame < 360; ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 60.0F);
        EXPECT_FALSE(isCollision(
            enemyBounds(world.enemies().front()),
            cover));
        minimumDistance = std::min(
            minimumDistance,
            playerEnemyCenterDistance(world));
    }

    EXPECT_GT(
        std::hypot(
            world.enemies().front().position().x - initialEnemyPosition.x,
            world.enemies().front().position().y - initialEnemyPosition.y),
        30.0F);
    EXPECT_LT(minimumDistance, initialDistance - 20.0F);
}

TEST(GameplayWorldRaidTest, AlertedEnemyContinuesInvestigationAfterPlayerBreaksSight)
{
    RaidWorldConfig config;
    config.worldSize = Vec2{800.0F, 600.0F};
    config.playerSpawn = Vec2{278.0F, 130.0F};
    config.extractionPoint =
        ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
    config.initialEnemies = {
        EnemySpawn{Vec2{360.0F, 130.0F}, Vec2{50.0F, 50.0F}, 12}};
    config.playerMaximumHealth = 100;
    config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    const Rect cover{Vec2{310.0F, 200.0F}, Vec2{20.0F, 220.0F}};
    config.ballisticBlockers = {BallisticBlocker{1, cover}};
    GameplayWorld world{config};

    world.update(GameplayInput{}, 0.0F);
    ASSERT_EQ(
        world.enemies().front().awarenessState(),
        EnemyAwarenessState::Alerted);

    const Vec2 enemyBeforeSightBreak = world.enemies().front().position();
    GameplayInput hideBehindCover{};
    hideBehindCover.moveDown = true;
    world.update(hideBehindCover, 0.45F);
    ASSERT_GT(world.player().position().y, cover.position.y);
    ASSERT_NE(
        world.enemies().front().awarenessState(),
        EnemyAwarenessState::Unaware);
    const float distanceAfterSightBreak = playerEnemyCenterDistance(world);
    float minimumDistance = distanceAfterSightBreak;

    for (int frame{}; frame < 300; ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 60.0F);
        EXPECT_FALSE(isCollision(
            enemyBounds(world.enemies().front()),
            cover));
        minimumDistance = std::min(
            minimumDistance,
            playerEnemyCenterDistance(world));
    }

    EXPECT_GT(
        std::hypot(
            world.enemies().front().position().x -
                enemyBeforeSightBreak.x,
            world.enemies().front().position().y -
                enemyBeforeSightBreak.y),
        30.0F);
    EXPECT_LT(minimumDistance, distanceAfterSightBreak - 20.0F);
}

TEST(GameplayWorldRaidTest, InteriorCoverBlocksVisualAcquisition)
{
    RaidWorldConfig config;
    config.worldSize = Vec2{800.0F, 600.0F};
    config.playerSpawn = Vec2{100.0F, 100.0F};
    config.extractionPoint =
        ContentRect{Vec2{650.0F, 450.0F}, Vec2{100.0F, 100.0F}};
    RaidInteriorWorldConfig interior;
    interior.id = RaidSpaceDefinitionId{"raid_space.test.covered_office"};
    interior.displayName = "Covered Office";
    interior.worldSize = Vec2{480.0F, 360.0F};
    interior.exteriorEntrance =
        ContentRect{Vec2{80.0F, 80.0F}, Vec2{100.0F, 100.0F}};
    interior.exteriorReturn = Vec2{200.0F, 100.0F};
    interior.interiorSpawn = Vec2{80.0F, 150.0F};
    interior.interiorExit =
        ContentRect{Vec2{60.0F, 300.0F}, Vec2{120.0F, 50.0F}};
    interior.initialEnemies = {
        EnemySpawn{Vec2{220.0F, 140.0F}, Vec2{50.0F, 50.0F}, 12}};
    interior.ballisticBlockers = {
        BallisticBlocker{
            1,
            Rect{Vec2{160.0F, 60.0F}, Vec2{20.0F, 240.0F}}}};
    config.interiors.push_back(std::move(interior));
    GameplayWorld world{config};

    GameplayInput enter{};
    enter.interactJustPressed = true;
    world.update(enter, 0.0F);
    ASSERT_FALSE(world.inOutdoorRaidSpace());
    ASSERT_EQ(world.enemies().size(), 1U);
    const Vec2 initialEnemyPosition = world.enemies().front().position();

    for (int frame{}; frame < 120; ++frame)
    {
        world.update(GameplayInput{}, 1.0F / 60.0F);
    }

    EXPECT_EQ(
        world.enemies().front().awarenessState(),
        EnemyAwarenessState::Unaware);
    EXPECT_FLOAT_EQ(
        world.enemies().front().position().x,
        initialEnemyPosition.x);
    EXPECT_FLOAT_EQ(
        world.enemies().front().position().y,
        initialEnemyPosition.y);
}

TEST(GameplayWorldRaidTest, AttackWindowsReplacePassiveContactAndLethalFrameDoesNotFire)
{
    GameplayWorld world;

    ASSERT_TRUE(advanceUntilFirstScratchHits(world));
    ASSERT_EQ(world.player().health(), 2);
    ASSERT_TRUE(world.player().isImpactSlowed());
    world.update(GameplayInput{}, 0.0F);
    EXPECT_EQ(world.player().health(), 2);

    ASSERT_TRUE(retreatAndHoldUntilGrabStarts(world));
    ASSERT_EQ(
        world.enemies().front().attackType(),
        EnemyAttackType::Grab);
    ASSERT_TRUE(advanceUntilGrabBites(world));

    EXPECT_EQ(world.player().health(), 0);
    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::PlayerDead);
    EXPECT_TRUE(world.logicalBallistics().empty());
}

TEST(GameplayWorldRaidTest, SurvivingBiteSuppressesMovementAndFire)
{
    GameplayWorld world{3, 5};

    ASSERT_TRUE(advanceUntilFirstScratchHits(world));
    ASSERT_EQ(world.player().health(), 4);
    ASSERT_TRUE(retreatAndHoldUntilGrabStarts(world));
    ASSERT_TRUE(advanceUntilGrabBites(world));
    ASSERT_EQ(world.player().health(), 2);
    ASSERT_TRUE(world.player().isControlled());

    const Vec2 controlledPosition = world.player().position();
    GameplayInput blockedInput{};
    blockedInput.moveRight = true;
    blockedInput.firePressed = true;
    blockedInput.fireJustPressed = true;
    blockedInput.interactJustPressed = true;
    world.update(blockedInput, 0.10F);

    EXPECT_FLOAT_EQ(
        world.player().position().x,
        controlledPosition.x);
    EXPECT_FLOAT_EQ(
        world.player().position().y,
        controlledPosition.y);
    EXPECT_TRUE(world.logicalBallistics().empty());
    EXPECT_TRUE(world.player().isControlled());
}

TEST(GameplayWorldTest, PlayerNoiseAlertsOnlyEnemiesInsideRadius)
{
    GameplayWorld world{
        std::vector<EnemySpawn>{
            EnemySpawn{Vec2{650.0F, 370.0F}, Vec2{50.0F, 50.0F}, 3},
            EnemySpawn{Vec2{1100.0F, 650.0F}, Vec2{50.0F, 50.0F}, 3}},
        100};

    world.emitPlayerNoise(300.0F);

    ASSERT_EQ(world.enemies().size(), 2U);
    EXPECT_EQ(world.enemies()[0].awarenessState(), EnemyAwarenessState::Alerted);
    EXPECT_EQ(world.enemies()[1].awarenessState(), EnemyAwarenessState::Unaware);
}

TEST(GameplayWorldTest, ReportsAnAlertTransitionOnlyOnTheTransitionFrame)
{
    GameplayWorld world{
        std::vector<EnemySpawn>{
            EnemySpawn{Vec2{650.0F, 370.0F}, Vec2{50.0F, 50.0F}, 3}},
        100};

    world.update(GameplayInput{}, 0.0F);
    EXPECT_EQ(world.enemiesAlertedLastUpdate(), 1U);

    world.update(GameplayInput{}, 0.0F);
    EXPECT_EQ(world.enemiesAlertedLastUpdate(), 0U);
}

TEST(GameplayWorldRaidTest, RaidTimeoutIsTerminalAndFreezesWorld)
{
    GameplayWorld world;
    const Vec2 activePosition =
        world.player().position();

    world.update(GameplayInput{}, 180.0F);

    ASSERT_EQ(
        world.raidSession().state(),
        RaidSessionState::RaidEnded);
    EXPECT_FLOAT_EQ(
        world.raidSession().raidTimeRemaining(),
        0.0F);

    GameplayInput moveLeft{};
    moveLeft.moveLeft = true;
    world.update(moveLeft, 1.0F);

    EXPECT_FLOAT_EQ(
        world.player().position().x,
        activePosition.x);
    EXPECT_FLOAT_EQ(
        world.player().position().y,
        activePosition.y);
}

TEST(GameplayWorldRaidTest, HighRiskPressureIsDeterministicAndCapped)
{
    GameplayWorld first{makeHighRiskWorldConfig(11U)};
    GameplayWorld second{makeHighRiskWorldConfig(11U)};

    first.update(GameplayInput{}, 0.16F);
    second.update(GameplayInput{}, 0.16F);

    ASSERT_EQ(first.raidSession().phase(), RaidPhase::HighRisk);
    ASSERT_EQ(first.highRiskPressureWaveCount(), 1U);
    ASSERT_EQ(first.aliveEnemyCount(), 2U);
    ASSERT_EQ(first.enemies().size(), second.enemies().size());
    for (std::size_t index = 0U; index < first.enemies().size(); ++index)
    {
        EXPECT_EQ(
            first.enemies()[index].combatTargetId(),
            second.enemies()[index].combatTargetId());
        EXPECT_FLOAT_EQ(
            first.enemies()[index].position().x,
            second.enemies()[index].position().x);
        EXPECT_FLOAT_EQ(
            first.enemies()[index].position().y,
            second.enemies()[index].position().y);
    }

    first.update(GameplayInput{}, 0.20F);
    EXPECT_EQ(first.aliveEnemyCount(), 3U);
    EXPECT_EQ(first.highRiskPressureWaveCount(), 2U);
    EXPECT_EQ(first.highRiskActiveEnemyCap(), 3U);
}

TEST(GameplayWorldRaidTest,
    HighRiskControlRequiresHeldInterruptibleInteraction)
{
    RaidWorldConfig config = makeHighRiskWorldConfig();
    config.highRisk.regularPhaseDurationSeconds = 10.0F;
    config.highRisk.activationDurationSeconds = 0.20F;
    GameplayWorld world{std::move(config)};

    GameplayInput hold{};
    hold.interactPressed = true;
    world.update(hold, 0.10F);
    EXPECT_NEAR(world.highRiskControlProgress(), 0.5F, 0.001F);
    EXPECT_EQ(world.raidSession().phase(), RaidPhase::Regular);

    world.update(GameplayInput{}, 0.01F);
    EXPECT_FLOAT_EQ(world.highRiskControlProgress(), 0.0F);

    world.update(hold, 0.05F);
    GameplayInput leave = hold;
    leave.moveRight = true;
    leave.sprint = true;
    world.update(leave, 0.50F);
    EXPECT_FALSE(world.highRiskControlInteractionInRange());
    EXPECT_FLOAT_EQ(world.highRiskControlProgress(), 0.0F);

    RaidWorldConfig damageConfig = makeHighRiskWorldConfig();
    damageConfig.highRisk.regularPhaseDurationSeconds = 10.0F;
    damageConfig.highRisk.activationDurationSeconds = 0.20F;
    GameplayWorld damageWorld{std::move(damageConfig)};
    damageWorld.update(hold, 0.08F);
    EXPECT_GT(damageWorld.highRiskControlProgress(), 0.0F);
    EXPECT_FALSE(damageWorld.damagePlayer(1));
    EXPECT_FLOAT_EQ(damageWorld.highRiskControlProgress(), 0.0F);

    damageWorld.update(hold, 0.21F);
    EXPECT_EQ(damageWorld.raidSession().phase(), RaidPhase::HighRisk);
    EXPECT_FLOAT_EQ(damageWorld.highRiskControlProgress(), 1.0F);
    EXPECT_TRUE(damageWorld.raidSession().emergencyExtractionOpen());
}

TEST(
    GameplayWorldRaidTest, HighRiskPressureSkipsNearAndOccupiedSpawns)
{
    RaidWorldConfig config = makeHighRiskWorldConfig(0U);
    config.highRisk.pressureSpawns[0].position = Vec2{610.0F, 330.0F};
    config.initialEnemies = {config.highRisk.pressureSpawns[1]};
    GameplayWorld world{std::move(config)};

    world.update(GameplayInput{}, 0.16F);

    ASSERT_EQ(world.aliveEnemyCount(), 3U);
    ASSERT_EQ(world.highRiskPressureWaveCount(), 1U);
    const Vec2 playerCenter{
        world.player().position().x + world.player().size() * 0.5F,
        world.player().position().y + world.player().size() * 0.5F};
    std::vector<CombatTargetId> ids;
    for (const Enemy &enemy : world.enemies())
    {
        EXPECT_NE(enemy.combatTargetId(), kInvalidCombatTargetId);
        if (enemy.combatTargetId() > 1U)
        {
            const Vec2 center{
                enemy.position().x + enemy.size().x * 0.5F,
                enemy.position().y + enemy.size().y * 0.5F};
            EXPECT_GT(
                std::hypot(
                    center.x - playerCenter.x,
                    center.y - playerCenter.y),
                200.0F);
        }
        ids.push_back(enemy.combatTargetId());
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}

TEST(GameplayWorldRaidTest, HighRiskEmergencySignalExtractionCompletes)
{
    RaidWorldConfig config = makeHighRiskWorldConfig();
    config.highRisk.initialWaveDelaySeconds = 100.0F;
    GameplayWorld world{std::move(config)};

    world.update(GameplayInput{}, 0.11F);
    ASSERT_EQ(world.raidSession().phase(), RaidPhase::HighRisk);
    ASSERT_TRUE(world.raidSession().emergencyExtractionOpen());
    ASSERT_FALSE(world.raidSession().normalExtractionOpen());

    world.update(GameplayInput{}, 0.0F);
    ASSERT_EQ(
        world.raidSession().extractionRoute(),
        RaidExtractionRoute::EmergencySignal);
    ASSERT_EQ(
        world.raidSession().state(),
        RaidSessionState::Extracting);

    world.update(GameplayInput{}, 0.50F);
    EXPECT_EQ(
        world.raidSession().state(),
        RaidSessionState::Extracted);
}

TEST(GameplayWorldRaidTest, ConditionalExtractionConsumesServiceEligibility)
{
    RaidWorldConfig config = makeHighRiskWorldConfig();
    config.highRisk.initialWaveDelaySeconds = 100.0F;
    config.playerSpawn = Vec2{840.0F, 80.0F};
    GameplayWorld world{std::move(config)};

    GameplayInput eligible;
    eligible.conditionalExtractionEligible = true;
    world.update(eligible, 0.11F);
    ASSERT_EQ(world.raidSession().phase(), RaidPhase::HighRisk);

    world.update(eligible, 0.10F);
    ASSERT_EQ(
        world.raidSession().extractionRoute(),
        RaidExtractionRoute::EmergencyConditional);
    ASSERT_EQ(world.raidSession().state(), RaidSessionState::Extracting);

    world.update(GameplayInput{}, 0.0F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(world.raidSession().extractionProgress(), 0.0F);

    world.update(eligible, 0.25F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::Extracted);
}
