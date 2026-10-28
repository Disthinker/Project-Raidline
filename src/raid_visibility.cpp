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
            maximumDistance, environment, geometryRevision, lightAssetId, lightBatteryId, lightAngleDegrees, lightDistance};
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
           geometryRevision == current.geometryRevision && lightAssetId == current.lightAssetId && lightBatteryId == current.lightBatteryId &&
           lightAngleDegrees == current.lightAngleDegrees && lightDistance == current.lightDistance;
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
    if (!std::isfinite(lightAngleDegrees) || !std::isfinite(lightDistance) ||
        (lightAssetId ? (!lightBatteryId || lightAngleDegrees <= 0 || lightAngleDegrees > 180 || lightDistance <= 0 || lightDistance > 1500 || maximumDistance <= 0) :
                        (lightBatteryId != 0 || lightAngleDegrees != 0 || lightDistance != 0))) return false;
    double area{};
    const float cosLightHalf = std::cos(lightAngleDegrees * 3.14159265358979323846F / 360);
    const float cosHalf =
        std::cos(angleDegrees * 3.14159265358979323846F / 360);
    const float maximumVertexDistanceSquared =
        (maximumDistance + 0.01F) * (maximumDistance + 0.01F);
    const float lightVertexDistanceSquared =
        (lightDistance + 0.01F) * (lightDistance + 0.01F);
    std::array<Vec2, 2> seamRays{};
    if (lightAssetId && angleDegrees != lightAngleDegrees)
    {
        const float inner = std::min(angleDegrees, lightAngleDegrees);
        auto *ray = seamRays.data();
        for (float sign : {-1.0F, 1.0F})
        {
            const float theta = inner * sign * 3.14159265358979323846F / 360;
            *ray++ = {direction.x * std::cos(theta) - direction.y * std::sin(theta),
                      direction.x * std::sin(theta) + direction.y * std::cos(theta)};
        }
    }
    const auto *const points = boundary.data();
    const auto pointCount = boundary.size();
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto p = points[i], q = points[i + 1 == pointCount ? 0 : i + 1];
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
            if (!(squared <= maximumVertexDistanceSquared && dot >= -0.01F &&
                    (dot + 0.01F) * (dot + 0.01F) >= squared * cosHalf * cosHalf) &&
                !(lightAssetId && squared <= lightVertexDistanceSquared && dot >= -0.01F &&
                    (dot + 0.01F) * (dot + 0.01F) >= squared * cosLightHalf * cosLightHalf))
                return false;
            if (lightAssetId && angleDegrees != lightAngleDegrees)
            {
                // Each angular subsegment must fit its own range. Testing only
                // vertices (or the enclosing angle/range) fills the union's
                // concave notch, including bridges ending exactly on a seam.
                const Vec2 edge{q.x-p.x, q.y-p.y};
                const float length=std::hypot(edge.x,edge.y);
                std::array<float,4> cuts{0,1}; std::size_t count=2;
                auto *const cutData = cuts.data();
                bool radialStep=false;
                const auto *const lastRay = seamRays.data() + seamRays.size();
                for (auto rayCursor = seamRays.data(); rayCursor != lastRay; ++rayCursor)
                {
                    const Vec2 ray = *rayCursor;
                    const double first=static_cast<double>(dx)*ray.y-static_cast<double>(dy)*ray.x;
                    const double last=static_cast<double>(q.x-observer.x)*ray.y-static_cast<double>(q.y-observer.y)*ray.x;
                    // Float translation at far map coordinates has <=0.01
                    // world-unit roundoff; a radial step has zero ideal area.
                    if (std::abs(first)<=0.01 && std::abs(last)<=0.01) { radialStep=true; break; }
                    const double denominator=first-last;
                    if (std::abs(denominator)<0.000001) continue;
                    const double t=first/denominator;
                    if (t>0 && t<1) cutData[count++]=static_cast<float>(t);
                }
                if (!radialStep)
                {
                    std::sort(cutData,cutData+count);
                    for (std::size_t n=1;n<count;++n)
                    {
                        const float a=cutData[n-1], b=cutData[n];
                        if ((b-a)*length<=0.02F) continue;
                        const float mx=dx+edge.x*(a+b)*0.5F, my=dy+edge.y*(a+b)*0.5F;
                        const float radius=std::hypot(mx,my), forward=mx*direction.x+my*direction.y;
                        const float cap=std::max(forward+0.01F>=radius*cosHalf ? maximumDistance : 0.0F,
                                                 forward+0.01F>=radius*cosLightHalf ? lightDistance : 0.0F);
                        for (float t : {a,b})
                            if (std::hypot(dx+edge.x*t,dy+edge.y*t)>cap+0.02F) return false;
                    }
                }
            }
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
        if (x * x + y * y > std::max(maximumDistance, lightDistance) * std::max(maximumDistance, lightDistance))
            return false;
        const float half = std::max(angleDegrees, lightAngleDegrees) * 3.14159265358979323846F / 360;
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

bool RaidVisibilityBoundsQuery::Edge::overlaps(Vec2 center, float hx, float hy,
                                               float sign) const noexcept
{
    return sign * (delta.x * (center.y - origin.y) -
                   delta.y * (center.x - origin.x)) +
               absoluteX * hy + absoluteY * hx >=
           -0.0001F;
}

