#include "ui_weapon_optic_view.h"
#include "ui_raid_illumination.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <span>
#include <vector>

namespace
{
void appendPolygon(std::vector<SDL_Vertex> &vertices, std::span<const Vec2> polygon,
                   const UiWeaponOpticView &view, float shade, bool world)
{
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
    {
        for (const Vec2 point : {polygon.front(), polygon[i], polygon[i + 1]})
        {
            const Vec2 pixel = world ? view.projection.worldToScreen(point) : point;
            vertices.push_back({{pixel.x, pixel.y}, {shade, shade, shade, 1.0F},
                {std::clamp(pixel.x / view.projection.screenSize.x, 0.0F, 1.0F),
                 std::clamp(pixel.y / view.projection.screenSize.y, 0.0F, 1.0F)}});
        }
    }
}

bool drawTexturedFan(SDL_Renderer *renderer, SDL_Texture *texture,
                     const std::vector<SDL_Vertex> &vertices)
{
    if (vertices.empty())
        return true;
    // Keep independent clipped triangles independent. The SDL software
    // renderer otherwise tries to merge coincident vertices into rectangles,
    // which can discard a wedge at a shared viewport edge.
    std::vector<int> indices(vertices.size());
    std::iota(indices.begin(), indices.end(), 0);
    const int count = static_cast<int>(vertices.size());
    return SDL_RenderGeometry(renderer, texture, vertices.data(), count,
                              indices.data(), count);
}

constexpr std::size_t kLensSegments = 72;

const auto &lensDirections()
{
    static const auto directions = []
    {
        std::array<Vec2, kLensSegments> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const float angle = 2.0F * std::numbers::pi_v<float> *
                static_cast<float>(i) / static_cast<float>(values.size());
            values[i] = {std::cos(angle), std::sin(angle)};
        }
        return values;
    }();
    return directions;
}

Vec2 lensPoint(const UiWeaponOpticView &view, Vec2 direction, float radius)
{
    return {view.reticle.x + direction.x * radius,
            view.reticle.y + direction.y * radius};
}
}

bool UiWeaponOpticView::hasOpaquePeriphery() const noexcept
{
    return projection.lensRadius > 0.0F && projection.transitionWeight >= 1.0F;
}

Rect UiWeaponOpticView::sceneBounds() const noexcept
{
    if (!hasOpaquePeriphery())
        return projection.worldBounds();

    const float radius = projection.lensRadius;

    const Vec2 first = projection.screenToWorld({
        std::max(0.0F, reticle.x - radius),
        std::max(0.0F, reticle.y - radius)});
    const Vec2 last = projection.screenToWorld({
        std::min(projection.screenSize.x, reticle.x + radius),
        std::min(projection.screenSize.y, reticle.y + radius)});
    return {first, {last.x - first.x, last.y - first.y}};
}

bool UiWeaponOpticView::includes(Vec2 world) const noexcept
{
    const Vec2 pixel = projection.worldToScreen(world);
    if (pixel.x < 0.0F || pixel.y < 0.0F ||
        pixel.x >= projection.screenSize.x || pixel.y >= projection.screenSize.y)
        return false;
    const float dx = pixel.x - reticle.x, dy = pixel.y - reticle.y;
    const float radius = projection.lensRadius;
    return !hasOpaquePeriphery() || dx * dx + dy * dy < radius * radius;
}

UiWeaponOpticView makeUiWeaponOpticView(
    const WeaponOpticViewProjection &projection, Vec2 aim) noexcept
{
    return {projection, projection.worldToScreen(aim)};
}

bool drawUiWeaponOpticObservation(SDL_Renderer *renderer, SDL_Texture *texture,
    const UiWeaponOpticView &view, const RaidVisibilityProjection &visibility,
    float shade)
{
    if (!visibility.valid)
        return true;
    const Rect visible = view.sceneBounds();
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(visibility.boundary.size() * 3);
    for (std::size_t i = 0; i < visibility.boundary.size(); ++i)
    {
        const auto polygon = clipRaidVisibilityTriangle(
            visibility.observer, visibility.boundary[i],
            visibility.boundary[(i + 1) % visibility.boundary.size()], visible);
        appendPolygon(vertices, polygon, view, shade, true);
    }
    for (const auto &triangle : clipUiIlluminatedObservation(visibility, visible))
        appendPolygon(vertices, triangle, view, shade, true);
    return drawTexturedFan(renderer, texture, vertices);
}

bool drawUiWeaponOpticLens(SDL_Renderer *renderer, SDL_Texture *texture,
    const UiWeaponOpticView &view)
{
    const float opacity = view.projection.transitionWeight;
    if (view.projection.lensRadius == 0.0F || opacity <= 0.0F)
        return SDL_RenderTexture(renderer, texture, nullptr, nullptr);

    if (!view.hasOpaquePeriphery())
    {
        // Reuse the already composed scene for the fading periphery.
        const auto shade = static_cast<Uint8>(std::lround(255.0F * (1.0F - opacity)));
        if (!SDL_SetTextureColorMod(texture, shade, shade, shade))
            return false;
        const bool rendered = SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        const bool restored = SDL_SetTextureColorMod(texture, 255, 255, 255);
        if (!rendered || !restored)
            return false;
    }

    const auto &directions = lensDirections();
    const Rect screen{{}, view.projection.screenSize};
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(kLensSegments * 6);
    for (std::size_t i = 0; i < directions.size(); ++i)
    {
        const Vec2 first = lensPoint(view, directions[i], view.projection.lensRadius);
        const Vec2 second = lensPoint(view, directions[(i + 1) % directions.size()],
                                      view.projection.lensRadius);
        const auto polygon = clipRaidVisibilityTriangle(view.reticle, first, second, screen);
        appendPolygon(vertices, polygon, view, 1.0F, false);
    }
    if (!drawTexturedFan(renderer, texture, vertices))
        return false;

    // Matte rim, with a soft falloff inside the glass. The ring is only
    // decoration and never samples scene pixels beyond the observation mask.
    vertices.clear();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (std::size_t i = 0; i < directions.size(); ++i)
    {
        const Vec2 a = directions[i], b = directions[(i + 1) % directions.size()];
        const auto vertex = [&](Vec2 direction, float offset, SDL_FColor color)
        {
            const Vec2 point = lensPoint(view, direction, view.projection.lensRadius + offset);
            color.a *= opacity;
            return SDL_Vertex{{point.x, point.y}, color, {}};
        };
        const auto band = [&](float inner, float outer, SDL_FColor inside, SDL_FColor outside)
        {
            const SDL_Vertex ai = vertex(a, inner, inside), bi = vertex(b, inner, inside);
            const SDL_Vertex ao = vertex(a, outer, outside), bo = vertex(b, outer, outside);
            vertices.insert(vertices.end(), {ai, ao, bo, ai, bo, bi});
        };
        band(-16.0F, 0.0F, {0.03F, 0.04F, 0.03F, 0.0F}, {0.03F, 0.04F, 0.03F, 0.78F});
        band(0.0F, 2.0F, {0.42F, 0.46F, 0.42F, 0.88F}, {0.10F, 0.12F, 0.10F, 0.88F});
    }
    return SDL_RenderGeometry(renderer, nullptr, vertices.data(),
        static_cast<int>(vertices.size()), nullptr, 0);
}
