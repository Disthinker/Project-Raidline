#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "player.h"

namespace
{
    constexpr std::size_t kPlayerMoveFrameCount{6};
    // Ten animation frames per second divides evenly into the current stable
    // 60 Hz presentation cadence. The former 0.09 s duration alternated
    // between five and six presented frames and created a visible rhythmic
    // hitch while the camera followed the player.
    constexpr float kPlayerMoveFrameDuration{0.10f};
    constexpr float kImpactSlowDurationSeconds{0.18F};
    constexpr float kImpactSlowTimeMultiplier{0.28F};
    constexpr float kSprintSpeedMultiplier{1.5F};
    constexpr float kTimerEpsilon{0.00001F};

    AnimationClip makePlayerMoveClip()
    {
        return AnimationClip{
            std::vector<AnimationFrame>(
                kPlayerMoveFrameCount,
                AnimationFrame{kPlayerMoveFrameDuration})};
    }
}

Player::Player(float x, float y, int maxHealth)
    : Player{x, y, maxHealth, maxHealth}
{
}

Player::Player(float x, float y, int maxHealth, int currentHealth)
    : position_{x, y},
      movementAnimator_{
          makePlayerMoveClip(),
          AnimationPlayMode::Loop},
      health_{maxHealth, currentHealth}
{
}

int Player::restoreHealth(int amount)
{
    return health_.restore(amount);
}

void Player::update(const GameplayInput &input, float deltaTime, float worldWidth, float worldHeight)
{
    const bool wasControlled = isControlled();
    float effectiveDeltaTime{};
    if (std::isfinite(deltaTime) &&
        deltaTime > 0.0F)
    {
        const float slowedTime = std::min(
            deltaTime,
            impactSlowRemaining_);
        const float remainingAfterUpdate =
            impactSlowRemaining_ - deltaTime;
        impactSlowRemaining_ =
            remainingAfterUpdate <= kTimerEpsilon
                ? 0.0F
                : remainingAfterUpdate;
        effectiveDeltaTime =
            slowedTime * kImpactSlowTimeMultiplier +
            (deltaTime - slowedTime);
        controlRemaining_ = std::max(
            0.0F,
            controlRemaining_ - deltaTime);
    }

    if (wasControlled)
    {
        isMoving_ = false;
        movementAnimator_.reset();
        return;
    }

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
        const float movementSpeed = speed_ *
            (input.sprint ? kSprintSpeedMultiplier : 1.0F) *
            std::clamp(input.movementSpeedMultiplier, 0.0F, 1.0F);
        position_.x += direction.x * movementSpeed * effectiveDeltaTime;
        position_.y += direction.y * movementSpeed * effectiveDeltaTime;

        movementAnimator_.update(effectiveDeltaTime);
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

bool Player::setPosition(Vec2 position) noexcept
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
    {
        return false;
    }
    position_ = position;
    return true;
}

float Player::size() const
{
    return size_;
}

Vec2 Player::facingDirection() const
{
    return facingDirection_;
}

bool Player::faceDirection(Vec2 direction) noexcept
{
    const float lengthSquared =
        direction.x * direction.x +
        direction.y * direction.y;

    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 0.0F)
    {
        return false;
    }

    const float length = std::sqrt(lengthSquared);
    facingDirection_ = Vec2{
        direction.x / length,
        direction.y / length};
    return true;
}

bool Player::isMoving() const
{
    return isMoving_;
}

std::size_t Player::currentAnimationFrameIndex() const
{
    return movementAnimator_.currentFrameIndex();
}

bool Player::takeDamage(int damage)
{
    const bool wasDead = isDead();
    const bool killed = health_.takeDamage(damage);
    if (!wasDead && !killed)
    {
        impactSlowRemaining_ = kImpactSlowDurationSeconds;
    }
    else if (killed)
    {
        impactSlowRemaining_ = 0.0F;
    }
    return killed;
}

bool Player::isImpactSlowed() const noexcept
{
    return impactSlowRemaining_ > 0.0F;
}

float Player::impactSlowRemaining() const noexcept
{
    return impactSlowRemaining_;
}

bool Player::applyControl(float duration) noexcept
{
    if (!std::isfinite(duration) ||
        duration <= 0.0F ||
        isDead())
    {
        return false;
    }

    controlRemaining_ = std::max(
        controlRemaining_,
        duration);
    isMoving_ = false;
    movementAnimator_.reset();
    return true;
}

bool Player::isControlled() const noexcept
{
    return controlRemaining_ > 0.0F;
}

float Player::controlRemaining() const noexcept
{
    return controlRemaining_;
}

int Player::health() const noexcept
{
    return health_.current();
}

int Player::maxHealth() const noexcept
{
    return health_.maximum();
}

bool Player::isDead() const noexcept
{
    return health_.isDead();
}
