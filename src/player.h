#pragma once

#include "gameplay_input.h"
#include "vec2.h"

class Player
{
public:
    Player(float x, float y);

    void update(const GameplayInput &input, float deltaTime, float worldWidth, float worldHeight);

    Vec2 position() const;
    float size() const;

private:
    Vec2 position_;
    float speed_{240.0f};
    float size_{32.0f};
};