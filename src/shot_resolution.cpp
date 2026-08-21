#include "shot_resolution.h"

#include <cmath>

namespace
{
    bool isFinite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) &&
               std::isfinite(value.y);
    }

    bool validHitRegion(HitRegion region) noexcept
    {
        switch (region)
        {
        case HitRegion::Head:
        case HitRegion::Torso:
        case HitRegion::Legs:
            return true;
        }
        return false;
    }
}

bool ShotResolution::accepted() const noexcept
{
    return status == ShotResolutionStatus::Accepted;
}

ShotResolution resolveShotCommand(
    const ShotCommand &command) noexcept
{
    ShotResolution result{};
    result.shotId = command.shotId;

    if (command.shotId == kInvalidShotId)
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidShotId;
        return result;
    }

    if (!isFinite(command.origin))
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidOrigin;
        return result;
    }

    if (!isFinite(command.direction))
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidDirection;
        return result;
    }

    const float directionLengthSquared =
        command.direction.x * command.direction.x +
        command.direction.y * command.direction.y;
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 0.0F)
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidDirection;
        return result;
    }

    if (!std::isfinite(command.speed) ||
        command.speed <= 0.0F)
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidSpeed;
        return result;
    }

    if (!std::isfinite(command.collisionExtent) ||
        command.collisionExtent <= 0.0F)
    {
        result.status = ShotResolutionStatus::
            RejectedInvalidCollisionExtent;
        return result;
    }

    if (command.damage <= 0)
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidDamage;
        return result;
    }

    if (command.aimIntent.has_value() &&
        (command.aimIntent->targetId == kInvalidCombatTargetId ||
         !validHitRegion(command.aimIntent->region)))
    {
        result.status =
            ShotResolutionStatus::RejectedInvalidAimIntent;
        return result;
    }

    if (!std::isfinite(command.maximumDistance) ||
        command.maximumDistance <= 0.0F)
    {
        result.status = ShotResolutionStatus::
            RejectedInvalidMaximumDistance;
        return result;
    }

    const float inverseDirectionLength =
        1.0F / std::sqrt(directionLengthSquared);
    result.status = ShotResolutionStatus::Accepted;
    result.origin = command.origin;
    result.direction = Vec2{
        command.direction.x * inverseDirectionLength,
        command.direction.y * inverseDirectionLength};
    result.velocity = Vec2{
        result.direction.x * command.speed,
        result.direction.y * command.speed};
    result.collisionExtent = command.collisionExtent;
    result.damage = command.damage;
    result.maximumDistance = command.maximumDistance;
    result.aimIntent = command.aimIntent;
    result.impactPosition = Vec2{
        result.origin.x +
            result.direction.x * result.maximumDistance,
        result.origin.y +
            result.direction.y * result.maximumDistance};
    if (!isFinite(result.impactPosition))
    {
        result.status = ShotResolutionStatus::
            RejectedInvalidMaximumDistance;
        return result;
    }
    return result;
}

const char *shotResolutionStatusName(
    ShotResolutionStatus status) noexcept
{
    switch (status)
    {
    case ShotResolutionStatus::Accepted:
        return "Accepted";
    case ShotResolutionStatus::RejectedInvalidShotId:
        return "RejectedInvalidShotId";
    case ShotResolutionStatus::RejectedInvalidOrigin:
        return "RejectedInvalidOrigin";
    case ShotResolutionStatus::RejectedInvalidDirection:
        return "RejectedInvalidDirection";
    case ShotResolutionStatus::RejectedInvalidSpeed:
        return "RejectedInvalidSpeed";
    case ShotResolutionStatus::RejectedInvalidCollisionExtent:
        return "RejectedInvalidCollisionExtent";
    case ShotResolutionStatus::RejectedInvalidDamage:
        return "RejectedInvalidDamage";
    case ShotResolutionStatus::RejectedInvalidMaximumDistance:
        return "RejectedInvalidMaximumDistance";
    case ShotResolutionStatus::RejectedInvalidAimIntent:
        return "RejectedInvalidAimIntent";
    }

    return "Unknown";
}
