#include "base_world.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "collision.h"

namespace
{
float distanceSquaredToRect(Vec2 point, const Rect &rect) noexcept
{
    const float nearestX = std::clamp(
        point.x,
        rect.position.x,
        rect.position.x + rect.size.x);
    const float nearestY = std::clamp(
        point.y,
        rect.position.y,
        rect.position.y + rect.size.y);
    const float dx = point.x - nearestX;
    const float dy = point.y - nearestY;
    return dx * dx + dy * dy;
}

AnimationClip makeBasePlayerMoveClip()
{
    return AnimationClip{
        std::vector<AnimationFrame>(6, AnimationFrame{0.10F})};
}

float resolveHorizontalMovement(
    Vec2 position,
    Vec2 size,
    float desiredX,
    const RaidSpaceBlockerIndex &index,
    const std::vector<std::size_t> &candidates) noexcept
{
    float resolvedX = desiredX;
    for (const std::size_t candidate : candidates)
    {
        resolvedX = resolveHorizontalCollision(
            Rect{position, size},
            resolvedX,
            index.blockerBounds(candidate));
    }
    return resolvedX;
}

float resolveVerticalMovement(
    Vec2 position,
    Vec2 size,
    float desiredY,
    const RaidSpaceBlockerIndex &index,
    const std::vector<std::size_t> &candidates) noexcept
{
    float resolvedY = desiredY;
    for (const std::size_t candidate : candidates)
    {
        resolvedY = resolveVerticalCollision(
            Rect{position, size},
            resolvedY,
            index.blockerBounds(candidate));
    }
    return resolvedY;
}

bool finiteRect(ContentRect bounds) noexcept
{
    return std::isfinite(bounds.position.x) &&
        std::isfinite(bounds.position.y) &&
        std::isfinite(bounds.size.x) && std::isfinite(bounds.size.y) &&
        bounds.size.x > 0.0F && bounds.size.y > 0.0F;
}
}

BaseWorld::BaseWorld()
    : playerMovementAnimator_{
          makeBasePlayerMoveClip(),
          AnimationPlayMode::Loop}
{
    rebuildSite("regional_base_site.greyline_yard");
}

void BaseWorld::configureSite(std::string_view siteDefinitionId)
{
    const std::string normalized = siteDefinitionId.empty()
        ? "regional_base_site.greyline_yard" : std::string{siteDefinitionId};
    if (normalized != siteDefinitionId_)
        rebuildSite(normalized);
}

void BaseWorld::rebuildSite(std::string_view siteDefinitionId)
{
    siteDefinitionId_ = siteDefinitionId;
    layout_ = generateHomeRegionLayout(siteDefinitionId_);
    walkableBounds_ = Rect{{24.0F, 24.0F},
                           {layout_.worldSize.x - 48.0F,
                            layout_.worldSize.y - 48.0F}};
    const Vec2 origin = layout_.baseParcel.position;
    facilities_ = {
        BaseFacility{BaseFacilityKind::Storage,
                     {{origin.x + 80.0F, origin.y + 220.0F},
                      {300.0F, 220.0F}}, 72.0F},
        BaseFacility{BaseFacilityKind::Supply,
                     {{origin.x + 1220.0F, origin.y + 220.0F},
                      {300.0F, 220.0F}}, 72.0F},
        BaseFacility{BaseFacilityKind::Allocation,
                     {{origin.x + 80.0F, origin.y + 760.0F},
                      {300.0F, 180.0F}}, 72.0F},
        BaseFacility{BaseFacilityKind::Medical,
                     {{origin.x + 1220.0F, origin.y + 760.0F},
                      {300.0F, 180.0F}}, 72.0F},
        BaseFacility{BaseFacilityKind::Dormitory,
                     {{origin.x + 460.0F, origin.y + 790.0F},
                      {270.0F, 150.0F}}, 72.0F},
        BaseFacility{BaseFacilityKind::Workshop,
                     {{origin.x + 870.0F, origin.y + 520.0F},
                      {270.0F, 170.0F}}, 72.0F},
        BaseFacility{BaseFacilityKind::RaidGate,
                     {{origin.x + 650.0F, origin.y + 40.0F},
                      {300.0F, 130.0F}}, 82.0F}};
    rebuildCollisionIndex();
    presentationCache_ = {};
    presentationCacheValid_ = false;
    resetAtMedicalPoint();
    shooting_.clearSpatialTransientPresentation();
    shooting_.reanchor(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        playerFacingDirection_,
        layout_.worldSize);
}

