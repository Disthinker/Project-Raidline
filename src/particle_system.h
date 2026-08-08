#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "particle.h"
#include "vec2.h"

struct ParticleBurstConfig
{
    std::size_t particleCount{16};
    float minSpeed{160.0f};
    float maxSpeed{340.0f};
    float minLifetime{0.10f};
    float maxLifetime{0.24f};
    float minSize{2.0f};
    float maxSize{4.5f};
};

class ParticleSystem
{
public:
    ParticleSystem(
        std::uint32_t seed,
        ParticleBurstConfig config);

    void emitImpact(Vec2 position);
    void update(float deltaTime);

    const std::vector<Particle> &particles() const;

private:
    ParticleBurstConfig config_;
    std::vector<Particle> particles_;
    std::mt19937 rng_;
};
