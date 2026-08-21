#include "enemy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
  constexpr std::size_t kEnemyMoveFrameCount{6};
  constexpr float kEnemyMoveFrameDurationSeconds{0.125f};
  constexpr float kImpactSlowDurationSeconds{0.18F};
  constexpr float kImpactSlowTimeMultiplier{0.28F};
  constexpr float kTimerEpsilon{0.00001F};

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

const char *enemyMovementStateName(
    EnemyMovementState state) noexcept
{
  switch (state)
  {
  case EnemyMovementState::Stationary:
    return "Stationary";
  case EnemyMovementState::Normal:
    return "Normal";
  case EnemyMovementState::Attack:
    return "Attack";
  }

  return "Unknown";
}

Enemy::Enemy(
    Vec2 position,
    Vec2 size,
    Vec2 velocity,
    int maxHealth,
    CombatTargetId combatTargetId)
    : combatTargetId_{combatTargetId},
      position_(position),
      size_(size),
      velocity_(velocity),
      facingDirection_{
          facingDirectionFromVelocity(velocity.x)},
      movementAnimator_{
          makeEnemyMoveClip(),
          AnimationPlayMode::Loop},
      health_{maxHealth},
      movementState_{
          velocity.x != 0.0F || velocity.y != 0.0F
              ? EnemyMovementState::Normal
              : EnemyMovementState::Stationary}
{
}

CombatTargetId Enemy::combatTargetId() const noexcept
{
  return combatTargetId_;
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
  static_cast<void>(
      update(
          deltaTime,
          worldWidth,
          std::numeric_limits<float>::max()));
}

EnemyAttackAdvance Enemy::update(
    float deltaTime,
    float worldWidth,
    float worldHeight)
{
  if (isDead())
  {
    attack_.reset();
    velocity_ = Vec2{};
    movementState_ = EnemyMovementState::Stationary;
    impactSlowRemaining_ = 0.0F;
    movementAnimator_.reset();
    return EnemyAttackAdvance{};
  }

  const float effectiveDeltaTime =
      consumeImpactAdjustedDeltaTime(deltaTime);

  if (attack_.phase() != EnemyAttackPhase::Idle)
  {
    return updateActiveAttack(
        attack_.direction(),
        effectiveDeltaTime,
        worldWidth,
        worldHeight);
  }

  // Update position based on velocity and time delta
  position_.x += velocity_.x * effectiveDeltaTime;

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
    movementState_ = EnemyMovementState::Normal;
    movementAnimator_.update(effectiveDeltaTime);
  }
  else
  {
    movementState_ = EnemyMovementState::Stationary;
    movementAnimator_.reset();
  }

  return EnemyAttackAdvance{};
}

EnemyAttackAdvance Enemy::updateTowardsTarget(
    Vec2 targetPosition,
    const EnemyTacticalDirective &tacticalDirective,
    float deltaTime,
    float worldWidth,
    float worldHeight)
{
  if (isDead())
  {
    attack_.reset();
    ai_.reset();
    velocity_ = Vec2{};
    movementState_ = EnemyMovementState::Stationary;
    tacticalRole_ = EnemyTacticalRole::Support;
    impactSlowRemaining_ = 0.0F;
    movementAnimator_.reset();
    return EnemyAttackAdvance{};
  }

  const float effectiveDeltaTime =
      consumeImpactAdjustedDeltaTime(deltaTime);
  tacticalRole_ = tacticalDirective.role;

  const Vec2 selfPosition{
      position_.x + size_.x / 2.0F,
      position_.y + size_.y / 2.0F};
  const Vec2 targetOffset{
      targetPosition.x - selfPosition.x,
      targetPosition.y - selfPosition.y};

  if (attack_.phase() != EnemyAttackPhase::Idle)
  {
    EnemyTacticalDirective activeDirective = tacticalDirective;
    activeDirective.canStartAttack = false;
    static_cast<void>(
        ai_.update(
            EnemyAiInput{
                selfPosition,
                targetPosition,
                activeDirective,
                deltaTime}));
    return updateActiveAttack(
        targetOffset,
        effectiveDeltaTime,
        worldWidth,
        worldHeight);
  }

  const EnemyAiDecision decision =
      ai_.update(
          EnemyAiInput{
              selfPosition,
              targetPosition,
              tacticalDirective,
              deltaTime});

  if (decision.attackRequest.has_value() &&
      tryStartAttack(
          *decision.attackRequest,
          targetOffset))
  {
    ai_.recordAttackStarted(
        *decision.attackRequest);
    tacticalRole_ = EnemyTacticalRole::Engage;
    return updateActiveAttack(
        targetOffset,
        effectiveDeltaTime,
        worldWidth,
        worldHeight);
  }

  velocity_ = Vec2{
      decision.moveDirection.x * ai_.config().normalMoveSpeed,
      decision.moveDirection.y * ai_.config().normalMoveSpeed};
  movementState_ = isMoving()
                       ? EnemyMovementState::Normal
                       : EnemyMovementState::Stationary;

  if (effectiveDeltaTime > 0.0F)
  {
    position_.x += velocity_.x * effectiveDeltaTime;
    position_.y += velocity_.y * effectiveDeltaTime;
  }

  clampToWorld(worldWidth, worldHeight);

  if (velocity_.x < 0.0F)
  {
    facingDirection_ = EnemyFacingDirection::Left;
  }
  else if (velocity_.x > 0.0F)
  {
    facingDirection_ = EnemyFacingDirection::Right;
  }

  if (isMoving())
  {
    movementAnimator_.update(effectiveDeltaTime);
  }
  else
  {
    movementAnimator_.reset();
  }

  return EnemyAttackAdvance{};
}

