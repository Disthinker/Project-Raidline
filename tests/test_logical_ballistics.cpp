#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "logical_ballistics.h"
#include "world_shooting_runtime.h"

namespace
{
    ShotResolution acceptedShot()
    {
        return resolveShotCommand(
            ShotCommand{
                17,
                Vec2{100.0F, 200.0F},
                Vec2{3.0F, 4.0F},
                100.0F,
                8.0F,
                2,
                250.0F,
                ShotAimIntent{44, HitRegion::Head, false},
                6});
    }
}

TEST(LogicalBallisticsTest, StoresFrozenAcceptedShot)
{
    const LogicalBallisticFlight flight{
        acceptedShot(), TracerStyle::Weak, 24.0F, 0.30F};

    EXPECT_EQ(flight.shotId(), 17U);
    EXPECT_FLOAT_EQ(flight.origin().x, 100.0F);
    EXPECT_FLOAT_EQ(flight.origin().y, 200.0F);
    EXPECT_FLOAT_EQ(flight.currentPosition().x, flight.origin().x);
    EXPECT_FLOAT_EQ(flight.currentPosition().y, flight.origin().y);
    EXPECT_FLOAT_EQ(flight.direction().x, 0.6F);
    EXPECT_FLOAT_EQ(flight.direction().y, 0.8F);
    EXPECT_FLOAT_EQ(flight.speed(), 100.0F);
    EXPECT_FLOAT_EQ(flight.collisionExtent(), 8.0F);
    EXPECT_FLOAT_EQ(flight.maximumDistance(), 250.0F);
    EXPECT_EQ(flight.damage(), 2);
    EXPECT_EQ(flight.penetration(), 6);
    ASSERT_TRUE(flight.aimIntent().has_value());
    EXPECT_EQ(flight.aimIntent()->targetId, 44U);
    EXPECT_EQ(flight.aimIntent()->region, HitRegion::Head);
    EXPECT_EQ(flight.tracerStyle(), TracerStyle::Weak);
    EXPECT_FLOAT_EQ(flight.tracerLength(), 24.0F);
    EXPECT_FLOAT_EQ(flight.tracerOpacity(), 0.30F);
    EXPECT_FLOAT_EQ(flight.tracerLifetimeSeconds(), 0.055F);
    EXPECT_FALSE(flight.reachedImpact());
}

TEST(LogicalBallisticsTest, AdvanceReturnsTravelledSegment)
{
    LogicalBallisticFlight flight{acceptedShot()};

    const LogicalBallisticAdvance advance = flight.advance(0.5F);

    EXPECT_FLOAT_EQ(advance.start.x, 100.0F);
    EXPECT_FLOAT_EQ(advance.start.y, 200.0F);
    EXPECT_FLOAT_EQ(advance.end.x, 130.0F);
    EXPECT_FLOAT_EQ(advance.end.y, 240.0F);
    EXPECT_FLOAT_EQ(flight.distanceTravelled(), 50.0F);
    EXPECT_FALSE(advance.reachedImpact);
}

TEST(LogicalBallisticsTest, AdvanceClampsToFrozenImpact)
{
    LogicalBallisticFlight flight{acceptedShot()};

    const LogicalBallisticAdvance advance = flight.advance(10.0F);

    EXPECT_TRUE(advance.reachedImpact);
    EXPECT_TRUE(flight.reachedImpact());
    EXPECT_FLOAT_EQ(
        flight.currentPosition().x,
        flight.impactPosition().x);
    EXPECT_FLOAT_EQ(
        flight.currentPosition().y,
        flight.impactPosition().y);
    EXPECT_FLOAT_EQ(flight.distanceTravelled(), 250.0F);
    EXPECT_FLOAT_EQ(flight.impactPosition().x, 250.0F);
    EXPECT_FLOAT_EQ(flight.impactPosition().y, 400.0F);
}

TEST(LogicalBallisticsTest, FramePartitionsProduceSamePosition)
{
    LogicalBallisticFlight whole{acceptedShot()};
    LogicalBallisticFlight split{acceptedShot()};

    static_cast<void>(whole.advance(1.5F));
    static_cast<void>(split.advance(0.25F));
    static_cast<void>(split.advance(0.50F));
    static_cast<void>(split.advance(0.75F));

    EXPECT_FLOAT_EQ(
        whole.currentPosition().x,
        split.currentPosition().x);
    EXPECT_FLOAT_EQ(
        whole.currentPosition().y,
        split.currentPosition().y);
    EXPECT_FLOAT_EQ(whole.distanceTravelled(), split.distanceTravelled());
}

