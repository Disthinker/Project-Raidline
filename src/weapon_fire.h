#pragma once

#include <cstdint>
#include <optional>

#include "vec2.h"

struct WeaponFireConfig
{
    float shotInterval{0.12F};
    float minimumSpreadDegrees{};
    float maximumSpreadDegrees{6.0F};
    float spreadPerShotDegrees{1.0F};
    float recoveryDelay{0.10F};
    float spreadRecoveryDegreesPerSecond{12.0F};
    float aimDownSightsAccuracyMultiplier{0.55F};
    float aimDownSightsStabilityMultiplier{0.70F};
    float movingSpreadFraction{0.35F};
    std::uint32_t spreadSeed{0x6D2B79F5U};
};

struct WeaponFireContext
{
    bool moving{};
    float aimDownSightsProgress{};
    float rangeSpreadFactor{};
    bool forceMaximumSpread{};
};

struct ShotSpec
{
    Vec2 direction{};
    float spreadOffsetDegrees{};
};

// SDL-independent cadence and accuracy state. Center-direction tracking and
// persistent recoil live in WeaponAimState; this class owns only spread.
class WeaponFireState
{
public:
    WeaponFireState();
    explicit WeaponFireState(WeaponFireConfig config);

    [[nodiscard]] std::optional<ShotSpec> update(
        bool triggerPressed,
        Vec2 baseAimDirection,
        float deltaTime,
        WeaponFireContext context = {});

    [[nodiscard]] float spreadDegrees() const noexcept;
    [[nodiscard]] float cooldownRemaining() const noexcept;

private:
    WeaponFireConfig config_;
    float cooldownRemaining_{};
    float spreadDegrees_{};
    float recoveryDelayRemaining_{};
    std::uint32_t randomState_{};
    std::uint32_t burstShotCount_{};

    void recover(float deltaTime, float targetSpread) noexcept;
    [[nodiscard]] float nextSignedUnit() noexcept;
};