void BaseWorld::rebuildCollisionIndex()
{
    movementBlockers_.clear();
    BallisticBlockerId id{1U};
    movementBlockers_.reserve(layout_.movementBlockers.size() +
                              facilities_.size());
    for (const ContentRect &bounds : layout_.movementBlockers)
        movementBlockers_.push_back(BallisticBlocker{
            id++, Rect{bounds.position, bounds.size}});
    for (const BaseFacility &facility : facilities_)
        movementBlockers_.push_back(BallisticBlocker{id++, facility.bounds});
    movementBlockerIndex_ = RaidSpaceBlockerIndex::build(
        layout_.worldSize, movementBlockers_, 320.0F);
    if (!movementBlockerIndex_.has_value())
        throw std::logic_error{"Home Region blocker index is invalid"};
}

std::optional<BaseFacilityKind> BaseWorld::update(
    const BaseInput &input,
    float deltaTime)
{
    shooting_.beginFrame(deltaTime);
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        Vec2 direction{
            static_cast<float>(input.moveRight) -
                static_cast<float>(input.moveLeft),
            static_cast<float>(input.moveDown) -
                static_cast<float>(input.moveUp)};
        const float lengthSquared =
            direction.x * direction.x + direction.y * direction.y;
        if (lengthSquared > 0.0F)
        {
            const bool wasMoving = playerIsMoving_;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            direction.x *= inverseLength;
            direction.y *= inverseLength;
            if (direction.x != 0.0F)
            {
                playerHorizontalFacing_ = direction.x;
            }
            playerFacingDirection_ = direction;
            if (playerFacingDirection_.x == 0.0F)
            {
                playerFacingDirection_.x = playerHorizontalFacing_;
            }
            playerIsMoving_ = true;
            if (!wasMoving)
            {
                playerMovementAnimator_.reset();
            }
            playerMovementAnimator_.update(deltaTime);
            const float speed = (input.sprint ? 280.0F : 180.0F) *
                std::clamp(input.movementSpeedMultiplier, 0.0F, 1.0F);
            const float maximumY = walkableBounds_.position.y +
                walkableBounds_.size.y - playerSize_.y;
            const float maximumPlayerX = walkableBounds_.position.x +
                walkableBounds_.size.x - playerSize_.x;

            const float desiredX = std::clamp(
                playerPosition_.x + direction.x * speed * deltaTime,
                walkableBounds_.position.x,
                maximumPlayerX);
            const float desiredYForQuery = std::clamp(
                playerPosition_.y + direction.y * speed * deltaTime,
                walkableBounds_.position.y,
                maximumY);
            const Rect queryBounds{
                {std::min(playerPosition_.x, desiredX),
                 std::min(playerPosition_.y, desiredYForQuery)},
                {std::abs(desiredX - playerPosition_.x) + playerSize_.x,
                 std::abs(desiredYForQuery - playerPosition_.y) +
                     playerSize_.y}};
            movementBlockerIndex_->queryCandidateIndices(
                queryBounds, movementCandidates_);
            playerPosition_.x = resolveHorizontalMovement(
                playerPosition_,
                playerSize_,
                desiredX,
                *movementBlockerIndex_,
                movementCandidates_);

            const float desiredY = std::clamp(
                playerPosition_.y + direction.y * speed * deltaTime,
                walkableBounds_.position.y,
                maximumY);
            playerPosition_.y = resolveVerticalMovement(
                playerPosition_,
                playerSize_,
                desiredY,
                *movementBlockerIndex_,
                movementCandidates_);
        }
        else
        {
            playerIsMoving_ = false;
            playerMovementAnimator_.reset();
        }
    }

    const Vec2 playerCenter{
        playerPosition_.x + playerSize_.x * 0.5F,
        playerPosition_.y + playerSize_.y * 0.5F};
    const bool pointerAiming = input.aimWorldPosition.has_value() ||
        input.aimMotionDelta.has_value();
    if (pointerAiming)
    {
        shooting_.updateAim(
            input,
            playerCenter,
            playerFacingDirection_,
            layout_.worldSize,
            deltaTime);
        playerFacingDirection_ = shooting_.aimDirection();
        if (playerFacingDirection_.x != 0.0F)
        {
            playerHorizontalFacing_ = playerFacingDirection_.x;
        }
    }
    else
    {
        shooting_.reanchor(
            playerCenter, playerFacingDirection_, layout_.worldSize);
    }
    static_cast<void>(shooting_.advanceShots(
        input,
        deltaTime,
        playerCenter,
        std::max(playerSize_.x, playerSize_.y),
        playerIsMoving_,
        false,
        layout_.worldSize,
        noCombatTargets_,
        movementBlockers_));

    if (input.interactJustPressed)
    {
        return interactableFacility();
    }
    return std::nullopt;
}

Vec2 BaseWorld::playerPosition() const noexcept
{
    return playerPosition_;
}

