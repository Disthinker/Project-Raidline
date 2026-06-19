#include <cmath>
#include "player.h"

Player::Player(float x, float y) 
    : position_{x, y}
{

}

void Player::update(const InputSystem& input, float deltaTime, float worldWidth, float worldHeight)
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
        // 归一化方向更新
        position_.x += direction.x * speed_ * deltaTime;
        position_.y += direction.y * speed_ * deltaTime;
        // 检查边界
        if(position_.x <= 0) position_.x = 0;
        if(position_.y <= 0) position_.y = 0;
        if(position_.x + size_ >= worldWidth) position_.x = worldWidth - size_;
        if(position_.y + size_ >= worldHeight) position_.y = worldHeight - size_;
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