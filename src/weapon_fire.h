#pragma once

#include <cstdint>
#include <optional>

#include "vec2.h"

struct WeaponFireConfig
{
    float shotInterval{0.12F};
    float spreadPerShotDegrees{1.0F};
    float maximumSpreadDegrees{6.0F};
    float recoveryDelay{0.10F};
    float spreadRecoveryDegreesPerSecond{12.0F};
    float visualRecoilPerShot{3.0F};
    float maximumVisualRecoil{9.0F};
    float visualRecoilRecoveryPerSecond{45.0F};
    std::uint32_t spreadSeed{0x6D2B79F5U};
};

struct ShotSpec
{
    Vec2 direction{};
    float spreadOffsetDegrees{};
};

// SDL-independent state for one automatic weapon. It owns cadence, bloom and
// cosmetic recoil, and emits at most one value-only shot command per update.
class WeaponFireState
{
public:
    WeaponFireState();
    explicit WeaponFireState(WeaponFireConfig config);

    [[nodiscard]]
    std::optional<ShotSpec> update(
        bool triggerPressed,
        Vec2 baseAimDirection,
        float deltaTime);

    [[nodiscard]] float spreadDegrees() const noexcept;
    [[nodiscard]] float visualRecoilPixels() const noexcept;
    [[nodiscard]] float cooldownRemaining() const noexcept;

private:
    WeaponFireConfig config_;
    float cooldownRemaining_{};
    float spreadDegrees_{};
    float visualRecoilPixels_{};
    float recoveryDelayRemaining_{};
    std::uint32_t randomState_{};
    std::uint32_t burstShotCount_{};

    void recover(float deltaTime) noexcept;
    [[nodiscard]] float nextSignedUnit() noexcept;
};
