#include "raid_grenade_runtime.h"
#include "grenade_damage.h"
#include "combat_damage_domain.h"

#include <gtest/gtest.h>
#include <limits>
#include <utility>

namespace
{
const RaidSpaceDefinitionId space{"space.test.grenade"};
const GrenadeUseDefinition use;
GrenadeLanding landing() { return {{100, 250}, {550, 250}, space, use}; }
RaidSpaceBlockerIndex geometry(std::vector<BallisticBlocker> blockers = {})
{
    return *RaidSpaceBlockerIndex::build({2000, 1200}, blockers);
}
}

TEST(GrenadeRuntimeTest, LandingUsesMaximumRangeWorldEdgeAndTheFirstSolidWall)
{
    auto walls = geometry();
    auto plan = queryGrenadeLanding({100, 250}, {1800, 250}, space, use, walls);
    ASSERT_TRUE(plan);
    EXPECT_FLOAT_EQ(plan->point.x, 550.0F);
    EXPECT_FLOAT_EQ(plan->point.y, 250.0F);
    EXPECT_EQ(plan->space, space);
    EXPECT_EQ(plan->use, use);
    plan = queryGrenadeLanding({1900, 250}, {2200, 250}, space, use, walls);
    ASSERT_TRUE(plan);
    EXPECT_FLOAT_EQ(plan->point.x, 1996.0F);
    walls = geometry({{1, {{300, 0}, {20, 1000}}}, {2, {{500, 0}, {20, 1000}}}});
    plan = queryGrenadeLanding({100, 250}, {800, 250}, space, use, walls);
    ASSERT_TRUE(plan);
    EXPECT_LT(plan->point.x, 300.0F);
    EXPECT_GT(plan->point.x, 292.0F);
    EXPECT_TRUE(walls.hasLineOfSight(plan->origin, plan->point));
}

TEST(GrenadeRuntimeTest, InvalidGeometryAimSpaceAndCapabilityFailBeforeAFlightExists)
{
    const auto walls = geometry({{1, {{90, 200}, {40, 100}}}});
    EXPECT_FALSE(queryGrenadeLanding({100, 250}, {800, 250}, space, use, walls));
    EXPECT_FALSE(queryGrenadeLanding({100, 250}, {100, 250}, space, use, geometry()));
    EXPECT_FALSE(queryGrenadeLanding({-1, 250}, {500, 250}, space, use, geometry()));
    EXPECT_FALSE(queryGrenadeLanding({100, 250}, {std::numeric_limits<float>::quiet_NaN(), 250},
        space, use, geometry()));
    EXPECT_FALSE(queryGrenadeLanding({100, 250}, {500, 250}, {}, use, geometry()));
    auto invalid = use;
    invalid.radius = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(queryGrenadeLanding({100, 250}, {500, 250}, space, invalid, geometry()));
    auto payload = landing();
    payload.use = invalid;
    RaidGrenadeRuntime runtime;
    EXPECT_FALSE(runtime.start(payload));
    EXPECT_FALSE(runtime.active());
}

TEST(GrenadeRuntimeTest, HalfSecondFlightStartsASeparateOneAndHalfSecondFuse)
{
    RaidGrenadeRuntime runtime;
    ASSERT_TRUE(runtime.start(landing()));
    EXPECT_TRUE(runtime.active());
    EXPECT_FALSE(runtime.start(landing()));
    runtime.advance(space, 0.49F);
    ASSERT_TRUE(runtime.flight());
    EXPECT_FALSE(runtime.fuse());
    EXPECT_FALSE(runtime.pendingExplosion());
    runtime.advance(space, 0.01F);
    ASSERT_TRUE(runtime.fuse());
    EXPECT_FALSE(runtime.flight());
    EXPECT_NEAR(runtime.fuse()->elapsed, 0.0, 0.000001);
    runtime.advance(space, 1.49F);
    EXPECT_FALSE(runtime.pendingExplosion());
    runtime.advance(space, 0.01F);
    ASSERT_TRUE(runtime.pendingExplosion());
    EXPECT_EQ(runtime.pendingExplosion()->landing.space, space);
    EXPECT_EQ(runtime.pendingExplosion()->landing.point.x, 550.0F);
    EXPECT_FALSE(runtime.fuse());
}

TEST(GrenadeRuntimeTest, AFrameCrossingLandingOnlySpendsItsRemainderOnTheFuse)
{
    RaidGrenadeRuntime runtime;
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(space, 0.75F);
    ASSERT_TRUE(runtime.fuse());
    EXPECT_DOUBLE_EQ(runtime.fuse()->elapsed, 0.25);
    runtime.advance(space, 1.25F);
    ASSERT_TRUE(runtime.pendingExplosion());
    EXPECT_TRUE(runtime.active());
}

