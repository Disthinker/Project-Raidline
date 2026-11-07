#pragma once

#include <cstdint>
#include <optional>

#include "stable_random.h"
#include "vec2.h"

enum class WeaponFireMode;

struct WeaponTriggerIntent
{
    bool pressed{};
    bool justPressed{};
};

// Input-sequence policy only. The Profile remains the mode authority and the
// existing WeaponFireState remains the cadence/spread/random authority.
// Restoring an activity intentionally does not restore a held trigger.
class WeaponTriggerGate
{
public:
    [[nodiscard]] WeaponTriggerIntent prepare(
        std::uint64_t weaponId, WeaponFireMode mode, unsigned burstCount,
        bool held, bool justPressed, bool controlsAvailable) noexcept;
    void acceptedShot() noexcept;
    void cancelPending() noexcept;
    void cancelUntilRelease() noexcept;
    void reset() noexcept;
    [[nodiscard]] unsigned remainingBurstShots() const noexcept { return remaining_; }

private:
    std::uint64_t weaponId_{};
    std::optional<WeaponFireMode> mode_;
    unsigned remaining_{};
    bool suppressedUntilRelease_{};
    bool triggerWasHeld_{};
};

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
    friend class WorldShootingRuntime;
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
