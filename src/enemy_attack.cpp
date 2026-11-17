#include "enemy_attack.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kPhaseBoundaryEpsilon{0.00001F};
    constexpr EnemyAttackConfig kLowLungeAttackConfigV1{
        0.70F, 0.18F, 0.70F, 0.0F, 42.0F, {100.0F, 48.0F}, 50.0F,
        22, 0.0F};
    constexpr EnemyAttackCombatDamage kLowLungeAttackDamageV1{
        22, HitRegion::Legs, 0, 0, false};

    float dot(Vec2 a, Vec2 b) noexcept
    {
        return a.x * b.x + a.y * b.y;
    }

    std::array<Vec2, 4> boxCorners(Rect box) noexcept
    {
        return {box.position,
            Vec2{box.position.x + box.size.x, box.position.y},
            Vec2{box.position.x + box.size.x, box.position.y + box.size.y},
            Vec2{box.position.x, box.position.y + box.size.y}};
    }

    bool isValidAttackType(
        EnemyAttackType type) noexcept
    {
        switch (type)
        {
        case EnemyAttackType::Grab:
        case EnemyAttackType::Scratch:
        case EnemyAttackType::Bite:
        case EnemyAttackType::LowLunge:
            return true;
        }

        return false;
    }

    bool isFinitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    bool isFiniteNonNegative(float value) noexcept
    {
        return std::isfinite(value) && value >= 0.0F;
    }

    void validateConfig(
        EnemyAttackType type,
        const EnemyAttackConfig &config)
    {
        if (!isFinitePositive(config.windupDuration) ||
            !isFinitePositive(config.activeDuration) ||
            !isFinitePositive(config.recoveryDuration) ||
            !isFiniteNonNegative(config.missRecoveryDuration) ||
            !isFiniteNonNegative(config.lungeDistance) ||
            !isFinitePositive(config.hitboxSize.x) ||
            !isFinitePositive(config.hitboxSize.y) ||
            !isFiniteNonNegative(config.hitboxForwardOffset) ||
            config.damage < 0 ||
            !isFiniteNonNegative(config.controlDuration))
        {
            throw std::invalid_argument{
                "EnemyAttackConfig contains invalid values"};
        }

        const bool isGrab = type == EnemyAttackType::Grab;
        const bool isBite = type == EnemyAttackType::Bite;

        if ((isGrab &&
             (config.lungeDistance <= 0.0F ||
              config.damage != 0 ||
              config.missRecoveryDuration <= 0.0F)) ||
            (!isGrab && config.lungeDistance != 0.0F) ||
            (!isGrab && config.missRecoveryDuration != 0.0F) ||
            (!isGrab && config.damage <= 0) ||
            (isBite && config.controlDuration <= 0.0F) ||
            (!isBite && config.controlDuration != 0.0F))
        {
            throw std::invalid_argument{
                "EnemyAttackConfig violates attack-specific rules"};
        }
    }
}

std::array<Vec2, 4> EnemyLowLungeCorridor::corners() const noexcept
{
    const Vec2 side{-direction.y * halfWidth, direction.x * halfWidth};
    const Vec2 end{origin.x + direction.x * reach, origin.y + direction.y * reach};
    return {Vec2{origin.x + side.x, origin.y + side.y},
            Vec2{end.x + side.x, end.y + side.y},
            Vec2{end.x - side.x, end.y - side.y},
            Vec2{origin.x - side.x, origin.y - side.y}};
}

