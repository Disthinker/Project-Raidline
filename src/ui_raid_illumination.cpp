#include "ui_raid_illumination.h"

#include <algorithm>
#include <cmath>

namespace
{
float side(Vec2 a, Vec2 b, Vec2 p) noexcept
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

struct FanTriangle
{
    UiIlluminationTriangle points;
    float left, right, top, bottom;
};

FanTriangle makeTriangle(Vec2 a, Vec2 b, Vec2 c) noexcept
{
    return {{a, b, c}, std::min({a.x, b.x, c.x}), std::max({a.x, b.x, c.x}),
        std::min({a.y, b.y, c.y}), std::max({a.y, b.y, c.y})};
}

bool overlaps(const FanTriangle &triangle, Rect visible) noexcept
{
    return triangle.left <= visible.position.x + visible.size.x &&
        triangle.right >= visible.position.x && triangle.top <= visible.position.y + visible.size.y &&
        triangle.bottom >= visible.position.y;
}

struct Polygon
{
    // An already viewport-clipped triangle gains at most one vertex per edge.
    std::array<Vec2, 12> points{};
    std::size_t size{};
};

Polygon clipLine(const Polygon &polygon, Vec2 a, Vec2 b, float orientation)
{
    Polygon clipped;
    if (polygon.size == 0)
        return clipped;
    Vec2 previous = polygon.points[polygon.size - 1];
    float previousSide = orientation * side(a, b, previous);
    for (std::size_t i = 0; i < polygon.size; ++i)
    {
        const Vec2 current = polygon.points[i];
        const float currentSide = orientation * side(a, b, current);
        if ((previousSide >= 0.0F) != (currentSide >= 0.0F))
        {
            const float t = previousSide / (previousSide - currentSide);
            clipped.points[clipped.size++] = {
                previous.x + (current.x - previous.x) * t,
                previous.y + (current.y - previous.y) * t};
        }
        if (currentSide >= 0.0F)
            clipped.points[clipped.size++] = current;
        previous = current;
        previousSide = currentSide;
    }
    return clipped;
}
}

std::vector<UiIlluminationTriangle> clipUiIlluminatedObservation(
    const RaidVisibilityProjection &view, Rect visible)
{
    std::vector<UiIlluminationTriangle> triangles;
    if (!view.valid || !view.illumination || !view.illumination->source)
        return triangles;
    const auto &observer = view.illumination->observer;
    if (!observer.valid)
        return triangles;

    std::vector<FanTriangle> lightTriangles;
    for (const auto &fan : view.illumination->source->fans)
    {
        for (std::size_t i = 0; fan.valid && i < fan.boundary.size(); ++i)
        {
            auto triangle = makeTriangle(fan.observer, fan.boundary[i],
                fan.boundary[(i + 1) % fan.boundary.size()]);
            if (overlaps(triangle, visible) &&
                std::abs(side(triangle.points[0], triangle.points[1], triangle.points[2])) > 0.001F)
                lightTriangles.push_back(triangle);
        }
    }
    if (lightTriangles.empty())
        return triangles;
    std::sort(lightTriangles.begin(), lightTriangles.end(),
        [](const auto &a, const auto &b) { return a.left < b.left; });

    for (std::size_t i = 0; i < observer.boundary.size(); ++i)
    {
        const auto player = makeTriangle(observer.observer, observer.boundary[i],
            observer.boundary[(i + 1) % observer.boundary.size()]);
        if (!overlaps(player, visible))
            continue;
        const auto clipped = clipRaidVisibilityTriangle(player.points[0], player.points[1],
            player.points[2], visible);
        if (clipped.size() < 3)
            continue;
        Polygon subject;
        subject.size = clipped.size();
        std::copy(clipped.begin(), clipped.end(), subject.points.begin());
        for (const auto &light : lightTriangles)
        {
            if (light.left > player.right)
                break;
            if (light.right < player.left || light.bottom < player.top || light.top > player.bottom)
                continue;
            const auto &edge = light.points;
            const float orientation = side(edge[0], edge[1], edge[2]) > 0.0F ? 1.0F : -1.0F;
            auto overlap = subject;
            for (std::size_t j = 0; j < edge.size() && overlap.size >= 3; ++j)
                overlap = clipLine(overlap, edge[j], edge[(j + 1) % edge.size()], orientation);
            for (std::size_t j = 1; j + 1 < overlap.size; ++j)
                triangles.push_back({overlap.points[0], overlap.points[j], overlap.points[j + 1]});
        }
    }
    return triangles;
}

void drawUiThrownIllumination(SDL_Renderer *renderer,
    const std::array<RaidVisibilityProjection, 2> &fans, Rect visible, float glareSeconds)
{
    std::vector<SDL_Vertex> vertices;
    const float glare = std::clamp(glareSeconds / 0.35F, 0.0F, 1.0F);
    for (const auto &fan : fans)
    {
        if (!fan.valid || fan.maximumDistance <= 0.0F)
            continue;
        for (std::size_t i = 0; i < fan.boundary.size(); ++i)
        {
            const auto polygon = clipRaidVisibilityTriangle(fan.observer, fan.boundary[i],
                fan.boundary[(i + 1) % fan.boundary.size()], visible);
            for (std::size_t j = 1; j + 1 < polygon.size(); ++j)
            {
                for (const auto p : {polygon[0], polygon[j], polygon[j + 1]})
                {
                    const float distance = std::hypot(p.x - fan.observer.x, p.y - fan.observer.y);
                    const float fade = std::clamp(1.0F - distance / fan.maximumDistance, 0.0F, 1.0F);
                    vertices.push_back({{p.x, p.y},
                        {0.43F + 0.10F * glare, 0.30F + 0.14F * glare,
                         0.14F + 0.20F * glare, fade}, {}});
                }
            }
        }
    }
    if (vertices.empty())
        return;
    SDL_BlendMode original{};
    SDL_GetRenderDrawBlendMode(renderer, &original);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
    SDL_SetRenderDrawBlendMode(renderer, original);
}
