#pragma once

#include <cstdint>
#include <optional>

#include "stable_random.h"
#include "vec2.h"

struct WeaponAimConfig
{
    float maximumReticleSpeed{5000.0F};
    float controlAcceleration{9000.0F};
    float recoilInitialSpeed{420.0F};
    float recoilDeceleration{2200.0F};
    float recoilLateralRatio{0.30F};
    float recoilBendDurationSeconds{0.080F};
    float aimDownSightsDurationSeconds{0.25F};
    float effectiveRange{500.0F};
    float maximumRange{750.0F};
    std::uint64_t recoilSeed{0x7265636f696cULL};
};

// SDL-independent transient reticle kinematics. Pointer input supplies a target
// world position; shots and App projections consume the independently moving
// actual position. Control and recoil velocities remain separate so firing can
// refresh a bounded outward-then-bending recoil stroke without stacking an
// unbounded impulse.
class WeaponAimState
{
public:
    WeaponAimState();
    explicit WeaponAimState(WeaponAimConfig config);

    void update(
        Vec2 targetWorldPosition,
        Vec2 shootingOrigin,
        Vec2 worldSize,
        bool aimDownSights,
        float deltaTime,
        std::optional<Vec2> inputMotionDelta = std::nullopt) noexcept;

    void applyShotRecoil(Vec2 shootingOrigin) noexcept;
    void reconfigure(WeaponAimConfig config);

    [[nodiscard]] Vec2 actualWorldPosition() const noexcept;
    [[nodiscard]] Vec2 targetWorldPosition() const noexcept;
    [[nodiscard]] Vec2 actualDirection() const noexcept;
    [[nodiscard]] Vec2 controlVelocity() const noexcept;
    [[nodiscard]] Vec2 recoilVelocity() const noexcept;
    [[nodiscard]] Vec2 recoilPresentationVelocity() const noexcept;
    [[nodiscard]] float aimDistance() const noexcept;
    [[nodiscard]] float aimDownSightsProgress() const noexcept;
    [[nodiscard]] bool beyondMaximumRange() const noexcept;
    [[nodiscard]] float rangeSpreadFactor() const noexcept;
    [[nodiscard]] float damageMultiplier() const noexcept;
    [[nodiscard]] bool recoilActive() const noexcept;

private:
    WeaponAimConfig config_;
    Vec2 currentWorldPosition_{};
    Vec2 targetWorldPosition_{};
    Vec2 inputWorldPosition_{};
    Vec2 shootingOrigin_{};
    Vec2 worldSize_{};
    Vec2 controlVelocity_{};
    Vec2 recoilVelocity_{};
    Vec2 recoilTargetDirection_{1.0F, 0.0F};
    Vec2 lastDirection_{1.0F, 0.0F};
    float aimDownSightsProgress_{};
    float recoilBendRemainingSeconds_{};
    bool initialized_{};
    Pcg32 recoilRandom_;

    void advanceStep(float deltaTime) noexcept;
    [[nodiscard]] float nextSignedUnit() noexcept;
};
