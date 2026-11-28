#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <map>
#include <set>

#include "content_registry.h"
#include "raid_map_generation.h"
#include "raid_district_fill.h"
#include "hospital_content_candidate.h"
#include "raid_optics_legacy_fixture.h"
#include "raid_hospital_outdoor.h"
#include "raid_lifecycle.h"

namespace
{
Vec2 center(ContentRect rect)
{
    return {rect.position.x + rect.size.x * 0.5F,
            rect.position.y + rect.size.y * 0.5F};
}

RaidMapGenerationAnchors publishedAnchors(const MapDefinition &map)
{
    RaidMapGenerationAnchors anchors;
    anchors.playerSpawn = map.spawnExtractionPairs.front().playerSpawn;
    anchors.extractionPoint =
        map.spawnExtractionPairs.front().extractionPoint;
    anchors.occupiedRegions = {
        map.highRisk.emergencyExtractionPoint,
        map.highRisk.conditionalExtractionPoint,
        map.highRisk.activationControlPoint,
        map.highRisk.advancedResourceArea};
    anchors.reachablePoints = {
        center(map.highRisk.emergencyExtractionPoint),
        center(map.highRisk.conditionalExtractionPoint),
        center(map.highRisk.activationControlPoint),
        center(map.highRisk.advancedResourceArea)};
    if (map.rescue.has_value())
    {
        anchors.occupiedRegions.push_back(map.rescue->transferPoint);
        anchors.reachablePoints.push_back(center(map.rescue->transferPoint));
    }
    for (const RaidLootSlotDefinition &slot : map.raidLootSlots)
    {
        anchors.reachablePoints.push_back(slot.position);
    }
    if (map.proceduralOutdoor.layoutVersion >= 3U)
    {
        anchors.requests = {
            {std::string{kRaidAnchorPlayerSpawn},
             RaidMapAnchorKind::PlayerSpawn, {50.0F, 50.0F}},
            {std::string{kRaidAnchorNormalExtraction},
             RaidMapAnchorKind::NormalExtraction,
             map.spawnExtractionPairs.front().extractionPoint.size},
            {std::string{kRaidAnchorEmergencyExtraction},
             RaidMapAnchorKind::EmergencyExtraction,
             map.highRisk.emergencyExtractionPoint.size},
            {std::string{kRaidAnchorConditionalExtraction},
             RaidMapAnchorKind::ConditionalExtraction,
             map.highRisk.conditionalExtractionPoint.size},
            {std::string{kRaidAnchorHighRiskControl},
             RaidMapAnchorKind::HighRiskControl,
             map.highRisk.activationControlPoint.size},
            {std::string{kRaidAnchorAdvancedResource},
             RaidMapAnchorKind::AdvancedResource,
             map.highRisk.advancedResourceArea.size}};
        if (map.rescue.has_value())
            anchors.requests.push_back({
                std::string{kRaidAnchorRescue}, RaidMapAnchorKind::Rescue,
                map.rescue->transferPoint.size});
        const auto &deployment = publishedContentRegistry().enemyDeployment(
            map.raidEnemyDeploymentIds.front());
        for (std::size_t index{}; index < deployment.enemies.size(); ++index)
            anchors.requests.push_back({
                raidIndexedAnchorId("enemy", index),
                RaidMapAnchorKind::Enemy,
                deployment.enemies[index].size});
        for (std::size_t index{}; index < map.raidLootSlots.size(); ++index)
            anchors.requests.push_back({
                raidIndexedAnchorId("loot", index),
                RaidMapAnchorKind::Loot, {32.0F, 32.0F}});
        for (std::size_t index{};
             index < map.highRisk.pressureSpawns.size(); ++index)
            anchors.requests.push_back({
                raidIndexedAnchorId("pressure", index),
                RaidMapAnchorKind::PressureSpawn,
                map.highRisk.pressureSpawns[index].size});
        for (std::size_t index{}; index < map.interiors.size(); ++index)
            anchors.requests.push_back({
                raidIndexedAnchorId("interior", index),
                RaidMapAnchorKind::InteriorEntrance,
                map.interiors[index].exteriorEntrance.size, {},
                map.proceduralOutdoor.layoutVersion == kHospitalLandmarkInteriorLayoutVersion
                    ? map.interiors[index].landmarkDefinitionId : std::string{}});
    }
    return anchors;
}

bool overlapsStrict(ContentRect first, ContentRect second)
{
    return first.position.x < second.position.x + second.size.x &&
        first.position.x + first.size.x > second.position.x &&
        first.position.y < second.position.y + second.size.y &&
        first.position.y + first.size.y > second.position.y;
}

bool districtIsContinuous(
    const RaidDistrictSnapshot &district,
    std::uint32_t columns,
    std::uint32_t rows)
{
    std::vector<bool> occupied(
        static_cast<std::size_t>(columns) * rows, false);
    std::size_t expected{};
    std::size_t first = occupied.size();
    for (const RaidGridSpan &span : district.cells)
        for (std::uint32_t column = span.firstColumn;
             column < span.firstColumn + span.length; ++column)
        {
            const std::size_t index =
                static_cast<std::size_t>(span.row) * columns + column;
            occupied[index] = true;
            first = std::min(first, index);
            ++expected;
        }
    if (first == occupied.size())
        return false;
    std::vector<bool> visited(occupied.size(), false);
    std::queue<std::size_t> frontier;
    frontier.push(first);
    visited[first] = true;
    std::size_t reached{};
    while (!frontier.empty())
    {
        const std::size_t current = frontier.front();
        frontier.pop();
        ++reached;
        const std::uint32_t row = static_cast<std::uint32_t>(current / columns);
        const std::uint32_t column =
            static_cast<std::uint32_t>(current % columns);
        const auto visit = [&](std::uint32_t nextColumn,
                               std::uint32_t nextRow)
        {
            const std::size_t next =
                static_cast<std::size_t>(nextRow) * columns + nextColumn;
            if (occupied[next] && !visited[next])
            {
                visited[next] = true;
                frontier.push(next);
            }
        };
        if (column > 0U) visit(column - 1U, row);
        if (column + 1U < columns) visit(column + 1U, row);
        if (row > 0U) visit(column, row - 1U);
        if (row + 1U < rows) visit(column, row + 1U);
    }
    return reached == expected;
}

float districtRectangularFillRatio(const RaidDistrictSnapshot &district)
{
    std::uint32_t firstColumn = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t lastColumn{};
    std::uint32_t firstRow = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t lastRow{};
    std::size_t cells{};
    for (const RaidGridSpan &span : district.cells)
    {
        firstColumn = std::min<std::uint32_t>(firstColumn, span.firstColumn);
        lastColumn = std::max<std::uint32_t>(
            lastColumn, span.firstColumn + span.length - 1U);
        firstRow = std::min<std::uint32_t>(firstRow, span.row);
        lastRow = std::max<std::uint32_t>(lastRow, span.row);
        cells += span.length;
    }
    const std::size_t boundingCells =
        static_cast<std::size_t>(lastColumn - firstColumn + 1U) *
        (lastRow - firstRow + 1U);
    return static_cast<float>(cells) /
        static_cast<float>(boundingCells);
}

bool districtTouchesKind(
    const std::vector<RaidDistrictSnapshot> &districts,
    const RaidDistrictSnapshot &subject,
    RaidDistrictKind neighborKind,
    std::uint32_t columns,
    std::uint32_t rows)
{
    std::vector<std::uint16_t> owners(
        static_cast<std::size_t>(columns) * rows, 0U);
    for (const RaidDistrictSnapshot &district : districts)
        for (const RaidGridSpan &span : district.cells)
            for (std::uint32_t column = span.firstColumn;
                 column < span.firstColumn + span.length; ++column)
                owners[static_cast<std::size_t>(span.row) * columns + column] =
                    district.instanceId;
    const auto kindForOwner = [&](std::uint16_t owner)
    {
        const auto found = std::find_if(
            districts.begin(), districts.end(),
            [owner](const RaidDistrictSnapshot &district)
            { return district.instanceId == owner; });
        return found == districts.end()
            ? RaidDistrictKind::OpenGround : found->kind;
    };
    for (const RaidGridSpan &span : subject.cells)
        for (std::uint32_t column = span.firstColumn;
             column < span.firstColumn + span.length; ++column)
        {
            const auto matches = [&](std::uint32_t nextColumn,
                                     std::uint32_t nextRow)
            {
                return kindForOwner(owners[
                    static_cast<std::size_t>(nextRow) * columns +
                    nextColumn]) == neighborKind;
            };
            if ((column > 0U && matches(column - 1U, span.row)) ||
                (column + 1U < columns &&
                 matches(column + 1U, span.row)) ||
                (span.row > 0U && matches(column, span.row - 1U)) ||
                (span.row + 1U < rows &&
                 matches(column, span.row + 1U)))
                return true;
        }
    return false;
}
}

