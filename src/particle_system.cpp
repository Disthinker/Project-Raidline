#include "particle_system.h"

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