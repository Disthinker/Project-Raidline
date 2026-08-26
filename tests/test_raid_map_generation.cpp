#include <gtest/gtest.h>

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
    return anchors;
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
    const RaidGeneratedMapLayout first = generateRaidMapLayout(
        map, 910223U, anchors);
    const RaidGeneratedMapLayout repeated = generateRaidMapLayout(
        map, 910223U, anchors);

    EXPECT_TRUE(map.proceduralOutdoor.enabled);
    EXPECT_EQ(first, repeated);
    EXPECT_FALSE(first.usedFallback);
    EXPECT_GE(first.ballisticBlockers.size(),
              map.proceduralOutdoor.minimumBlockers);
    EXPECT_LE(first.ballisticBlockers.size(),
              map.proceduralOutdoor.maximumBlockers);
    EXPECT_NE(first.layoutHash, 0U);
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, first, anchors));
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
    EXPECT_NE(first.ballisticBlockers, second.ballisticBlockers);
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

TEST(RaidMapGenerationTest, SelectsOnlyLegalSpecialLocationCandidate)
{
    const MapDefinition &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_EQ(map.interiors.size(), 1U);
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
}
