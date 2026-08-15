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
            playerFacingDirection_ = direction;
            playerIsMoving_ = true;
            if (!wasMoving)
            {
                playerMovementAnimator_.reset();
            }
            playerMovementAnimator_.update(deltaTime);
            const float speed = input.sprint ? 280.0F : 180.0F;
            playerPosition_.x += direction.x * speed * deltaTime;
            playerPosition_.y += direction.y * speed * deltaTime;

            playerPosition_.x = std::clamp(
                playerPosition_.x,
                walkableBounds_.position.x,
                walkableBounds_.position.x +
                    walkableBounds_.size.x - playerSize_.x);
            playerPosition_.y = std::clamp(
                playerPosition_.y,
                walkableBounds_.position.y,
                walkableBounds_.position.y +
                    walkableBounds_.size.y - playerSize_.y);
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

const std::array<BaseFacility, 3> &BaseWorld::facilities() const noexcept
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
    case BaseFacilityKind::RaidGate:
        return "RAID DEPLOYMENT";
    }
    return "UNKNOWN";
}
