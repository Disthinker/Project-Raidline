#include "particle.h"

#include <stdexcept>

Particle::Particle(
    Vec2 position,
    Vec2 velocity,
    float duration,
    float size)
    : position_(position),
      velocity_(velocity),
      duration_(duration),
      remainingLifetime_(duration),
      size_(size)
{
    if (duration <= 0.0f)
    {
        throw std::invalid_argument("Particle duration must be greater than zero");
    }

    if (size <= 0.0f)
    {
        throw std::invalid_argument("Particle size must be greater than zero");
    }
}

Vec2 Particle::position() const
{
    return position_;
}

Vec2 Particle::velocity() const
{
    return velocity_;
}

float Particle::duration() const
{
    return duration_;
}

float Particle::remainingLifetime() const
{
    return remainingLifetime_;
}

float Particle::size() const
{
    return size_;
}