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
    float sprintingSpreadFraction{0.55F};
    float reticleMotionSpreadDegreesPerSecond{5.0F};
    float reticleMotionSoftThreshold{120.0F};
    float reticleMotionFullSpeed{1800.0F};
    float nearDistanceSpreadScale{0.04F};
    float distanceBloomAtEffectiveRange{0.10F};
    float overEffectiveRangeSpreadMultiplier{1.50F};
    std::uint64_t spreadSeed{0x737072656164ULL};
};

struct WeaponFireContext
{
    bool moving{};
    bool sprinting{};
    float aimDownSightsProgress{};
    float aimDistance{};
    float distanceSpreadFactor{1.0F};
    float overEffectiveRangeFactor{};
    float reticleControlSpeed{};
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
    [[nodiscard]] float contextualMinimumSpreadDegrees() const noexcept;
    [[nodiscard]] float contextualMaximumSpreadDegrees() const noexcept;
    [[nodiscard]] float spreadPresentationFraction() const noexcept;
    [[nodiscard]] float spreadRadiusAtDistance(float distance) const noexcept;
    [[nodiscard]] float spreadDegreesAtDistance(float distance) const noexcept;
    [[nodiscard]] float cooldownRemaining() const noexcept;

private:
    WeaponFireConfig config_;
    float cooldownRemaining_{};
    float spreadDegrees_{};
    float contextualMinimumSpreadDegrees_{};
    float contextualMaximumSpreadDegrees_{};
    float recoveryDelayRemaining_{};
    float movementBloomFraction_{};
    float reticleMotionBloomFraction_{};
    float shotBloomFraction_{};
    float distanceBloomFraction_{};
    float combinedBloomFraction_{};
    float lastAimDownSightsProgress_{};
    float lastDistanceSpreadFactor_{1.0F};
    float lastOverEffectiveRangeFactor_{};
    Pcg32 random_;
    std::uint32_t burstShotCount_{};

    void updateContextualEnvelope(WeaponFireContext context) noexcept;
    void updateMovementAndMotionBloom(
        float deltaTime,
        WeaponFireContext context) noexcept;
    void recoverShotBloom(bool triggerPressed, float deltaTime) noexcept;
    void refreshSpread() noexcept;
    [[nodiscard]] float baseSpreadEnvelopeDegrees() const noexcept;
    [[nodiscard]] float bloomRecoveryFractionPerSecond() const noexcept;
    [[nodiscard]] float nextSignedUnit() noexcept;
};