TEST(RaidMapGenerationTest, FrontierV4AcceptedBaselineHashes)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    const auto anchors = publishedAnchors(map);
    // Capture from the accepted generator before adding a second theme. A
    // deterministic repeat alone cannot detect a deterministic regression.
    constexpr std::array<std::pair<std::uint64_t, std::uint64_t>, 3> accepted{{
        {1ULL, 15387931812094865162ULL},
        {42ULL, 3232885663766108429ULL},
        {910223ULL, 14375321402881869312ULL}}};
    for (const auto &[seed, hash] : accepted)
    {
        SCOPED_TRACE(seed);
        const auto layout = generateRaidMapLayout(map, seed, anchors);
        EXPECT_EQ(layout.layoutVersion, 4U);
        EXPECT_FALSE(layout.usedFallback);
        EXPECT_EQ(layout.layoutHash, hash);
    }
}

TEST(RaidMapGenerationTest, FrontierBlockadeFreezesPlayerClearanceDetour)
{
    auto map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    map.worldSize = {2400.0F, 1600.0F};
    map.walkableBounds = {{}, map.worldSize};
    map.proceduralOutdoor.columns = 30U;
    map.proceduralOutdoor.rows = 20U;
    RaidGeneratedMapLayout layout;
    layout.layoutVersion = 4U;
    for (std::uint16_t row = 8U; row < 12U; ++row)
        for (std::uint16_t column = 9U; column < 21U; ++column)
            layout.roadCells.push_back({column, row, RaidOutdoorRoadKind::Primary});
    layout.anchorPlacements = {
        {std::string{kRaidAnchorPlayerSpawn}, RaidMapAnchorKind::PlayerSpawn,
         {{330.0F, 800.0F}, {50.0F, 50.0F}}},
        {std::string{kRaidAnchorNormalExtraction}, RaidMapAnchorKind::NormalExtraction,
         {{1280.0F, 220.0F}, {140.0F, 140.0F}}},
        {std::string{kRaidAnchorEmergencyExtraction}, RaidMapAnchorKind::EmergencyExtraction,
         {{1880.0F, 750.0F}, {140.0F, 140.0F}}},
        {std::string{kRaidAnchorConditionalExtraction}, RaidMapAnchorKind::ConditionalExtraction,
         {{1800.0F, 1150.0F}, {140.0F, 140.0F}}},
        {std::string{kRaidAnchorCredentialExtraction}, RaidMapAnchorKind::CredentialExtraction,
         {{1780.0F, 480.0F}, {140.0F, 140.0F}}},
        {std::string{kRaidAnchorHighRiskControl}, RaidMapAnchorKind::HighRiskControl,
         {{880.0F, 380.0F}, {150.0F, 84.0F}}},
        {raidIndexedAnchorId("interior", 0U), RaidMapAnchorKind::InteriorEntrance,
         {{450.0F, 1200.0F}, {80.0F, 80.0F}}}};
    const ContentRect focus{{1130.0F, 1200.0F}, {160.0F, 140.0F}};
    const auto blockade = chooseFrontierRoadBlockade(map, layout, focus, 42U);
    ASSERT_TRUE(blockade);
    EXPECT_EQ(chooseFrontierRoadBlockade(map, layout, focus, 42U), blockade);
    EXPECT_TRUE(validateFrontierRoadBlockade(map, layout, focus, *blockade));
    EXPECT_FLOAT_EQ(blockade->bounds.size.x, 64.0F);
    EXPECT_GE(blockade->bounds.size.y, 320.0F);
    EXPECT_GE(blockade->detourExtraDistance, 320.0F);
    EXPECT_LE(blockade->detourExtraDistance, 960.0F);
    const float side = blockade->detourCorners.front().y;
    EXPECT_TRUE(side <= blockade->bounds.position.y - 16.0F ||
                side >= blockade->bounds.position.y + blockade->bounds.size.y + 16.0F);
    EXPECT_FLOAT_EQ(blockade->detourCorners.back().y, side);

    auto invalid = *blockade;
    invalid.routeEntry.y += 1.0F;
    EXPECT_FALSE(validateFrontierRoadBlockade(map, layout, focus, invalid));
    invalid = *blockade;
    invalid.detourCorners.back().x -= 0.5F;
    EXPECT_FALSE(validateFrontierRoadBlockade(map, layout, focus, invalid));

    // A thin solid perimeter leaves the same road clear but isolates the
    // signal extraction. Geometric detour validity alone cannot pass.
    auto trapped = layout;
    trapped.ballisticBlockers = {
        {{1840.0F, 710.0F}, {220.0F, 30.0F}},
        {{1840.0F, 890.0F}, {220.0F, 30.0F}},
        {{1840.0F, 740.0F}, {30.0F, 150.0F}},
        {{2030.0F, 740.0F}, {30.0F, 150.0F}}};
    EXPECT_FALSE(validateFrontierRoadBlockade(map, trapped, focus, *blockade));
    EXPECT_FALSE(chooseFrontierRoadBlockade(map, trapped, focus, 42U));

    // The return center is south of the visible entrance. The obstruction
    // here is outside the entrance and away from the road.
    auto badReturn = layout;
    badReturn.ballisticBlockers.push_back(
        {{498.0F, 1358.0F}, {16.0F, 16.0F}});
    EXPECT_FALSE(validateFrontierRoadBlockade(map, badReturn, focus, *blockade));

    layout.usedFallback = true;
    EXPECT_FALSE(chooseFrontierRoadBlockade(map, layout, focus, 42U));
}

