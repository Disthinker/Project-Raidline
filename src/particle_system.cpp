#include "particle_system.h"

#include <cmath>
#include <random>
#include <algorithm>
#include <stdexcept>

ParticleSystem::ParticleSystem(
    std::uint32_t seed,
    ParticleBurstConfig config)
    : config_(config),
      rng_(seed)
{
    if (config_.particleCount == 0)
    {
        throw std::invalid_argument("Particle count must be greater than zero");
    }

    if (config_.minSpeed < 0.0f || config_.maxSpeed < config_.minSpeed)
    {
        throw std::invalid_argument("Particle speed range is invalid");
    }

    if (config_.minLifetime <= 0.0f ||
        config_.maxLifetime < config_.minLifetime)
    {
        throw std::invalid_argument("Particle lifetime range is invalid");
    }

    if (config_.minSize <= 0.0f || config_.maxSize < config_.minSize)
    {
        throw std::invalid_argument("Particle size range is invalid");
    }
}

const std::vector<Particle> &ParticleSystem::particles() const
{
    return particles_;
}

void ParticleSystem::emitImpact(Vec2 position)
{
    constexpr float kTwoPi{6.28318530718f};
    std::uniform_real_distribution<float> angleDistribution{
        0.0f,
        kTwoPi};
    std::uniform_real_distribution<float> speedDistribution{
        config_.minSpeed,
        config_.maxSpeed};
    std::uniform_real_distribution<float> lifetimeDistribution{
        config_.minLifetime,
        config_.maxLifetime};
    std::uniform_real_distribution<float> sizeDistribution{
        config_.minSize,
        config_.maxSize};

    for (std::size_t i{0}; i < config_.particleCount; ++i)
    {
        const float angle = angleDistribution(rng_);
        const float speed = speedDistribution(rng_);
        const float lifetime = lifetimeDistribution(rng_);
        const float size = sizeDistribution(rng_);

        const Vec2 velocity{
            std::cos(angle) * speed,
            std::sin(angle) * speed};

        particles_.emplace_back(position, velocity, lifetime, size);
    }
}

void ParticleSystem::update(float deltaTime)
{
    for (auto &particle : particles_)
    {
        particle.update(deltaTime);
    }

    std::erase_if(
        particles_,
        [](const Particle &particle)
        {
            return particle.isExpired();
        });
}