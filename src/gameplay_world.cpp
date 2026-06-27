#include "gameplay_world.h"

GameplayWorld::GameplayWorld()
{
    enemies_.emplace_back(Vec2{600.0f, 100.0f}, Vec2{50.0f, 50.0f});
}

void GameplayWorld::update(const GameplayInput &input, float deltaTime)
{
    (void)input;
    (void)deltaTime;
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
