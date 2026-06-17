#pragma once
#include <fmt/core.h>

class Health{
private:
    int health;
public:
    Health(int initial_health): health(initial_health) {}

    void takeDamage(int damage) {
        if (damage < 0) return; // 异常处理：防止负数伤害变成加血
        health -= damage;
        if (health < 0) health = 0; // 边界处理：血量不能为负

        // 使用引入的 fmt 库进行现代化的日志输出
        fmt::print("[DAMAGE] Player took {} damage. Current Health: {}\n", damage, health);
    }

    int getHealth() const { return health; }
    bool isDead() const { return health == 0; }
};