RaidVisibilityBoundsQuery::RaidVisibilityBoundsQuery(
    const RaidVisibilityProjection &view)
{
    if (!view.validGeometry())
        return;
    valid_ = true;
    bounded_ = view.maximumDistance > 0;
    observer_ = view.observer;
    const float maximum = std::max(view.maximumDistance, view.lightDistance);
    maximumDistanceSquared_ = maximum * maximum;
    const float half = std::max(view.angleDegrees, view.lightAngleDegrees) * 3.14159265358979323846F / 360;
    const float c = std::cos(half), s = std::sin(half);
    leftDirection_ = {view.direction.x * c + view.direction.y * s,
                      view.direction.y * c - view.direction.x * s};
    rightDirection_ = {view.direction.x * c - view.direction.y * s,
                       view.direction.y * c + view.direction.x * s};
    // Establish live elements before writing through the contiguous view.
    // The final resize removes only triangles rejected by the original area
    // test; no capacity-only writes or changed fan order are permitted.
    triangles_.assign(view.boundary.size(), Triangle{});
    auto *const triangles = triangles_.data();
    std::size_t triangleCount{};
    const auto *const points = view.boundary.data();
    const auto pointCount = view.boundary.size();
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto p = points[i], q = points[i + 1 == pointCount ? 0 : i + 1];
        const auto area = cross(observer_, p, q);
        if (std::abs(area) < 0.00001F)
            continue;
        Vec2 minimum = observer_, maximum = observer_;
        if (p.x < minimum.x) minimum.x = p.x;
        if (q.x < minimum.x) minimum.x = q.x;
        if (p.y < minimum.y) minimum.y = p.y;
        if (q.y < minimum.y) minimum.y = q.y;
        if (maximum.x < p.x) maximum.x = p.x;
        if (maximum.x < q.x) maximum.x = q.x;
        if (maximum.y < p.y) maximum.y = p.y;
        if (maximum.y < q.y) maximum.y = q.y;
        const Vec2 firstDelta{p.x - observer_.x, p.y - observer_.y};
        const Vec2 middleDelta{q.x - p.x, q.y - p.y};
        const Vec2 lastDelta{observer_.x - q.x, observer_.y - q.y};
        triangles[triangleCount++] = {minimum, maximum,
            area > 0 ? 1.0F : -1.0F,
            {Edge{observer_, firstDelta, std::abs(firstDelta.x), std::abs(firstDelta.y)},
             Edge{p, middleDelta, std::abs(middleDelta.x), std::abs(middleDelta.y)},
             Edge{q, lastDelta, std::abs(lastDelta.x), std::abs(lastDelta.y)}}};
    }
    triangles_.resize(triangleCount);
}

bool RaidVisibilityBoundsQuery::boundsVisible(Rect rect) const noexcept
{
    if (!valid_ || !finite(rect.position) || !finite(rect.size) ||
        rect.size.x <= 0 || rect.size.y <= 0)
        return false;
    if (bounded_)
    {
        const float x = std::clamp(observer_.x, rect.position.x,
                                   rect.position.x + rect.size.x) -
                        observer_.x;
        const float y = std::clamp(observer_.y, rect.position.y,
                                   rect.position.y + rect.size.y) -
                        observer_.y;
        if (x * x + y * y > maximumDistanceSquared_)
            return false;
        const Vec2 center{rect.position.x + rect.size.x / 2 - observer_.x,
                          rect.position.y + rect.size.y / 2 - observer_.y};
        const auto extent = [&](Vec2 d)
        {
            return std::abs(d.x) * rect.size.y / 2 +
                   std::abs(d.y) * rect.size.x / 2;
        };
        if (leftDirection_.x * center.y - leftDirection_.y * center.x +
                    extent(leftDirection_) <
                -0.0001F ||
            rightDirection_.x * center.y - rightDirection_.y * center.x -
                    extent(rightDirection_) >
                0.0001F)
            return false;
    }
    const float dx = std::min(kRaidSightGeometryEpsilon, rect.size.x * 0.1F);
    const float dy = std::min(kRaidSightGeometryEpsilon, rect.size.y * 0.1F);
    const float left = rect.position.x + dx,
                right = rect.position.x + rect.size.x - dx;
    const float top = rect.position.y + dy,
                bottom = rect.position.y + rect.size.y - dy;
    const Vec2 center{(left + right) / 2, (top + bottom) / 2};
    const float hx = (right - left) / 2, hy = (bottom - top) / 2;
    const auto *const lastTriangle = triangles_.data() + triangles_.size();
    for (auto triangleCursor = triangles_.data(); triangleCursor != lastTriangle; ++triangleCursor)
    {
        const auto &triangle = *triangleCursor;
        if (triangle.maximum.x < left || triangle.minimum.x > right ||
            triangle.maximum.y < top || triangle.minimum.y > bottom)
            continue;
        const auto *const edges = triangle.edges.data();
        if (edges[0].overlaps(center, hx, hy, triangle.sign) &&
            edges[1].overlaps(center, hx, hy, triangle.sign) &&
            edges[2].overlaps(center, hx, hy, triangle.sign))
            return true;
    }
    return false;
}