bool Enemy::tryStartAttack(
    EnemyAttackType type,
    Vec2 direction) noexcept
{
  if (isDead() ||
      !attack_.tryStart(type, direction))
  {
    return false;
  }

  const Vec2 normalizedDirection =
      attack_.direction();
  if (normalizedDirection.x < 0.0F)
  {
    facingDirection_ = EnemyFacingDirection::Left;
  }
  else if (normalizedDirection.x > 0.0F)
  {
    facingDirection_ = EnemyFacingDirection::Right;
  }

  refreshMovementStateFromAttack();
  velocity_ = movementState_ == EnemyMovementState::Stationary
                  ? Vec2{}
                  : Vec2{
                        normalizedDirection.x * movementSpeed(),
                        normalizedDirection.y * movementSpeed()};
  movementAnimator_.reset();
  return true;
}

EnemyAttackPhase Enemy::attackPhase() const noexcept
{
  return attack_.phase();
}

std::optional<EnemyAttackType>
Enemy::attackType() const noexcept
{
  return attack_.type();
}

Vec2 Enemy::attackDirection() const noexcept
{
  return attack_.direction();
}

float Enemy::attackPhaseRemaining() const noexcept
{
  return attack_.phaseRemaining();
}

std::optional<Rect> Enemy::attackHitbox() const noexcept
{
  const std::optional<EnemyAttackType> type =
      attack_.type();
  const std::optional<EnemyAttackConfig> config =
      attack_.currentConfig();

  if (!type.has_value() ||
      !config.has_value())
  {
    return std::nullopt;
  }

  if (*type == EnemyAttackType::Grab)
  {
    return bounds();
  }

  const Vec2 center{
      position_.x + size_.x / 2.0F,
      position_.y + size_.y / 2.0F};
  const Vec2 direction = attack_.direction();
  const Vec2 hitboxCenter{
      center.x +
          direction.x * config->hitboxForwardOffset,
      center.y +
          direction.y * config->hitboxForwardOffset};

  return Rect{
      Vec2{
          hitboxCenter.x - config->hitboxSize.x / 2.0F,
          hitboxCenter.y - config->hitboxSize.y / 2.0F},
      config->hitboxSize};
}

std::optional<Rect>
Enemy::attackTelegraphBounds() const noexcept
{
  const std::optional<EnemyAttackType> type =
      attack_.type();
  const std::optional<EnemyAttackConfig> config =
      attack_.currentConfig();

  if (!type.has_value() ||
      !config.has_value())
  {
    return std::nullopt;
  }

  if (attack_.phase() == EnemyAttackPhase::OffBalance)
  {
    return bounds();
  }

  if (*type != EnemyAttackType::Grab)
  {
    return attackHitbox();
  }

  const Vec2 direction = attack_.direction();
  const Vec2 destination{
      position_.x + direction.x * config->lungeDistance,
      position_.y + direction.y * config->lungeDistance};
  const float left = std::min(position_.x, destination.x);
  const float top = std::min(position_.y, destination.y);
  const float right = std::max(
      position_.x + size_.x,
      destination.x + size_.x);
  const float bottom = std::max(
      position_.y + size_.y,
      destination.y + size_.y);

  return Rect{
      Vec2{left, top},
      Vec2{right - left, bottom - top}};
}

std::optional<EnemyAttackConfig>
Enemy::attackConfig() const noexcept
{
  return attack_.currentConfig();
}

bool Enemy::hasAttackHitOpportunity() const noexcept
{
  return attack_.hasHitOpportunity();
}

bool Enemy::hasGrabContactOpportunity() const noexcept
{
  return attack_.hasGrabContactOpportunity();
}

bool Enemy::confirmGrabContact() noexcept
{
  const bool confirmed = attack_.tryConfirmGrabContact();
  if (confirmed)
  {
    movementState_ = EnemyMovementState::Stationary;
    velocity_ = Vec2{};
    movementAnimator_.reset();
  }
  return confirmed;
}

bool Enemy::consumeAttackHit() noexcept
{
  return attack_.tryConsumeHit();
}

EnemyFacingDirection Enemy::facingDirection() const
{
  return facingDirection_;
}

bool Enemy::isMoving() const
{
  return velocity_.x != 0.0F ||
         velocity_.y != 0.0F;
}

