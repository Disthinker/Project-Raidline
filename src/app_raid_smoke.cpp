#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>

namespace
{
constexpr int kSmokeLayerWidth = 1280;
constexpr int kSmokeLayerHeight = 720;
// Copy terrain pixels only where the sightline enters smoke. Each strip starts
// at the near face of the cloud (or the contact radius when standing in it).
std::vector<SDL_Vertex> smokeShadow(Vec2 observer, const SmokeCloud &cloud,
    Rect visible, SDL_Rect viewport, float scaleX, float scaleY, bool textured)
{
    const float dx = cloud.point.x - observer.x, dy = cloud.point.y - observer.y;
    const float distance = std::hypot(dx, dy);
    const float direction = std::atan2(dy, dx);
    const bool inside = distance < cloud.radius;
    const float halfAngle = inside ? std::numbers::pi_v<float> :
        std::asin(std::clamp(cloud.radius / distance, 0.0F, 1.0F));
    const float far = std::hypot(visible.position.x - observer.x, visible.position.y - observer.y) +
        std::hypot(visible.size.x, visible.size.y) + cloud.radius + 4.0F;
    constexpr unsigned strips = 72;
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(strips * 6);
    const auto endpoints = [&](unsigned index) {
        const float a = direction + (2.0F * index / strips - 1.0F) * halfAngle;
        const Vec2 ray{std::cos(a), std::sin(a)};
        const float projection = ray.x * dx + ray.y * dy;
        const float perpendicular = ray.x * dy - ray.y * dx;
        const float entry = inside ? 0.0F : projection - std::sqrt(std::max(0.0F,
            cloud.radius * cloud.radius - perpendicular * perpendicular));
        // Half a pixel of inset closes raster cracks between tangent samples.
        const float near = std::max(RaidSmokeSight::kContactDistance, entry - .5F);
        return std::array<Vec2, 2>{{
            {observer.x + near * ray.x, observer.y + near * ray.y},
            {observer.x + far * ray.x, observer.y + far * ray.y}}};
    };
    const auto append = [&](Vec2 a, Vec2 b, Vec2 c) {
        const auto polygon = clipRaidVisibilityTriangle(a, b, c, visible);
        for (std::size_t i = 1; i+1 < polygon.size(); ++i)
            for (const Vec2 point : {polygon.front(), polygon[i], polygon[i+1]}) {
                const SDL_FPoint uv{std::clamp((point.x + viewport.x) * scaleX / kSmokeLayerWidth, 0.0F, 1.0F),
                                    std::clamp((point.y + viewport.y) * scaleY / kSmokeLayerHeight, 0.0F, 1.0F)};
                const SDL_FColor tint = textured ? SDL_FColor{.86F,.89F,.90F,1} : SDL_FColor{.16F,.19F,.20F,1};
                vertices.push_back({{point.x,point.y}, tint, uv});
            }
    };
    auto previous = endpoints(0);
    for (unsigned i = 1; i <= strips; ++i) {
        const auto next = endpoints(i);
        append(previous[0], previous[1], next[1]);
        append(previous[0], next[1], next[0]);
        previous = next;
    }
    return vertices;
}

void drawSmokeCloud(SDL_Renderer *renderer, const SmokeCloud &cloud)
{
    constexpr unsigned segments = 28;
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    const auto puff = [&](Vec2 center, float radius, float opacity) {
        std::array<SDL_Vertex, segments + 1> vertices{};
        std::array<int, segments * 3> indices{};
        vertices[0] = {{center.x,center.y}, {.76F,.80F,.79F,opacity}, {}};
        for (unsigned i = 0; i < segments; ++i) {
            const float a = tau * i / segments;
            vertices[i+1] = {{center.x + radius*std::cos(a),center.y + radius*std::sin(a)},
                {.63F,.69F,.69F,0}, {}};
            indices[3*i+1] = static_cast<int>(i+1);
            indices[3*i+2] = static_cast<int>(1+(i+1)%segments);
        }
        SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
            indices.data(), static_cast<int>(indices.size()));
    };
    const float t = static_cast<float>(cloud.remainingSeconds);
    const float fade = std::clamp(t, .24F, 1.0F);
    puff(cloud.point, cloud.radius, .72F*fade);
    for (unsigned i = 0; i < 6; ++i) {
        const float a = tau*i/6.0F + .07F*t;
        const float offset = cloud.radius*(.40F + .04F*std::sin(t + i));
        puff({cloud.point.x + offset*std::cos(a), cloud.point.y + offset*std::sin(a)},
            .59F*cloud.radius, .48F*fade);
    }
}
}

