#include <gtest/gtest.h>

#include "raid_tactical_map_presentation.h"

namespace
{
RaidTacticalMapState configuredMap(RaidIntelligenceLoadout loadout = {})
{
    RaidTacticalMapState map;
    map.configure(
        {960.0F, 540.0F},
        loadout,
        {{820.0F, 430.0F}, {80.0F, 60.0F}},
        ContentRect{{70.0F, 30.0F}, {60.0F, 70.0F}},
        ContentRect{{430.0F, 220.0F}, {75.0F, 75.0F}},
        ContentRect{{610.0F, 130.0F}, {120.0F, 90.0F}},
        {{250.0F, 150.0F}},
        {{RaidSpaceDefinitionId{"raid_space.test.office"},
          "Exchange Office",
          ContentRect{{730.0F, 80.0F}, {120.0F, 100.0F}},
          false}});
    return map;
}
}

TEST(RaidTacticalMapPresentationTest, FogModeUsesAuthoritativeDiscovery)
{
    RaidTacticalMapState map = configuredMap();
    EXPECT_FALSE(tacticalMapCellVisible(
        map, 3, 3, RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_FALSE(tacticalMapPointVisible(
        map, {100.0F, 100.0F},
        RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_FALSE(tacticalMapExtractionVisible(
        map, RaidMapExtractionKind::Normal,
        RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_FALSE(tacticalMapAdvancedResourceVisible(
        map, RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_FALSE(tacticalMapSpecialLocationVisible(
        map.specialLocations().front(),
        RaidTacticalMapPresentationMode::FogOfWar));
}

TEST(RaidTacticalMapPresentationTest, FullStaticMapRevealsStaticGeometryOnly)
{
    const RaidTacticalMapState map = configuredMap();
    EXPECT_TRUE(tacticalMapCellVisible(
        map, 3, 3, RaidTacticalMapPresentationMode::FullStaticMap));
    EXPECT_TRUE(tacticalMapPointVisible(
        map, {100.0F, 100.0F},
        RaidTacticalMapPresentationMode::FullStaticMap));
    EXPECT_TRUE(tacticalMapExtractionVisible(
        map, RaidMapExtractionKind::Normal,
        RaidTacticalMapPresentationMode::FullStaticMap));
    EXPECT_TRUE(tacticalMapAdvancedResourceVisible(
        map, RaidTacticalMapPresentationMode::FullStaticMap));
    EXPECT_TRUE(tacticalMapSpecialLocationVisible(
        map.specialLocations().front(),
        RaidTacticalMapPresentationMode::FullStaticMap));

    EXPECT_FALSE(tacticalMapEnemyDeploymentVisible(map));
    EXPECT_FALSE(map.cellRevealed(3, 3));
    EXPECT_FALSE(map.specialLocations().front().discovered);
}

TEST(RaidTacticalMapPresentationTest, FullStaticMapCannotRevealOutsideWorld)
{
    const RaidTacticalMapState map = configuredMap();
    EXPECT_FALSE(tacticalMapCellVisible(
        map, -1, 0, RaidTacticalMapPresentationMode::FullStaticMap));
    EXPECT_FALSE(tacticalMapPointVisible(
        map, {-1.0F, 100.0F},
        RaidTacticalMapPresentationMode::FullStaticMap));
}

TEST(RaidTacticalMapPresentationTest, EnemyIntelRemainsIndependent)
{
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Enemy, true);
    const RaidTacticalMapState map = configuredMap(loadout);
    EXPECT_TRUE(tacticalMapEnemyDeploymentVisible(map));
}

TEST(RaidTacticalMapPresentationTest,
     ObjectivesSeparateBriefingExplorationAndDebugVisibility)
{
    RaidTacticalMapState map;
    map.configure(
        {960.0F, 540.0F}, {},
        {{820.0F, 430.0F}, {80.0F, 60.0F}},
        std::nullopt, std::nullopt, std::nullopt, {}, {},
        {
            {RaidTacticalObjectiveKind::HighRiskControl,
             {{420.0F, 210.0F}, {80.0F, 80.0F}},
             RaidTacticalObjectiveVisibility::Explored},
            {RaidTacticalObjectiveKind::Rescue,
             {{700.0F, 360.0F}, {80.0F, 80.0F}},
             RaidTacticalObjectiveVisibility::Briefed},
            {RaidTacticalObjectiveKind::SelfRecovery,
             {{100.0F, 360.0F}, {80.0F, 80.0F}},
             RaidTacticalObjectiveVisibility::Briefed},
        });

    ASSERT_EQ(map.objectives().size(), 3U);
    EXPECT_FALSE(tacticalMapObjectiveVisible(
        map, map.objectives()[0],
        RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_TRUE(tacticalMapObjectiveVisible(
        map, map.objectives()[1],
        RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_TRUE(tacticalMapObjectiveVisible(
        map, map.objectives()[2],
        RaidTacticalMapPresentationMode::FogOfWar));

    map.revealAround({460.0F, 250.0F});
    EXPECT_TRUE(tacticalMapObjectiveVisible(
        map, map.objectives()[0],
        RaidTacticalMapPresentationMode::FogOfWar));

    const RaidTacticalMapState hidden = []
    {
        RaidTacticalMapState result;
        result.configure(
            {960.0F, 540.0F}, {},
            {{820.0F, 430.0F}, {80.0F, 60.0F}},
            std::nullopt, std::nullopt, std::nullopt, {}, {},
            {{RaidTacticalObjectiveKind::HighRiskControl,
              {{420.0F, 210.0F}, {80.0F, 80.0F}},
              RaidTacticalObjectiveVisibility::Explored}});
        return result;
    }();
    EXPECT_TRUE(tacticalMapObjectiveVisible(
        hidden, hidden.objectives().front(),
        RaidTacticalMapPresentationMode::FullStaticMap));
    EXPECT_FALSE(tacticalMapEnemyDeploymentVisible(hidden));
}

TEST(RaidTacticalMapPresentationTest,
     ResourcePointsRespectDiscoveryResourceIntelAndDebugMap)
{
    const auto configureResourceMap = [](RaidIntelligenceLoadout loadout)
    {
        RaidTacticalMapState map = configuredMap(loadout);
        RaidGeneratedMapLayout layout;
        layout.layoutVersion = 4U;
        layout.resourcePoints = {{
            "resource.secured.0",
            "resource.frontier.secured_cargo",
            "SECURED CARGO",
            RaidResourcePointKind::HighValue,
            LootTableDefinitionId{"loot.raid.high_risk"},
            3U,
            3U,
            {{250.0F, 150.0F}, {80.0F, 60.0F}},
            1U,
            {}}};
        map.configureOutdoorLayout(layout, 32U, 18U);
        return map;
    };

    RaidTacticalMapState unexplored = configureResourceMap({});
    ASSERT_EQ(unexplored.outdoorResourcePoints().size(), 1U);
    const RaidTacticalResourcePoint &resourcePoint =
        unexplored.outdoorResourcePoints().front();
    EXPECT_FALSE(tacticalMapResourcePointVisible(
        unexplored, resourcePoint,
        RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_TRUE(tacticalMapResourcePointVisible(
        unexplored, resourcePoint,
        RaidTacticalMapPresentationMode::FullStaticMap));

    unexplored.revealAround({290.0F, 180.0F});
    EXPECT_TRUE(tacticalMapResourcePointVisible(
        unexplored, resourcePoint,
        RaidTacticalMapPresentationMode::FogOfWar));

    RaidIntelligenceLoadout resourceIntel;
    resourceIntel.set(RaidIntelligenceCategory::Resource, true);
    const RaidTacticalMapState informed =
        configureResourceMap(resourceIntel);
    EXPECT_TRUE(tacticalMapResourcePointVisible(
        informed, informed.outdoorResourcePoints().front(),
        RaidTacticalMapPresentationMode::FogOfWar));
    EXPECT_FALSE(tacticalMapEnemyDeploymentVisible(informed));
}
