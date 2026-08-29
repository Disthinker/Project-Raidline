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
