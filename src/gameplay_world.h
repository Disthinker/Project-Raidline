#pragma once

#include <vector>
#include "enemy.h"
#include "gameplay_input.h"
#include "player.h"
#include "projectile.h"

class GameplayWorld
{
public:
    GameplayWorld();

    void update(const GameplayInput &input, float deltaTime);

    const Player &player() const;
    const std::vector<Projectile> &projectiles() const;
    const std::vector<Enemy> &enemies() const;

private:
    Player player_{640.0f, 360.0f};
    std::vector<Projectile> projectiles_;
    std::vector<Enemy> enemies_;
};