#include <cmath>
#include "projectile.h"

Projectile::Projectile(Vec2 position, Vec2 velocity, float width, float height):
    position_(position),
    velocity_(velocity),
    width_(width),
    height_(height)
{

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

bool Projectile::isOutside(float worldWidth, float worldHeight) const
{
    return (position_.x + width_ < 0.0f || position_.x > worldWidth) || 
           (position_.y + height_ < 0.0f || position_.y > worldHeight);
}
