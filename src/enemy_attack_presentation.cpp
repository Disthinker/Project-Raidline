#include "enemy_attack_presentation.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool isValidAttackType(EnemyAttackType type) noexcept
    {
        switch (type)
        {
        case EnemyAttackType::Grab:
        case EnemyAttackType::Scratch:
        case EnemyAttackType::Bite:
            return true;
        }

        return false;
    }

    std::optional<float> durationForPhase(
        EnemyAttackPhase phase,
        const EnemyAttackConfig &config) noexcept
    {
        switch (phase)
        {
        case EnemyAttackPhase::Windup:
            return config.windupDuration;
        case EnemyAttackPhase::Active:
            return config.activeDuration;
        case EnemyAttackPhase::Recovery:
            return config.recoveryDuration;
        case EnemyAttackPhase::Idle:
        case EnemyAttackPhase::OffBalance:
            return std::nullopt;
        }

        return std::nullopt;
    }

    std::size_t sampleFrameRange(
        std::size_t firstFrame,
        std::size_t frameCount,
        float progress) noexcept
    {
        const float clampedProgress =
            std::clamp(progress, 0.0F, 1.0F);
        const std::size_t offset = std::min(
            static_cast<std::size_t>(
                clampedProgress *
                static_cast<float>(frameCount)),
            frameCount - 1U);
        return firstFrame + offset;
    }
}

EnemyAttackPresentationSample sampleEnemyAttackPresentation(
    std::optional<EnemyAttackType> type,
    EnemyAttackPhase phase,
    float phaseRemaining,
    std::optional<EnemyAttackConfig> config) noexcept
{
    if (!type.has_value() ||
        !config.has_value() ||
        !isValidAttackType(*type) ||
        !std::isfinite(phaseRemaining) ||
        phaseRemaining < 0.0F)
    {
        return {};
    }

    const std::optional<float> duration =
        durationForPhase(phase, *config);
    if (!duration.has_value() ||
        !std::isfinite(*duration) ||
        *duration <= 0.0F)
    {
        return {};
    }

    const float progress = std::clamp(
        1.0F - phaseRemaining / *duration,
        0.0F,
        1.0F);

    EnemyAttackPresentationSample sample{};
    sample.usesAttackSheet = true;
    sample.phaseProgress = progress;

    switch (phase)
    {
    case EnemyAttackPhase::Windup:
        sample.frameIndex = sampleFrameRange(
            0U,
            *type == EnemyAttackType::Grab ? 3U : 2U,
            progress);
        sample.emphasis = 0.55F + 0.25F * progress;
        break;
    case EnemyAttackPhase::Active:
        if (*type == EnemyAttackType::Grab)
        {
            sample.frameIndex = sampleFrameRange(3U, 2U, progress);
        }
        else
        {
            sample.frameIndex = sampleFrameRange(2U, 3U, progress);
        }
        sample.emphasis = 1.0F;
        break;
    case EnemyAttackPhase::Recovery:
        sample.frameIndex = 5U;
        sample.emphasis = 0.75F * (1.0F - progress);
        break;
    case EnemyAttackPhase::Idle:
    case EnemyAttackPhase::OffBalance:
        return {};
    }

    sample.emphasis = std::clamp(sample.emphasis, 0.0F, 1.0F);
    return sample;
}