Vec2 BaseWorld::playerSize() const noexcept
{
    return playerSize_;
}

Vec2 BaseWorld::playerFacingDirection() const noexcept
{
    return playerFacingDirection_;
}

bool BaseWorld::playerIsMoving() const noexcept
{
    return playerIsMoving_;
}

std::size_t BaseWorld::playerAnimationFrame() const noexcept
{
    return playerMovementAnimator_.currentFrameIndex();
}

const std::array<BaseFacility, 7> &BaseWorld::facilities() const noexcept
{
    return facilities_;
}

void BaseWorld::configureWeaponFire(const WeaponUseDefinition &definition)
{
    shooting_.configureWeapon(definition);
}

void BaseWorld::configureWeaponFire(
    const WeaponUseDefinition &definition,
    const WeaponHandlingParameters &handling,
    bool preserveWeaponFireTransientState)
{
    shooting_.configureWeapon(
        definition, handling, preserveWeaponFireTransientState);
}

void BaseWorld::configureWeaponAmmunition(int penetration) noexcept
{
    shooting_.configureAmmunition(penetration);
}

std::vector<ShotPresentationSnapshot>
BaseWorld::shotPresentationSnapshots() const
{
    return shooting_.shotPresentationSnapshots();
}

std::vector<ShotFeedbackPresentationSnapshot>
BaseWorld::shotFeedbackPresentationSnapshots() const
{
    return shooting_.shotFeedbackPresentationSnapshots();
}

const std::vector<Particle> &BaseWorld::particles() const noexcept
{
    return shooting_.particles();
}

const std::vector<HitResult> &
BaseWorld::hitResultsLastUpdate() const noexcept
{
    return shooting_.hitResultsLastUpdate();
}

bool BaseWorld::shotFiredLastUpdate() const noexcept
{
    return shooting_.shotFiredLastUpdate();
}

WeaponAccuracyProjection BaseWorld::weaponAccuracyProjection() const noexcept
{
    return shooting_.accuracyProjection();
}

Vec2 BaseWorld::weaponAimWorldPosition() const noexcept
{
    return shooting_.aimWorldPosition();
}

Vec2 BaseWorld::weaponAimDirection() const noexcept
{
    return shooting_.aimDirection();
}

Vec2 BaseWorld::normalizedShotScreenShakeOffset() const noexcept
{
    return shooting_.normalizedScreenShakeOffset();
}

Vec2 BaseWorld::worldSize() const noexcept
{
    return layout_.worldSize;
}

const ContentRect &BaseWorld::baseParcel() const noexcept
{
    return layout_.baseParcel;
}

bool BaseWorld::canAccessStash() const noexcept
{
    return playerPosition_.x >= layout_.baseParcel.position.x &&
        playerPosition_.y >= layout_.baseParcel.position.y &&
        playerPosition_.x + playerSize_.x <=
            layout_.baseParcel.position.x + layout_.baseParcel.size.x &&
        playerPosition_.y + playerSize_.y <=
            layout_.baseParcel.position.y + layout_.baseParcel.size.y;
}

const HomeRegionLayout &BaseWorld::layout() const noexcept
{
    return layout_;
}

