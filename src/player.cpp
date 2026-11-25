#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include "player.h"

namespace
{
    // Cumulative ends from the authored frame table, in milliseconds.
    constexpr std::array walkEnds{110, 225, 335, 450, 560, 675, 785, 900};
    constexpr std::array runEnds{90, 205, 295, 385, 500, 590};
    constexpr std::array standStartEnds{55, 110, 155};
    constexpr std::array walkStartEnds{55, 100};
    // Smaller than one microsecond; absorb binary float error at exact authored boundaries.
    constexpr double boundaryToleranceMs{0.00005};

    constexpr float kImpactSlowDurationSeconds{0.18F};
    constexpr float kImpactSlowTimeMultiplier{0.28F};
    constexpr float kSprintSpeedMultiplier{1.5F};
    constexpr float kTimerEpsilon{0.00001F};

    std::span<const int> frameEnds(PlayerMotionClip clip) noexcept
    {
        switch (clip)
        {
        case PlayerMotionClip::Idle: return {};
        case PlayerMotionClip::Walk: return walkEnds;
        case PlayerMotionClip::Run: return runEnds;
        case PlayerMotionClip::StandToRun: return standStartEnds;
        case PlayerMotionClip::WalkToRunNear:
        case PlayerMotionClip::WalkToRunFar: return walkStartEnds;
        }
        return {};
    }
}

void PlayerMotion::move(bool sprint, float seconds) noexcept
{
    using Clip = PlayerMotionClip;
    if (!sprint && frame_.clip != Clip::Walk)
    {
        // Carry the bearing foot through a slowdown, including an interrupted start.
        if (frame_.clip == Clip::Run)
            elapsedMs_ *= static_cast<double>(walkEnds.back()) / runEnds.back();
        else if (frame_.clip == Clip::WalkToRunNear || frame_.clip == Clip::WalkToRunFar)
        {
            const bool nearFoot = frame_.clip == Clip::WalkToRunNear;
            const std::size_t walkIndex = (nearFoot ? 0U : 4U) + frame_.entryPhase;
            const int entry = walkIndex == 0 ? 0 : walkEnds[walkIndex - 1];
            const int landing = nearFoot ? walkEnds[3] : walkEnds.back();
            elapsedMs_ = entry + elapsedMs_ * (landing - entry) / walkStartEnds.back();
        }
        else
            elapsedMs_ = 0.0;
        frame_.clip = Clip::Walk;
        frame_.entryPhase = 0;
    }
    else if (sprint && frame_.clip == Clip::Idle)
    {
        frame_.clip = Clip::StandToRun;
        elapsedMs_ = 0.0;
    }
    else if (sprint && frame_.clip == Clip::Walk)
    {
        frame_.clip = frame_.index < 4 ? Clip::WalkToRunNear : Clip::WalkToRunFar;
        frame_.entryPhase = static_cast<std::uint8_t>(frame_.index % 4);
        elapsedMs_ = 0.0;
    }

    if (seconds > 0.0F && std::isfinite(seconds))
        elapsedMs_ += static_cast<double>(seconds) * 1000.0;
    auto ends = frameEnds(frame_.clip);
    if (frame_.clip != Clip::Walk && frame_.clip != Clip::Run &&
        elapsedMs_ + boundaryToleranceMs >= ends.back())
    {
        // The near-supported transition lands on the far foot (run frame four).
        const double landing = frame_.clip == Clip::WalkToRunNear ? runEnds[2] : 0.0;
        elapsedMs_ = landing + std::max(0.0, elapsedMs_ - ends.back());
        frame_.clip = Clip::Run;
        frame_.entryPhase = 0;
        ends = runEnds;
    }
    if (elapsedMs_ >= ends.back())
        elapsedMs_ = std::fmod(elapsedMs_, ends.back());
    if (elapsedMs_ + boundaryToleranceMs >= ends.back())
        elapsedMs_ = 0.0;
    frame_.index = static_cast<std::size_t>(
        std::upper_bound(ends.begin(), ends.end(), elapsedMs_ + boundaryToleranceMs) - ends.begin());
}

void PlayerMotion::stop() noexcept
{
    frame_.clip = PlayerMotionClip::Idle;
    frame_.index = 0;
    frame_.entryPhase = 0;
    elapsedMs_ = 0.0;
}

void PlayerMotion::face(float horizontal) noexcept
{
    // Normalizing a vertical world-space sight line can leave a subpixel residue.
    if (std::abs(horizontal) > 0.00001F)
        frame_.left = horizontal < 0.0F;
}

Player::Player(float x, float y, int maxHealth)
    : Player{x, y, maxHealth, maxHealth}
{
}

Player::Player(float x, float y, int maxHealth, int currentHealth)
    : position_{x, y},
      health_{maxHealth, currentHealth}
{
}

int Player::restoreHealth(int amount)
{
    return health_.restore(amount);
}

void Player::update(const GameplayInput &input, float deltaTime, float worldWidth, float worldHeight,
                    FacingSource facingSource)
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
        motion_.stop();
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
        if (facingSource == FacingSource::Movement) motion_.face(direction.x);
        // 归一化方向更新
        const float movementSpeed = speed_ *
            (input.sprint ? kSprintSpeedMultiplier : 1.0F) *
            std::clamp(input.movementSpeedMultiplier, 0.0F, 1.0F);
        position_.x += direction.x * movementSpeed * effectiveDeltaTime;
        position_.y += direction.y * movementSpeed * effectiveDeltaTime;

        motion_.move(input.sprint, effectiveDeltaTime);
    }
    else if (wasMoving)
    {
        // 如果停止移动，重置动画到初始帧
        motion_.stop();
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
    motion_.face(facingDirection_.x);
    return true;
}

bool Player::isMoving() const
{
    return isMoving_;
}

std::size_t Player::currentAnimationFrameIndex() const
{
    return motionFrame().index;
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
    motion_.stop();
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
