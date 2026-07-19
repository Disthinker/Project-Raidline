#pragma once

#include "vec2.h"

class Particle
{
public:
    Particle(
        Vec2 position,
        Vec2 velocity,
        float duration,
        float size);

    void update(float deltaTime);

    bool isExpired() const;
    float normalizedLifetime() const;

    Vec2 position() const;
    Vec2 velocity() const;
    float duration() const;
    float remainingLifetime() const;
    float size() const;

private:
    Vec2 position_{};
    Vec2 velocity_{};
    float duration_{};
    float remainingLifetime_{};
    float size_{};
};