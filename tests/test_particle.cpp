#include <gtest/gtest.h>

#include <stdexcept>

#include "particle.h"

TEST(ParticleTest, InitialStateStoresFields)
{
    Particle particle{
        Vec2{10.0f, 20.0f},
        Vec2{30.0f, -40.0f},
        0.5f,
        6.0f};

    EXPECT_FLOAT_EQ(particle.position().x, 10.0f);
    EXPECT_FLOAT_EQ(particle.position().y, 20.0f);
    EXPECT_FLOAT_EQ(particle.velocity().x, 30.0f);
    EXPECT_FLOAT_EQ(particle.velocity().y, -40.0f);
    EXPECT_FLOAT_EQ(particle.duration(), 0.5f);
    EXPECT_FLOAT_EQ(particle.remainingLifetime(), 0.5f);
    EXPECT_FLOAT_EQ(particle.size(), 6.0f);
}

TEST(ParticleTest, UpdateMovesByVelocityTimesDeltaTime)
{
    Particle particle{
        Vec2{10.0f, 20.0f},
        Vec2{100.0f, -50.0f},
        1.0f,
        4.0f};

    particle.update(0.1f);

    EXPECT_FLOAT_EQ(particle.position().x, 20.0f);
    EXPECT_FLOAT_EQ(particle.position().y, 15.0f);
}

TEST(ParticleTest, UpdateReducesLifetime)
{
    Particle particle{
        Vec2{0.0f, 0.0f},
        Vec2{0.0f, 0.0f},
        1.0f,
        4.0f};

    particle.update(0.25f);

    EXPECT_FLOAT_EQ(particle.remainingLifetime(), 0.75f);
}

TEST(ParticleTest, NonPositiveDeltaTimeDoesNotAdvance)
{
    Particle particle{
        Vec2{10.0f, 20.0f},
        Vec2{100.0f, 100.0f},
        1.0f,
        4.0f};

    particle.update(0.0f);
    particle.update(-0.1f);

    EXPECT_FLOAT_EQ(particle.position().x, 10.0f);
    EXPECT_FLOAT_EQ(particle.position().y, 20.0f);
    EXPECT_FLOAT_EQ(particle.remainingLifetime(), 1.0f);
}

TEST(ParticleTest, LifetimeClampsAtZero)
{
    Particle particle{
        Vec2{0.0f, 0.0f},
        Vec2{0.0f, 0.0f},
        0.5f,
        4.0f};

    particle.update(1.0f);

    EXPECT_FLOAT_EQ(particle.remainingLifetime(), 0.0f);
    EXPECT_TRUE(particle.isExpired());
}

TEST(ParticleTest, NormalizedLifetimeStartsAtOne)
{
    Particle particle{
        Vec2{0.0f, 0.0f},
        Vec2{0.0f, 0.0f},
        0.5f,
        4.0f};

    EXPECT_FLOAT_EQ(particle.normalizedLifetime(), 1.0f);
}

TEST(ParticleTest, NormalizedLifetimeReachesZero)
{
    Particle particle{
        Vec2{0.0f, 0.0f},
        Vec2{0.0f, 0.0f},
        0.5f,
        4.0f};

    particle.update(0.5f);

    EXPECT_FLOAT_EQ(particle.normalizedLifetime(), 0.0f);
    EXPECT_TRUE(particle.isExpired());
}

TEST(ParticleTest, InvalidDurationRejected)
{
    EXPECT_THROW(
        Particle(Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f}, 0.0f, 4.0f),
        std::invalid_argument);
}

TEST(ParticleTest, InvalidSizeRejected)
{
    EXPECT_THROW(
        Particle(Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f}, 1.0f, 0.0f),
        std::invalid_argument);
}
