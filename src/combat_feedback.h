#pragma once

#include "enemy_attack.h"
#include "vec2.h"

struct CombatFeedbackConfig
{
    float muzzleFlashDuration{0.055F};
    float hitConfirmDuration{0.12F};
    float playerDamagePulseDuration{0.18F};
    float scratchPulseStrength{0.55F};
    float bitePulseStrength{1.0F};
};

class CombatFeedbackState
{
public:
    CombatFeedbackState();
    explicit CombatFeedbackState(CombatFeedbackConfig config);

    void update(float deltaTime) noexcept;

    [[nodiscard]]
    bool recordShot(Vec2 origin, Vec2 direction) noexcept;

    void recordEnemyHit() noexcept;

    [[nodiscard]]
    bool recordPlayerHit(EnemyAttackType type) noexcept;

    void reset() noexcept;

    [[nodiscard]] float muzzleFlashIntensity() const noexcept;
    [[nodiscard]] float hitConfirmIntensity() const noexcept;
    [[nodiscard]] float playerDamagePulseIntensity() const noexcept;
    [[nodiscard]] Vec2 muzzleOrigin() const noexcept;
    [[nodiscard]] Vec2 muzzleDirection() const noexcept;

private:
    CombatFeedbackConfig config_;
    float muzzleFlashRemaining_{};
    float hitConfirmRemaining_{};
    float playerDamagePulseRemaining_{};
    float playerDamagePulseStrength_{};
    Vec2 muzzleOrigin_{};
    Vec2 muzzleDirection_{};
};
