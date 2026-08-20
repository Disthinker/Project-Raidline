#pragma once

#include <cstddef>
#include <optional>

#include "animation.h"
#include "enemy_ai.h"
#include "enemy_attack.h"
#include "rect.h"
#include "vec2.h"
#include "health_system.h"

enum class EnemyFacingDirection
{
    Left,
    Right
};

enum class EnemyMovementState
{
    Stationary,
    Normal,
    Attack
};

[[nodiscard]]
const char *enemyMovementStateName(
    EnemyMovementState state) noexcept;

class Enemy
{
public:
    Enemy(
        Vec2 position,
        Vec2 size,
        Vec2 velocity = Vec2{},
        int maxHealth = 1);

    Vec2 position() const;
    Vec2 size() const;
    Vec2 velocity() const;
    Rect bounds() const;

    void update(float deltaTime, float worldWidth);

    [[nodiscard]]
    EnemyAttackAdvance update(
        float deltaTime,
        float worldWidth,
        float worldHeight);

    [[nodiscard]]
    EnemyAttackAdvance updateTowardsTarget(
        Vec2 targetPosition,
        const EnemyTacticalDirective &tacticalDirective,
        float deltaTime,
        float worldWidth,
        float worldHeight);

    [[nodiscard]]
    bool tryStartAttack(
        EnemyAttackType type,
        Vec2 direction) noexcept;

    [[nodiscard]]
    EnemyAttackPhase attackPhase() const noexcept;

    [[nodiscard]]
    std::optional<EnemyAttackType> attackType() const noexcept;

    [[nodiscard]]
    Vec2 attackDirection() const noexcept;

    [[nodiscard]]
    float attackPhaseRemaining() const noexcept;

    [[nodiscard]]
    std::optional<Rect> attackHitbox() const noexcept;

    [[nodiscard]]
    std::optional<Rect> attackTelegraphBounds() const noexcept;

    [[nodiscard]]
    std::optional<EnemyAttackConfig> attackConfig() const noexcept;

    [[nodiscard]]
    bool hasAttackHitOpportunity() const noexcept;

    [[nodiscard]]
    bool hasGrabContactOpportunity() const noexcept;

    [[nodiscard]]
    bool confirmGrabContact() noexcept;

    [[nodiscard]]
    bool consumeAttackHit() noexcept;

    EnemyFacingDirection facingDirection() const;
    bool isMoving() const;
    [[nodiscard]] EnemyMovementState movementState() const noexcept;
    [[nodiscard]] float movementSpeed() const noexcept;
    [[nodiscard]] EnemyAwarenessState awarenessState() const noexcept;
    [[nodiscard]] EnemyTacticalRole tacticalRole() const noexcept;
    [[nodiscard]] float searchTimeRemaining() const noexcept;
    [[nodiscard]] std::optional<Vec2> lastKnownTargetPosition() const noexcept;
    std::size_t currentAnimationFrameIndex() const;

    [[nodiscard]] bool takeDamage(int damage);
    void hearTarget(Vec2 targetPosition) noexcept;

    [[nodiscard]] bool isImpactSlowed() const noexcept;
    [[nodiscard]] float impactSlowRemaining() const noexcept;

    int health() const noexcept;
    int maxHealth() const noexcept;
    bool isDead() const noexcept;

private:
    Vec2 position_;
    Vec2 size_;
    Vec2 velocity_;

    EnemyFacingDirection facingDirection_;
    Animator movementAnimator_;
    Health health_;
    EnemyAttackState attack_;
    EnemyAiState ai_;
    EnemyMovementState movementState_{EnemyMovementState::Stationary};
    EnemyTacticalRole tacticalRole_{EnemyTacticalRole::Support};
    float impactSlowRemaining_{};

    [[nodiscard]]
    float consumeImpactAdjustedDeltaTime(
        float deltaTime) noexcept;

    [[nodiscard]]
    EnemyAttackAdvance updateActiveAttack(
        Vec2 targetOffset,
        float effectiveDeltaTime,
        float worldWidth,
        float worldHeight);

    void clampToWorld(
        float worldWidth,
        float worldHeight) noexcept;

    void refreshMovementStateFromAttack() noexcept;
};
