#include <cmath>
#include "player.h"

Player::Player(float x, float y) 
    : position_{x, y}
{

}

void Player::update(const InputSystem& input, float deltaTime)
{
    Vec2 direction {};

    if (input.isActionPressed(GameAction::MoveUp)) {
        direction.y -= 1.0f;
    }
    if (input.isActionPressed(GameAction::MoveDown)) {
        direction.y += 1.0f;
    }
    if (input.isActionPressed(GameAction::MoveLeft)) {
        direction.x -= 1.0f;
    }
    if (input.isActionPressed(GameAction::MoveRight)) {
        direction.x += 1.0f;
    }

    const float length = std::sqrt(
        direction.x * direction.x + direction.y * direction.y
    );

    if (length > 0.0f) {
        direction.x /= length;
        direction.y /= length;

        position_.x += direction.x * speed_ * deltaTime;
        position_.y += direction.y * speed_ * deltaTime;
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