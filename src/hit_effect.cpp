#include "hit_effect.h"

HitEffect::HitEffect(Vec2 position, float lifetimeRemaining, float size)
    : position_(position),
      lifetimeRemaining_(lifetimeRemaining),
      size_(size)
{
}

Vec2 HitEffect::position() const
{
    return position_;
}

float HitEffect::lifetimeRemaining() const
{
    return lifetimeRemaining_;
}

float HitEffect::size() const
{
    return size_;
}

void HitEffect::update(float deltaTime)
{
    // Decrease the lifetime remaining by the delta time
    lifetimeRemaining_ -= deltaTime;
}

bool HitEffect::isExpired() const
{
    // Check if the lifetime has expired
    return lifetimeRemaining_ <= 0.0f;
}