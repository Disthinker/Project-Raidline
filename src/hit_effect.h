#pragma once

#include "vec2.h"

class HitEffect
{
public:
    HitEffect(Vec2 position, float lifetimeRemaining, float size);
    Vec2 position() const;
    float lifetimeRemaining() const;
    float size() const;
    void update(float deltaTime);
    bool isExpired() const;

private:
    Vec2 position_{};
    float lifetimeRemaining_{};
    float size_{};
};
