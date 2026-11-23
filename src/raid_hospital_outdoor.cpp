#include "raid_hospital_outdoor.h"

#include "stable_random.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace
{
struct GridPoint
{
    int x, y;
};
constexpr std::array<GridPoint, 4> neighbors{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

struct Grid
{
    ContentRect bounds;
    int width, height;
    float dx, dy;
    explicit Grid(const MapDefinition &map)
        : bounds(map.walkableBounds), width(static_cast<int>(map.proceduralOutdoor.columns)),
          height(static_cast<int>(map.proceduralOutdoor.rows)), dx(bounds.size.x / width),
          dy(bounds.size.y / height)
    {
    }
    bool contains(GridPoint p) const
    {
        return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height;
    }
    int index(GridPoint p) const
    {
        return p.y * width + p.x;
    }
    GridPoint cell(Vec2 p) const
    {
        return {static_cast<int>(std::floor((p.x - bounds.position.x) / dx)),
                static_cast<int>(std::floor((p.y - bounds.position.y) / dy))};
    }
    Vec2 world(float x, float y) const
    {
        return {bounds.position.x + x * dx, bounds.position.y + y * dy};
    }
    Vec2 center(GridPoint p) const
    {
        return world(p.x + 0.5F, p.y + 0.5F);
    }
};

std::uint32_t noiseSample(std::uint64_t seed, int x, int y) noexcept
{
    std::uint64_t h = seed ^ (static_cast<std::uint64_t>(x) * 0x9e3779b97f4a7c15ULL) ^
                      (static_cast<std::uint64_t>(y) * 0x94d049bb133111ebULL);
    h = (h ^ (h >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    return static_cast<std::uint32_t>((h ^ (h >> 27U)) >> 56U);
}

int lattice(std::uint64_t seed, int x, int y, int period) noexcept
{
    const int gx = x / period, gy = y / period, u = x % period, v = y % period;
    const auto sample = [&](int i, int j) { return static_cast<int>(noiseSample(seed, i, j)); };
    const int top = sample(gx, gy) * (period - u) + sample(gx + 1, gy) * u;
    const int bottom = sample(gx, gy + 1) * (period - u) + sample(gx + 1, gy + 1) * u;
    return (top * (period - v) + bottom * v) / (period * period);
}

void street(const Grid &grid, std::vector<std::uint8_t> &roads, GridPoint a, GridPoint b, int radius,
            RaidOutdoorRoadKind kind)
{
    const auto rank = static_cast<std::uint8_t>(static_cast<int>(kind) + 1);
    for (int y = std::min(a.y, b.y) - radius; y <= std::max(a.y, b.y) + radius; ++y)
        for (int x = std::min(a.x, b.x) - radius; x <= std::max(a.x, b.x) + radius; ++x)
            if (grid.contains({x, y}))
                roads[grid.index({x, y})] = std::max(rank, roads[grid.index({x, y})]);
}

std::vector<std::uint16_t> growDistricts(const MapDefinition &map, const Grid &grid,
                                         const std::vector<GridPoint> &seeds, std::uint64_t seed)
{
    const int width = static_cast<int>(map.proceduralOutdoor.districtColumns);
    const int height = static_cast<int>(map.proceduralOutdoor.districtRows);
    const int count = width * height;
    std::vector<std::uint16_t> owners(count);
    std::vector<int> distance(count, std::numeric_limits<int>::max());
    using Visit = std::tuple<int, std::uint16_t, int>;
    std::priority_queue<Visit, std::vector<Visit>, std::greater<Visit>> frontier;
    for (std::uint16_t theme = 0; theme < seeds.size(); ++theme)
    {
        const GridPoint p = seeds[theme];
        const int cell = (p.y * height / grid.height) * width + p.x * width / grid.width;
        if (distance[cell] == 0)
            throw std::invalid_argument("Coincident hospital district seeds");
        distance[cell] = 0;
        owners[cell] = theme;
        frontier.emplace(0, theme, cell);
    }
    while (!frontier.empty())
    {
        const auto [cost, theme, cell] = frontier.top();
        frontier.pop();
        if (distance[cell] != cost || owners[cell] != theme)
            continue;
        for (auto delta : neighbors)
        {
            const int x = cell % width + delta.x, y = cell / width + delta.y;
            if (x < 0 || y < 0 || x >= width || y >= height)
                continue;
            const int next = y * width + x;
            const int field = hospitalEnvironmentalField(seed, x * 4, y * 5);
            const int step = (delta.y ? 12 : 10) + std::abs(field - 90) / 9;
            if (cost + step >= distance[next])
                continue;
            owners[next] = theme;
            distance[next] = cost + step;
            frontier.emplace(distance[next], theme, next);
        }
    }
    return owners;
}

RaidLandmarkPlacementSnapshot site(const Grid &grid, GridPoint front,
                                   const RaidLandmarkTemplateDefinition &definition, std::uint16_t district,
                                   int verge)
{
    const Vec2 extent = definition.footprintCells;
    if (!(extent.x > 0 && extent.x <= 24 && extent.y > 0 && extent.y <= 14) ||
        definition.structures.empty())
        throw std::invalid_argument("Invalid hospital landmark footprint");
    RaidLandmarkPlacementSnapshot result;
    result.definitionId = definition.id;
    result.displayName = definition.displayName;
    result.districtInstanceId = district;
    result.bounds = {grid.world(front.x + 0.5F - extent.x * 0.5F, front.y - verge - extent.y),
                     {extent.x * grid.dx, extent.y * grid.dy}};
    for (const auto &rect : definition.structures)
    {
        if (!(rect.position.x >= 0 && rect.position.y >= 0 && rect.size.x > 0 && rect.size.y > 0 &&
              rect.position.x + rect.size.x <= 1 && rect.position.y + rect.size.y <= 1))
            throw std::invalid_argument("Invalid hospital structure geometry");
        result.structures.push_back(
            {{result.bounds.position.x + rect.position.x * result.bounds.size.x,
              result.bounds.position.y + rect.position.y * result.bounds.size.y},
             {rect.size.x * result.bounds.size.x, rect.size.y * result.bounds.size.y}});
    }
    result.roadSockets = {grid.center(front),
                          grid.center({front.x + static_cast<int>(extent.x / 3), front.y})};
    return result;
}
} // namespace

int hospitalEnvironmentalField(std::uint64_t seed, int x, int y) noexcept
{
    return (3 * lattice(seed, x, y, 20) + lattice(seed ^ 0x6d6f697374ULL, x, y, 7)) / 4;
}

bool usesHospitalOutdoorPlan(const MapDefinition &map) noexcept
{
    return map.id == kHospitalFloorplanMapId && isHospitalHybridLayoutVersion(map.proceduralOutdoor.layoutVersion);
}

HospitalOutdoorPlan planHospitalOutdoor(const MapDefinition &map,
                                        const std::vector<RaidDistrictArchetypeDefinition> &districts,
                                        std::uint64_t seed, std::uint32_t attempt)
{
    const auto &rules = map.proceduralOutdoor;
    if (rules.columns != 160 || rules.rows != 90 || rules.districtColumns != 40 || rules.districtRows != 18)
        throw std::invalid_argument("Hospital outdoor grid does not match the authored site scale");
    const Grid grid(map);
    Pcg32 random(seed, 0x484f53504954414cULL + attempt);
    const auto between = [&](int low, int high)
    { return low + static_cast<int>(random.bounded(high - low + 1)); };
    const int avenue = between(39, 46), west = between(10, 19), east = between(140, 149);
    const int hospital = between(73, 84), clinic = between(34, 43), depot = between(110, 123);
    const int south = between(65, 75), north = between(18, 22);
    const bool northBypass = random.bounded(2) != 0;
    const int ring = northBypass ? north : south;
    const int service = northBypass ? south : north;
    const int clinicStreet = random.bounded(2) ? avenue : ring;
    const int depotStreet = random.bounded(2) ? service : ring;
    const auto westApproach = hospital - between(19, 26);
    const auto eastApproach = hospital + 17;
    HospitalOutdoorPlan plan;
    plan.playerApproach = grid.center({westApproach, avenue});
    plan.roadRanks.resize(grid.width * grid.height);
    const auto line = [&](int x, int y, int xx, int yy, int radius, RaidOutdoorRoadKind kind)
    { street(grid, plan.roadRanks, {x, y}, {xx, yy}, radius, kind); };
    const auto side = [&](int x, int y, int xx, int yy)
    { line(x, y, xx, yy, 1, RaidOutdoorRoadKind::Secondary); };
    // The ambulance avenue changes alignment on its arrival at the western perimeter.
    const int gateway = west - 5;
    const int entryRow = avenue + (random.bounded(2) ? 5 : -5);
    line(3, entryRow, gateway, entryRow, 2, RaidOutdoorRoadKind::Primary);
    line(gateway, entryRow, gateway, avenue, 2, RaidOutdoorRoadKind::Primary);
    line(gateway, avenue, grid.width - 4, avenue, 2, RaidOutdoorRoadKind::Primary);
    side(west, avenue, west, ring);
    side(west, ring, east, ring);
    side(east, ring, east, avenue);
    side(westApproach, avenue, westApproach, ring);
    side(eastApproach, avenue, eastApproach, ring);
    // The second service precinct can join one side of the avenue or both.
    const int returnStreet = random.bounded(2) ? east : west;
    const int serviceWest = returnStreet == east ? westApproach : west;
    side(serviceWest, service, east, service);
    side(returnStreet, service, returnStreet, avenue);
    if (returnStreet == east || random.bounded(2))
        side(serviceWest, avenue, serviceWest, service);
    const int cross = between(124, 133);
    side(cross, avenue, cross, service);
    // Access lanes carry nearby placements without adding another full-width arterial.
    line(west, ring, west, northBypass ? 5 : 84, 0, RaidOutdoorRoadKind::Access);
    line(east, service, east, northBypass ? 84 : 5, 0, RaidOutdoorRoadKind::Access);

    const std::array<std::pair<std::string_view, GridPoint>, 8> districtSites{
        {{"campus", {hospital, avenue - 3}},
         {"parking", {westApproach, avenue + 2}},
         {"access", {east - 2, avenue}},
         {"shops", {clinic, clinicStreet - 2}},
         {"residential", {48, service - 4}},
         {"park", {west - 2, avenue + 4}},
         {"service_yard", {depot, depotStreet - 3}},
         {"waste_ground", {101, north - 5}}}};
    std::vector<GridPoint> sites;
    for (const auto &theme : districts)
    {
        const auto found =
            std::find_if(districtSites.begin(), districtSites.end(), [&](const auto &entry)
                         { return theme.id == "district.hospital." + std::string(entry.first); });
        if (found == districtSites.end())
            throw std::invalid_argument("Unknown hospital district");
        sites.push_back(found->second);
    }
    if (sites.size() != districtSites.size())
        throw std::invalid_argument("Incomplete hospital district set");
    plan.owners = growDistricts(map, grid, sites, seed ^ attempt);
    std::vector<std::size_t> areas(districts.size());
    bool campusAdjoinsParking = false;
    for (int row = 0; row < static_cast<int>(rules.districtRows); ++row)
        for (int column = 0; column < static_cast<int>(rules.districtColumns); ++column)
        {
            const auto cell = row * rules.districtColumns + column;
            const auto owner = plan.owners[cell];
            ++areas[owner];
            if (districts[owner].id != kHospitalRescueParkingDistrict)
                continue;
            for (auto delta : neighbors)
            {
                const int x = column + delta.x, y = row + delta.y;
                if (x < 0 || y < 0 || x >= static_cast<int>(rules.districtColumns) ||
                    y >= static_cast<int>(rules.districtRows))
                    continue;
                const auto other = plan.owners[y * rules.districtColumns + x];
                if (districts[other].id == kHospitalRescueCampusDistrict)
                    campusAdjoinsParking = true;
            }
        }
    // Refuse isolated ambulance access or sliver/dominant wards before freezing anything.
    if (!campusAdjoinsParking ||
        std::any_of(areas.begin(), areas.end(), [&](auto area)
                    { return area * 100 < plan.owners.size() || area * 3 > plan.owners.size(); }))
        return {};
    const auto addSite = [&](std::string_view id, std::string_view district, GridPoint frontage)
    {
        const auto selected = std::find_if(rules.landmarkTemplates.begin(), rules.landmarkTemplates.end(),
                                           [&](const auto &t) { return t.id == id; });
        const auto owner = std::find_if(districts.begin(), districts.end(),
                                        [&](const auto &d) { return d.id == district; });
        if (selected == rules.landmarkTemplates.end() || owner == districts.end())
            throw std::invalid_argument("Missing authored hospital site");
        plan.landmarks.push_back(site(grid, frontage, *selected,
                                      static_cast<std::uint16_t>(owner - districts.begin() + 1),
                                      frontage.y == avenue ? 2 : 1));
    };
    addSite("landmark.hospital.main_building", "district.hospital.campus", {hospital, avenue});
    addSite("landmark.hospital.service_clinic", "district.hospital.shops", {clinic, clinicStreet});
    addSite("landmark.hospital.transport_depot", "district.hospital.service_yard", {depot, depotStreet});
    // A secondary street can meet the transport shed from either side. Reject
    // a crossing through any building, even when the two hospital routes work.
    for (const auto &landmark : plan.landmarks)
        for (const auto &structure : landmark.structures)
        {
            const auto first = grid.cell(structure.position);
            const auto right = structure.position.x + structure.size.x;
            const auto bottom = structure.position.y + structure.size.y;
            const auto last = grid.cell({std::nextafter(right, structure.position.x),
                                         std::nextafter(bottom, structure.position.y)});
            if (!grid.contains(first) || !grid.contains(last))
                return {};
            for (int y = first.y; y <= last.y; ++y)
                for (int x = first.x; x <= last.x; ++x)
                    if (plan.roadRanks[grid.index({x, y})] != 0)
                        return {};
        }
    return plan;
}

namespace
{
struct ParcelPiece
{
    RaidOutdoorPropKind kind;
    float x, y, width, height;
};
using Kind = RaidOutdoorPropKind;
constexpr std::array<std::array<ParcelPiece, 3>, 3> parcelParts{
    {{{{Kind::Warehouse, 0.6F, 3.0F, 6.8F, 2.1F},
       {Kind::Warehouse, 0.6F, 1.4F, 2.1F, 1.1F},
       {Kind::Container, 6.0F, 0.9F, 1.4F, 0.7F}}},
     {{{Kind::Factory, 1.2F, 3.9F, 5.5F, 1.5F},
       {Kind::Warehouse, 0.7F, 1.1F, 1.1F, 2.3F},
       {Kind::RoadBarrier, 6.4F, 0.8F, 0.5F, 2.5F}}},
     {{{Kind::Warehouse, 0.9F, 2.3F, 2.6F, 2.8F},
       {Kind::Container, 4.5F, 3.7F, 2.7F, 0.9F},
       {Kind::EngineeringEquipment, 5.7F, 1.3F, 1.3F, 1.3F}}}}};

// Local frontage is the eight-cell side at v=0. The gate stays clear between the side wings.
HospitalRoadsideParcel parcelAt(const Grid &grid, GridPoint origin, int rotation,
                                HospitalParcelFamily family)
{
    const auto transform = [&](float u, float v) -> Vec2
    {
        switch (rotation)
        {
        case 0:
            return grid.world(origin.x + u, origin.y + v);
        case 1:
            return grid.world(origin.x + 6 - v, origin.y + u);
        case 2:
            return grid.world(origin.x + 8 - u, origin.y + 6 - v);
        default:
            return grid.world(origin.x + v, origin.y + 8 - u);
        }
    };
    HospitalRoadsideParcel plot;
    plot.family = family;
    plot.quarterTurns = static_cast<std::uint8_t>(rotation);
    plot.bounds = {grid.world(origin.x, origin.y),
                   {(rotation % 2 ? 6 : 8) * grid.dx, (rotation % 2 ? 8 : 6) * grid.dy}};
    plot.gate = transform(4, 0);
    for (std::size_t i = 0; i < plot.parts.size(); ++i)
    {
        const auto &p = parcelParts[static_cast<std::size_t>(family)][i];
        const auto a = transform(p.x, p.y);
        const auto b = transform(p.x + p.width, p.y + p.height);
        plot.parts[i] = {
            0,
            p.kind,
            RaidOutdoorPropState::Weathered,
            {{std::min(a.x, b.x), std::min(a.y, b.y)}, {std::abs(b.x - a.x), std::abs(b.y - a.y)}},
            plot.quarterTurns,
            true};
    }
    return plot;
}
} // namespace

std::vector<HospitalRoadsideParcel> hospitalParcelCandidates(const MapDefinition &map,
                                                             const std::vector<std::uint8_t> &roads,
                                                             std::uint64_t seed)
{
    const Grid grid(map);
    std::vector<HospitalRoadsideParcel> result;
    for (int y = 1; y < grid.height - 8; ++y)
        for (int x = 1; x < grid.width - 8; ++x)
        {
            if (roads[grid.index({x, y})])
                continue;
            for (int orientation = 0; orientation < 4; ++orientation)
            {
                const std::array<GridPoint, 4> faces{
                    {{x + 4, y - 1}, {x + 6, y + 4}, {x + 4, y + 6}, {x - 1, y + 4}}};
                if (roads[grid.index(faces[orientation])] < 2)
                    continue;
                auto pick = noiseSample(seed, x + orientation, y);
                result.push_back(
                    parcelAt(grid, {x, y}, orientation, static_cast<HospitalParcelFamily>(pick % 3)));
            }
        }
    Pcg32 order(seed, 0x70617263656cULL);
    for (std::size_t i = result.size(); i > 1; --i)
        std::swap(result[i - 1], result[order.bounded(static_cast<std::uint32_t>(i))]);
    return result;
}

std::vector<HospitalRoadsideParcel> inspectHospitalParcels(const MapDefinition &map,
                                                           const RaidGeneratedMapLayout &layout)
{
    const Grid grid(map);
    std::vector<HospitalRoadsideParcel> result;
    for (std::size_t pos = 0; pos + 2 < layout.props.size(); ++pos)
    {
        const auto &head = layout.props[pos];
        if (head.kind != Kind::Factory && head.kind != Kind::Warehouse)
            continue;
        for (auto family : {HospitalParcelFamily::Shopfront, HospitalParcelFamily::Courtyard,
                            HospitalParcelFamily::ServiceShed})
        {
            const auto prototype = parcelAt(grid, {0, 0}, head.quarterTurns, family);
            const Vec2 offset{head.bounds.position.x - prototype.parts[0].bounds.position.x,
                              head.bounds.position.y - prototype.parts[0].bounds.position.y};
            const GridPoint origin{static_cast<int>(std::lround(offset.x / grid.dx)),
                                   static_cast<int>(std::lround(offset.y / grid.dy))};
            auto candidate = parcelAt(grid, origin, head.quarterTurns, family);
            bool match = true;
            for (std::size_t i = 0; i < 3; ++i)
            {
                auto &expected = candidate.parts[i];
                expected.instanceId = layout.props[pos + i].instanceId;
                if (!(expected == layout.props[pos + i]))
                    match = false;
            }
            if (match)
            {
                result.push_back(candidate);
                pos += 2;
                break;
            }
        }
    }
    return result;
}

HospitalOutdoorRouteReport inspectHospitalOutdoorRoutes(const MapDefinition &map,
                                                        const RaidGeneratedMapLayout &layout)
{
    HospitalOutdoorRouteReport report;
    if (!usesHospitalOutdoorPlan(map) || layout.landmarks.empty())
        return report;
    const Grid grid(map);
    if (grid.width != 160 || grid.height != 90)
        return report;
    const auto count = grid.width * grid.height;
    std::vector<std::uint8_t> road(count), blocked(count);
    for (const auto &r : layout.roadCells)
    {
        if (!grid.contains({r.column, r.row}))
            return report;
        road[grid.index({r.column, r.row})] = static_cast<std::uint8_t>(static_cast<int>(r.kind) + 1);
    }
    // Reserve the entire swept 32x32 player on every cardinal segment. A cell
    // touching an expanded blocker is conservatively excluded from both paths.
    constexpr float clearance = 17.0F;
    for (const auto &b : layout.ballisticBlockers)
    {
        const auto first = grid.cell({b.position.x - clearance, b.position.y - clearance});
        const auto last =
            grid.cell({b.position.x + b.size.x + clearance, b.position.y + b.size.y + clearance});
        for (int y = std::max(0, first.y); y <= std::min(grid.height - 1, last.y); ++y)
            for (int x = std::max(0, first.x); x <= std::min(grid.width - 1, last.x); ++x)
                blocked[grid.index({x, y})] = 1;
    }
    const auto &hospital = layout.landmarks.front();
    if (hospital.roadSockets.empty())
        return report;
    const auto target = grid.cell(hospital.roadSockets.front());
    const auto *spawn = findRaidAnchorPlacement(layout, kRaidAnchorPlayerSpawn);
    if (!spawn)
        return report;
    const GridPoint start = grid.cell({spawn->bounds.position.x + spawn->bounds.size.x * 0.5F,
                                       spawn->bounds.position.y + spawn->bounds.size.y * 0.5F});
    if (!grid.contains(start) || !grid.contains(target))
        return report;
    const auto visit = [&](GridPoint source, auto allowed)
    {
        std::vector<int> parent(count, -1), queue;
        if (!grid.contains(source) || blocked[grid.index(source)] || !allowed(source))
            return parent;
        queue.push_back(grid.index(source));
        parent[queue.front()] = queue.front();
        for (std::size_t i = 0; i < queue.size(); ++i)
        {
            const int current = queue[i];
            for (auto delta : neighbors)
            {
                GridPoint next{current % grid.width + delta.x, current / grid.width + delta.y};
                if (!grid.contains(next))
                    continue;
                const int cell = grid.index(next);
                if (parent[cell] != -1 || blocked[cell] || !allowed(next))
                    continue;
                parent[cell] = current;
                queue.push_back(cell);
            }
        }
        return parent;
    };
    const auto extract = [&](const std::vector<int> &parent)
    {
        std::vector<Vec2> points;
        for (int cell = grid.index(target); parent[cell] >= 0; cell = parent[cell])
        {
            points.push_back(grid.center({cell % grid.width, cell / grid.width}));
            if (parent[cell] == cell)
                break;
        }
        std::reverse(points.begin(), points.end());
        return points;
    };
    report.mainRoute = extract(visit(start, [&](GridPoint p) { return road[grid.index(p)] == 3; }));
    report.bypassRoute = extract(visit(
        start, [&](GridPoint p)
        { return road[grid.index(p)] != 0 && (p.x != target.x - 10 || std::abs(p.y - target.y) > 8); }));
    // Rescue transfer points are selected on a reachable forecourt after the roads
    // are frozen. Their approach can legitimately cross open ground.
    const auto reached = visit(target, [](GridPoint) { return true; });
    const auto reachable = [&](Vec2 p)
    {
        auto cell = grid.cell(p);
        return grid.contains(cell) && reached[grid.index(cell)] >= 0;
    };
    report.anchorsReachable =
        std::all_of(layout.anchorPlacements.begin(), layout.anchorPlacements.end(),
                    [&](const auto &a)
                    {
                        if (layout.layoutVersion == kHospitalLandmarkInteriorLayoutVersion &&
                            a.kind == RaidMapAnchorKind::InteriorEntrance)
                        {
                            const Vec2 arrival = raidExteriorReturnPoint(map, a.bounds);
                            // A portal passes the top-left to Player::setPosition, not the body's center.
                            constexpr float playerSize = 32.0F;
                            if (!reachable({arrival.x + playerSize / 2, arrival.y + playerSize / 2}) ||
                                std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
                                    [&](ContentRect b) {
                                        return arrival.x + playerSize > b.position.x && arrival.x < b.position.x + b.size.x &&
                                               arrival.y + playerSize > b.position.y && arrival.y < b.position.y + b.size.y;
                                    }))
                                return false;
                        }
                        return reachable({a.bounds.position.x + a.bounds.size.x * 0.5F,
                                          a.bounds.position.y + a.bounds.size.y * 0.5F});
                    });
    for (const auto &landmark : layout.landmarks)
        for (auto socket : landmark.roadSockets)
            if (!reachable(socket))
                report.anchorsReachable = false;
    return report;
}
