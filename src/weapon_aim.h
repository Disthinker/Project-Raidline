#pragma once

#include "vec2.h"

struct WeaponAimConfig
{
    float maximumFollowDegreesPerSecond{450.0F};
    float recoilDegreesPerShot{};
    float aimDownSightsDurationSeconds{0.25F};
    float effectiveRange{500.0F};
    float maximumRange{750.0F};
};

// SDL-independent transient weapon-direction state. The raw pointer supplies a
// desired direction, but shots and App projections consume actualDirection().
// Recoil is a persistent angular offset: time alone never recenters it.
class WeaponAimState
{
public:
    WeaponAimState();
    explicit WeaponAimState(WeaponAimConfig config);

    void update(
        Vec2 desiredDirection,
        float desiredDistance,
        bool aimDownSights,
        float deltaTime) noexcept;

    void applyShotRecoil() noexcept;

    [[nodiscard]] Vec2 actualDirection() const noexcept;
    [[nodiscard]] float aimDistance() const noexcept;
    [[nodiscard]] float aimDownSightsProgress() const noexcept;
    [[nodiscard]] bool beyondMaximumRange() const noexcept;
    [[nodiscard]] float rangeSpreadFactor() const noexcept;
    [[nodiscard]] float damageMultiplier() const noexcept;
    [[nodiscard]] float recoilOffsetDegrees() const noexcept;

private:
    WeaponAimConfig config_;
    float trackedAngleDegrees_{};
    float desiredAngleDegrees_{};
    float aimDistance_{};
    float recoilOffsetDegrees_{};
    float aimDownSightsProgress_{};
    bool initialized_{};
};