TEST(RaidMapGenerationTest, HospitalCandidateHasDistinctExteriorAndFixedInterior)
{
    const auto content = contentBeforeHospitalHybridOutdoor();
    const auto &map = content.map(MapDefinitionId{"map.raid.hospital_district"});
    EXPECT_FLOAT_EQ(map.worldSize.x, 12800.0F);
    EXPECT_FLOAT_EQ(map.worldSize.y, 7200.0F);
    ASSERT_EQ(map.interiors.size(), 1U);
    EXPECT_EQ(map.interiors.front().id.value(),
              "raid_space.hospital_district.main_building");
    EXPECT_EQ(map.interiors.front().ballisticBlockers.size(), 12U);
    const auto anchors = publishedAnchors(map);
    std::set<std::uint64_t> hashes;
    for (std::uint64_t seed = 1U; seed <= 128U; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto layout = generateRaidMapLayout(map, seed, anchors);
        EXPECT_FALSE(layout.usedFallback);
        EXPECT_EQ(layout.districts.size(), 8U);
        EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, layout, anchors));
        ASSERT_EQ(layout.landmarks.size(), 3U);
        EXPECT_EQ(layout.landmarks.front().definitionId, "landmark.hospital.main_building");
        ASSERT_NE(findRaidAnchorPlacement(layout, raidIndexedAnchorId("interior", 0)), nullptr);
        hashes.insert(layout.layoutHash);
        for (const std::string_view role : {kRaidAnchorHighRiskControl, kRaidAnchorAdvancedResource})
        {
            const auto *anchor = findRaidAnchorPlacement(layout, role);
            ASSERT_NE(anchor, nullptr);
            ASSERT_GT(anchor->districtInstanceId, 0U);
            EXPECT_EQ(layout.districts.at(anchor->districtInstanceId - 1U).kind,
                      RaidDistrictKind::RoadsideService);
        }
        EXPECT_EQ(layout, generateRaidMapLayout(map, seed, anchors));
    }
    EXPECT_GT(hashes.size(), 120U);
    auto bad = map;
    bad.proceduralOutdoor.minimumBlockers = 1U;
    bad.proceduralOutdoor.maximumBlockers = 1U;
    const auto fallback = generateRaidMapLayout(bad, 77U, anchors);
    EXPECT_TRUE(fallback.usedFallback);
    EXPECT_EQ(fallback, generateRaidMapLayout(bad, 88U, anchors));
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(bad, fallback, anchors));
    EXPECT_EQ(publishedContentRegistry().maps().size(), 5U);
}

namespace
{
double hospitalRouteLength(const std::vector<Vec2> &path)
{
    double length{};
    for (std::size_t i = 1; i < path.size(); ++i)
        length += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
    return length;
}

// Sweep the actual 32-square footprint through every cardinal move. This
// check does not use the generator's cell occupancy or pathfinding code.
bool hospitalRouteHasPlayerClearance(const std::vector<Vec2> &path,
                                     ContentRect bounds,
                                     const std::vector<ContentRect> &blockers)
{
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const auto a = path[i - 1], b = path[i];
        if (a.x != b.x && a.y != b.y) return false;
        const ContentRect swept{{std::min(a.x, b.x) - 16, std::min(a.y, b.y) - 16},
            {std::abs(b.x - a.x) + 32, std::abs(b.y - a.y) + 32}};
        if (swept.position.x < bounds.position.x || swept.position.y < bounds.position.y ||
            swept.position.x + swept.size.x > bounds.position.x + bounds.size.x ||
            swept.position.y + swept.size.y > bounds.position.y + bounds.size.y)
            return false;
        if (std::any_of(blockers.begin(), blockers.end(),
                        [&](ContentRect blocker) { return overlapsStrict(swept, blocker); }))
            return false;
    }
    return true;
}
}

TEST(RaidMapGenerationTest, DepotReceivingHasDirectAndSideApproachesToThePartsRoom)
{
    const auto &depot = publishedContentRegistry().raidInterior(kHospitalDepotSpaceId);
    const ContentRect space{{}, depot.worldSize};
    std::vector<ContentRect> blockers;
    for (const auto &fixture : depot.ballisticBlockers)
        blockers.push_back(fixture.bounds);
    ASSERT_EQ(depot.lootSlots.size(), 2U);
    const auto bench = depot.lootSlots[0].position;
    const auto parts = depot.lootSlots[1].position;
    const Vec2 spawn = depot.interiorSpawn;
    EXPECT_TRUE(hospitalRouteHasPlayerClearance(
        {spawn, {spawn.x, parts.y}, parts}, space, blockers));
    EXPECT_TRUE(hospitalRouteHasPlayerClearance(
        {spawn, {spawn.x, 735}, {350, 735}, {350, bench.y}, bench, {bench.x, 238}, {740, 238}, {740, parts.y}, parts},
        space, blockers));
    EXPECT_FALSE(hospitalRouteHasPlayerClearance({bench, parts}, space, blockers));
}

