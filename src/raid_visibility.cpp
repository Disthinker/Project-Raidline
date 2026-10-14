#include "raid_visibility.h"
#include <algorithm>
#include <cmath>

namespace
{
bool finite(Vec2 p) noexcept
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}
float cross(Vec2 a, Vec2 b, Vec2 p) noexcept
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
} // namespace

bool raidSightSegmentIntersectsRectInterior(Vec2 start, Vec2 end,
                                            Rect rect) noexcept
{
    if (!finite(start) || !finite(end) || !finite(rect.position) ||
        !finite(rect.size) || rect.size.x <= 0 || rect.size.y <= 0)
        return true;
    const float lowX = rect.position.x + kRaidSightGeometryEpsilon,
                highX =
                    rect.position.x + rect.size.x - kRaidSightGeometryEpsilon;
    const float lowY = rect.position.y + kRaidSightGeometryEpsilon,
                highY =
                    rect.position.y + rect.size.y - kRaidSightGeometryEpsilon;
    if (lowX >= highX || lowY >= highY)
        return false;
    const Vec2 delta{end.x - start.x, end.y - start.y};
    float enter{}, leave{1};
    const auto axis = [&](float origin, float movement, float low, float high)
    {
        if (std::abs(movement) <= kRaidSightGeometryEpsilon)
            return origin > low && origin < high;
        float first = (low - origin) / movement,
              second = (high - origin) / movement;
        if (first > second)
            std::swap(first, second);
        enter = std::max(enter, first);
        leave = std::min(leave, second);
        return enter <= leave;
    };
    return axis(start.x, delta.x, lowX, highX) &&
           axis(start.y, delta.y, lowY, highY) && leave >= 0 && enter <= 1 &&
           leave - enter > kRaidSightGeometryEpsilon;
}

bool RaidVisibilityProjection::validGeometry() const noexcept
{
    if (!valid || geometryRevision == 0 || boundary.size() < 3 ||
        !finite(observer) || !finite(worldSize) || worldSize.x <= 0 ||
        worldSize.y <= 0 || observer.x < 0 || observer.y < 0 ||
        observer.x > worldSize.x || observer.y > worldSize.y)
        return false;
    double area{};
    for (std::size_t i = 0; i < boundary.size(); ++i)
    {
        const auto p = boundary[i], q = boundary[(i + 1) % boundary.size()];
        if (!finite(p) || p.x < 0 || p.y < 0 || p.x > worldSize.x ||
            p.y > worldSize.y)
            return false;
        area += (static_cast<double>(p.x) - observer.x) * (q.y - observer.y) -
                (static_cast<double>(p.y) - observer.y) * (q.x - observer.x);
    }
    return std::isfinite(area) && area > 0.0001;
}

bool RaidVisibilityProjection::matches(const RaidSpaceDefinitionId &space,
                                       Vec2 position) const noexcept
{
    return validGeometry() && spaceId == space && finite(position) &&
           position.x == observer.x && position.y == observer.y;
}

bool RaidVisibilityProjection::pointVisible(Vec2 p) const noexcept
{
    if (!validGeometry() || !finite(p) || !finite(observer) || p.x < 0 ||
        p.y < 0 || p.x > worldSize.x || p.y > worldSize.y)
        return false;
    for (std::size_t i = 0; i < boundary.size(); ++i)
    {
        const auto a = boundary[i], b = boundary[(i + 1) % boundary.size()];
        const auto area = cross(observer, a, b);
        if (std::abs(area) < 0.00001F)
            continue;
        const float sign = area > 0 ? 1.0F : -1.0F;
        if (sign * cross(observer, a, p) >= -0.0001F &&
            sign * cross(a, b, p) >= -0.0001F &&
            sign * cross(b, observer, p) >= -0.0001F)
            return true;
    }
    return false;
}

std::vector<Vec2> clipRaidVisibilityTriangle(Vec2 a, Vec2 b, Vec2 c, Rect rect)
{
    std::vector<Vec2> points{a, b, c};
    if (!finite(rect.position) || !finite(rect.size) || rect.size.x <= 0 ||
        rect.size.y <= 0)
        return {};
    const float bounds[4]{rect.position.x, rect.position.x + rect.size.x,
                          rect.position.y, rect.position.y + rect.size.y};
    for (int edge = 0; edge < 4 && !points.empty(); ++edge)
    {
        std::vector<Vec2> next;
        const auto value = [edge](Vec2 p) { return edge < 2 ? p.x : p.y; };
        const auto inside = [&](Vec2 p)
        {
            return edge % 2 == 0 ? value(p) >= bounds[edge]
                                 : value(p) <= bounds[edge];
        };
        Vec2 previous = points.back();
        bool wasInside = inside(previous);
        for (Vec2 p : points)
        {
            const bool isInside = inside(p);
            if (isInside != wasInside)
            {
                const float t = (bounds[edge] - value(previous)) /
                                (value(p) - value(previous));
                next.push_back({previous.x + t * (p.x - previous.x),
                                previous.y + t * (p.y - previous.y)});
            }
            if (isInside)
                next.push_back(p);
            previous = p;
            wasInside = isInside;
        }
        points = std::move(next);
    }
    return points;
}

bool RaidVisibilityProjection::boundsVisible(Rect rect) const
{
    if (!validGeometry() || !finite(rect.position) || !finite(rect.size) ||
        rect.size.x <= 0 || rect.size.y <= 0)
        return false;
    const float dx = std::min(kRaidSightGeometryEpsilon, rect.size.x * 0.1F);
    const float dy = std::min(kRaidSightGeometryEpsilon, rect.size.y * 0.1F);
    const float left = rect.position.x + dx,
                right = rect.position.x + rect.size.x - dx;
    const float top = rect.position.y + dy,
                bottom = rect.position.y + rect.size.y - dy;
    for (Vec2 p :
         {Vec2{(left + right) * 0.5F, (top + bottom) * 0.5F}, Vec2{left, top},
          Vec2{right, top}, Vec2{right, bottom}, Vec2{left, bottom}})
        if (pointVisible(p))
            return true;
    // Rectangle/region overlap also exists when neither rectangle corner is
    // inside the region. Test its edges without allocating per-object polygons.
    for (std::size_t i = 0; i < boundary.size(); ++i)
    {
        const auto p = boundary[i], q = boundary[(i + 1) % boundary.size()];
        if ((p.x > left && p.x < right && p.y > top && p.y < bottom) ||
            raidSightSegmentIntersectsRectInterior(p, q, rect))
            return true;
    }
    return false;
}
