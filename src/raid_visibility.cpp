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
bool pointInBoundary(const RaidVisibilityProjection &view, Vec2 p) noexcept
{
    for (std::size_t i = 0; i < view.boundary.size(); ++i)
    {
        const auto a = view.boundary[i],
                   b = view.boundary[(i + 1) % view.boundary.size()];
        const auto area = cross(view.observer, a, b);
        if (std::abs(area) < 0.00001F)
            continue;
        const float sign = area > 0 ? 1.0F : -1.0F;
        if (sign * cross(view.observer, a, p) >= -0.0001F &&
            sign * cross(a, b, p) >= -0.0001F &&
            sign * cross(b, view.observer, p) >= -0.0001F)
            return true;
    }
    return false;
}
} // namespace

RaidSightParameters
raidSightParameters(RaidLightEnvironment environment) noexcept
{
    switch (environment)
    {
    case RaidLightEnvironment::DayOutdoor:
        return {180, 1000};
    case RaidLightEnvironment::NightOutdoor:
        return {100, 600};
    case RaidLightEnvironment::WeakIndoor:
        return {110, 500};
    }
    return {};
}

RaidObservationKey RaidVisibilityProjection::key() const
{
    return {spaceId,         observer,    direction,       angleDegrees,
            maximumDistance, environment, geometryRevision};
}

bool RaidVisibilityProjection::hasSameParameters(
    const RaidObservationKey &current) const noexcept
{
    return spaceId == current.spaceId && observer.x == current.observer.x &&
           observer.y == current.observer.y &&
           direction.x == current.direction.x &&
           direction.y == current.direction.y &&
           angleDegrees == current.angleDegrees &&
           maximumDistance == current.maximumDistance &&
           environment == current.environment &&
           geometryRevision == current.geometryRevision;
}

bool RaidVisibilityProjection::matches(
    const RaidObservationKey &current) const noexcept
{
    return hasSameParameters(current) && validGeometry();
}

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
        observer.x > worldSize.x || observer.y > worldSize.y ||
        !finite(direction) || !std::isfinite(angleDegrees) ||
        !std::isfinite(maximumDistance) || angleDegrees <= 0 ||
        angleDegrees > 360 || maximumDistance < 0 ||
        (maximumDistance > 0 &&
         (angleDegrees > 180 ||
          std::abs(std::hypot(direction.x, direction.y) - 1) > 0.001F)))
        return false;
    double area{};
    const float cosHalf =
        std::cos(angleDegrees * 3.14159265358979323846F / 360);
    for (std::size_t i = 0; i < boundary.size(); ++i)
    {
        const auto p = boundary[i], q = boundary[(i + 1) % boundary.size()];
        if (!finite(p) || p.x < 0 || p.y < 0 || p.x > worldSize.x ||
            p.y > worldSize.y)
            return false;
        if (maximumDistance > 0)
        {
            // Far-map float coordinates lose several thousandths on
            // translation. Use 0.01-unit linear roundoff for vertices, rather
            // than a fixed squared tolerance whose effective allowance shrinks
            // at long range.
            const float dx = p.x - observer.x, dy = p.y - observer.y;
            const float squared = dx * dx + dy * dy,
                        dot = dx * direction.x + dy * direction.y;
            if (squared >
                    (maximumDistance + 0.01F) * (maximumDistance + 0.01F) ||
                dot < -0.01F ||
                (dot + 0.01F) * (dot + 0.01F) < squared * cosHalf * cosHalf)
                return false;
        }
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
    return pointInBoundary(*this, p);
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
    if (!finite(rect.position) || !finite(rect.size) || rect.size.x <= 0 ||
        rect.size.y <= 0)
        return false;
    if (maximumDistance > 0 && finite(observer) && finite(direction) &&
        std::isfinite(angleDegrees))
    {
        const float x = std::clamp(observer.x, rect.position.x,
                                   rect.position.x + rect.size.x) -
                        observer.x;
        const float y = std::clamp(observer.y, rect.position.y,
                                   rect.position.y + rect.size.y) -
                        observer.y;
        if (x * x + y * y > maximumDistance * maximumDistance)
            return false;
        const float half = angleDegrees * 3.14159265358979323846F / 360;
        const float c = std::cos(half), s = std::sin(half);
        const Vec2 left{direction.x * c + direction.y * s,
                        direction.y * c - direction.x * s};
        const Vec2 right{direction.x * c - direction.y * s,
                         direction.y * c + direction.x * s};
        const Vec2 center{rect.position.x + rect.size.x / 2 - observer.x,
                          rect.position.y + rect.size.y / 2 - observer.y};
        const auto extent = [&](Vec2 d)
        {
            return std::abs(d.x) * rect.size.y / 2 +
                   std::abs(d.y) * rect.size.x / 2;
        };
        if (left.x * center.y - left.y * center.x + extent(left) < -0.0001F ||
            right.x * center.y - right.y * center.x - extent(right) > 0.0001F)
            return false;
    }
    if (!validGeometry())
        return false;
    const float dx = std::min(kRaidSightGeometryEpsilon, rect.size.x * 0.1F);
    const float dy = std::min(kRaidSightGeometryEpsilon, rect.size.y * 0.1F);
    const float left = rect.position.x + dx,
                right = rect.position.x + rect.size.x - dx;
    const float top = rect.position.y + dy,
                bottom = rect.position.y + rect.size.y - dy;
    const Vec2 center{(left + right) / 2, (top + bottom) / 2};
    const float hx = (right - left) / 2, hy = (bottom - top) / 2;
    // Triangle/rectangle separating axes handle tiny exposed slivers without
    // repeated whole-polygon validation, five point scans, or allocations.
    for (std::size_t i = 0; i < boundary.size(); ++i)
    {
        const auto p = boundary[i], q = boundary[(i + 1) % boundary.size()];
        const auto area = cross(observer, p, q);
        if (std::abs(area) < 0.00001F ||
            std::max({observer.x, p.x, q.x}) < left ||
            std::min({observer.x, p.x, q.x}) > right ||
            std::max({observer.y, p.y, q.y}) < top ||
            std::min({observer.y, p.y, q.y}) > bottom)
            continue;
        const float sign = area > 0 ? 1.0F : -1.0F;
        const auto overlaps = [&](Vec2 a, Vec2 b)
        {
            return sign * cross(a, b, center) + std::abs(b.x - a.x) * hy +
                       std::abs(b.y - a.y) * hx >=
                   -0.0001F;
        };
        if (overlaps(observer, p) && overlaps(p, q) && overlaps(q, observer))
            return true;
    }
    return false;
}