TEST(RaidMapGenerationTest, HospitalHybridNormalDeploymentsKeepTwoPlayerRoutesAcross128Seeds)
{
    const auto &content = publishedContentRegistry();
    const auto &map = content.map(kHospitalFloorplanMapId);
    ASSERT_EQ(map.proceduralOutdoor.layoutVersion, kHospitalLandmarkInteriorLayoutVersion);
    std::set<long> extraDistances;
    std::set<HospitalParcelFamily> families;
    std::set<unsigned> orientations;
    unsigned long waterCells{}, joinedWaterCells{};
    for (std::uint64_t seed = 1; seed <= 128; ++seed)
    {
        SCOPED_TRACE(seed);
        auto profile = makeNewAlphaProfile("hospital-quality", content);
        const auto receipt = executeDeploy(profile, content,
            {"hospital-inspection", "hospital-return", seed, kHospitalFloorplanMapId},
            {profile.revision, "inspect-deploy"});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        ASSERT_TRUE(profile.pendingRaid);
        const auto &layout = profile.pendingRaid->spatialLayout;
        ASSERT_FALSE(layout.usedFallback);
        const auto &rescue = profile.pendingRaid->rescue;
        ASSERT_TRUE(rescue && rescue->escortStart);
        const auto start = *rescue->escortStart;
        const auto handover = center(rescue->transferPoint);
        const auto separation = std::hypot(start.x - handover.x, start.y - handover.y);
        EXPECT_GE(separation, 600.0F);
        EXPECT_LE(separation, 1000.0F);
        const ContentRect survivorBody{{start.x - 16.0F, start.y - 16.0F}, {32.0F, 32.0F}};
        EXPECT_FALSE(std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
            [&](ContentRect obstacle) { return overlapsStrict(survivorBody, obstacle); }));
        EXPECT_LE(layout.generationAttempt, map.proceduralOutdoor.maximumAttempts);
        EXPECT_EQ(layout.layoutHash, raidMapLayoutHash(layout));
        ASSERT_EQ(layout.districts.size(), 8U);
        for (const auto &district : layout.districts)
            EXPECT_TRUE(districtIsContinuous(district, map.proceduralOutdoor.districtColumns,
                                              map.proceduralOutdoor.districtRows)) << district.definitionId;
        EXPECT_TRUE(std::any_of(layout.districts.begin(), layout.districts.end(), [](const auto &district) {
            return districtRectangularFillRatio(district) < 0.85F;
        }));
        const auto columns = map.proceduralOutdoor.columns;
        const auto rows = map.proceduralOutdoor.rows;
        std::vector<RaidTerrainKind> terrain(columns * rows);
        std::size_t covered{};
        for (const auto &span : layout.terrainSpans)
        {
            ASSERT_LT(span.row, rows);
            ASSERT_LE(span.firstColumn + span.length, columns);
            ASSERT_EQ(std::size_t(span.row) * columns + span.firstColumn, covered);
            std::fill_n(terrain.begin() + covered, span.length, span.kind);
            covered += span.length;
        }
        ASSERT_EQ(covered, terrain.size());
        EXPECT_TRUE(std::all_of(layout.roadCells.begin(), layout.roadCells.end(), [&](auto road) {
            return road.row < rows && road.column < columns &&
                   terrain[road.row * columns + road.column] == RaidTerrainKind::Asphalt;
        }));
        for (std::size_t i = 0; i < terrain.size(); ++i)
        {
            if (terrain[i] != RaidTerrainKind::Puddle) continue;
            ++waterCells;
            if ((i % columns > 0 && terrain[i - 1] == terrain[i]) ||
                (i % columns + 1 < columns && terrain[i + 1] == terrain[i]) ||
                (i >= columns && terrain[i - columns] == terrain[i]) ||
                (i + columns < terrain.size() && terrain[i + columns] == terrain[i]))
                ++joinedWaterCells;
        }
        ASSERT_EQ(layout.landmarks.size(), 3U);
        ASSERT_FALSE(layout.landmarks.front().roadSockets.empty());
        const auto area = map.walkableBounds;
        const Vec2 roadCellSize{area.size.x / columns, area.size.y / rows};
        for (const auto &landmark : layout.landmarks)
            for (const auto &building : landmark.structures)
                EXPECT_FALSE(std::any_of(layout.roadCells.begin(), layout.roadCells.end(), [&](auto cell) {
                    const ContentRect road{{area.position.x + cell.column * roadCellSize.x,
                                            area.position.y + cell.row * roadCellSize.y}, roadCellSize};
                    return overlapsStrict(road, building);
                })) << landmark.definitionId;
        const auto routes = inspectHospitalOutdoorRoutes(map, layout);
        ASSERT_TRUE(routes.passable());
        ASSERT_GE(routes.mainRoute.size(), 2U);
        ASSERT_GE(routes.bypassRoute.size(), 2U);
        EXPECT_FLOAT_EQ(routes.mainRoute.front().x, routes.bypassRoute.front().x);
        EXPECT_FLOAT_EQ(routes.mainRoute.front().y, routes.bypassRoute.front().y);
        const auto *spawn = findRaidAnchorPlacement(layout, kRaidAnchorPlayerSpawn);
        ASSERT_NE(spawn, nullptr);
        EXPECT_FLOAT_EQ(spawn->bounds.position.x, profile.pendingRaid->playerSpawn.x);
        EXPECT_FLOAT_EQ(spawn->bounds.position.y, profile.pendingRaid->playerSpawn.y);
        EXPECT_FLOAT_EQ(routes.mainRoute.front().x, center(spawn->bounds).x);
        EXPECT_FLOAT_EQ(routes.mainRoute.front().y, center(spawn->bounds).y);
        ASSERT_EQ(profile.pendingRaid->interiors.size(), 2U);
        for (std::size_t index{}; index < map.interiors.size(); ++index)
        {
            const auto &room = profile.pendingRaid->interiors[index];
            const auto &landmarkId = map.interiors[index].landmarkDefinitionId;
            const auto site = std::find_if(layout.landmarks.begin(), layout.landmarks.end(),
                [&](const auto &landmark) { return landmark.definitionId == landmarkId; });
            ASSERT_NE(site, layout.landmarks.end());
            EXPECT_EQ(center(room.exteriorEntrance).x, site->roadSockets.front().x);
            EXPECT_EQ(center(room.exteriorEntrance).y, site->roadSockets.front().y);
            const Vec2 returnPoint = raidExteriorReturnPoint(map, room.exteriorEntrance);
            EXPECT_EQ(room.exteriorReturn.x, returnPoint.x);
            EXPECT_EQ(room.exteriorReturn.y, returnPoint.y);
            const ContentRect arrival{room.exteriorReturn, {32, 32}};
            EXPECT_FALSE(std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
                [&](ContentRect blocker) { return overlapsStrict(blocker, arrival); }));
        }
        const auto entrance = center(profile.pendingRaid->interiors.front().exteriorEntrance);
        EXPECT_FLOAT_EQ(routes.mainRoute.back().x, entrance.x);
        EXPECT_FLOAT_EQ(routes.mainRoute.back().y, entrance.y);
        EXPECT_FLOAT_EQ(routes.mainRoute.back().x, routes.bypassRoute.back().x);
        EXPECT_FLOAT_EQ(routes.mainRoute.back().y, routes.bypassRoute.back().y);
        EXPECT_FLOAT_EQ(routes.mainRoute.back().x, layout.landmarks.front().roadSockets.front().x);
        EXPECT_FLOAT_EQ(routes.mainRoute.back().y, layout.landmarks.front().roadSockets.front().y);
        EXPECT_TRUE(hospitalRouteHasPlayerClearance(routes.mainRoute, map.walkableBounds, layout.ballisticBlockers));
        EXPECT_TRUE(hospitalRouteHasPlayerClearance(routes.bypassRoute, map.walkableBounds, layout.ballisticBlockers));
        const auto extra = std::lround(hospitalRouteLength(routes.bypassRoute) - hospitalRouteLength(routes.mainRoute));
        EXPECT_GT(extra, 0);
        extraDistances.insert(extra);

        const auto plots = inspectHospitalParcels(map, layout);
        ASSERT_FALSE(plots.empty());
        for (std::size_t index = 0; index < plots.size(); ++index)
        {
            const auto &plot = plots[index];
            families.insert(plot.family);
            orientations.insert(plot.quarterTurns);
            for (std::size_t previous = 0; previous < index; ++previous)
                EXPECT_FALSE(overlapsStrict(plot.bounds, plots[previous].bounds));
            const ContentRect gate{{plot.gate.x - 16, plot.gate.y - 16}, {32, 32}};
            EXPECT_FALSE(std::any_of(layout.ballisticBlockers.begin(), layout.ballisticBlockers.end(),
                                     [&](ContentRect blocker) { return overlapsStrict(gate, blocker); }));
        }
    }
    EXPECT_GE(extraDistances.size(), 3U); // Rotation and mirroring preserve path length.
    EXPECT_EQ(families.size(), 3U);
    EXPECT_GE(orientations.size(), 2U);
    EXPECT_GT(waterCells, 0UL);
    EXPECT_GT(joinedWaterCells, waterCells / 2); // Patches dominate isolated pixels.
}