Rect EnemyLowLungeCorridor::bounds() const noexcept
{
    const auto points = corners();
    float left = points[0].x, right = left;
    float top = points[0].y, bottom = top;
    for (const Vec2 p : points)
    {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    return {{left, top}, {right - left, bottom - top}};
}

bool EnemyLowLungeCorridor::overlaps(Rect target) const noexcept
{
    if (reach <= 0.0F || target.size.x <= 0.0F || target.size.y <= 0.0F)
        return false;
    const Rect area = bounds();
    if (target.position.x >= area.position.x + area.size.x ||
        target.position.y >= area.position.y + area.size.y ||
        target.position.x + target.size.x <= area.position.x ||
        target.position.y + target.size.y <= area.position.y)
        return false;
    const Vec2 center{target.position.x + target.size.x * 0.5F - origin.x,
        target.position.y + target.size.y * 0.5F - origin.y};
    const Vec2 side{-direction.y, direction.x};
    const float along = dot(center, direction);
    const float across = dot(center, side);
    const float alongRadius = (std::abs(direction.x) * target.size.x +
        std::abs(direction.y) * target.size.y) * 0.5F;
    const float acrossRadius = (std::abs(side.x) * target.size.x +
        std::abs(side.y) * target.size.y) * 0.5F;
    return along + alongRadius > 0.0F && along - alongRadius < reach &&
        std::abs(across) < acrossRadius + halfWidth;
}

void EnemyLowLungeCorridor::clipBefore(Rect blocker) noexcept
{
    if (!overlaps(blocker)) return;
    const Vec2 side{-direction.y, direction.x};
    const auto points = boxCorners(blocker);
    std::array<Vec2, 4> projected{};
    float firstForward = reach;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const Vec2 offset{points[i].x - origin.x, points[i].y - origin.y};
        projected[i] = {dot(offset, direction), dot(offset, side)};
        if (std::abs(projected[i].y) <= halfWidth)
            firstForward = std::min(firstForward, projected[i].x);
    }

    // Intersections of the blocker edges with the two corridor sides complete
    // the clipped polygon. Its earliest point is the visible stop plane.
    for (std::size_t i = 0; i < projected.size(); ++i)
    {
        const Vec2 a = projected[i], b = projected[(i + 1U) % projected.size()];
        for (const float sideOffset : {-halfWidth, halfWidth})
        {
            if ((a.y < sideOffset && b.y > sideOffset) ||
                (a.y > sideOffset && b.y < sideOffset))
            {
                const float u = (sideOffset - a.y) / (b.y - a.y);
                firstForward = std::min(firstForward, a.x + u * (b.x - a.x));
            }
        }
    }
    reach = std::max(0.0F, firstForward);
}

EnemyAttackConfigSet defaultEnemyAttackConfigs()
{
    return standardInfectedAttackConfigsV1();
}

EnemyAttackConfigSet standardInfectedAttackConfigsV1()
{
    return EnemyAttackConfigSet{
        EnemyAttackConfig{
            0.55F,
            0.55F,
            0.35F,
            1.35F,
            74.25F,
            Vec2{50.0F, 50.0F},
            0.0F,
            0,
            0.0F},
        EnemyAttackConfig{
            0.18F,
            0.10F,
            0.28F,
            0.0F,
            0.0F,
            Vec2{64.0F, 48.0F},
            42.0F,
            1,
            0.0F},
        EnemyAttackConfig{
            0.25F,
            0.12F,
            0.60F,
            0.0F,
            0.0F,
            Vec2{52.0F, 44.0F},
            38.0F,
            2,
            0.75F}};
}

EnemyAttackCombatDamage enemyAttackCombatDamage(
    EnemyAttackType type) noexcept
{
    if (type == EnemyAttackType::LowLunge)
        return kLowLungeAttackDamageV1;
    return standardInfectedAttackCombatDamageV1(type);
}

EnemyAttackCombatDamage standardInfectedAttackCombatDamageV1(
    EnemyAttackType type) noexcept
{
    switch (type)
    {
    case EnemyAttackType::Grab:
        return EnemyAttackCombatDamage{};
    case EnemyAttackType::Scratch:
        return EnemyAttackCombatDamage{
            12,
            HitRegion::Torso,
            1,
            2,
            false};
    case EnemyAttackType::Bite:
        return EnemyAttackCombatDamage{
            18,
            HitRegion::Head,
            1,
            3,
            false};
    case EnemyAttackType::LowLunge:
        return EnemyAttackCombatDamage{};
    }
    return EnemyAttackCombatDamage{};
}