TEST(LogicalBallisticsTest, NonPositiveDeltaDoesNotAdvance)
{
    LogicalBallisticFlight flight{acceptedShot()};

    const LogicalBallisticAdvance zero = flight.advance(0.0F);
    const LogicalBallisticAdvance negative = flight.advance(-1.0F);

    EXPECT_FLOAT_EQ(zero.start.x, zero.end.x);
    EXPECT_FLOAT_EQ(zero.start.y, zero.end.y);
    EXPECT_FLOAT_EQ(negative.start.x, negative.end.x);
    EXPECT_FLOAT_EQ(negative.start.y, negative.end.y);
    EXPECT_FLOAT_EQ(flight.distanceTravelled(), 0.0F);
}

TEST(LogicalBallisticsTest, NonFiniteDeltaDoesNotAdvance)
{
    LogicalBallisticFlight flight{acceptedShot()};

    static_cast<void>(flight.advance(
        std::numeric_limits<float>::infinity()));
    static_cast<void>(flight.advance(
        std::numeric_limits<float>::quiet_NaN()));

    EXPECT_FLOAT_EQ(flight.currentPosition().x, flight.origin().x);
    EXPECT_FLOAT_EQ(flight.currentPosition().y, flight.origin().y);
    EXPECT_FLOAT_EQ(flight.distanceTravelled(), 0.0F);
}

TEST(LogicalBallisticsTest, CompletedFlightRemainsAtImpact)
{
    LogicalBallisticFlight flight{acceptedShot()};
    static_cast<void>(flight.advance(10.0F));

    const LogicalBallisticAdvance later = flight.advance(1.0F);

    EXPECT_FLOAT_EQ(later.start.x, flight.impactPosition().x);
    EXPECT_FLOAT_EQ(later.start.y, flight.impactPosition().y);
    EXPECT_FLOAT_EQ(later.end.x, flight.impactPosition().x);
    EXPECT_FLOAT_EQ(later.end.y, flight.impactPosition().y);
    EXPECT_TRUE(later.reachedImpact);
}

TEST(LogicalBallisticsTest, RejectsUnacceptedResolution)
{
    EXPECT_THROW(
        static_cast<void>(LogicalBallisticFlight{ShotResolution{}}),
        std::invalid_argument);
}

TEST(LogicalBallisticsTest, MixedAmmunitionFlightsKeepFireTimeEffectAcrossCheckpoint)
{
    WeaponUseDefinition weapon = *itemDefinition(ItemId::Rifle).weaponUse;
    weapon.shotIntervalSeconds = 0.1F;
    weapon.baseDamage = 5;
    weapon.effectiveRange = 180.0F;
    weapon.maximumRange = 400.0F;
    weapon.logicalBallisticSpeed = 100.0F;
    WorldShootingRuntime runtime;
    runtime.configureWeapon(weapon);

    constexpr Vec2 shooter{100.0F, 100.0F};
    constexpr Vec2 world{500.0F, 500.0F};
    EnemyRoster<> targets;
    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.aimWorldPosition = Vec2{240.0F, 100.0F};
    runtime.updateAim(fire, shooter, {1.0F, 0.0F}, world, 0.0F);

    runtime.configureAmmunition(2, 1);
    static_cast<void>(runtime.advanceShots(fire, 0.0F, shooter, 20.0F, false, false, world, targets, {}));
    ASSERT_EQ(runtime.logicalBallistics().size(), 1U);

    weapon.effectiveRange = 90.0F;
    runtime.configureWeapon(weapon);
    runtime.configureAmmunition(7);
    static_cast<void>(runtime.advanceShots(fire, 0.2F, shooter, 20.0F, false, false, world, targets, {}));
    ASSERT_EQ(runtime.logicalBallistics().size(), 2U);
    EXPECT_EQ(runtime.logicalBallistics()[0].penetration(), 2);
    EXPECT_EQ(runtime.logicalBallistics()[0].unarmoredDamageBonus(), 1);
    EXPECT_FLOAT_EQ(runtime.logicalBallistics()[0].unarmoredDamageRange(), 180.0F);
    EXPECT_EQ(runtime.logicalBallistics()[1].penetration(), 7);
    EXPECT_EQ(runtime.logicalBallistics()[1].unarmoredDamageBonus(), 0);
    EXPECT_FLOAT_EQ(runtime.logicalBallistics()[1].unarmoredDamageRange(), 0.0F);

    runtime.configureAmmunition(2, 1);
    const WorldShootingCheckpoint saved = runtime.checkpoint();
    EXPECT_EQ(saved.weaponUnarmoredDamageBonus, 1);
    WorldShootingRuntime restored;
    ASSERT_TRUE(restored.restoreCheckpoint(saved));
    EXPECT_EQ(restored.checkpoint(), saved);

    WorldShootingCheckpoint invalid = saved;
    invalid.flights.front().unarmoredDamageRange = 0.0F;
    EXPECT_FALSE(restored.restoreCheckpoint(invalid));
    invalid = saved;
    invalid.weaponUnarmoredDamageBonus = 2;
    EXPECT_FALSE(restored.restoreCheckpoint(invalid));
    EXPECT_EQ(restored.checkpoint(), saved);
}
