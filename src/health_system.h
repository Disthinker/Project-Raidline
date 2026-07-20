#pragma once

#include <stdexcept>

class Health
{
public:
    explicit Health(int maxHealth)
        : maxHealth_(maxHealth),
          currentHealth_(maxHealth)
    {
        if (maxHealth <= 0)
        {
            throw std::invalid_argument("Max health must be greater than zero");
        }
    }

    [[nodiscard]] bool takeDamage(int damage)
    {
        // TODO 1：damage <= 0 时抛出 std::invalid_argument
        if (damage <= 0)
        {
            throw std::invalid_argument("Damage must be greater than zero");
        }
        if (isDead())
        {
            return false;
        }
        currentHealth_ -= damage;
        if (currentHealth_ < 0)
        {
            currentHealth_ = 0;
        }
        return currentHealth_ == 0;
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