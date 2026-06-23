#include "enemy.h"

Enemy::Enemy(Vec2 position, Vec2 size)
    : position_(position),
      size_(size)
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

Rect Enemy::bounds() const
{
  return Rect{position_, size_};
}