EnemyMovementState Enemy::movementState() const noexcept
{
  return movementState_;
}

float Enemy::movementSpeed() const noexcept
{
  switch (movementState_)
  {
  case EnemyMovementState::Stationary:
    return 0.0F;
  case EnemyMovementState::Normal:
    return ai_.config().normalMoveSpeed;
  case EnemyMovementState::Attack:
    return ai_.config().attackMoveSpeed;
  }

  return 0.0F;
}

EnemyAwarenessState Enemy::awarenessState() const noexcept
{
  return ai_.awarenessState();
}

EnemyTacticalRole Enemy::tacticalRole() const noexcept
{
  return tacticalRole_;
}

float Enemy::searchTimeRemaining() const noexcept
{
  return ai_.searchTimeRemaining();
}

std::optional<Vec2>
Enemy::lastKnownTargetPosition() const noexcept
{
  return ai_.lastKnownTargetPosition();
}

std::size_t Enemy::currentAnimationFrameIndex() const
{
  return movementAnimator_.currentFrameIndex();
}

bool Enemy::takeDamage(int damage)
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

void Enemy::hearTarget(Vec2 targetPosition) noexcept
{
  if (!isDead())
  {
    ai_.hearTarget(targetPosition);
  }
}

bool Enemy::isImpactSlowed() const noexcept
{
  return impactSlowRemaining_ > 0.0F;
}

float Enemy::impactSlowRemaining() const noexcept
{
  return impactSlowRemaining_;
}

int Enemy::health() const noexcept
{
  return health_.current();
}

int Enemy::maxHealth() const noexcept
{
  return health_.maximum();
}

bool Enemy::isDead() const noexcept
{
  return health_.isDead();
}

float Enemy::consumeImpactAdjustedDeltaTime(
    float deltaTime) noexcept
{
  if (!std::isfinite(deltaTime) ||
      deltaTime <= 0.0F)
  {
    return 0.0F;
  }

  const float slowedTime = std::min(
      deltaTime,
      impactSlowRemaining_);
  const float remainingAfterUpdate =
      impactSlowRemaining_ - deltaTime;
  impactSlowRemaining_ =
      remainingAfterUpdate <= kTimerEpsilon
          ? 0.0F
          : remainingAfterUpdate;
  return slowedTime * kImpactSlowTimeMultiplier +
         (deltaTime - slowedTime);
}

EnemyAttackAdvance Enemy::updateActiveAttack(
    Vec2 targetOffset,
    float effectiveDeltaTime,
    float worldWidth,
    float worldHeight)
{
  float windupMovementTime{};
  if (attack_.type() == EnemyAttackType::Grab &&
      attack_.phase() == EnemyAttackPhase::Windup)
  {
    static_cast<void>(attack_.trackDirection(targetOffset));
    windupMovementTime = std::min(
        effectiveDeltaTime,
        attack_.phaseRemaining());
  }

  const Vec2 movementDirection = attack_.direction();
  const EnemyAttackAdvance attackAdvance =
      attack_.update(effectiveDeltaTime);

  position_.x += movementDirection.x *
                 ai_.config().normalMoveSpeed *
                 windupMovementTime;
  position_.y += movementDirection.y *
                 ai_.config().normalMoveSpeed *
                 windupMovementTime;
  position_.x +=
      movementDirection.x * attackAdvance.lungeDistance;
  position_.y +=
      movementDirection.y * attackAdvance.lungeDistance;
  clampToWorld(worldWidth, worldHeight);

  if (movementDirection.x < 0.0F)
  {
    facingDirection_ = EnemyFacingDirection::Left;
  }
  else if (movementDirection.x > 0.0F)
  {
    facingDirection_ = EnemyFacingDirection::Right;
  }

  refreshMovementStateFromAttack();
  if (movementState_ == EnemyMovementState::Stationary)
  {
    velocity_ = Vec2{};
    movementAnimator_.reset();
  }
  else
  {
    velocity_ = Vec2{
        movementDirection.x * movementSpeed(),
        movementDirection.y * movementSpeed()};
    movementAnimator_.update(effectiveDeltaTime);
  }

  return attackAdvance;
}

void Enemy::clampToWorld(
    float worldWidth,
    float worldHeight) noexcept
{
  position_.x = std::clamp(
      position_.x,
      0.0F,
      std::max(0.0F, worldWidth - size_.x));
  position_.y = std::clamp(
      position_.y,
      0.0F,
      std::max(0.0F, worldHeight - size_.y));
}

void Enemy::refreshMovementStateFromAttack() noexcept
{
  if (attack_.type() == EnemyAttackType::Grab &&
      attack_.phase() == EnemyAttackPhase::Windup)
  {
    movementState_ = EnemyMovementState::Normal;
    return;
  }

  if (attack_.type() == EnemyAttackType::Grab &&
      attack_.phase() == EnemyAttackPhase::Active)
  {
    movementState_ = EnemyMovementState::Attack;
    return;
  }

  movementState_ = EnemyMovementState::Stationary;
}
