#include <cmath>
#include "projectile.h"
#include <stdexcept>

Projectile::Projectile(Vec2 position, Vec2 velocity, float width, float height, int damage)
    : position_(position),
      velocity_(velocity),
      width_(width),
      height_(height),
      damage_{damage}
{
    if (damage <= 0)
    {
        throw std::invalid_argument(
            "Projectile damage must be greater than zero");
    }
}

void Projectile::update(float deltaTime)
{
    position_.x += velocity_.x * deltaTime;
    position_.y += velocity_.y * deltaTime;
}

Vec2 Projectile::position() const
{
    return position_;
}
float Projectile::width() const
{
    return width_;
}
float Projectile::height() const
{
    return height_;
}
Rect Projectile::bounds() const
{
    return Rect{position_, Vec2{width_, height_}};
}

bool Projectile::isOutside(float worldWidth, float worldHeight) const
{
    return (position_.x + width_ < 0.0f || position_.x > worldWidth) ||
           (position_.y + height_ < 0.0f || position_.y > worldHeight);
}

int Projectile::damage() const noexcept
{
    return damage_;
}