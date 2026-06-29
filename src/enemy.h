#pragma once

#include "vec2.h"
#include "rect.h"

class Enemy
{
public:
    Enemy(Vec2 position, Vec2 size, Vec2 velocity);

    Vec2 position() const;
    Vec2 size() const;
    Vec2 velocity() const;
    Rect bounds() const;

    void update(float deltaTime, float worldWidth);

private:
    Vec2 position_;
    Vec2 size_;
    Vec2 velocity_;
};
