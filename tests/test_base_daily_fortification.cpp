#include "base_fortification_placement.h"
#include "alpha_content_ids.h"
#include "shot_resolution.h"
#include <gtest/gtest.h>

struct EnemyLifecycleTestAccess
{
    static void place(BaseWorld &w, Vec2 p) { w.playerPosition_ = p; }
    static const auto &blockers(const BaseWorld &w) { return w.dailyShotBlockers_; }
    static void enemy(BaseWorld &w, Vec2 p)
    {
        w.perimeterEnemies_.spawn(Enemy{p, {40, 52}, {}, 100, 91}, p);
    }
};

namespace
{
struct DailyWood : testing::Test
{
    BaseWorld world;
    BaseFortificationState owned;
    Rect rect;
    const ContentRegistry &content = publishedContentRegistry();
    void SetUp() override
    {
        const auto *def = content.findFortification(kWoodBarricadeDefinition);
        ASSERT_NE(def, nullptr);
        const auto positions = baseDefensePositionCandidates(world.layout(), "", *def);
        for (const auto &p : positions)
            if (p.available)
            {
                owned.nextInstanceId = 2;
                owned.instances.emplace(FortificationInstanceId{1},
                    FortificationRecord{kWoodBarricadeDefinition, 120, p.key});
                rect = {p.footprint.position, p.footprint.size};
                break;
            }
        ASSERT_FALSE(owned.instances.empty());
        ASSERT_TRUE(world.configureFortifications(owned, content));
    }
};
}

TEST_F(DailyWood, BlocksBothPlayerAxesAndBrokenRemnantOpensPassage)
{
    const Vec2 left{rect.position.x - 42, rect.position.y + rect.size.y / 2 - 26};
    EnemyLifecycleTestAccess::place(world, left);
    BaseInput move;
    move.moveRight = true;
    for (int i = 0; i < 10; ++i) static_cast<void>(world.update(move, 0.1F));
    EXPECT_LE(world.playerPosition().x + 40, rect.position.x + 0.01F);
    EnemyLifecycleTestAccess::place(world, {rect.position.x + rect.size.x / 2 - 20, rect.position.y - 54});
    move = {}; move.moveDown = true;
    for (int i = 0; i < 10; ++i) static_cast<void>(world.update(move, 0.1F));
    EXPECT_LE(world.playerPosition().y + 52, rect.position.y + 0.01F);
    owned.instances.at({1}).durability = 0;
    ASSERT_TRUE(world.configureFortifications(owned, content));
    for (int i = 0; i < 3; ++i) static_cast<void>(world.update(move, 0.1F));
    EXPECT_GT(world.playerPosition().y + 52, rect.position.y + 1);
    ASSERT_EQ(world.fortifications().size(), 1U);
    EXPECT_EQ(world.fortifications()[0].durability, 0U);
}

TEST_F(DailyWood, ShotGeometryTracksDamageRepairAndStorageWithoutPlayerDamageToWood)
{
    EnemyRoster<> targets;
    const Vec2 from{rect.position.x - 60, rect.position.y + rect.size.y / 2};
    const Vec2 to{rect.position.x + rect.size.x + 60, from.y};
    const std::vector<ShotCollisionCandidate> shots{{1, from, to, 1, 12}};
    const auto hit = [&] { return resolveShotHits(shots, targets, EnemyLifecycleTestAccess::blockers(world)); };
    ASSERT_EQ(hit().hits.size(), 1U);
    EXPECT_EQ(hit().hits[0].targetKind, HitTargetKind::Obstacle);
    EXPECT_EQ(world.fortifications()[0].durability, 120U);
    owned.instances.at({1}).durability = 0;
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_TRUE(hit().hits.empty());
    owned.instances.at({1}).durability = 30;
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_EQ(hit().hits.size(), 1U);
    owned.instances.at({1}).slot.reset();
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_TRUE(hit().hits.empty());
    EXPECT_TRUE(world.fortifications().empty());
    EXPECT_EQ(owned.instances.at({1}).durability, 30U);
}

TEST_F(DailyWood, RepeatedProjectionDoesNotRebuildGeometryOrEnemies)
{
    const auto *geometry = EnemyLifecycleTestAccess::blockers(world).data();
    const auto *projection = world.fortifications().data();
    EnemyLifecycleTestAccess::enemy(world, {rect.position.x - 160, rect.position.y - 100});
    const auto before = world.perimeterEnemySnapshots();
    for (int i = 0; i < 1000; ++i) ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_EQ(geometry, EnemyLifecycleTestAccess::blockers(world).data());
    EXPECT_EQ(projection, world.fortifications().data());
    ASSERT_EQ(world.perimeterEnemies().size(), 1U);
    EXPECT_EQ(world.perimeterEnemies()[0].combatTargetId(), 91U);
    EXPECT_EQ(world.perimeterEnemySnapshots()[0].position.x, before[0].position.x);
    EXPECT_EQ(world.perimeterEnemySnapshots()[0].position.y, before[0].position.y);
    owned.instances.at({1}).durability = 0;
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_EQ(world.perimeterEnemies()[0].combatTargetId(), 91U);
    EXPECT_EQ(world.perimeterEnemySnapshots()[0].position.x, before[0].position.x);
}

TEST_F(DailyWood, InvalidProjectionIsAtomicAndProfileLoadClearsOldWood)
{
    auto invalid = owned;
    invalid.instances.at({1}).durability = 121;
    EXPECT_FALSE(world.configureFortifications(invalid, content));
    EXPECT_EQ(world.fortifications()[0].durability, 120U);
    invalid = owned;
    invalid.instances.at({1}).slot->plot = "unknown";
    EXPECT_FALSE(world.configureFortifications(invalid, content));
    EXPECT_EQ(world.fortifications().size(), 1U);
    world.resetCombatForProfileLoad();
    EXPECT_TRUE(world.fortifications().empty());
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_EQ(world.fortifications().size(), 1U);
}

TEST_F(DailyWood, DefenseCheckoutOverridesDailyUntilCommittedOwnerReturns)
{
    BaseDefenseSnapshot seed;
    seed.eventId = "daily-wood-transition";
    seed.siegeSequence = 1;
    seed.seed = 12345;
    seed.frozenMoraleTier = 1;
    seed.rulesVersion = kFortifiedBaseDefenseRulesVersion;
    seed.fortifications.assign(world.fortifications().begin(), world.fortifications().end());
    auto frozen = world.prepareBaseDefenseSnapshot(seed,
        content.enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    ASSERT_TRUE(frozen);
    ASSERT_TRUE(world.resumeBaseDefense(*frozen));
    owned.instances.at({1}).durability = 0;
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_EQ(world.fortifications()[0].durability, 120U);
    world.clearBaseDefense();
    ASSERT_TRUE(world.configureFortifications(owned, content));
    EXPECT_EQ(world.fortifications()[0].durability, 0U);
}
