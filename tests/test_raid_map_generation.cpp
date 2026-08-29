#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <queue>

#include "content_registry.h"
#include "raid_map_generation.h"

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
                map.interiors[index].exteriorEntrance.size});
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
    EXPECT_EQ(first.layoutVersion, 3U);
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
            if (layout.props[first].kind == RaidOutdoorPropKind::Debris)
                continue;
            for (std::size_t second = first + 1U;
                 second < layout.props.size(); ++second)
            {
                if (layout.props[second].kind == RaidOutdoorPropKind::Debris)
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