TEST(RaidMapGenerationTest, HospitalHybridRefusesInvalidTemplateAndChecksFallbackAtAttemptLimit)
{
    const auto &map = publishedContentRegistry().map(kHospitalFloorplanMapId);
    const auto anchors = publishedAnchors(map);
    auto malformed = map;
    malformed.proceduralOutdoor.landmarkTemplates.front().structures.clear();
    EXPECT_THROW(static_cast<void>(generateRaidMapLayout(malformed, 42, anchors)), std::invalid_argument);

    auto constrained = map;
    constrained.proceduralOutdoor.maximumAttempts = 1;
    constrained.proceduralOutdoor.minimumBlockers = 1;
    constrained.proceduralOutdoor.maximumBlockers = 1;
    const auto a = generateRaidMapLayout(constrained, 1, anchors);
    const auto b = generateRaidMapLayout(constrained, 42, anchors);
    ASSERT_TRUE(a.usedFallback);
    EXPECT_EQ(a.generationAttempt, 1U);
    EXPECT_EQ(a.fallbackReason, RaidMapFallbackReason::AttemptsExhausted);
    EXPECT_EQ(a, b);
    const auto routes = inspectHospitalOutdoorRoutes(constrained, a);
    ASSERT_TRUE(routes.passable());
    EXPECT_TRUE(hospitalRouteHasPlayerClearance(routes.mainRoute, constrained.walkableBounds, a.ballisticBlockers));
    EXPECT_TRUE(hospitalRouteHasPlayerClearance(routes.bypassRoute, constrained.walkableBounds, a.ballisticBlockers));
    auto badReturn = a;
    const auto *depotDoor = findRaidAnchorPlacement(badReturn, raidIndexedAnchorId("interior", 1));
    ASSERT_NE(depotDoor, nullptr);
    const Vec2 arrival = raidExteriorReturnPoint(constrained, depotDoor->bounds);
    badReturn.ballisticBlockers.push_back({{arrival.x + 30, arrival.y + 30}, {8, 8}});
    EXPECT_FALSE(inspectHospitalOutdoorRoutes(constrained, badReturn).passable());

    auto sealed = a;
    const auto door = sealed.landmarks.front().roadSockets.front();
    sealed.ballisticBlockers.push_back({{door.x - 16, door.y - 16}, {32, 32}});
    EXPECT_FALSE(inspectHospitalOutdoorRoutes(constrained, sealed).passable());
}

TEST(RaidMapGenerationTest, HospitalHybridPublishedSeedsReplayFrozenGeometry)
{
    const auto content = contentBeforeHospitalLandmarkInteriors();
    const auto &map = content.map(kHospitalFloorplanMapId);
    struct Expected
    {
        std::uint64_t seed, hash;
        long mainLength, bypassLength;
    };
    for (const auto expected : std::array{
             Expected{1, 12284713511449745002ULL, 1920, 8000},
             Expected{42, 4251823121744383302ULL, 1840, 8400},
             Expected{910223, 16716575597676622975ULL, 1520, 7440}})
    {
        SCOPED_TRACE(expected.seed);
        for (unsigned replay = 0; replay < 2; ++replay)
        {
            auto profile = makeNewAlphaProfile("hospital-inspection", content);
            const auto receipt = executeDeploy(profile, content,
                {"hospital-inspection", "hospital-return", expected.seed, kHospitalFloorplanMapId},
                {profile.revision, "inspect-deploy"});
            ASSERT_TRUE(receipt.succeeded) << receipt.message;
            const auto &layout = profile.pendingRaid->spatialLayout;
            EXPECT_EQ(layout.layoutHash, expected.hash);
            const auto paths = inspectHospitalOutdoorRoutes(map, layout);
            ASSERT_TRUE(paths.passable());
            EXPECT_EQ(std::lround(hospitalRouteLength(paths.mainRoute)), expected.mainLength);
            EXPECT_EQ(std::lround(hospitalRouteLength(paths.bypassRoute)), expected.bypassLength);
        }
    }
}

TEST(RaidMapGenerationTest, DistrictFillSeparatesTerrainBuildingsAndEquipment)
{
    const auto industrial = raidDistrictFill(RaidDistrictKind::Industrial);
    const auto logistics = raidDistrictFill(RaidDistrictKind::Logistics);
    EXPECT_TRUE(industrial.largeBuildings);
    EXPECT_TRUE(logistics.largeBuildings);
    EXPECT_EQ(industrial.building, RaidOutdoorPropKind::Factory);
    EXPECT_EQ(logistics.building, RaidOutdoorPropKind::Warehouse);
    EXPECT_EQ(industrial.smallBlocker(3), RaidOutdoorPropKind::EngineeringEquipment);
    EXPECT_EQ(industrial.smallBlocker(4), RaidOutdoorPropKind::Container);
    EXPECT_EQ(logistics.smallBlocker(3), RaidOutdoorPropKind::Container);
    EXPECT_EQ(raidDistrictFill(RaidDistrictKind::Highway).terrain,
              RaidTerrainKind::Asphalt);
    EXPECT_EQ(raidDistrictFill(RaidDistrictKind::Greenbelt).terrain,
              RaidTerrainKind::Grass);
    EXPECT_EQ(raidDistrictFill(RaidDistrictKind::OpenGround).terrain,
              RaidTerrainKind::Dirt);
    EXPECT_FALSE(raidDistrictFill(RaidDistrictKind::RoadsideService).largeBuildings);
    EXPECT_EQ(raidDistrictFill(RaidDistrictKind::RoadsideService).smallBlocker(2),
              RaidOutdoorPropKind::EngineeringEquipment);
}

