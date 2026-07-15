#pragma once

#include "gameplay_input.h"
#include "vec2.h"
#include "animation.h"
#include <cstddef>

class Player
{
public:
    Player(float x, float y);

    void update(const GameplayInput &input, float deltaTime, float worldWidth, float worldHeight);

    Vec2 position() const;
    float size() const;
    Vec2 facingDirection() const;
    bool isMoving() const;
    std::size_t currentAnimationFrameIndex() const;

private:
    Vec2 position_;
    float speed_{240.0f};
    float size_{32.0f};
    Vec2 facingDirection_{0.0f, -1.0f};

    bool isMoving_{false};
    Animator movementAnimator_;
};