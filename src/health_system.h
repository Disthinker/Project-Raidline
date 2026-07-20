#pragma once

#include <stdexcept>

class Health
{
public:
    explicit Health(int maxHealth)
        : maxHealth_(maxHealth),
          currentHealth_(maxHealth)
    {
        // TODO：maxHealth <= 0 时抛出 std::invalid_argument
    }

    [[nodiscard]] bool takeDamage(int damage)
    {
        // TODO 1：damage <= 0 时抛出 std::invalid_argument
        // TODO 2：如果已经死亡，返回 false
        // TODO 3：判断本次伤害是否导致死亡
        // TODO 4：生命值最低为 0
        // TODO 5：只在 alive -> dead 时返回 true
    }

    int current() const noexcept
    {
        return currentHealth_;
    }

    int maximum() const noexcept
    {
        return maxHealth_;
    }

    bool isDead() const noexcept
    {
        return currentHealth_ == 0;
    }

private:
    int maxHealth_;
    int currentHealth_;
};