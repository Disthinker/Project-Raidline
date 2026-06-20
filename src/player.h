#pragma once

#include "input_system.h"

struct Vec2{
    float x {};
    float y {};
};

class Player{
public:
    Player(float x, float y);

    void update(const InputSystem& input, float deltaTime, float worldWidth, float worldHeight);

    Vec2 position() const;
    float size() const;
private:
    Vec2 position_;
    float speed_ {240.0f};
    float size_ {32.0f};

};