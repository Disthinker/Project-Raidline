#include <gtest/gtest.h>

#include <cmath>

#include "particle_system.h"

namespace
{
    constexpr float kEpsilon{0.0001f};

    ParticleBurstConfig makeTestConfig()
    {
        ParticleBurstConfig config{};
        config.particleCount = 4;
        config.minSpeed = 10.0f;
        config.maxSpeed = 20.0f;
        config.minLifetime = 0.20f;
        config.maxLifetime = 0.40f;
        config.minSize = 2.0f;
        config.maxSize = 5.0f;
        return config;
    }

    float velocityLength(Vec2 velocity)
    {
        return std::sqrt(
            velocity.x * velocity.x +
            velocity.y * velocity.y);
    }
}

TEST(ParticleSystemTest, StartsEmpty)
{
    ParticleSystem system{123u, makeTestConfig()};

    EXPECT_TRUE(system.particles().empty());
}

TEST(ParticleSystemTest, EmitCreatesConfiguredParticlesWithinRanges)
{
    ParticleBurstConfig config = makeTestConfig();
    ParticleSystem system{123u, config};

    const Vec2 impactPosition{50.0f, 60.0f};
    system.emitImpact(impactPosition);

    ASSERT_EQ(system.particles().size(), config.particleCount);

    for (const Particle &particle : system.particles())
    {
        EXPECT_FLOAT_EQ(particle.position().x, impactPosition.x);
        EXPECT_FLOAT_EQ(particle.position().y, impactPosition.y);

        const float speed = velocityLength(particle.velocity());
        EXPECT_GE(speed + kEpsilon, config.minSpeed);
        EXPECT_LE(speed - kEpsilon, config.maxSpeed);

        EXPECT_GE(particle.duration(), config.minLifetime);
        EXPECT_LE(particle.duration(), config.maxLifetime);
        EXPECT_FLOAT_EQ(particle.remainingLifetime(), particle.duration());

        EXPECT_GE(particle.size(), config.minSize);
        EXPECT_LE(particle.size(), config.maxSize);
    }
}

TEST(ParticleSystemTest, UpdateRemovesExpiredParticles)
{
    ParticleBurstConfig config{};
    config.particleCount = 3;
    config.minSpeed = 0.0f;
    config.maxSpeed = 0.0f;
    config.minLifetime = 0.10f;
    config.maxLifetime = 0.10f;
    config.minSize = 2.0f;
    config.maxSize = 2.0f;

    ParticleSystem system{123u, config};
    system.emitImpact(Vec2{0.0f, 0.0f});

    ASSERT_EQ(system.particles().size(), 3u);

    system.update(0.11f);

    EXPECT_TRUE(system.particles().empty());
}