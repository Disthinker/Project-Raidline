#include "app.h"

#include <algorithm>
#include <cmath>

void App::renderRaidFireZones()
{
    const auto &world = gameSession_.world();
    if (world.fireZones().zones().empty()) return;
    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer_,&previous);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    for (const auto &zone : world.fireZones().zones()) {
        if (zone.space != world.activeRaidSpaceId()) continue;
        const float r = zone.radius;
        if (!world.raidBoundsObservable({{zone.point.x-r,zone.point.y-r},{2*r,2*r}})) continue;
        const auto coverage = world.fireZoneCoverage(zone);
        if (!coverage) continue;
        const float time = static_cast<float>(zone.remainingSeconds);
        const float fade = std::clamp(time/.5F,.24F,1.0F);
        const float pulse = .5F+.5F*std::sin(8*time);
        std::vector<SDL_Vertex> vertices;
        const auto vertex = [&](Vec2 p, SDL_FColor color) {
            color.a *= fade;
            return SDL_Vertex{{p.x,p.y},color,{}};
        };
        // Both half-fans come from the fire's own solid-wall query. The same
        // narrow partitions and door gaps that protect actors shape its edge.
        for (const auto &fan : coverage->fans) {
            if (!fan.valid || fan.boundary.empty()) continue;
            const auto middle = vertex(fan.observer,{1.0F,.48F+.13F*pulse,.07F,.68F});
            for (std::size_t i=0; i<fan.boundary.size(); ++i) {
                const auto edge = [&](Vec2 p) {
                    const float fraction = std::clamp(std::hypot(p.x-zone.point.x,p.y-zone.point.y)/r,0.0F,1.0F);
                    return vertex(p,{.87F,.22F,.035F,.60F-.44F*fraction});
                };
                vertices.insert(vertices.end(),{middle,edge(fan.boundary[i]),edge(fan.boundary[(i+1)%fan.boundary.size()])});
            }
        }
        // Small amber flickers give the hazard a recognizable moving surface.
        // Check their entire triangle against the same zone before painting.
        for (unsigned i=0; i<12; ++i) {
            const float angle = i*2.399963F;
            const float distance = .76F*r*std::sqrt((i+.5F)/12.0F);
            const Vec2 foot{zone.point.x+std::cos(angle)*distance,zone.point.y+std::sin(angle)*distance};
            const float height = 4.0F + 5.0F*(.5F+.5F*std::sin(time*7+i));
            const Vec2 left{foot.x-2,foot.y},tip{foot.x+1.5F,foot.y-height},right{foot.x+2,foot.y};
            if (world.fireZoneReaches(zone,left) && world.fireZoneReaches(zone,tip) && world.fireZoneReaches(zone,right)) {
                const SDL_FColor base{1,.63F,.12F,.85F}, top{1,.91F,.42F,.92F};
                vertices.insert(vertices.end(),{vertex(left,base),vertex(tip,top),vertex(right,base)});
            }
        }
        if (!vertices.empty())
            SDL_RenderGeometry(renderer_,nullptr,vertices.data(),static_cast<int>(vertices.size()),nullptr,0);
    }
    SDL_SetRenderDrawBlendMode(renderer_,previous);
}
