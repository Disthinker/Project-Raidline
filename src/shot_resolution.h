#pragma once

#include <cstdint>

#include "vec2.h"

using ShotId = std::uint64_t;

inline constexpr ShotId kInvalidShotId{0};

struct ShotCommand
{
    ShotId shotId{kInvalidShotId};
    Vec2 origin{};
    Vec2 direction{};
    float speed{};
    float collisionExtent{};
    int damage{};
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
};

// A domain result produced at successful fire time. The current V0 adapter
// converts accepted results into legacy Projectile objects, but weapon,
// damage, persistence and UI contracts consume these shot-domain values.
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

    [[nodiscard]]
    bool accepted() const noexcept;
};

enum class HitTargetKind
{
    Enemy,
    World,
};

// Damage and presentation consumers receive this result instead of
// inspecting a Projectile. Hit regions and weak points can extend this value
// later without being inferred by App.
struct HitResult
{
    ShotId shotId{kInvalidShotId};
    HitTargetKind targetKind{HitTargetKind::World};
    Vec2 position{};
    int damageApplied{};
    bool targetKilled{false};
};

// Read-only App projection for the temporary V0 shot presentation. It is not
// a persistent asset and carries no collision or damage authority.
struct ShotPresentationSnapshot
{
    ShotId shotId{kInvalidShotId};
    Vec2 center{};
    Vec2 direction{};
};

[[nodiscard]]
ShotResolution resolveShotCommand(
    const ShotCommand &command) noexcept;

[[nodiscard]]
const char *shotResolutionStatusName(
    ShotResolutionStatus status) noexcept;
