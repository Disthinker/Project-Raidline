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
        if (damage <= 0)
        {
            throw std::invalid_argument("Damage must be greater than zero");
        }
        if (isDead())
        {
            return false;
        }
        if (damage >= currentHealth_)
        {
            currentHealth_ = 0;
            return true;
        }

        currentHealth_ -= damage;
        return false;
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