const char *enemyAttackTypeName(
    EnemyAttackType type) noexcept
{
    switch (type)
    {
    case EnemyAttackType::Grab:
        return "Grab";
    case EnemyAttackType::Scratch:
        return "Scratch";
    case EnemyAttackType::Bite:
        return "Bite";
    case EnemyAttackType::LowLunge:
        return "Low Lunge";
    }

    return "Unknown";
}

const char *enemyAttackPhaseName(
    EnemyAttackPhase phase) noexcept
{
    switch (phase)
    {
    case EnemyAttackPhase::Idle:
        return "Idle";
    case EnemyAttackPhase::Windup:
        return "Windup";
    case EnemyAttackPhase::Active:
        return "Active";
    case EnemyAttackPhase::Recovery:
        return "Recovery";
    case EnemyAttackPhase::OffBalance:
        return "OffBalance";
    }

    return "Unknown";
}

EnemyAttackState::EnemyAttackState()
    : EnemyAttackState{defaultEnemyAttackConfigs()}
{
}

EnemyAttackState::EnemyAttackState(
    EnemyAttackConfigSet configs)
    : configs_{configs}
{
    validateConfig(
        EnemyAttackType::Grab,
        configs_[indexOf(EnemyAttackType::Grab)]);
    validateConfig(
        EnemyAttackType::Scratch,
        configs_[indexOf(EnemyAttackType::Scratch)]);
    validateConfig(
        EnemyAttackType::Bite,
        configs_[indexOf(EnemyAttackType::Bite)]);
}

bool EnemyAttackState::tryStart(
    EnemyAttackType type,
    Vec2 direction) noexcept
{
    if (phase_ != EnemyAttackPhase::Idle ||
        !isValidAttackType(type))
    {
        return false;
    }

    const float lengthSquared =
        direction.x * direction.x +
        direction.y * direction.y;

    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 0.0F)
    {
        return false;
    }

    const float length = std::sqrt(lengthSquared);
    type_ = type;
    direction_ = Vec2{
        direction.x / length,
        direction.y / length};
    phase_ = EnemyAttackPhase::Windup;
    phaseRemaining_ = config(type).windupDuration;
    hitConsumed_ = false;
    activeOpportunityPending_ = false;
    return true;
}

bool EnemyAttackState::trackDirection(
    Vec2 direction) noexcept
{
    if (type_ != EnemyAttackType::Grab ||
        phase_ != EnemyAttackPhase::Windup)
    {
        return false;
    }

    const float lengthSquared =
        direction.x * direction.x +
        direction.y * direction.y;
    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 0.0F)
    {
        return false;
    }

    const float length = std::sqrt(lengthSquared);
    direction_ = Vec2{
        direction.x / length,
        direction.y / length};
    return true;
}

EnemyAttackAdvance EnemyAttackState::update(
    float deltaTime) noexcept
{
    activeOpportunityPending_ = false;
    EnemyAttackAdvance result{};

    if (phase_ == EnemyAttackPhase::Idle ||
        !std::isfinite(deltaTime) ||
        deltaTime <= 0.0F)
    {
        return result;
    }

    float remainingDelta = deltaTime;

    // At most Windup, Active, Recovery/OffBalance can be consumed in one call.
    // The explicit bound keeps abnormal deltaTime from creating an
    // unbounded transition loop.
    for (int transitionCount = 0;
         transitionCount < 5 &&
         phase_ != EnemyAttackPhase::Idle &&
         remainingDelta > 0.0F;
         ++transitionCount)
    {
        const float consumedTime =
            std::min(remainingDelta, phaseRemaining_);

        if (phase_ == EnemyAttackPhase::Active)
        {
            result.hadActiveTime =
                result.hadActiveTime ||
                consumedTime > 0.0F;

            const EnemyAttackConfig &attackConfig =
                config(*type_);
            result.lungeDistance +=
                attackConfig.lungeDistance *
                (consumedTime /
                 attackConfig.activeDuration);
        }

        phaseRemaining_ -= consumedTime;
        remainingDelta -= consumedTime;

        if (phaseRemaining_ <= kPhaseBoundaryEpsilon)
        {
            advancePhase();
        }
    }

    activeOpportunityPending_ =
        result.hadActiveTime &&
        type_.has_value();
    return result;
}

