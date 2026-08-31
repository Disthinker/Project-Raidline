#pragma once

#include <cstddef>
#include <vector>

#include "enemy.h"
#include "gameplay_input.h"
#include "hit_resolution.h"
#include "item_definition.h"
#include "logical_ballistics.h"
#include "particle_system.h"
#include "shot_feedback_presentation.h"
#include "weapon_aim.h"
#include "weapon_fire.h"

struct WeaponAccuracyProjection
{
    Vec2 center{};
    float aimDistance{};
    float currentSpreadDegrees{};
    float minimumSpreadDegrees{};
    float maximumSpreadDegrees{};
    float worldRadius{};
    float reticleRadius{};
    bool beyondEffectiveRange{};
};

struct WorldShootingAdvance
{
    std::vector<std::size_t> removedTargetIndices;
    std::size_t targetsKilled{};
};

// SDL-free shooting capability shared by mutually exclusive Base and Raid
// activities. It owns aiming, spread, non-entity logical flight and read-only
// feedback projections. Activity lifecycle, assets, ammo transactions and
// target ownership remain with GameSession and the consuming world.
class WorldShootingRuntime
{
public:
    WorldShootingRuntime();

    void beginFrame(float deltaTime) noexcept;

    void updateAim(
        const GameplayInput &input,
        Vec2 shooterCenter,
        Vec2 fallbackDirection,
        Vec2 worldSize,
        float deltaTime,
        bool allowPointerInput = true) noexcept;

    [[nodiscard]] WorldShootingAdvance advanceShots(
        const GameplayInput &input,
        float deltaTime,
        Vec2 shooterCenter,
        float shooterExtent,
        bool shooterMoving,
        bool controlsSuppressed,
        Vec2 worldSize,
        std::vector<Enemy> &targets,
        const std::vector<BallisticBlocker> &blockers);

    void configureWeapon(
        const WeaponUseDefinition &definition,
        const WeaponHandlingParameters &handling,
        bool preserveTransientState);
    void configureWeapon(const WeaponUseDefinition &definition);
    void configureAmmunition(int penetration) noexcept;
    void reanchor(Vec2 shooterCenter, Vec2 direction, Vec2 worldSize) noexcept;
    void clearSpatialTransientPresentation() noexcept;

    [[nodiscard]] const std::vector<LogicalBallisticFlight> &
    logicalBallistics() const noexcept;
    [[nodiscard]] std::vector<ShotPresentationSnapshot>
    shotPresentationSnapshots() const;
    [[nodiscard]] std::vector<ShotFeedbackPresentationSnapshot>
    shotFeedbackPresentationSnapshots() const;
    [[nodiscard]] Vec2 normalizedScreenShakeOffset() const noexcept;
    [[nodiscard]] const std::vector<Particle> &particles() const noexcept;
    [[nodiscard]] const std::vector<HitResult> &
    hitResultsLastUpdate() const noexcept;
    [[nodiscard]] bool shotFiredLastUpdate() const noexcept;
    [[nodiscard]] float spreadDegrees() const noexcept;
    [[nodiscard]] float visualRecoilPixels() const noexcept;
    [[nodiscard]] WeaponAccuracyProjection accuracyProjection() const noexcept;
    [[nodiscard]] Vec2 aimWorldPosition() const noexcept;
    [[nodiscard]] Vec2 aimDirection() const noexcept;
    [[nodiscard]] float aimDownSightsProgress() const noexcept;
    [[nodiscard]] bool aimBeyondEffectiveRange() const noexcept;
    [[nodiscard]] bool aimBeyondMaximumRange() const noexcept;

private:
    struct TracerPresentationSegment
    {
        ShotId shotId{kInvalidShotId};
        Vec2 start{};
        Vec2 end{};
        Vec2 direction{};
        TracerStyle style{TracerStyle::Weak};
        float opacity{};
        float lifetimeSeconds{};
        float remainingSeconds{};
        float ageSeconds{};
    };

    static constexpr int kDefaultWeaponDamage{1};
    static constexpr float kShotExtent{8.0F};

    std::vector<LogicalBallisticFlight> logicalBallistics_;
    std::vector<TracerPresentationSegment> tracerPresentations_;
    ShotFeedbackPresentationState shotFeedbackPresentation_;
    ShotId nextShotId_{1};
    WeaponFireState weaponFire_;
    WeaponAimState weaponAim_;
    int weaponBaseDamage_{kDefaultWeaponDamage};
    int weaponPenetration_{};
    float weaponMaximumRange_{2048.0F};
    float weaponLogicalBallisticSpeed_{6000.0F};
    TracerStyle weaponTracerStyle_{TracerStyle::Weak};
    float weaponTracerLength_{30.0F};
    float weaponTracerOpacity_{0.42F};
    float weaponTracerLifetimeSeconds_{0.055F};
    ParticleSystem particleSystem_;
    std::vector<HitResult> hitResultsLastUpdate_;
    bool shotFiredLastUpdate_{};
};
