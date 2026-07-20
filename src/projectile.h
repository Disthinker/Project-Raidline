#pragma once
#include "vec2.h"
#include "rect.h"

class Projectile
{
public:
    Projectile(
        Vec2 position,
        Vec2 velocity,
        float width,
        float height,
        int damage = 1);

    void update(float deltaTime);

    Vec2 position() const;
    float width() const;
    float height() const;
    Rect bounds() const;

    bool isOutside(float worldWidth, float worldHeight) const;

    int damage() const noexcept;

private:
    Vec2 position_;
    Vec2 velocity_;
    float width_;
    float height_;
    int damage_;
};