#include <gtest/gtest.h>

#include "raid_tactical_map.h"

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
        {{250.0F, 150.0F}, {700.0F, 380.0F}});
    return map;
}
}

TEST(RaidTacticalMapTest, ExplorationRevealsNearbyCellsAndPersists)
{
    RaidTacticalMapState map = configuredMap();
    EXPECT_FALSE(map.pointRevealed({100.0F, 100.0F}));
    EXPECT_FALSE(map.pointRevealed({850.0F, 450.0F}));

    map.revealAround({100.0F, 100.0F});
    EXPECT_TRUE(map.pointRevealed({100.0F, 100.0F}));
    EXPECT_FALSE(map.pointRevealed({850.0F, 450.0F}));

    map.revealAround({850.0F, 450.0F});
    EXPECT_TRUE(map.pointRevealed({100.0F, 100.0F}));
    EXPECT_TRUE(map.pointRevealed({850.0F, 450.0F}));
}

TEST(RaidTacticalMapTest, ExtractionRequiresDiscoveryWithoutTransportMap)
{
    RaidTacticalMapState map = configuredMap();
    EXPECT_FALSE(map.extractionVisible(RaidMapExtractionKind::Normal));
    EXPECT_FALSE(map.extractionVisible(
        RaidMapExtractionKind::EmergencySignal));
    EXPECT_FALSE(map.extractionVisible(
        RaidMapExtractionKind::EmergencyConditional));

    map.revealAround({850.0F, 450.0F});
    EXPECT_TRUE(map.extractionVisible(RaidMapExtractionKind::Normal));
    EXPECT_FALSE(map.extractionVisible(
        RaidMapExtractionKind::EmergencySignal));
}

TEST(RaidTacticalMapTest, FrozenIntelligenceExposesOnlyItsOwnProjection)
{
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Transport, true);
    loadout.set(RaidIntelligenceCategory::Enemy, true);
    const RaidTacticalMapState map = configuredMap(loadout);

    EXPECT_TRUE(map.extractionVisible(RaidMapExtractionKind::Normal));
    EXPECT_TRUE(map.extractionVisible(
        RaidMapExtractionKind::EmergencySignal));
    EXPECT_TRUE(map.extractionVisible(
        RaidMapExtractionKind::EmergencyConditional));
    EXPECT_TRUE(map.hasIntelligence(RaidIntelligenceCategory::Enemy));
    EXPECT_FALSE(map.hasIntelligence(RaidIntelligenceCategory::Resource));
    EXPECT_EQ(map.initialEnemyCenters().size(), 2U);
    ASSERT_TRUE(map.advancedResourceArea().has_value());
}

TEST(RaidTacticalMapTest, OutOfBoundsQueriesAreNeverRevealed)
{
    RaidTacticalMapState map = configuredMap();
    map.revealAround({0.0F, 0.0F});
    EXPECT_FALSE(map.pointRevealed({-1.0F, 0.0F}));
    EXPECT_FALSE(map.pointRevealed({960.0F, 100.0F}));
    EXPECT_FALSE(map.cellRevealed(-1, 0));
    EXPECT_FALSE(map.cellRevealed(map.columns(), 0));
}
