#include "base_defense_positions.h"
#include "collision.h"
#include "home_perimeter_domain.h"
#include <algorithm>
#include <cmath>

namespace
{
bool overlaps(ContentRect a, ContentRect b)
{
    return isCollision({a.position, a.size}, {b.position, b.size});
}
bool within(ContentRect a, ContentRect b)
{
    return a.position.x >= b.position.x && a.position.y >= b.position.y &&
           a.position.x + a.size.x <= b.position.x + b.size.x &&
           a.position.y + a.size.y <= b.position.y + b.size.y;
}
} // namespace

bool baseDefensePositionClear(const BaseDefensePosition &position,
                              std::span<const ContentRect> blockers) noexcept
{
    return position.available &&
           std::none_of(blockers.begin(), blockers.end(), [&](ContentRect blocker) {
               return overlaps(position.circulation, blocker);
           });
}

std::array<BaseDefensePosition, 4> baseDefensePositionCandidates(
    const HomeRegionLayout &layout, std::string_view plot,
    const FortificationDefinition &definition)
{
    std::array<BaseDefensePosition, 4> result{};
    const auto &core = layout.baseParcel;
    constexpr float offset = 240;
    constexpr float clearance = 72; // includes a 48-unit actor plus navigable side clearance
    const ContentRect world{{0, 0}, layout.worldSize};
    const ContentRect buffer{{core.position.x - kHomePerimeterTransitionWidth,
                              core.position.y - kHomePerimeterTransitionWidth},
                             {core.size.x + 2 * kHomePerimeterTransitionWidth,
                              core.size.y + 2 * kHomePerimeterTransitionWidth}};
    for (std::uint32_t side = 0; side < 4; ++side)
    {
        auto &position = result[side];
        position.key = {RegionalBaseSiteDefinitionId{layout.siteDefinitionId}, std::string{plot},
                        static_cast<DefenseSide>(side)};
        // Fail closed for malformed authoring input; no geometry enters Profile.
        if (!std::isfinite(definition.length) || !std::isfinite(definition.depth) ||
            definition.length < 32 || definition.length > 240 || definition.depth < 16 ||
            definition.depth > 80)
            continue;
        for (const float fraction : {0.5F, 0.375F, 0.625F, 0.25F, 0.75F})
        {
            Vec2 center{}, outward{};
            if (side == 0)
            {
                center = {core.position.x - offset, core.position.y + core.size.y * fraction};
                outward = {-1, 0};
            }
            if (side == 1)
            {
                center = {core.position.x + core.size.x + offset,
                          core.position.y + core.size.y * fraction};
                outward = {1, 0};
            }
            if (side == 2)
            {
                center = {core.position.x + core.size.x * fraction, core.position.y - offset};
                outward = {0, -1};
            }
            if (side == 3)
            {
                center = {core.position.x + core.size.x * fraction,
                          core.position.y + core.size.y + offset};
                outward = {0, 1};
            }
            const Vec2 size = side < 2 ? Vec2{definition.depth, definition.length}
                                       : Vec2{definition.length, definition.depth};
            position.footprint = {{center.x - size.x / 2, center.y - size.y / 2}, size};
            position.circulation = {{position.footprint.position.x - clearance,
                                     position.footprint.position.y - clearance},
                                    {size.x + 2 * clearance, size.y + 2 * clearance}};
            position.outsideApproach = {center.x + outward.x * 100, center.y + outward.y * 100};
            position.insideApproach = {center.x - outward.x * 100, center.y - outward.y * 100};
            position.available = within(position.circulation, world) &&
                                 within(position.circulation, buffer) &&
                                 !overlaps(position.circulation, core);
            if (baseDefensePositionClear(position, layout.movementBlockers))
                break;
            position.available = false;
        }
    }
    return result;
}
