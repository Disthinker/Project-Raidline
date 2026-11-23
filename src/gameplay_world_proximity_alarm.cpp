#include "gameplay_world.h"
#include "collision.h"

std::optional<RaidAlarmPlacement> GameplayWorld::previewProximityAlarm(
    Vec2 aim, const ProximityAlarmUseDefinition &use) const
{
    if (!grenadeEnabled_ || developerRange_ || !raidSession_.isActive() || player_.isDead() || player_.isControlled() ||
        !use.valid() || !std::isfinite(aim.x) || !std::isfinite(aim.y) || !proximityAlarms_.canPlace() ||
        grenade_.active() || noiseDecoy_.flight() || thrownIllumination_.flight() || smoke_.flight() ||
        fireZones_.flight() || melee_.recoverySeconds() > 0) return std::nullopt;
    const Vec2 p = player_.position();
    const float half = player_.size() * 0.5F;
    const Vec2 origin{p.x + half, p.y + half};
    const Vec2 world = activeWorldSize();
    constexpr float deviceHalf = 4;
    const float dx = aim.x - origin.x, dy = aim.y - origin.y;
    const Rect footprint{{aim.x - deviceHalf, aim.y - deviceHalf}, {2 * deviceHalf, 2 * deviceHalf}};
    if (dx * dx + dy * dy > use.maximumRange * use.maximumRange || aim.x < deviceHalf || aim.y < deviceHalf ||
        aim.x > world.x - deviceHalf || aim.y > world.y - deviceHalf ||
        !activeBlockerIndex().hasLineOfSight(origin, aim)) return std::nullopt;

    const auto touchesPortal = [&](const ContentRect &portal) {
        const Rect door{portal.position, portal.size};
        const bool startsInside = origin.x >= door.position.x && origin.x <= door.position.x + door.size.x &&
            origin.y >= door.position.y && origin.y <= door.position.y + door.size.y;
        return isCollision(footprint, door) || (!startsInside && raidSpaceSegmentIntersectsBlockerInterior(origin, aim, door));
    };
    if (activeInteriorIndex_ ? touchesPortal(interiors_[*activeInteriorIndex_].interiorExit) :
        std::ranges::any_of(interiors_, [&](const auto &room) { return touchesPortal(room.exteriorEntrance); }))
        return std::nullopt;
    std::vector<std::size_t> nearWalls;
    activeBlockerIndex().queryCandidateIndices(footprint, nearWalls);
    for (const auto index : nearWalls)
        if (isCollision(footprint, activeBlockerIndex().blockerBounds(index))) return std::nullopt;
    RaidAlarmPlacement placement{aim, activeRaidSpaceId(), use};
    return proximityAlarms_.canPlaceAt(placement) ? std::optional{std::move(placement)} : std::nullopt;
}
