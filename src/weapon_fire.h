#pragma once

#include <cstdint>
#include <optional>

#include "stable_random.h"
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
    std::uint64_t spreadSeed{0x737072656164ULL};
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

    void reconfigure(WeaponFireConfig config);

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
    Pcg32 random_;
    std::uint32_t burstShotCount_{};

    void recover(float deltaTime, float targetSpread) noexcept;
    [[nodiscard]] float nextSignedUnit() noexcept;
};