const HomeRegionPresentationProjection &BaseWorld::outdoorPresentation(
    ContentRect visibleWorldBounds) const
{
    if (!finiteRect(visibleWorldBounds))
    {
        presentationCache_ = {};
        presentationCacheValid_ = false;
        return presentationCache_;
    }
    const float chunkWorldSize = static_cast<float>(layout_.chunkSizeCells) *
        (layout_.worldSize.x / static_cast<float>(layout_.columns));
    const std::uint32_t chunkColumns =
        (layout_.columns + layout_.chunkSizeCells - 1U) /
        layout_.chunkSizeCells;
    const std::uint32_t chunkRows =
        (layout_.rows + layout_.chunkSizeCells - 1U) /
        layout_.chunkSizeCells;
    const std::uint32_t firstColumn = std::min(
        static_cast<std::uint32_t>(std::max(0.0F,
            visibleWorldBounds.position.x - 160.0F) / chunkWorldSize),
        chunkColumns - 1U);
    const std::uint32_t lastColumn = std::min(
        static_cast<std::uint32_t>((visibleWorldBounds.position.x +
            visibleWorldBounds.size.x + 160.0F) / chunkWorldSize),
        chunkColumns - 1U);
    const std::uint32_t firstRow = std::min(
        static_cast<std::uint32_t>(std::max(0.0F,
            visibleWorldBounds.position.y - 160.0F) / chunkWorldSize),
        chunkRows - 1U);
    const std::uint32_t lastRow = std::min(
        static_cast<std::uint32_t>((visibleWorldBounds.position.y +
            visibleWorldBounds.size.y + 160.0F) / chunkWorldSize),
        chunkRows - 1U);
    if (presentationCacheValid_ &&
        firstColumn == cachedFirstChunkColumn_ &&
        lastColumn == cachedLastChunkColumn_ &&
        firstRow == cachedFirstChunkRow_ && lastRow == cachedLastChunkRow_)
        return presentationCache_;

    cachedFirstChunkColumn_ = firstColumn;
    cachedLastChunkColumn_ = lastColumn;
    cachedFirstChunkRow_ = firstRow;
    cachedLastChunkRow_ = lastRow;
    presentationCacheValid_ = true;
    HomeRegionPresentationProjection next;
    next.cacheRevision = presentationCache_.cacheRevision + 1U;
    next.queriedChunkCount = static_cast<std::size_t>(
        lastColumn - firstColumn + 1U) * (lastRow - firstRow + 1U);
    const ContentRect query{
        {firstColumn * chunkWorldSize, firstRow * chunkWorldSize},
        {(lastColumn - firstColumn + 1U) * chunkWorldSize,
         (lastRow - firstRow + 1U) * chunkWorldSize}};
    const float cellWidth = layout_.worldSize.x /
        static_cast<float>(layout_.columns);
    const float cellHeight = layout_.worldSize.y /
        static_cast<float>(layout_.rows);
    for (const RaidTerrainSpan &span : layout_.terrainSpans)
    {
        const ContentRect bounds{{span.firstColumn * cellWidth,
                                  span.row * cellHeight},
                                 {span.length * cellWidth, cellHeight}};
        if (isCollision(Rect{bounds.position, bounds.size},
                        Rect{query.position, query.size}))
            next.terrainSpans.push_back(span);
    }
    for (const RaidOutdoorRoadCell &road : layout_.roadCells)
    {
        const ContentRect bounds{{road.column * cellWidth,
                                  road.row * cellHeight},
                                 {cellWidth, cellHeight}};
        if (isCollision(Rect{bounds.position, bounds.size},
                        Rect{query.position, query.size}))
            next.roadCells.push_back(road);
    }
    for (const RaidOutdoorPropSnapshot &prop : layout_.props)
        if (isCollision(Rect{prop.bounds.position, prop.bounds.size},
                        Rect{query.position, query.size}))
            next.props.push_back(prop);
    for (const HomeRegionDistrictSnapshot &district : layout_.districts)
        if (isCollision(Rect{district.bounds.position, district.bounds.size},
                        Rect{query.position, query.size}))
            next.districts.push_back(district);
    presentationCache_ = std::move(next);
    return presentationCache_;
}

std::optional<BaseFacilityKind> BaseWorld::interactableFacility() const noexcept
{
    const Vec2 center{
        playerPosition_.x + playerSize_.x / 2.0F,
        playerPosition_.y + playerSize_.y / 2.0F};
    for (const BaseFacility &facility : facilities_)
    {
        if (distanceSquaredToRect(center, facility.bounds) <=
            facility.interactionRange * facility.interactionRange)
        {
            return facility.kind;
        }
    }
    return std::nullopt;
}

void BaseWorld::resetAtRaidGate() noexcept
{
    const BaseFacility &gate = facilities_.back();
    playerPosition_ = Vec2{
        gate.bounds.position.x + gate.bounds.size.x * 0.5F -
            playerSize_.x * 0.5F,
        gate.bounds.position.y + gate.bounds.size.y + 28.0F};
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
    shooting_.reanchor(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        playerFacingDirection_,
        layout_.worldSize);
}

void BaseWorld::resetAtMedicalPoint() noexcept
{
    const Vec2 origin = layout_.baseParcel.position;
    playerPosition_ = Vec2{origin.x + 780.0F, origin.y + 900.0F};
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
    shooting_.reanchor(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        playerFacingDirection_,
        layout_.worldSize);
}

const char *baseFacilityName(BaseFacilityKind kind) noexcept
{
    switch (kind)
    {
    case BaseFacilityKind::Storage:
        return "STORAGE & LOADOUT";
    case BaseFacilityKind::Supply:
        return "SUPPLY & RECOVERY";
    case BaseFacilityKind::Allocation:
        return "ALLOCATION & NEEDS";
    case BaseFacilityKind::Medical:
        return "MEDICAL SERVICE";
    case BaseFacilityKind::Dormitory:
        return "DORMITORY & REST";
    case BaseFacilityKind::Workshop:
        return "WORKSHOP & PRODUCTION";
    case BaseFacilityKind::RaidGate:
        return "RAID DEPLOYMENT";
    }
    return "UNKNOWN";
}
