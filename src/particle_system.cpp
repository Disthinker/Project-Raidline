#include "particle_system.h"
#include <cmath>
#include <numbers>
#include <random>

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
    float angle{};
    float speed{};
    float lifetime{};
    float size{};
    for (int i{0}; i < config_.particleCount; i++)
    {
        angle = static_cast<float>(rand()) / RAND_MAX * 360.0f;
        speed = config_.minSpeed + static_cast<float>(rand()) / RAND_MAX *
                                       (config_.maxSpeed - config_.minSpeed);
        lifetime = config_.minLifetime + static_cast<float>(rand()) / RAND_MAX *
                                             (config_.maxLifetime - config_.minLifetime);
        size = config_.minSize + static_cast<float>(rand()) / RAND_MAX *
                                     (config_.maxSize - config_.minSize);
        particles_.emplace_back(Particle{
            position,
            Vec2{speed * cos(angle), speed * sin(angle)},
            lifetime,
            size});
    }
}