bool App::activeRaidSmoke() const
{
    const auto &world = gameSession_.world();
    return std::ranges::any_of(world.smoke().clouds(), [&](const SmokeCloud &cloud) {
        return cloud.space == world.activeRaidSpaceId();
    });
}

void App::renderAreaThrowableIcon(const ItemDefinition &item,SDL_FRect bounds,Uint8 alpha)
{
    const float unit = std::min(bounds.w, bounds.h) / 26.0F;
    const float x = bounds.x + bounds.w*.5F, y = bounds.y + bounds.h*.5F;
    if (item.alarmUse) {
        const SDL_FRect caseRect{x-8*unit,y-2*unit,16*unit,9*unit};
        const SDL_FRect lamp{x+3*unit,y,3*unit,3*unit};
        SDL_SetRenderDrawColor(renderer_,85,112,116,alpha);
        SDL_RenderFillRect(renderer_,&caseRect);
        SDL_SetRenderDrawColor(renderer_,183,216,211,alpha);
        SDL_RenderRect(renderer_,&caseRect);
        SDL_RenderLine(renderer_,x-5*unit,y-2*unit,x-7*unit,y-10*unit);
        SDL_RenderFillRect(renderer_,&lamp);
        return;
    }
    if (item.fireZoneUse) {
        const SDL_FRect neck{x-2*unit,y-10*unit,4*unit,6*unit};
        const SDL_FRect body{x-6*unit,y-4*unit,12*unit,13*unit};
        SDL_SetRenderDrawColor(renderer_,220,146,80,alpha);
        SDL_RenderFillRect(renderer_,&neck);
        SDL_SetRenderDrawColor(renderer_,168,79,48,alpha);
        SDL_RenderFillRect(renderer_,&body);
        SDL_SetRenderDrawColor(renderer_,252,196,104,alpha);
        SDL_RenderLine(renderer_,x-3*unit,y+5*unit,x,y-unit);
        SDL_RenderLine(renderer_,x,y-unit,x+3*unit,y+5*unit);
        return;
    }
    const SDL_FRect cap{x-3*unit,y-10*unit,6*unit,4*unit};
    const SDL_FRect body{x-5*unit,y-6*unit,10*unit,15*unit};
    const SDL_FRect stripe{x-5*unit,y-unit,10*unit,4*unit};
    SDL_SetRenderDrawColor(renderer_, 118,148,143,alpha);
    SDL_RenderFillRect(renderer_, &body);
    SDL_SetRenderDrawColor(renderer_, 199,217,208,alpha);
    SDL_RenderFillRect(renderer_, &cap);
    SDL_RenderFillRect(renderer_, &stripe);
    SDL_RenderLine(renderer_,x+3*unit,y-8*unit,x+7*unit,y-5*unit);
}

void App::renderRaidSmokeCover(Rect visible)
{
    if (!activeRaidSmoke()) return;
    const auto &world = gameSession_.world();
    const auto &player = world.player();
    const Vec2 observer{player.position().x + player.size()*.5F, player.position().y + player.size()*.5F};
    SDL_Texture *target = SDL_GetRenderTarget(renderer_);
    SDL_Rect viewport{}, clip{};
    SDL_GetRenderViewport(renderer_, &viewport);
    SDL_GetRenderClipRect(renderer_, &clip);
    const bool clipped = SDL_RenderClipEnabled(renderer_), moved = SDL_RenderViewportSet(renderer_);
    float scaleX = 1, scaleY = 1;
    SDL_GetRenderScale(renderer_, &scaleX, &scaleY);
    SDL_BlendMode blend{};
    SDL_GetRenderDrawBlendMode(renderer_, &blend);
    const auto restoreScene = [&] {
        SDL_SetRenderScale(renderer_, scaleX, scaleY);
        SDL_SetRenderViewport(renderer_, moved ? &viewport : nullptr);
        SDL_SetRenderClipRect(renderer_, clipped ? &clip : nullptr);
    };
    if (!raidSmokeTerrain_.valid())
        raidSmokeTerrain_ = Texture{SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, kSmokeLayerWidth, kSmokeLayerHeight)};
    SDL_Texture *terrain = raidSmokeTerrain_.get();
    if (terrain && SDL_SetRenderTarget(renderer_, terrain)) {
        restoreScene();
        renderBackground(true, true, visible);
        renderBallisticBlockers(true, visible);
        SDL_SetRenderTarget(renderer_, target);
        restoreScene();
        SDL_SetTextureBlendMode(terrain, SDL_BLENDMODE_NONE);
    } else {
        terrain = nullptr;
    }
    for (const SmokeCloud &cloud : world.smoke().clouds()) {
        if (cloud.space != world.activeRaidSpaceId()) continue;
        const auto vertices = smokeShadow(observer, cloud, visible, viewport, scaleX, scaleY, terrain != nullptr);
        if (!vertices.empty()) {
            std::vector<int> indices(vertices.size());
            std::iota(indices.begin(), indices.end(), 0);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_RenderGeometry(renderer_, terrain, vertices.data(), static_cast<int>(vertices.size()),
                indices.data(), static_cast<int>(indices.size()));
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const SmokeCloud &cloud : world.smoke().clouds())
        if (cloud.space == world.activeRaidSpaceId()) drawSmokeCloud(renderer_, cloud);
    SDL_SetRenderDrawBlendMode(renderer_, blend);
}