TEST(GrenadeRuntimeTest, PendingExplosionIsStableUntilTheConsumerAcknowledgesItsCommit)
{
    RaidGrenadeRuntime runtime;
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(space, 100.0F);
    ASSERT_TRUE(runtime.pendingExplosion());
    const auto id = runtime.pendingExplosion()->id;
    EXPECT_NE(id, 0U);
    EXPECT_FALSE(runtime.start(landing()));
    for (unsigned frame{}; frame < 10; ++frame) runtime.advance(space, 100.0F);
    ASSERT_TRUE(runtime.pendingExplosion());
    EXPECT_EQ(runtime.pendingExplosion()->id, id);
    runtime.acknowledgeExplosion();
    EXPECT_FALSE(runtime.active());
    runtime.advance(space, 100.0F);
    EXPECT_FALSE(runtime.pendingExplosion());
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(space, 2.0F);
    ASSERT_TRUE(runtime.pendingExplosion());
    EXPECT_GT(runtime.pendingExplosion()->id, id);
}

TEST(GrenadeRuntimeTest, WrongSpaceAndInvalidTimeCannotMoveOrDetonateTheGrenade)
{
    RaidGrenadeRuntime runtime;
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(RaidSpaceDefinitionId{"space.test.other"}, 100.0F);
    for (float seconds : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
        runtime.advance(space, seconds);
    ASSERT_TRUE(runtime.flight());
    EXPECT_DOUBLE_EQ(runtime.flight()->elapsed, 0.0);
    runtime.advance(space, 0.5F);
    runtime.advance(RaidSpaceDefinitionId{"space.test.other"}, 100.0F);
    ASSERT_TRUE(runtime.fuse());
    EXPECT_DOUBLE_EQ(runtime.fuse()->elapsed, 0.0);
    EXPECT_FALSE(runtime.pendingExplosion());
}

TEST(GrenadeRuntimeTest, CleanupClearsAllStagesWithoutReusingAnExplosionIdentity)
{
    RaidGrenadeRuntime runtime;
    ASSERT_TRUE(runtime.start(landing()));
    runtime.clear();
    EXPECT_FALSE(runtime.active());
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(space, 0.5F);
    runtime.clear();
    EXPECT_FALSE(runtime.active());
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(space, 2.0F);
    const auto id = runtime.pendingExplosion()->id;
    runtime.clear();
    EXPECT_FALSE(runtime.active());
    ASSERT_TRUE(runtime.start(landing()));
    runtime.advance(space, 2.0F);
    ASSERT_TRUE(runtime.pendingExplosion());
    EXPECT_GT(runtime.pendingExplosion()->id, id);
}

TEST(GrenadeDamageTest, CoreAndEdgeBoundariesUseTheApprovedCeilingFalloff)
{
    const std::pair<float, int> cases[]{
        {0.0F, 24}, {60.0F, 24}, {60.001F, 24}, {80.0F, 20},
        {100.0F, 16}, {140.0F, 8}, {159.999F, 5}, {160.0F, 4}, {160.001F, 0}};
    for (const auto &[distance, expected] : cases)
    {
        SCOPED_TRACE(distance);
        EXPECT_EQ(grenadeDamageAtDistance(distance, use), expected);
    }
}

TEST(GrenadeDamageTest, InvalidDistanceOrDefinitionCannotProduceDamage)
{
    for (float distance : {-1.0F, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()})
        EXPECT_EQ(grenadeDamageAtDistance(distance, use), 0);
    auto invalid = use;
    invalid.coreRadius = invalid.radius;
    EXPECT_EQ(grenadeDamageAtDistance(60, invalid), 0);
    invalid = use;
    invalid.edgeDamage = invalid.coreDamage + 1;
    EXPECT_EQ(grenadeDamageAtDistance(60, invalid), 0);
}

TEST(GrenadeDamageTest, TheSameTorsoDamageFeedsExistingArmorReductionWithoutAHeadshot)
{
    const int damage = grenadeDamageAtDistance(0.0F, use);
    const auto unarmored = resolveCombatDamage({damage, HitRegion::Torso,
        use.penetration, use.armorDamage, false, std::nullopt});
    ASSERT_TRUE(unarmored.resolved());
    EXPECT_EQ(unarmored.damageApplied, 24);
    EXPECT_EQ(unarmored.semantic, HitSemantic::Normal);
    const auto protectedTarget = resolveCombatDamage({damage, HitRegion::Torso,
        use.penetration, use.armorDamage, false,
        ArmorProtectionView{HitRegion::Torso, 7, 9, 10000}});
    ASSERT_TRUE(protectedTarget.resolved());
    EXPECT_EQ(protectedTarget.damageApplied, 11);
    EXPECT_EQ(protectedTarget.armorDurabilityLoss, 6U);
    EXPECT_EQ(protectedTarget.semantic, HitSemantic::Normal);
}
