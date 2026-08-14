#pragma once
#include "rect.h"
#include "shot_resolution.h"
#include "vec2.h"

class Projectile
{
public:
    Projectile(
        Vec2 position,
        Vec2 velocity,
        float width,
        float height,
        int damage = 1,
        ShotId shotId = kInvalidShotId);

    void update(float deltaTime);

    Vec2 position() const;
    Vec2 velocity() const noexcept;
    float width() const;
    float height() const;
    Rect bounds() const;

    bool isOutside(float worldWidth, float worldHeight) const;

    int damage() const noexcept;

    ShotId shotId() const noexcept;

private:
    Vec2 position_;
    Vec2 velocity_;
    float width_;
    float height_;
    int damage_;
    ShotId shotId_;
};
