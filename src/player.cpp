#include "player.h"

Player::Player(float x, float y) 
    : position_{x, y}
{

}

void Player::update(const InputSystem& input, float deltaTime)
{
    if (input.isActionPressed(GameAction::MoveUp)) {
        position_.y -= speed_ * deltaTime;
    }
    if (input.isActionPressed(GameAction::MoveDown)) {
        position_.y += speed_ * deltaTime;
    }
    if (input.isActionPressed(GameAction::MoveLeft)) {
        position_.x -= speed_ * deltaTime;
    }
    if (input.isActionPressed(GameAction::MoveRight)) {
        position_.x += speed_ * deltaTime;
    }
}

Vec2 Player::position() const
{
    return position_;
}

float Player::size() const
{
    return size_;
}