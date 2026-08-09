#pragma once

#include <cstddef>
#include <optional>

#include "enemy_attack.h"

struct EnemyAttackPresentationSample
{
    bool usesAttackSheet{};
    std::size_t frameIndex{};
    float phaseProgress{};
    float emphasis{};
};

// Pure presentation sampling over the EnemyAttackState-owned clock.
// This layer never advances time and is safe to call once or many times per
// rendered frame.
[[nodiscard]]
EnemyAttackPresentationSample sampleEnemyAttackPresentation(
    std::optional<EnemyAttackType> type,
    EnemyAttackPhase phase,
    float phaseRemaining,
    std::optional<EnemyAttackConfig> config) noexcept;
