#include "shot_feedback_presentation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    constexpr float kMinimumDirectionLengthSquared{0.000001F};
    constexpr std::size_t kMaximumActiveShots{8U};

    bool isFinitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    bool isFinite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    float remainingFraction(float age, float lifetime) noexcept
    {
        return std::clamp(1.0F - age / lifetime, 0.0F, 1.0F);
    }
}

ShotFeedbackPresentationState::ShotFeedbackPresentationState()
    : ShotFeedbackPresentationState{ShotFeedbackPresentationConfig{}}
{
}

ShotFeedbackPresentationState::ShotFeedbackPresentationState(
    ShotFeedbackPresentationConfig config)
    : config_{config}
{
    if (!isFinitePositive(config_.muzzleFlashLifetimeSeconds) ||
        !isFinitePositive(config_.smokeLifetimeSeconds) ||
        !isFinitePositive(config_.screenShakeLifetimeSeconds) ||
        !std::isfinite(config_.maximumSmokeOpacity) ||
        config_.maximumSmokeOpacity <= 0.0F ||
        config_.maximumSmokeOpacity > 0.35F)
    {
        throw std::invalid_argument{
            "ShotFeedbackPresentationConfig contains invalid values"};
    }
}

bool ShotFeedbackPresentationState::recordAcceptedShot(
    ShotId shotId,
    Vec2 origin,
    Vec2 direction) noexcept
{
    const float directionLengthSquared =
        direction.x * direction.x + direction.y * direction.y;
    if (shotId == kInvalidShotId ||
        !isFinite(origin) ||
        !isFinite(direction) ||
        !std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= kMinimumDirectionLengthSquared)
    {
        return false;
    }

    const float directionLength = std::sqrt(directionLengthSquared);
    if (activeShots_.size() >= kMaximumActiveShots)
    {
        activeShots_.erase(activeShots_.begin());
    }
    activeShots_.push_back(
        ActiveShotFeedback{
            shotId,
            origin,
            Vec2{
                direction.x / directionLength,
                direction.y / directionLength},
            0.0F});
    return true;
}

void ShotFeedbackPresentationState::update(float deltaTime) noexcept
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return;
    }

    for (ActiveShotFeedback &shot : activeShots_)
    {
        shot.ageSeconds += deltaTime;
    }

    const float maximumLifetime = std::max({
        config_.muzzleFlashLifetimeSeconds,
        config_.smokeLifetimeSeconds,
        config_.screenShakeLifetimeSeconds});
    std::erase_if(
        activeShots_,
        [maximumLifetime](const ActiveShotFeedback &shot)
        {
            return shot.ageSeconds >= maximumLifetime;
        });
}

void ShotFeedbackPresentationState::reset() noexcept
{
    activeShots_.clear();
}

std::vector<ShotFeedbackPresentationSnapshot>
ShotFeedbackPresentationState::snapshots() const
{
    std::vector<ShotFeedbackPresentationSnapshot> result;
    result.reserve(activeShots_.size());
    for (const ActiveShotFeedback &shot : activeShots_)
    {
        const float flashRemaining = remainingFraction(
            shot.ageSeconds,
            config_.muzzleFlashLifetimeSeconds);
        const float smokeProgress = std::clamp(
            shot.ageSeconds / config_.smokeLifetimeSeconds,
            0.0F,
            1.0F);
        const float smokeRemaining = 1.0F - smokeProgress;
        result.push_back(
            ShotFeedbackPresentationSnapshot{
                shot.shotId,
                shot.origin,
                shot.direction,
                flashRemaining * flashRemaining,
                config_.maximumSmokeOpacity *
                    smokeRemaining * smokeRemaining,
                smokeProgress});
    }
    return result;
}

Vec2 ShotFeedbackPresentationState::normalizedScreenShakeOffset() const noexcept
{
    Vec2 offset{};
    for (const ActiveShotFeedback &shot : activeShots_)
    {
        const float remaining = remainingFraction(
            shot.ageSeconds,
            config_.screenShakeLifetimeSeconds);
        if (remaining <= 0.0F)
        {
            continue;
        }
        const float envelope = remaining * remaining;
        const float seedPhase =
            static_cast<float>(shot.shotId % 29U) * 1.731F;
        const float phase = seedPhase + shot.ageSeconds * 140.0F;
        offset.x += std::cos(phase) * envelope;
        offset.y += std::sin(phase) * envelope * 0.72F;
    }

    const float length = std::hypot(offset.x, offset.y);
    if (length > 1.0F)
    {
        offset.x /= length;
        offset.y /= length;
    }
    return offset;
}

std::size_t ShotFeedbackPresentationState::activeShotCount() const noexcept
{
    return activeShots_.size();
}
