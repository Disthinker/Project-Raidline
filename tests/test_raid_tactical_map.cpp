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
        {{250.0F, 150.0F}, {700.0F, 380.0F}},
        {{RaidSpaceDefinitionId{"raid_space.test.office"},
          "Exchange Office",
          ContentRect{{730.0F, 80.0F}, {120.0F, 100.0F}},
          false}});
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

TEST(RaidTacticalMapTest, SpecialLocationRequiresNearbyDiscoveryAndPersists)
{
    RaidTacticalMapState map = configuredMap();
    const RaidSpaceDefinitionId officeId{"raid_space.test.office"};
    ASSERT_EQ(map.specialLocations().size(), 1U);
    EXPECT_FALSE(map.specialLocations().front().discovered);
    EXPECT_FALSE(map.specialLocationVisible(officeId));

    map.revealAround({600.0F, 130.0F});
    EXPECT_FALSE(map.specialLocationVisible(officeId));

    map.revealAround({650.0F, 130.0F});
    EXPECT_TRUE(map.specialLocationVisible(officeId));
    EXPECT_TRUE(map.specialLocations().front().discovered);

    map.revealAround({50.0F, 500.0F});
    EXPECT_TRUE(map.specialLocationVisible(officeId));
}

TEST(RaidTacticalMapTest, RejectsInvalidOrDuplicateSpecialLocations)
{
    const RaidSpecialLocationMapState office{
        RaidSpaceDefinitionId{"raid_space.test.office"},
        "Exchange Office",
        ContentRect{{730.0F, 80.0F}, {120.0F, 100.0F}},
        false};
    const auto configure = [](std::vector<RaidSpecialLocationMapState> sites)
    {
        RaidTacticalMapState map;
        map.configure(
            {960.0F, 540.0F}, {},
            {{820.0F, 430.0F}, {80.0F, 60.0F}},
            std::nullopt, std::nullopt, std::nullopt, {},
            std::move(sites));
    };

    EXPECT_THROW(
        configure({office, office}),
        std::invalid_argument);
    RaidSpecialLocationMapState outside = office;
    outside.entrance.position.x = 900.0F;
    EXPECT_THROW(
        configure({std::move(outside)}),
        std::invalid_argument);
}

TEST(RaidTacticalMapTest, OutdoorLayoutProjectsReadableDistrictKinds)
{
    RaidTacticalMapState map;
    map.configure(
        {25600.0F, 14400.0F}, {},
        {{25000.0F, 13800.0F}, {100.0F, 100.0F}},
        std::nullopt, std::nullopt, std::nullopt, {});
    RaidGeneratedMapLayout layout;
    layout.layoutVersion = 3U;
    layout.districts = {
        {1U, "district.test.industrial", "INDUSTRIAL",
         RaidDistrictKind::Industrial,
         {{0U, 0U, 2U}, {1U, 0U, 2U}}, {6400.0F, 7200.0F}},
        {2U, "district.test.greenbelt", "GREENBELT",
         RaidDistrictKind::Greenbelt,
         {{0U, 2U, 2U}, {1U, 2U, 2U}}, {19200.0F, 7200.0F}}};
    map.configureOutdoorLayout(layout, 320U, 180U);

    ASSERT_TRUE(map.outdoorDistrictKind(5, 5).has_value());
    EXPECT_EQ(map.outdoorDistrictKind(5, 5),
              RaidDistrictKind::Industrial);
    EXPECT_EQ(map.outdoorDistrictKind(map.columns() - 5, 5),
              RaidDistrictKind::Greenbelt);
    EXPECT_FALSE(map.outdoorDistrictKind(-1, 0).has_value());
}

TEST(RaidTacticalMapTest, OutdoorLayoutProjectsTerrainWithoutChangingReveal)
{
    RaidTacticalMapState map;
    map.configure(
        {25600.0F, 14400.0F}, {},
        {{25000.0F, 13800.0F}, {100.0F, 100.0F}},
        std::nullopt, std::nullopt, std::nullopt, {});
    RaidGeneratedMapLayout layout;
    layout.layoutVersion = 3U;
    layout.terrainSpans = {
        RaidTerrainSpan{0U, 0U, 320U, RaidTerrainKind::Asphalt},
        RaidTerrainSpan{176U, 0U, 320U, RaidTerrainKind::Grass}};

    map.configureOutdoorLayout(layout, 320U, 180U);

    EXPECT_EQ(map.outdoorTerrainKind(0, 0), RaidTerrainKind::Asphalt);
    EXPECT_EQ(
        map.outdoorTerrainKind(map.columns() - 1, map.rows() - 1),
        RaidTerrainKind::Grass);
    EXPECT_FALSE(map.cellRevealed(0, 0));
}

TEST(RaidTacticalMapTest, OutdoorLayoutProjectsFrozenResourcePoints)
{
    RaidTacticalMapState map;
    map.configure(
        {25600.0F, 14400.0F}, {},
        {{25000.0F, 13800.0F}, {100.0F, 100.0F}},
        std::nullopt, std::nullopt, std::nullopt, {});
    RaidGeneratedMapLayout layout;
    layout.layoutVersion = 4U;
    layout.resourcePoints = {{
        "resource.maintenance.0",
        "resource.frontier.maintenance_cache",
        "MAINTENANCE CACHE",
        RaidResourcePointKind::Ordinary,
        LootTableDefinitionId{"loot.raid.alpha"},
        1U,
        2U,
        {{3200.0F, 2400.0F}, {240.0F, 160.0F}},
        1U,
        {}}};

    map.configureOutdoorLayout(layout, 320U, 180U);

    ASSERT_EQ(map.outdoorResourcePoints().size(), 1U);
    EXPECT_EQ(map.outdoorResourcePoints().front().instanceId,
              "resource.maintenance.0");
    EXPECT_EQ(map.outdoorResourcePoints().front().riskTier, 1U);
    EXPECT_FALSE(map.pointRevealed({3320.0F, 2480.0F}));
}
