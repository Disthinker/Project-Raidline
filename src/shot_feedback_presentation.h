#pragma once

#include <cstddef>
#include <vector>

#include "shot_resolution.h"
#include "vec2.h"

struct ShotFeedbackPresentationConfig
{
    float muzzleFlashLifetimeSeconds{0.050F};
    float smokeLifetimeSeconds{0.220F};
    float screenShakeLifetimeSeconds{0.085F};
    float maximumSmokeOpacity{0.18F};
};

// SDL-free, read-only presentation data for one accepted shot. It carries no
// collision, damage, aiming, lighting, or persistence authority.
struct ShotFeedbackPresentationSnapshot
{
    ShotId shotId{kInvalidShotId};
    Vec2 origin{};
    Vec2 direction{};
    float muzzleFlashIntensity{};
    float smokeOpacity{};
    float smokeProgress{};
};

class ShotFeedbackPresentationState
{
public:
    ShotFeedbackPresentationState();
    explicit ShotFeedbackPresentationState(
        ShotFeedbackPresentationConfig config);

    [[nodiscard]] bool recordAcceptedShot(
        ShotId shotId,
        Vec2 origin,
        Vec2 direction) noexcept;

    void update(float deltaTime) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::vector<ShotFeedbackPresentationSnapshot>
    snapshots() const;

    // Normalized render-only camera displacement. App applies a deliberately
    // tiny pixel amplitude to the world viewport and never feeds it back into
    // aim, recoil, collision, or hit resolution.
    [[nodiscard]] Vec2 normalizedScreenShakeOffset() const noexcept;

    [[nodiscard]] std::size_t activeShotCount() const noexcept;

private:
    struct ActiveShotFeedback
    {
        ShotId shotId{kInvalidShotId};
        Vec2 origin{};
        Vec2 direction{};
        float ageSeconds{};
    };

    ShotFeedbackPresentationConfig config_;
    std::vector<ActiveShotFeedback> activeShots_;
};