TEST(RaidMapGenerationTest, FixedMapsPreservePublishedBlockers)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.riverside"});
    const RaidGeneratedMapLayout layout = generateRaidMapLayout(
        map, 4113U, publishedAnchors(map));

    ASSERT_EQ(layout.ballisticBlockers.size(), map.ballisticBlockers.size());
    EXPECT_EQ(layout.generationAttempt, 0U);
    EXPECT_FALSE(layout.usedFallback);
    EXPECT_NE(layout.layoutHash, 0U);
    for (std::size_t index{}; index < map.ballisticBlockers.size(); ++index)
    {
        EXPECT_EQ(layout.ballisticBlockers[index],
                  map.ballisticBlockers[index].bounds);
    }
}

TEST(RaidMapGenerationTest, AuthoredHospitalCourtyardKeepsItsShapeAcrossSeeds)
{
    auto map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    // Geometry prototype only: not a published hospital map or a replacement
    // for its later district/resource/entry integration.
    auto &landmark = map.proceduralOutdoor.landmarkTemplates.front();
    landmark.structures = {
        {{0.08F, 0.08F}, {0.84F, 0.24F}},
        {{0.08F, 0.36F}, {0.24F, 0.48F}},
        {{0.68F, 0.36F}, {0.24F, 0.48F}}};
    const auto anchors = publishedAnchors(map);
    for (std::uint64_t seed = 1; seed <= 128; ++seed)
    {
        SCOPED_TRACE(seed);
        const auto layout = generateRaidMapLayout(map, seed, anchors);
        ASSERT_FALSE(layout.landmarks.empty());
        const auto &placed = layout.landmarks.front();
        ASSERT_EQ(placed.structures.size(), landmark.structures.size());
        EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, layout, anchors));
        for (std::size_t index = 0; index < placed.structures.size(); ++index)
        {
            const auto &local = landmark.structures[index];
            const auto &world = placed.structures[index];
            EXPECT_FLOAT_EQ(world.position.x,
                placed.bounds.position.x + placed.bounds.size.x * local.position.x);
            EXPECT_FLOAT_EQ(world.position.y,
                placed.bounds.position.y + placed.bounds.size.y * local.position.y);
            EXPECT_FLOAT_EQ(world.size.x, placed.bounds.size.x * local.size.x);
            EXPECT_FLOAT_EQ(world.size.y, placed.bounds.size.y * local.size.y);
        }
        EXPECT_EQ(layout, generateRaidMapLayout(map, seed, anchors));
    }
    map.proceduralOutdoor.minimumBlockers = 1U;
    map.proceduralOutdoor.maximumBlockers = 1U;
    const auto fallback = generateRaidMapLayout(map, 77119U, anchors);
    ASSERT_TRUE(fallback.usedFallback);
    EXPECT_EQ(fallback, generateRaidMapLayout(map, 88220U, anchors));
    ASSERT_FALSE(fallback.landmarks.empty());
    EXPECT_EQ(fallback.landmarks.front().structures.size(), 3U);
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, fallback, anchors));
}

TEST(RaidMapGenerationTest, ProceduralMapIsDeterministicAndConnected)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    const RaidMapGenerationAnchors anchors = publishedAnchors(map);
    const auto started = std::chrono::steady_clock::now();
    const RaidGeneratedMapLayout first = generateRaidMapLayout(
        map, 910223U, anchors);
    const auto generationElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
    const RaidGeneratedMapLayout repeated = generateRaidMapLayout(
        map, 910223U, anchors);

    EXPECT_TRUE(map.proceduralOutdoor.enabled);
    EXPECT_EQ(first, repeated);
    EXPECT_FALSE(first.usedFallback);
    EXPECT_EQ(first.layoutVersion, 4U);
    EXPECT_EQ(first.districts.size(), 8U);
    EXPECT_EQ(std::count_if(
                  first.districts.begin(), first.districts.end(),
                  [](const RaidDistrictSnapshot &district)
                  { return district.kind == RaidDistrictKind::Industrial; }),
              2);
    EXPECT_EQ(std::count_if(
                  first.districts.begin(), first.districts.end(),
                  [](const RaidDistrictSnapshot &district)
                  { return district.kind == RaidDistrictKind::Logistics; }),
              2);
    EXPECT_EQ(first.landmarks.size(), 3U);
    EXPECT_EQ(first.anchorPlacements.size(), anchors.requests.size());
    EXPECT_FALSE(first.terrainSpans.empty());
    EXPECT_GE(first.props.size(),
              map.proceduralOutdoor.minimumDecorativeProps +
                  map.proceduralOutdoor.minimumBlockers);
    EXPECT_FALSE(first.roadCells.empty());
    EXPECT_TRUE(std::any_of(
        first.roadCells.begin(), first.roadCells.end(),
        [](const RaidOutdoorRoadCell &cell)
        { return cell.kind == RaidOutdoorRoadKind::Primary; }));
    EXPECT_TRUE(std::any_of(
        first.roadCells.begin(), first.roadCells.end(),
        [](const RaidOutdoorRoadCell &cell)
        { return cell.kind == RaidOutdoorRoadKind::Secondary; }));
    EXPECT_GE(first.ballisticBlockers.size(),
              map.proceduralOutdoor.minimumBlockers);
    EXPECT_LE(first.ballisticBlockers.size(),
              map.proceduralOutdoor.maximumBlockers);
    const auto largeBuildingCount = std::count_if(
        first.props.begin(), first.props.end(),
        [](const RaidOutdoorPropSnapshot &prop)
        {
            return prop.kind == RaidOutdoorPropKind::Factory ||
                prop.kind == RaidOutdoorPropKind::Warehouse;
        });
    EXPECT_GE(largeBuildingCount, 18);
    EXPECT_LE(largeBuildingCount, 28);
    const auto roadObstacleCount = std::count_if(
        first.props.begin(), first.props.end(),
        [](const RaidOutdoorPropSnapshot &prop)
        {
            return prop.kind == RaidOutdoorPropKind::Car ||
                prop.kind == RaidOutdoorPropKind::Truck ||
                prop.kind == RaidOutdoorPropKind::RoadBarrier;
        });
    EXPECT_GE(roadObstacleCount,
              map.proceduralOutdoor.minimumRoadObstacles);
    EXPECT_LE(roadObstacleCount,
              map.proceduralOutdoor.maximumRoadObstacles);
    const auto decorativeCount = std::count_if(
        first.props.begin(), first.props.end(),
        [](const RaidOutdoorPropSnapshot &prop) { return !prop.collidable; });
    EXPECT_GE(decorativeCount,
              map.proceduralOutdoor.minimumDecorativeProps);
    EXPECT_LE(decorativeCount,
              map.proceduralOutdoor.maximumDecorativeProps);
    EXPECT_NE(first.layoutHash, 0U);
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, first, anchors));
    EXPECT_LT(generationElapsed.count(), 5000);
}

