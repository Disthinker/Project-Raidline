#include "home_region_layout.h"
#include "home_founding_types.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

#include "collision.h"
#include "stable_random.h"

namespace
{
constexpr float kCellSize{80.0F};
constexpr std::uint32_t kColumns{160U};
constexpr std::uint32_t kRows{90U};

std::uint64_t fnv1a(std::string_view text) noexcept
{
    std::uint64_t hash{1469598103934665603ULL};
    for (const unsigned char character : text)
    {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void hashValue(std::uint64_t &hash, std::uint64_t value) noexcept
{
    hash ^= value;
    hash *= 1099511628211ULL;
}

void hashFloat(std::uint64_t &hash, float value) noexcept
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

bool overlapsWithMargin(ContentRect left, ContentRect right, float margin)
{
    const Rect expandedLeft{
        {left.position.x - margin, left.position.y - margin},
        {left.size.x + margin * 2.0F, left.size.y + margin * 2.0F}};
    return isCollision(expandedLeft, Rect{right.position, right.size});
}

bool rectTouchesRoad(
    ContentRect bounds,
    const std::vector<bool> &roadMask,
    bool requireRoad)
{
    const std::uint32_t firstColumn = static_cast<std::uint32_t>(
        std::clamp(std::floor(bounds.position.x / kCellSize),
                   0.0F, static_cast<float>(kColumns - 1U)));
    const std::uint32_t lastColumn = static_cast<std::uint32_t>(
        std::clamp(std::floor((bounds.position.x + bounds.size.x - 1.0F) /
                              kCellSize),
                   0.0F, static_cast<float>(kColumns - 1U)));
    const std::uint32_t firstRow = static_cast<std::uint32_t>(
        std::clamp(std::floor(bounds.position.y / kCellSize),
                   0.0F, static_cast<float>(kRows - 1U)));
    const std::uint32_t lastRow = static_cast<std::uint32_t>(
        std::clamp(std::floor((bounds.position.y + bounds.size.y - 1.0F) /
                              kCellSize),
                   0.0F, static_cast<float>(kRows - 1U)));
    bool touches{};
    for (std::uint32_t row = firstRow; row <= lastRow; ++row)
        for (std::uint32_t column = firstColumn; column <= lastColumn; ++column)
            touches = touches || roadMask[row * kColumns + column];
    return requireRoad ? touches : !touches;
}

void appendRoadCell(
    HomeRegionLayout &layout,
    std::vector<bool> &roadMask,
    std::uint32_t column,
    std::uint32_t row,
    RaidOutdoorRoadKind kind)
{
    if (column >= kColumns || row >= kRows)
        return;
    const std::size_t index = row * kColumns + column;
    if (!roadMask[index])
    {
        roadMask[index] = true;
        layout.roadCells.push_back(RaidOutdoorRoadCell{
            static_cast<std::uint16_t>(column),
            static_cast<std::uint16_t>(row), kind});
    }
}

void appendHorizontalRoad(
    HomeRegionLayout &layout,
    std::vector<bool> &roadMask,
    std::uint32_t centerRow,
    std::uint32_t width,
    RaidOutdoorRoadKind kind)
{
    const std::uint32_t first = centerRow - width / 2U;
    for (std::uint32_t row = first; row < first + width; ++row)
        for (std::uint32_t column{}; column < kColumns; ++column)
            appendRoadCell(layout, roadMask, column, row, kind);
}

void appendVerticalRoad(
    HomeRegionLayout &layout,
    std::vector<bool> &roadMask,
    std::uint32_t centerColumn,
    std::uint32_t width,
    RaidOutdoorRoadKind kind)
{
    const std::uint32_t first = centerColumn - width / 2U;
    for (std::uint32_t column = first; column < first + width; ++column)
        for (std::uint32_t row{}; row < kRows; ++row)
            appendRoadCell(layout, roadMask, column, row, kind);
}

ContentRect parcelForSite(std::uint64_t seed)
{
    constexpr std::array<std::uint32_t, 4> columns{10U, 50U, 90U, 130U};
    constexpr std::array<std::uint32_t, 3> rows{7U, 38U, 69U};
    const std::uint32_t column = columns[seed % columns.size()];
    const std::uint32_t row = rows[(seed >> 8U) % rows.size()];
    return ContentRect{{column * kCellSize, row * kCellSize},
                       {20.0F * kCellSize, 14.0F * kCellSize}};
}

RaidTerrainKind terrainForBand(std::uint32_t row, std::uint32_t band)
{
    if (row < 30U)
        return band == 0U ? RaidTerrainKind::Grass
                          : band == 1U ? RaidTerrainKind::Concrete
                                       : RaidTerrainKind::Dirt;
    if (row < 60U)
        return band == 0U ? RaidTerrainKind::Concrete
                          : band == 1U ? RaidTerrainKind::Asphalt
                                       : RaidTerrainKind::Dirt;
    return band == 0U ? RaidTerrainKind::Grass
                      : band == 1U ? RaidTerrainKind::Dirt
                                   : RaidTerrainKind::Concrete;
}

Vec2 propSize(RaidOutdoorPropKind kind, bool rotated) noexcept
{
    Vec2 result{};
    switch (kind)
    {
    case RaidOutdoorPropKind::Factory: result = {720.0F, 420.0F}; break;
    case RaidOutdoorPropKind::Warehouse: result = {560.0F, 360.0F}; break;
    case RaidOutdoorPropKind::Container: result = {192.0F, 80.0F}; break;
    case RaidOutdoorPropKind::EngineeringEquipment:
        result = {240.0F, 144.0F}; break;
    case RaidOutdoorPropKind::Car: result = {224.0F, 96.0F}; break;
    case RaidOutdoorPropKind::Truck: result = {400.0F, 128.0F}; break;
    case RaidOutdoorPropKind::RoadBarrier: result = {160.0F, 64.0F}; break;
    case RaidOutdoorPropKind::Debris: result = {48.0F, 48.0F}; break;
    }
    if (rotated)
        std::swap(result.x, result.y);
    return result;
}

void appendProps(
    HomeRegionLayout &layout,
    const std::vector<bool> &roadMask,
    Pcg32 &random,
    RaidOutdoorPropKind kind,
    std::uint32_t count)
{
    const bool roadProp = kind == RaidOutdoorPropKind::Car ||
        kind == RaidOutdoorPropKind::Truck ||
        kind == RaidOutdoorPropKind::RoadBarrier;
    const bool collidable = kind != RaidOutdoorPropKind::Debris;
    for (std::uint32_t ordinal{}; ordinal < count; ++ordinal)
    {
        bool placed{};
        for (std::uint32_t attempt{}; attempt < 600U && !placed; ++attempt)
        {
            const bool rotated = random.bounded(2U) != 0U;
            const Vec2 size = propSize(kind, rotated);
            const float x = static_cast<float>(random.bounded(
                static_cast<std::uint32_t>((layout.worldSize.x - size.x -
                                            160.0F) / 40.0F))) * 40.0F + 80.0F;
            const float y = static_cast<float>(random.bounded(
                static_cast<std::uint32_t>((layout.worldSize.y - size.y -
                                            160.0F) / 40.0F))) * 40.0F + 80.0F;
            const ContentRect candidate{{x, y}, size};
            if (overlapsWithMargin(candidate, layout.baseParcel, 120.0F) ||
                !rectTouchesRoad(candidate, roadMask, roadProp))
                continue;
            bool overlaps{};
            for (const RaidOutdoorPropSnapshot &existing : layout.props)
            {
                if (overlapsWithMargin(candidate, existing.bounds,
                                       kind == RaidOutdoorPropKind::Debris
                                           ? 12.0F : 32.0F))
                {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps)
                continue;
            const std::uint32_t instanceId =
                static_cast<std::uint32_t>(layout.props.size() + 1U);
            layout.props.push_back(RaidOutdoorPropSnapshot{
                instanceId, kind,
                static_cast<RaidOutdoorPropState>(random.bounded(4U)),
                candidate, static_cast<std::uint8_t>(rotated ? 1U : 0U),
                collidable});
            if (collidable)
                layout.movementBlockers.push_back(candidate);
            placed = true;
        }
    }
}
}

HomeRegionLayout generateHomeRegionLayout(std::string_view siteDefinitionId)
{
    HomeRegionLayout layout;
    layout.siteDefinitionId = siteDefinitionId.empty()
        ? "regional_base_site.greyline_yard" : std::string{siteDefinitionId};
    const std::uint64_t seed = fnv1a(layout.siteDefinitionId);
    layout.baseParcel = parcelForSite(seed);

    layout.districts = {
        {1U, HomeRegionDistrictKind::Industrial,
         {{0.0F, 0.0F}, {4266.0F, 2400.0F}}, {1800.0F, 760.0F}},
        {2U, HomeRegionDistrictKind::Logistics,
         {{4266.0F, 0.0F}, {4267.0F, 2400.0F}}, {6100.0F, 760.0F}},
        {3U, HomeRegionDistrictKind::RoadsideService,
         {{8533.0F, 0.0F}, {4267.0F, 2400.0F}}, {10400.0F, 760.0F}},
        {4U, HomeRegionDistrictKind::Highway,
         {{0.0F, 2400.0F}, {12800.0F, 2400.0F}}, {6000.0F, 3120.0F}},
        {5U, HomeRegionDistrictKind::OpenGround,
         {{0.0F, 4800.0F}, {4266.0F, 2400.0F}}, {1800.0F, 5600.0F}},
        {6U, HomeRegionDistrictKind::Greenbelt,
         {{4266.0F, 4800.0F}, {4267.0F, 2400.0F}}, {6100.0F, 5600.0F}},
        {7U, HomeRegionDistrictKind::Industrial,
         {{8533.0F, 4800.0F}, {4267.0F, 2400.0F}}, {10400.0F, 5600.0F}},
        {8U, HomeRegionDistrictKind::Base,
         layout.baseParcel,
         {layout.baseParcel.position.x + layout.baseParcel.size.x * 0.5F,
          layout.baseParcel.position.y + 72.0F}}};

    for (std::uint32_t row{}; row < kRows; ++row)
    {
        constexpr std::array<std::uint16_t, 3> starts{0U, 54U, 107U};
        constexpr std::array<std::uint16_t, 3> lengths{54U, 53U, 53U};
        for (std::uint32_t band{}; band < 3U; ++band)
            layout.terrainSpans.push_back(RaidTerrainSpan{
                static_cast<std::uint16_t>(row), starts[band], lengths[band],
                terrainForBand(row, band)});
    }
    Pcg32 terrainRandom{seed ^ 0x7465727261696eULL};
    for (std::uint32_t puddle{}; puddle < 72U; ++puddle)
    {
        const std::uint16_t length = static_cast<std::uint16_t>(
            1U + terrainRandom.bounded(4U));
        layout.terrainSpans.push_back(RaidTerrainSpan{
            static_cast<std::uint16_t>(terrainRandom.bounded(kRows)),
            static_cast<std::uint16_t>(
                terrainRandom.bounded(kColumns - length)),
            length, RaidTerrainKind::Puddle});
    }

    std::vector<bool> roadMask(kColumns * kRows, false);
    appendHorizontalRoad(layout, roadMask, 30U, 4U,
                         RaidOutdoorRoadKind::Primary);
    appendHorizontalRoad(layout, roadMask, 60U, 4U,
                         RaidOutdoorRoadKind::Primary);
    appendVerticalRoad(layout, roadMask, 40U, 4U,
                       RaidOutdoorRoadKind::Secondary);
    appendVerticalRoad(layout, roadMask, 80U, 4U,
                       RaidOutdoorRoadKind::Primary);
    appendVerticalRoad(layout, roadMask, 120U, 4U,
                       RaidOutdoorRoadKind::Secondary);

    const std::uint32_t baseFirstColumn = static_cast<std::uint32_t>(
        layout.baseParcel.position.x / kCellSize);
    const std::uint32_t baseLastColumn = static_cast<std::uint32_t>(
        (layout.baseParcel.position.x + layout.baseParcel.size.x) /
        kCellSize) - 1U;
    const std::uint32_t baseCenterColumn = static_cast<std::uint32_t>(
        (layout.baseParcel.position.x + layout.baseParcel.size.x * 0.5F) /
        kCellSize);
    const std::uint32_t baseCenterRow = static_cast<std::uint32_t>(
        (layout.baseParcel.position.y + layout.baseParcel.size.y * 0.5F) /
        kCellSize);
    const std::uint32_t nearestVertical =
        std::min({40U, 80U, 120U}, [&](std::uint32_t left, std::uint32_t right)
                 { return std::abs(static_cast<int>(left) -
                                   static_cast<int>(baseCenterColumn)) <
                          std::abs(static_cast<int>(right) -
                                   static_cast<int>(baseCenterColumn)); });
    const std::uint32_t parcelRoadEdge = nearestVertical < baseFirstColumn
        ? baseFirstColumn - 1U : baseLastColumn + 1U;
    for (std::uint32_t column = std::min(parcelRoadEdge, nearestVertical);
         column <= std::max(parcelRoadEdge, nearestVertical); ++column)
        for (std::uint32_t width{}; width < 2U; ++width)
            appendRoadCell(layout, roadMask, column,
                           std::min(kRows - 1U, baseCenterRow + width),
                           RaidOutdoorRoadKind::Access);

    Pcg32 propRandom{seed ^ 0x70726f7073ULL};
    appendProps(layout, roadMask, propRandom, RaidOutdoorPropKind::Factory, 10U);
    appendProps(layout, roadMask, propRandom, RaidOutdoorPropKind::Warehouse, 16U);
    appendProps(layout, roadMask, propRandom, RaidOutdoorPropKind::Container, 36U);
    appendProps(layout, roadMask, propRandom,
                RaidOutdoorPropKind::EngineeringEquipment, 20U);
    appendProps(layout, roadMask, propRandom, RaidOutdoorPropKind::Car, 32U);
    appendProps(layout, roadMask, propRandom, RaidOutdoorPropKind::Truck, 14U);
    appendProps(layout, roadMask, propRandom,
                RaidOutdoorPropKind::RoadBarrier, 22U);
    appendProps(layout, roadMask, propRandom, RaidOutdoorPropKind::Debris, 120U);

    layout.layoutHash = homeRegionLayoutHash(layout);
    return layout;
}

HomeRegionLayout generateFoundingHomeRegionLayout(
    std::string_view siteDefinitionId, std::string_view plotId)
{
    auto layout = generateHomeRegionLayout(siteDefinitionId);
    layout.layoutVersion = 2U;
    const auto *selected = homePlotDefinition(plotId);
    layout.baseParcel = selected ? selected->bounds : kFoundingCamp;
    layout.districts.back().bounds = layout.baseParcel;
    layout.districts.back().labelPosition = layout.baseParcel.position;
    // Reserve every candidate and its connection before placing authored
    // obstacles. Selection changes only the active parcel, not world props.
    std::erase_if(layout.props, [&](const auto &prop) {
        if (overlapsWithMargin(prop.bounds, kFoundingCamp, 100)) return true;
        for (const auto &plot : homePlotDefinitions())
        {
            if (overlapsWithMargin(prop.bounds, plot.bounds, 100)) return true;
            const ContentRect access{{plot.corePosition.x - 100, 1560}, {200, 1000}};
            if (overlapsWithMargin(prop.bounds, access, 40)) return true;
        }
        return overlapsWithMargin(prop.bounds, {{700,2240},{7100,200}}, 40);
    });
    layout.movementBlockers.clear();
    for (const auto &prop : layout.props)
        if (prop.collidable) layout.movementBlockers.push_back(prop.bounds);
    for (const auto &plot : homePlotDefinitions())
        for (const auto &bounds : plot.fixedBlockers)
        {
            RaidOutdoorPropSnapshot prop{};
            prop.instanceId = 30000U + static_cast<std::uint32_t>(layout.props.size());
            prop.kind = RaidOutdoorPropKind::Warehouse;
            prop.bounds = bounds;
            prop.collidable = true;
            layout.props.push_back(prop);
            layout.movementBlockers.push_back(bounds);
        }
    std::vector<bool> roads(kColumns * kRows, false);
    for (const auto &road : layout.roadCells) roads[road.row*kColumns + road.column] = true;
    for (std::uint32_t row = 28; row <= 30; ++row)
        for (std::uint32_t column = 10; column <= 97; ++column)
            appendRoadCell(layout, roads, column, row, RaidOutdoorRoadKind::Access);
    for (const auto &plot : homePlotDefinitions())
        for (std::uint32_t row = 20; row <= 30; ++row)
            for (std::uint32_t width = 0; width < 2; ++width)
                appendRoadCell(layout, roads,
                    static_cast<std::uint32_t>(plot.corePosition.x / kCellSize) + width,
                    row, RaidOutdoorRoadKind::Access);
    layout.layoutHash = homeRegionLayoutHash(layout);
    return layout;
}

std::uint64_t homeRegionLayoutHash(const HomeRegionLayout &layout) noexcept
{
    std::uint64_t hash{1469598103934665603ULL};
    hashValue(hash, layout.layoutVersion);
    for (const unsigned char character : layout.siteDefinitionId)
        hashValue(hash, character);
    hashFloat(hash, layout.worldSize.x);
    hashFloat(hash, layout.worldSize.y);
    hashFloat(hash, layout.baseParcel.position.x);
    hashFloat(hash, layout.baseParcel.position.y);
    for (const RaidOutdoorRoadCell &road : layout.roadCells)
    {
        hashValue(hash, road.column);
        hashValue(hash, road.row);
        hashValue(hash, static_cast<std::uint8_t>(road.kind));
    }
    for (const RaidOutdoorPropSnapshot &prop : layout.props)
    {
        hashValue(hash, prop.instanceId);
        hashValue(hash, static_cast<std::uint8_t>(prop.kind));
        hashFloat(hash, prop.bounds.position.x);
        hashFloat(hash, prop.bounds.position.y);
        hashFloat(hash, prop.bounds.size.x);
        hashFloat(hash, prop.bounds.size.y);
    }
    return hash;
}

const char *homeRegionDistrictName(HomeRegionDistrictKind kind) noexcept
{
    switch (kind)
    {
    case HomeRegionDistrictKind::Base: return "SAFE BASE PARCEL";
    case HomeRegionDistrictKind::Industrial: return "INDUSTRIAL BLOCK";
    case HomeRegionDistrictKind::Logistics: return "LOGISTICS YARD";
    case HomeRegionDistrictKind::Highway: return "HIGHWAY CORRIDOR";
    case HomeRegionDistrictKind::OpenGround: return "OPEN GROUND";
    case HomeRegionDistrictKind::Greenbelt: return "DRAINAGE GREENBELT";
    case HomeRegionDistrictKind::RoadsideService: return "ROADSIDE SERVICE";
    }
    return "UNKNOWN DISTRICT";
}
