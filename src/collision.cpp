#include "collision.h"

bool isCollision(const Rect &rect1, const Rect &rect2)
{
    // Check if the rectangles overlap on both axes
    return !(rect1.position.x + rect1.size.x <= rect2.position.x ||
             rect1.position.x >= rect2.position.x + rect2.size.x ||
             rect1.position.y + rect1.size.y <= rect2.position.y ||
             rect1.position.y >= rect2.position.y + rect2.size.y);
}

namespace
{
bool rangesOverlap(
    float firstMinimum,
    float firstMaximum,
    float secondMinimum,
    float secondMaximum) noexcept
{
    return firstMinimum < secondMaximum && firstMaximum > secondMinimum;
}
}

float resolveHorizontalCollision(
    const Rect &actorBounds,
    float desiredX,
    const Rect &obstacleBounds) noexcept
{
    if (!rangesOverlap(
            actorBounds.position.y,
            actorBounds.position.y + actorBounds.size.y,
            obstacleBounds.position.y,
            obstacleBounds.position.y + obstacleBounds.size.y))
    {
        return desiredX;
    }

    const float obstacleLeft = obstacleBounds.position.x;
    const float obstacleRight = obstacleLeft + obstacleBounds.size.x;
    if (desiredX > actorBounds.position.x &&
        actorBounds.position.x + actorBounds.size.x <= obstacleLeft &&
        desiredX + actorBounds.size.x > obstacleLeft)
    {
        return obstacleLeft - actorBounds.size.x;
    }
    if (desiredX < actorBounds.position.x &&
        actorBounds.position.x >= obstacleRight &&
        desiredX < obstacleRight)
    {
        return obstacleRight;
    }
    return desiredX;
}

float resolveVerticalCollision(
    const Rect &actorBounds,
    float desiredY,
    const Rect &obstacleBounds) noexcept
{
    if (!rangesOverlap(
            actorBounds.position.x,
            actorBounds.position.x + actorBounds.size.x,
            obstacleBounds.position.x,
            obstacleBounds.position.x + obstacleBounds.size.x))
    {
        return desiredY;
    }

    const float obstacleTop = obstacleBounds.position.y;
    const float obstacleBottom = obstacleTop + obstacleBounds.size.y;
    if (desiredY > actorBounds.position.y &&
        actorBounds.position.y + actorBounds.size.y <= obstacleTop &&
        desiredY + actorBounds.size.y > obstacleTop)
    {
        return obstacleTop - actorBounds.size.y;
    }
    if (desiredY < actorBounds.position.y &&
        actorBounds.position.y >= obstacleBottom &&
        desiredY < obstacleBottom)
    {
        return obstacleBottom;
    }
    return desiredY;
}
