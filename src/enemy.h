#pragma once

#include <cstddef>

#include "animation.h"
#include "rect.h"
#include "vec2.h"
#include "health_system.h"

enum class EnemyFacingDirection
{
    Left,
    Right
};

class Enemy
{
public:
    Enemy(
        Vec2 position,
        Vec2 size,
        Vec2 velocity = Vec2{},
        int maxHealth = 1);

    Vec2 position() const;
    Vec2 size() const;
    Vec2 velocity() const;
    Rect bounds() const;

    void update(float deltaTime, float worldWidth);

    EnemyFacingDirection facingDirection() const;
    bool isMoving() const;
    std::size_t currentAnimationFrameIndex() const;

    [[nodiscard]] bool takeDamage(int damage);

    int health() const noexcept;
    int maxHealth() const noexcept;
    bool isDead() const noexcept;

private:
    Vec2 position_;
    Vec2 size_;
    Vec2 velocity_;

    EnemyFacingDirection facingDirection_;
    Animator movementAnimator_;
    Health health_;
};