void App::renderRaidAreaThrows()
{
    const auto &world = gameSession_.world();
    const auto visible = [&](Vec2 point) { return world.raidBoundsObservable({{point.x-1,point.y-1},{2,2}}); };
    const auto drawFlight = [&](const auto &flight,SDL_Color color) {
        if (!flight || flight->space != world.activeRaidSpaceId()) return;
        const float t = static_cast<float>(flight->elapsed / flight->use.flightSeconds);
        const Vec2 point{std::lerp(flight->origin.x, flight->point.x, t),
                         std::lerp(flight->origin.y, flight->point.y, t)};
        if (visible(point)) {
            SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
            const SDL_FRect canister{point.x-3,point.y-4,6,8};
            SDL_RenderFillRect(renderer_, &canister);
        }
    };
    drawFlight(world.smoke().flight(),{167,203,193,255});
    drawFlight(world.fireZones().flight(),{239,163,84,255});
    if (!selectedThrowable_) return;
    const auto *item = gameSession_.profile().assets.find(*selectedThrowable_);
    if (!item) return;
    const auto &definition = publishedContentRegistry().item(item->definitionId);
    const auto preview = [&](const auto &landing,SDL_Color color) {
        if (!landing || !visible(landing->point)) return;
        SDL_SetRenderDrawColor(renderer_,color.r,color.g,color.b,color.a);
        SDL_RenderLine(renderer_,landing->origin.x,landing->origin.y,landing->point.x,landing->point.y);
        const SDL_FRect marker{landing->point.x-7,landing->point.y-7,14,14};
        SDL_RenderRect(renderer_,&marker);
    };
    if (definition.smokeUse)
        preview(world.previewSmoke(world.weaponAimWorldPosition(),*definition.smokeUse),{175,216,205,210});
    else if (definition.fireZoneUse)
        preview(world.previewFireZone(world.weaponAimWorldPosition(),*definition.fireZoneUse),{247,170,80,210});
    else if (definition.alarmUse) {
        const auto placement=world.previewProximityAlarm(world.weaponAimWorldPosition(),*definition.alarmUse);
        if (placement && visible(placement->point)) {
            SDL_SetRenderDrawColor(renderer_,165,220,210,230);
            const SDL_FRect footprint{placement->point.x-8,placement->point.y-4,16,8};
            SDL_RenderRect(renderer_,&footprint);
        }
    }
}

void App::renderRaidAlarmDevices()
{
    const auto &world=gameSession_.world();
    for (const auto &device : world.proximityAlarms().entries()) {
        if (device.space!=world.activeRaidSpaceId()) continue;
        const auto [x,y]=device.point;
        const SDL_FRect body{x-6,y-3,12,6};
        if (!world.raidBoundsObservable({{body.x,body.y},{body.w,body.h}})) continue;
        SDL_SetRenderDrawColor(renderer_,49,67,67,255);
        SDL_RenderFillRect(renderer_,&body);
        SDL_SetRenderDrawColor(renderer_,132,155,151,255);
        SDL_RenderRect(renderer_,&body);
        SDL_RenderLine(renderer_,x-4,y-3,x-6,y-8);
        if (!device.triggered) {
            const SDL_FRect lamp{x+1,y-1,3,2};
            SDL_SetRenderDrawColor(renderer_,165,220,210,255);
            SDL_RenderFillRect(renderer_,&lamp);
        }
    }
}
