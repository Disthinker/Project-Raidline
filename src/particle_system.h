#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "particle.h"
#include "vec2.h"

struct ParticleBurstConfig
{
    std::size_t particleCount{12};
    float minSpeed{100.0f};
    float maxSpeed{220.0f};
    float minLifetime{0.18f};
    float maxLifetime{0.38f};
    float minSize{3.0f};
    float maxSize{7.0f};
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