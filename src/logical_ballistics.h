#pragma once

#include "shot_resolution.h"

struct LogicalBallisticAdvance
{
    Vec2 start{};
    Vec2 end{};
    bool reachedImpact{};
};

// A value-only record of a shot that is still travelling. It is not a scene
// entity: it has no sprite, transform component, collision body or ownership
// identity. GameplayWorld advances it and resolves the travelled segment.
class LogicalBallisticFlight
{
public:
    explicit LogicalBallisticFlight(
        const ShotResolution &resolution,
        TracerStyle tracerStyle = TracerStyle::Weak,
        float tracerLength = 30.0F,
        float tracerOpacity = 0.42F,
        float tracerLifetimeSeconds = 0.055F);

    [[nodiscard]] LogicalBallisticAdvance advance(float deltaTime) noexcept;

    [[nodiscard]] ShotId shotId() const noexcept;
    [[nodiscard]] Vec2 origin() const noexcept;
    [[nodiscard]] Vec2 currentPosition() const noexcept;
    [[nodiscard]] Vec2 direction() const noexcept;
    [[nodiscard]] Vec2 impactPosition() const noexcept;
    [[nodiscard]] float speed() const noexcept;
    [[nodiscard]] float collisionExtent() const noexcept;
    [[nodiscard]] float distanceTravelled() const noexcept;
    [[nodiscard]] float maximumDistance() const noexcept;
    [[nodiscard]] int damage() const noexcept;
    [[nodiscard]] int penetration() const noexcept;
    [[nodiscard]] std::optional<ShotAimIntent> aimIntent() const noexcept;
    [[nodiscard]] bool reachedImpact() const noexcept;
    [[nodiscard]] TracerStyle tracerStyle() const noexcept;
    [[nodiscard]] float tracerLength() const noexcept;
    [[nodiscard]] float tracerOpacity() const noexcept;
    [[nodiscard]] float tracerLifetimeSeconds() const noexcept;

private:
    ShotId shotId_{kInvalidShotId};
    Vec2 origin_{};
    Vec2 currentPosition_{};
    Vec2 direction_{};
    Vec2 impactPosition_{};
    float speed_{};
    float collisionExtent_{};
    float distanceTravelled_{};
    float maximumDistance_{};
    int damage_{};
    int penetration_{};
    std::optional<ShotAimIntent> aimIntent_;
    TracerStyle tracerStyle_{TracerStyle::Weak};
    float tracerLength_{30.0F};
    float tracerOpacity_{0.42F};
    float tracerLifetimeSeconds_{0.055F};
};