bool EnemyAttackState::tryConsumeHit() noexcept
{
    if (!type_.has_value() ||
        *type_ == EnemyAttackType::Grab ||
        hitConsumed_ ||
        (phase_ != EnemyAttackPhase::Active &&
         !activeOpportunityPending_))
    {
        return false;
    }

    hitConsumed_ = true;
    activeOpportunityPending_ = false;
    return true;
}

bool EnemyAttackState::hasGrabContactOpportunity() const noexcept
{
    return type_ == EnemyAttackType::Grab &&
           !hitConsumed_ &&
           (phase_ == EnemyAttackPhase::Active ||
            activeOpportunityPending_);
}

bool EnemyAttackState::tryConfirmGrabContact() noexcept
{
    if (!hasGrabContactOpportunity())
    {
        return false;
    }

    type_ = EnemyAttackType::Bite;
    phase_ = EnemyAttackPhase::Active;
    phaseRemaining_ = config(EnemyAttackType::Bite).activeDuration;
    hitConsumed_ = false;
    activeOpportunityPending_ = true;
    return true;
}

void EnemyAttackState::reset() noexcept
{
    phase_ = EnemyAttackPhase::Idle;
    type_.reset();
    direction_ = Vec2{};
    phaseRemaining_ = 0.0F;
    hitConsumed_ = false;
    activeOpportunityPending_ = false;
}

EnemyAttackPhase EnemyAttackState::phase() const noexcept
{
    return phase_;
}

std::optional<EnemyAttackType>
EnemyAttackState::type() const noexcept
{
    return type_;
}

Vec2 EnemyAttackState::direction() const noexcept
{
    return direction_;
}

float EnemyAttackState::phaseRemaining() const noexcept
{
    return phaseRemaining_;
}

bool EnemyAttackState::hitConsumed() const noexcept
{
    return hitConsumed_;
}

bool EnemyAttackState::hasHitOpportunity() const noexcept
{
    return type_.has_value() &&
           *type_ != EnemyAttackType::Grab &&
           !hitConsumed_ &&
           (phase_ == EnemyAttackPhase::Active ||
            activeOpportunityPending_);
}

const EnemyAttackConfig &EnemyAttackState::config(
    EnemyAttackType type) const noexcept
{
    if (type == EnemyAttackType::LowLunge)
        return kLowLungeAttackConfigV1;
    return configs_[indexOf(type)];
}

std::optional<EnemyAttackConfig>
EnemyAttackState::currentConfig() const noexcept
{
    if (!type_.has_value())
    {
        return std::nullopt;
    }

    return config(*type_);
}

std::size_t EnemyAttackState::indexOf(
    EnemyAttackType type) noexcept
{
    switch (type)
    {
    case EnemyAttackType::Grab:
        return 0U;
    case EnemyAttackType::Scratch:
        return 1U;
    case EnemyAttackType::Bite:
        return 2U;
    case EnemyAttackType::LowLunge:
        break;
    }

    return 0U;
}

void EnemyAttackState::advancePhase() noexcept
{
    const EnemyAttackConfig &attackConfig =
        config(*type_);

    switch (phase_)
    {
    case EnemyAttackPhase::Windup:
        phase_ = EnemyAttackPhase::Active;
        phaseRemaining_ = attackConfig.activeDuration;
        break;
    case EnemyAttackPhase::Active:
        if (*type_ == EnemyAttackType::Grab)
        {
            phase_ = EnemyAttackPhase::OffBalance;
            phaseRemaining_ = attackConfig.missRecoveryDuration;
        }
        else
        {
            phase_ = EnemyAttackPhase::Recovery;
            phaseRemaining_ = attackConfig.recoveryDuration;
        }
        break;
    case EnemyAttackPhase::Recovery:
        reset();
        break;
    case EnemyAttackPhase::OffBalance:
        reset();
        break;
    case EnemyAttackPhase::Idle:
        break;
    }
}
