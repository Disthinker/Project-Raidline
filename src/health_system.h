#pragma once

#include <algorithm>
#include <stdexcept>

class Health
{
public:
    explicit Health(int maxHealth)
        : Health(maxHealth, maxHealth)
    {
    }

    Health(int maxHealth, int currentHealth)
        : maxHealth_(maxHealth),
          currentHealth_(currentHealth)
    {
        if (maxHealth <= 0 || currentHealth <= 0 ||
            currentHealth > maxHealth)
        {
            throw std::invalid_argument("Health values are invalid");
        }
    }

    [[nodiscard]] int restore(int amount)
    {
        if (amount <= 0)
        {
            throw std::invalid_argument("Restore amount must be positive");
        }
        const int before = currentHealth_;
        currentHealth_ = std::min(maxHealth_, currentHealth_ + amount);
        return currentHealth_ - before;
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
