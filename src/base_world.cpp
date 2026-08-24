#include "base_world.h"

#include <algorithm>
#include <cmath>
#include <vector>

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
        std::vector<AnimationFrame>(6, AnimationFrame{0.09F})};
}

bool overlaps(const Rect &first, const Rect &second) noexcept
{
    return first.position.x < second.position.x + second.size.x &&
        first.position.x + first.size.x > second.position.x &&
        first.position.y < second.position.y + second.size.y &&
        first.position.y + first.size.y > second.position.y;
}

bool collidesWithFacility(
    Vec2 position,
    Vec2 size,
    const std::array<BaseFacility, 4> &facilities) noexcept
{
    const Rect playerBounds{position, size};
    return std::any_of(
        facilities.begin(),
        facilities.end(),
        [&playerBounds](const BaseFacility &facility)
        {
            return overlaps(playerBounds, facility.bounds);
        });
}
}

BaseWorld::BaseWorld()
    : playerMovementAnimator_{
          makeBasePlayerMoveClip(),
          AnimationPlayMode::Loop},
      facilities_{
          BaseFacility{
              BaseFacilityKind::Storage,
              Rect{{76.0F, 176.0F}, {228.0F, 188.0F}},
              64.0F},
          BaseFacility{
              BaseFacilityKind::Supply,
              Rect{{976.0F, 176.0F}, {228.0F, 188.0F}},
              64.0F},
          BaseFacility{
              BaseFacilityKind::Allocation,
              Rect{{76.0F, 470.0F}, {228.0F, 140.0F}},
              64.0F},
          BaseFacility{
              BaseFacilityKind::RaidGate,
              Rect{{520.0F, 28.0F}, {240.0F, 104.0F}},
              72.0F}}
{
}

std::optional<BaseFacilityKind> BaseWorld::update(
    const BaseInput &input,
    float deltaTime) noexcept
{
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
            const float speed = input.sprint ? 280.0F : 180.0F;
            const float maximumY = walkableBounds_.position.y +
                walkableBounds_.size.y - playerSize_.y;
            const float maximumPlayerX = walkableBounds_.position.x +
                walkableBounds_.size.x - playerSize_.x;

            Vec2 candidate = playerPosition_;
            candidate.x = std::clamp(
                candidate.x + direction.x * speed * deltaTime,
                walkableBounds_.position.x,
                maximumPlayerX);
            if (!collidesWithFacility(candidate, playerSize_, facilities_))
            {
                playerPosition_.x = candidate.x;
            }

            candidate = playerPosition_;
            candidate.y = std::clamp(
                candidate.y + direction.y * speed * deltaTime,
                walkableBounds_.position.y,
                maximumY);
            if (!collidesWithFacility(candidate, playerSize_, facilities_))
            {
                playerPosition_.y = candidate.y;
            }
        }
        else
        {
            playerIsMoving_ = false;
            playerMovementAnimator_.reset();
        }
    }

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

const std::array<BaseFacility, 4> &BaseWorld::facilities() const noexcept
{
    return facilities_;
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
    playerPosition_ = Vec2{620.0F, 152.0F};
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
}

void BaseWorld::resetAtMedicalPoint() noexcept
{
    playerPosition_ = Vec2{620.0F, 600.0F};
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
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
    case BaseFacilityKind::RaidGate:
        return "RAID DEPLOYMENT";
    }
    return "UNKNOWN";
}