TEST(RaidMapGenerationTest, DifferentSeedsVaryAcceptedOutdoorCover)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    const RaidMapGenerationAnchors anchors = publishedAnchors(map);
    const RaidGeneratedMapLayout first = generateRaidMapLayout(
        map, 11001U, anchors);
    const RaidGeneratedMapLayout second = generateRaidMapLayout(
        map, 22002U, anchors);

    EXPECT_FALSE(first.usedFallback);
    EXPECT_FALSE(second.usedFallback);
    EXPECT_NE(first.layoutHash, second.layoutHash);
    EXPECT_NE(first.roadCells, second.roadCells);
    EXPECT_NE(first.ballisticBlockers, second.ballisticBlockers);
}

TEST(RaidMapGenerationTest,
     V3PropsUseReadablePhysicalScaleAndDoNotOverlap)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    const RaidMapGenerationAnchors anchors = publishedAnchors(map);
    const RaidGeneratedMapLayout layout = generateRaidMapLayout(
        map, 910223U, anchors);

    std::vector<const RaidOutdoorPropSnapshot *> reservedProps;
    std::array<std::size_t, 8U> kindCounts{};
    for (const RaidOutdoorPropSnapshot &prop : layout.props)
    {
        ++kindCounts[static_cast<std::size_t>(prop.kind)];
        if (prop.kind == RaidOutdoorPropKind::Debris)
            continue;

        reservedProps.push_back(&prop);
        const float longitudinal = std::max(
            prop.bounds.size.x, prop.bounds.size.y);
        const float lateral = std::min(
            prop.bounds.size.x, prop.bounds.size.y);
        switch (prop.kind)
        {
        case RaidOutdoorPropKind::Factory:
        case RaidOutdoorPropKind::Warehouse:
            EXPECT_GE(longitudinal, 320.0F);
            EXPECT_GE(lateral, 240.0F);
            break;
        case RaidOutdoorPropKind::Container:
            EXPECT_FLOAT_EQ(longitudinal, 288.0F);
            EXPECT_FLOAT_EQ(lateral, 112.0F);
            break;
        case RaidOutdoorPropKind::EngineeringEquipment:
            EXPECT_GE(longitudinal, 224.0F);
            EXPECT_GE(lateral, 144.0F);
            break;
        case RaidOutdoorPropKind::Car:
            EXPECT_FLOAT_EQ(longitudinal, 272.0F);
            EXPECT_FLOAT_EQ(lateral, 108.0F);
            break;
        case RaidOutdoorPropKind::Truck:
            EXPECT_FLOAT_EQ(longitudinal, 432.0F);
            EXPECT_FLOAT_EQ(lateral, 152.0F);
            break;
        case RaidOutdoorPropKind::RoadBarrier:
            EXPECT_FLOAT_EQ(longitudinal, 176.0F);
            EXPECT_FLOAT_EQ(lateral, 34.0F);
            break;
        case RaidOutdoorPropKind::Debris:
            FAIL() << "debris is handled before physical scale checks";
            break;
        }

        for (const RaidAnchorPlacementSnapshot &anchor :
             layout.anchorPlacements)
        {
            EXPECT_FALSE(overlapsStrict(prop.bounds, anchor.bounds))
                << "prop " << prop.instanceId << " overlaps anchor "
                << anchor.id;
        }
    }

    for (std::size_t first{}; first < reservedProps.size(); ++first)
    {
        for (std::size_t second = first + 1U;
             second < reservedProps.size(); ++second)
        {
            EXPECT_FALSE(overlapsStrict(
                reservedProps[first]->bounds,
                reservedProps[second]->bounds))
                << "props " << reservedProps[first]->instanceId << " and "
                << reservedProps[second]->instanceId << " overlap";
        }
    }

    for (const RaidOutdoorPropSnapshot &prop : layout.props)
    {
        if (prop.kind != RaidOutdoorPropKind::Debris)
            continue;
        for (const RaidOutdoorPropSnapshot *reserved : reservedProps)
        {
            EXPECT_FALSE(overlapsStrict(prop.bounds, reserved->bounds))
                << "debris " << prop.instanceId << " overlaps prop "
                << reserved->instanceId;
        }
    }

    for (std::size_t count : kindCounts)
        EXPECT_GT(count, 0U);
}

TEST(RaidMapGenerationTest, PublishedOutdoorLayoutRemainsLegalAcrossSeeds)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    const RaidMapGenerationAnchors anchors = publishedAnchors(map);

    for (std::uint64_t seed = 1U; seed <= 128U; ++seed)
    {
        const RaidGeneratedMapLayout layout = generateRaidMapLayout(
            map, seed, anchors);
        EXPECT_FALSE(layout.usedFallback) << "seed " << seed;
        EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, layout, anchors))
            << "seed " << seed;
        EXPECT_EQ(layout.layoutHash, raidMapLayoutHash(layout))
            << "seed " << seed;
        EXPECT_EQ(layout.districts.size(), 8U) << "seed " << seed;
        EXPECT_EQ(layout.landmarks.size(), 3U) << "seed " << seed;
        EXPECT_EQ(layout.anchorPlacements.size(), anchors.requests.size())
            << "seed " << seed;
        for (const RaidDistrictSnapshot &district : layout.districts)
        {
            EXPECT_TRUE(districtIsContinuous(
                district,
                map.proceduralOutdoor.districtColumns,
                map.proceduralOutdoor.districtRows))
                << "seed " << seed << " district " << district.instanceId;
            EXPECT_GE(districtRectangularFillRatio(district), 0.95F)
                << "seed " << seed << " district " << district.instanceId;
            if (district.kind == RaidDistrictKind::Industrial)
                EXPECT_TRUE(districtTouchesKind(
                    layout.districts, district, RaidDistrictKind::Logistics,
                    map.proceduralOutdoor.districtColumns,
                    map.proceduralOutdoor.districtRows))
                    << "seed " << seed << " district "
                    << district.instanceId;
        }
        EXPECT_GE(layout.ballisticBlockers.size(),
                  map.proceduralOutdoor.minimumBlockers)
            << "seed " << seed;
        EXPECT_LE(layout.ballisticBlockers.size(),
                  map.proceduralOutdoor.maximumBlockers)
            << "seed " << seed;
        const auto roadObstacleCount = std::count_if(
            layout.props.begin(), layout.props.end(),
            [](const RaidOutdoorPropSnapshot &prop)
            {
                return prop.kind == RaidOutdoorPropKind::Car ||
                    prop.kind == RaidOutdoorPropKind::Truck ||
                    prop.kind == RaidOutdoorPropKind::RoadBarrier;
            });
        EXPECT_GE(roadObstacleCount,
                  map.proceduralOutdoor.minimumRoadObstacles)
            << "seed " << seed;
        EXPECT_LE(roadObstacleCount,
                  map.proceduralOutdoor.maximumRoadObstacles)
            << "seed " << seed;
        bool forbiddenOverlap{};
        std::uint32_t firstOverlapId{};
        std::uint32_t secondOverlapId{};
        for (std::size_t first{};
             first < layout.props.size() && !forbiddenOverlap; ++first)
        {
            for (std::size_t second = first + 1U;
                 second < layout.props.size(); ++second)
            {
                if (layout.props[first].kind == RaidOutdoorPropKind::Debris &&
                    layout.props[second].kind == RaidOutdoorPropKind::Debris)
                    continue;
                if (overlapsStrict(
                        layout.props[first].bounds,
                        layout.props[second].bounds))
                {
                    forbiddenOverlap = true;
                    firstOverlapId = layout.props[first].instanceId;
                    secondOverlapId = layout.props[second].instanceId;
                    break;
                }
            }
        }
        EXPECT_FALSE(forbiddenOverlap)
            << "seed " << seed << " props " << firstOverlapId << " and "
            << secondOverlapId << " overlap";
    }
}

