#include <gtest/gtest.h>

#include <stdexcept>

#include "health_system.h"

TEST(HealthTest, ValidConstruction)
{
    const Health health{100};

    EXPECT_EQ(health.maximum(), 100);
    EXPECT_EQ(health.current(), 100);
    EXPECT_FALSE(health.isDead());
}

TEST(HealthTest, RejectsNonPositiveMaximum)
{
    EXPECT_THROW(
        static_cast<void>(Health{0}),
        std::invalid_argument);

    EXPECT_THROW(
        static_cast<void>(Health{-1}),
        std::invalid_argument);
}

TEST(HealthTest, NonLethalDamageReducesCurrentHealth)
{
    Health health{100};

    const bool killed = health.takeDamage(30);

    EXPECT_FALSE(killed);
    EXPECT_EQ(health.current(), 70);
    EXPECT_FALSE(health.isDead());
}

TEST(HealthTest, LethalDamageClampsToZero)
{
    Health health{50};

    const bool killed = health.takeDamage(50);

    EXPECT_TRUE(killed);
    EXPECT_EQ(health.current(), 0);
    EXPECT_TRUE(health.isDead());
}

TEST(HealthTest, OverkillClampsToZero)
{
    Health health{50};

    const bool killed = health.takeDamage(100);

    EXPECT_TRUE(killed);
    EXPECT_EQ(health.current(), 0);
    EXPECT_TRUE(health.isDead());
}

TEST(HealthTest, DamageAfterDeathDoesNotReportAnotherDeath)
{
    Health health{50};

    EXPECT_TRUE(health.takeDamage(50));
    EXPECT_FALSE(health.takeDamage(10));

    EXPECT_EQ(health.current(), 0);
    EXPECT_TRUE(health.isDead());
}

TEST(HealthTest, RejectsNonPositiveDamage)
{
    Health health{100};

    EXPECT_THROW(
        static_cast<void>(health.takeDamage(0)),
        std::invalid_argument);

    EXPECT_THROW(
        static_cast<void>(health.takeDamage(-20)),
        std::invalid_argument);

    EXPECT_EQ(health.current(), 100);
}