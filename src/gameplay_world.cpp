#include "gameplay_world.h"

void GameplayWorld::update(const GameplayInput &input, float deltaTime)
{
}

const Player &GameplayWorld::player() const
{
    return player_;
}

const std::vector<Projectile> &GameplayWorld::projectiles() const
{
    return projectiles_;
}

const std::vector<Enemy> &GameplayWorld::enemies() const
{
    return enemies_;
}
