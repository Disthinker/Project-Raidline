#include <cmath>
#include <cstddef>
#include <vector>
#include "player.h"

namespace
{
    constexpr std::size_t kPlayerMoveFrameCount{6};
    constexpr float kPlayerMoveFrameDuration{0.09f};

    AnimationClip makePlayerMoveClip()
    {
        return AnimationClip{
            std::vector<AnimationFrame>(
                kPlayerMoveFrameCount,
                AnimationFrame{kPlayerMoveFrameDuration})};
    }
}

Player::Player(float x, float y)
    : position_{x, y},
      movementAnimator_{
          makePlayerMoveClip(),
          AnimationPlayMode::Loop}
{
}

void Player::update(const GameplayInput &input, float deltaTime, float worldWidth, float worldHeight)
{
    Vec2 direction{};

    if (input.moveUp)
    {
        direction.y -= 1.0f;
    }
    if (input.moveDown)
    {
        direction.y += 1.0f;
    }
    if (input.moveLeft)
    {
        direction.x -= 1.0f;
    }
    if (input.moveRight)
    {
        direction.x += 1.0f;
    }

    const float length = std::sqrt(
        direction.x * direction.x + direction.y * direction.y);
    const bool wasMoving = isMoving_;
    isMoving_ = length > 0.0f;

    if (isMoving_)
    {
        direction.x /= length;
        direction.y /= length;
        facingDirection_ = direction;
        // 归一化方向更新
        position_.x += direction.x * speed_ * deltaTime;
        position_.y += direction.y * speed_ * deltaTime;

        movementAnimator_.update(deltaTime);
    }
    else if (wasMoving)
    {
        // 如果停止移动，重置动画到初始帧
        movementAnimator_.reset();
    }
    // 检查边界
    if (position_.x <= 0)
        position_.x = 0;
    if (position_.y <= 0)
        position_.y = 0;
    if (position_.x + size_ >= worldWidth)
        position_.x = worldWidth - size_;
    if (position_.y + size_ >= worldHeight)
        position_.y = worldHeight - size_;
}

Vec2 Player::position() const
{
    return position_;
}

float Player::size() const
{
    return size_;
}

Vec2 Player::facingDirection() const
{
    return facingDirection_;
}

bool Player::isMoving() const
{
    return isMoving_;
}

std::size_t Player::currentAnimationFrameIndex() const
{
    return movementAnimator_.currentFrameIndex();
}