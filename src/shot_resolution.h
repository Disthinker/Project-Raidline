#pragma once

#include <cstdint>
#include <optional>

#include "combat_damage_domain.h"
#include "combat_target.h"
#include "vec2.h"

using ShotId = std::uint64_t;

inline constexpr ShotId kInvalidShotId{0};

// Frozen at successful fire time from the actual reticle point. A later
// collision can claim a special region only when it hits this same target and
// the physical impact remains in the aimed region.
struct ShotAimIntent
{
    CombatTargetId targetId{kInvalidCombatTargetId};
    HitRegion region{HitRegion::Torso};
    bool weakPoint{};
};

struct ShotCommand
{
    ShotId shotId{kInvalidShotId};
    Vec2 origin{};
    Vec2 direction{};
    float speed{};
    float collisionExtent{};
    int damage{};
    float maximumDistance{2048.0F};
    std::optional<ShotAimIntent> aimIntent;
};

enum class ShotResolutionStatus
{
    Accepted,
    RejectedInvalidShotId,
    RejectedInvalidOrigin,
    RejectedInvalidDirection,
    RejectedInvalidSpeed,
    RejectedInvalidCollisionExtent,
    RejectedInvalidDamage,
    RejectedInvalidMaximumDistance,
    RejectedInvalidAimIntent,
};

// A domain result produced at successful fire time. It freezes all values
// needed by the non-entity logical flight; later aim input cannot change it.
struct ShotResolution
{
    ShotResolutionStatus status{
        ShotResolutionStatus::RejectedInvalidShotId};
    ShotId shotId{kInvalidShotId};
    Vec2 origin{};
    Vec2 direction{};
    Vec2 velocity{};
    float collisionExtent{};
    int damage{};
    float maximumDistance{};
    Vec2 impactPosition{};
    std::optional<ShotAimIntent> aimIntent;

    [[nodiscard]]
    bool accepted() const noexcept;
};

enum class HitTargetKind
{
    Enemy,
    Obstacle,
    Ground,
};

enum class TracerStyle
{
    None,
    Weak,
};

// Damage and presentation consumers receive this result instead of inspecting
// a scene collision object. Hit semantics are never inferred by App.
struct HitResult
{
    ShotId shotId{kInvalidShotId};
    HitTargetKind targetKind{HitTargetKind::Ground};
    Vec2 position{};
    int damageApplied{};
    bool targetKilled{false};
    HitRegion region{HitRegion::Torso};
    HitSemantic semantic{HitSemantic::Normal};
};

// Read-only App projection for one short, already-travelled tracer segment.
// It is not a persistent asset and carries no collision or damage authority.
struct ShotPresentationSnapshot
{
    ShotId shotId{kInvalidShotId};
    Vec2 start{};
    Vec2 end{};
    Vec2 direction{};
    TracerStyle tracerStyle{TracerStyle::Weak};
    float tracerOpacity{0.42F};
};

[[nodiscard]]
ShotResolution resolveShotCommand(
    const ShotCommand &command) noexcept;

[[nodiscard]]
const char *shotResolutionStatusName(
    ShotResolutionStatus status) noexcept;
