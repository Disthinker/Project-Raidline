#include "collision.h"

bool isCollision(const Rect &rect1, const Rect &rect2)
{
    // Check if the rectangles overlap on both axes
    return !(rect1.position.x + rect1.size.x <= rect2.position.x ||
             rect1.position.x >= rect2.position.x + rect2.size.x ||
             rect1.position.y + rect1.size.y <= rect2.position.y ||
             rect1.position.y >= rect2.position.y + rect2.size.y);
}