TEST(RaidMapGenerationTest, InvalidSeedRejectsInsteadOfInventingLayout)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    EXPECT_THROW(
        static_cast<void>(generateRaidMapLayout(
            map, 0U, publishedAnchors(map))),
        std::invalid_argument);
}

TEST(RaidMapGenerationTest, ExhaustedAttemptsUseDeterministicConnectedFallback)
{
    MapDefinition map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    map.proceduralOutdoor.minimumBlockers = 1U;
    map.proceduralOutdoor.maximumBlockers = 1U;
    const RaidMapGenerationAnchors anchors = publishedAnchors(map);

    const RaidGeneratedMapLayout first = generateRaidMapLayout(
        map, 77119U, anchors);
    const RaidGeneratedMapLayout repeated = generateRaidMapLayout(
        map, 77119U, anchors);
    const RaidGeneratedMapLayout otherBadSeed = generateRaidMapLayout(
        map, 88220U, anchors);

    EXPECT_EQ(first, repeated);
    EXPECT_EQ(first, otherBadSeed);
    EXPECT_TRUE(first.usedFallback);
    EXPECT_EQ(first.fallbackReason,
              RaidMapFallbackReason::AttemptsExhausted);
    EXPECT_FALSE(first.roadCells.empty());
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, first, anchors));
    EXPECT_EQ(first.layoutHash, raidMapLayoutHash(first));
}

TEST(RaidMapGenerationTest, SelectsOnlyLegalSpecialLocationCandidate)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_EQ(map.interiors.size(), 2U);
    RaidMapGenerationAnchors anchors = publishedAnchors(map);
    anchors.occupiedRegions.push_back(
        map.interiors.front().exteriorPlacements.front().entrance);

    const RaidExteriorPlacementDefinition *selected =
        selectRaidExteriorPlacement(
            map.interiors.front(), 99117U, 0U, anchors);

    ASSERT_NE(selected, nullptr);
    EXPECT_TRUE(raidExteriorPlacementIsLegal(*selected, anchors));
    EXPECT_NE(
        selected->id,
        map.interiors.front().exteriorPlacements.front().id);
    EXPECT_EQ(
        selectRaidExteriorPlacement(
            map.interiors.front(), 99117U, 0U, anchors),
        selected);

    appendRaidExteriorPlacementAnchors(anchors, *selected);
    const RaidExteriorPlacementDefinition *second =
        selectRaidExteriorPlacement(
            map.interiors[1], 99117U, 1U, anchors);
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(raidExteriorPlacementIsLegal(*second, anchors));
    appendRaidExteriorPlacementAnchors(anchors, *second);

    const RaidGeneratedMapLayout layout = generateRaidMapLayout(
        map, 99117U, anchors);
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, layout, anchors));
}

TEST(RaidMapGenerationTest, FrontierNewSortieFreezesOrthogonalRoadNetwork)
{
    const auto &map = publishedContentRegistry().map(MapDefinitionId{"map.raid.frontier_exchange"});
    const auto anchors = publishedAnchors(map);
    const auto a = generateRaidMapLayout(map, 910223U, anchors, true);
    const auto b = generateRaidMapLayout(map, 910224U, anchors, true);
    ASSERT_FALSE(a.roadNetwork.empty());
    EXPECT_EQ(a.roadNetwork, generateRaidMapLayout(map, 910223U, anchors, true).roadNetwork);
    EXPECT_NE(a.roadNetwork, b.roadNetwork);
    EXPECT_EQ(a.layoutHash, raidMapLayoutHash(a));
    std::map<std::pair<int, int>, unsigned> arms;
    std::set<int> widths;
    for (const auto &edge : a.roadNetwork)
    {
        ASSERT_TRUE((edge.x1 == edge.x2) != (edge.y1 == edge.y2));
        ASSERT_LE(edge.x1, edge.x2);
        ASSERT_LE(edge.y1, edge.y2);
        EXPECT_GE(edge.x1, 0);
        EXPECT_GE(edge.y1, 0);
        EXPECT_LE(edge.x2, 400);
        EXPECT_LE(edge.y2, 225);
        const bool horizontal = edge.y1 == edge.y2;
        unsigned &from = arms[{edge.x1, edge.y1}], &to = arms[{edge.x2, edge.y2}];
        const unsigned out = horizontal ? 2 : 4, back = horizontal ? 8 : 1;
        EXPECT_EQ(from & out, 0U);
        EXPECT_EQ(to & back, 0U);
        from |= out;
        to |= back;
        widths.insert(128 * raidRoadHalfWidthTiles(edge.kind));
    }
    EXPECT_EQ(widths, (std::set<int>{128, 256, 512}));
    const auto has = [&](unsigned mask) {
        return std::any_of(arms.begin(), arms.end(),
                           [&](const auto &entry) { return entry.second == mask; });
    };
    EXPECT_TRUE(has(15));
    EXPECT_TRUE(has(7) || has(11) || has(13) || has(14));
    EXPECT_TRUE(has(3) || has(6) || has(9) || has(12));
    auto prior = generateRaidMapLayout(map, 910223U, anchors);
    EXPECT_TRUE(prior.roadNetwork.empty());
    EXPECT_EQ(prior, generateRaidMapLayout(map, 910223U, anchors, true,
                                           [](const auto &) { return false; }));
}
