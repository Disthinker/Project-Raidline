#include <cmath>
#include "enemy.h"

Enemy::Enemy(Vec2 position, Vec2 size, Vec2 velocity)
    : position_(position),
      size_(size),
      velocity_(velocity)

{
}

Vec2 Enemy::position() const
{
  return position_;
}

Vec2 Enemy::size() const
{
  return size_;
}

Vec2 Enemy::velocity() const
{
  return velocity_;
}

Rect Enemy::bounds() const
{
  return Rect{position_, size_};
}

void Enemy::update(float deltaTime, float worldWidth)
{
  // Update position based on velocity and time delta
  position_.x += velocity_.x * deltaTime;

  if (position_.x + size_.x > worldWidth)
  {
    position_.x = worldWidth - size_.x;
    // Reverse the direction of movement
    velocity_.x = -std::abs(velocity_.x);
  }
  if (position_.x < 0)
  {
    position_.x = 0;
    // Reverse the direction of movement
    velocity_.x = std::abs(velocity_.x);
  }
}