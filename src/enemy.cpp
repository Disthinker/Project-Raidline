#include "enemy.h"

#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
  constexpr std::size_t kEnemyMoveFrameCount{6};
  constexpr float kEnemyMoveFrameDurationSeconds{0.125f};

  AnimationClip makeEnemyMoveClip()
  {
    return AnimationClip{
        std::vector<AnimationFrame>(
            kEnemyMoveFrameCount,
            AnimationFrame{
                kEnemyMoveFrameDurationSeconds})};
  }

  EnemyFacingDirection facingDirectionFromVelocity(
      float horizontalVelocity)
  {
    if (horizontalVelocity < 0.0f)
    {
      return EnemyFacingDirection::Left;
    }

    return EnemyFacingDirection::Right;
  }
}

Enemy::Enemy(
    Vec2 position,
    Vec2 size,
    Vec2 velocity)
    : position_(position),
      size_(size),
      velocity_(velocity),
      facingDirection_{
          facingDirectionFromVelocity(velocity.x)},
      movementAnimator_{
          makeEnemyMoveClip(),
          AnimationPlayMode::Loop}
{
}

Vec2 Enemy::position() const
{
  return position_;
}

Vec2 Enemy::size() const
{
  return size_;
}

Vec2 Enemy::velocity() const
{
  return velocity_;
}

Rect Enemy::bounds() const
{
  return Rect{position_, size_};
}

void Enemy::update(float deltaTime, float worldWidth)
{
  // Update position based on velocity and time delta
  position_.x += velocity_.x * deltaTime;

  if (position_.x + size_.x > worldWidth)
  {
    position_.x = worldWidth - size_.x;
    // Reverse the direction of movement
    velocity_.x = -std::abs(velocity_.x);
  }
  if (position_.x < 0)
  {
    position_.x = 0;
    // Reverse the direction of movement
    velocity_.x = std::abs(velocity_.x);
  }

  if (velocity_.x < 0.0f)
  {
    facingDirection_ = EnemyFacingDirection::Left;
  }
  else if (velocity_.x > 0.0f)
  {
    facingDirection_ = EnemyFacingDirection::Right;
  }

  if (isMoving())
  {
    movementAnimator_.update(deltaTime);
  }
  else
  {
    movementAnimator_.reset();
  }
}

EnemyFacingDirection Enemy::facingDirection() const
{
  return facingDirection_;
}

bool Enemy::isMoving() const
{
  return velocity_.x != 0.0f;
}

std::size_t Enemy::currentAnimationFrameIndex() const
{
  return movementAnimator_.currentFrameIndex();
}