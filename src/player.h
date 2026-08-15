#pragma once

#include <cstddef>

#include "animation.h"
#include "gameplay_input.h"
#include "health_system.h"
#include "vec2.h"

class Player
{
public:
    Player(float x, float y, int maxHealth = 3);
    Player(float x, float y, int maxHealth, int currentHealth);

    void update(const GameplayInput &input, float deltaTime, float worldWidth, float worldHeight);

    Vec2 position() const;
    float size() const;
    Vec2 facingDirection() const;
    [[nodiscard]] bool faceDirection(Vec2 direction) noexcept;
    bool isMoving() const;
    std::size_t currentAnimationFrameIndex() const;

    [[nodiscard]] bool takeDamage(int damage);
    [[nodiscard]] int restoreHealth(int amount);

    [[nodiscard]] bool isImpactSlowed() const noexcept;
    [[nodiscard]] float impactSlowRemaining() const noexcept;

    [[nodiscard]] bool applyControl(float duration) noexcept;
    [[nodiscard]] bool isControlled() const noexcept;
    [[nodiscard]] float controlRemaining() const noexcept;

    int health() const noexcept;
    int maxHealth() const noexcept;
    bool isDead() const noexcept;

private:
    Vec2 position_;
    float speed_{240.0f};
    float size_{32.0f};
    Vec2 facingDirection_{0.0f, -1.0f};

    bool isMoving_{false};
    Animator movementAnimator_;
    Health health_{3};
    float controlRemaining_{};
    float impactSlowRemaining_{};
};
