#pragma once

#include "vec2.h"
#include "rect.h"

class Enemy
{
public:
    Enemy(Vec2 position, Vec2 size);

    Vec2 position() const;
    Vec2 size() const;
    Rect bounds() const;

private:
    Vec2 position_;
    Vec2 size_;
};
