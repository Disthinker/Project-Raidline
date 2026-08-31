#include <gtest/gtest.h>

#include <algorithm>

#include "collision.h"
#include "home_region_layout.h"

namespace
{
Rect rect(ContentRect value)
{
    return Rect{value.position, value.size};
}
}

TEST(HomeRegionLayoutTest, GenerationIsDeterministicPerBaseSite)
{
    const HomeRegionLayout first =
        generateHomeRegionLayout("regional_base_site.greyline_yard");
    const HomeRegionLayout second =
        generateHomeRegionLayout("regional_base_site.greyline_yard");
    EXPECT_EQ(first.layoutHash, second.layoutHash);
    EXPECT_EQ(first.districts, second.districts);
    EXPECT_EQ(first.terrainSpans, second.terrainSpans);
    EXPECT_EQ(first.roadCells, second.roadCells);
    EXPECT_EQ(first.props, second.props);
    EXPECT_EQ(first.layoutHash, homeRegionLayoutHash(first));
    EXPECT_NE(first.layoutHash,
              generateHomeRegionLayout(
                  "regional_base_site.ashworks_logistics_yard").layoutHash);
}

TEST(HomeRegionLayoutTest, BaseIsSmallParcelInsideLargeWorld)
{
    const HomeRegionLayout layout =
        generateHomeRegionLayout("regional_base_site.greyline_yard");
    EXPECT_FLOAT_EQ(layout.worldSize.x, 12800.0F);
    EXPECT_FLOAT_EQ(layout.worldSize.y, 7200.0F);
    EXPECT_GE(layout.baseParcel.position.x, 0.0F);
    EXPECT_GE(layout.baseParcel.position.y, 0.0F);
    EXPECT_LE(layout.baseParcel.position.x + layout.baseParcel.size.x,
              layout.worldSize.x);
    EXPECT_LE(layout.baseParcel.position.y + layout.baseParcel.size.y,
              layout.worldSize.y);
    EXPECT_LT(layout.baseParcel.size.x * layout.baseParcel.size.y,
              layout.worldSize.x * layout.worldSize.y * 0.03F);
    EXPECT_EQ(std::count_if(
                  layout.districts.begin(), layout.districts.end(),
                  [](const HomeRegionDistrictSnapshot &district)
                  { return district.kind == HomeRegionDistrictKind::Base; }),
              1);
}

TEST(HomeRegionLayoutTest, RoadsTerrainAndPropsFormEnvironmentWithoutOverlap)
{
    const HomeRegionLayout layout =
        generateHomeRegionLayout("regional_base_site.greyline_yard");
    EXPECT_GT(layout.terrainSpans.size(), 300U);
    EXPECT_GT(layout.roadCells.size(), 1000U);
    EXPECT_GT(layout.props.size(), 150U);
    EXPECT_EQ(layout.movementBlockers.size(),
              std::count_if(
                  layout.props.begin(), layout.props.end(),
                  [](const RaidOutdoorPropSnapshot &prop)
                  { return prop.collidable; }));
    for (std::size_t left{}; left < layout.props.size(); ++left)
    {
        EXPECT_FALSE(isCollision(rect(layout.props[left].bounds),
                                 rect(layout.baseParcel)));
        for (std::size_t right = left + 1U; right < layout.props.size(); ++right)
            EXPECT_FALSE(isCollision(rect(layout.props[left].bounds),
                                     rect(layout.props[right].bounds)));
    }
}

TEST(HomeRegionLayoutTest, PublishesAllRequiredEnvironmentKinds)
{
    const HomeRegionLayout layout =
        generateHomeRegionLayout("regional_base_site.greyline_yard");
    for (const RaidOutdoorPropKind kind : {
             RaidOutdoorPropKind::Factory,
             RaidOutdoorPropKind::Warehouse,
             RaidOutdoorPropKind::Container,
             RaidOutdoorPropKind::EngineeringEquipment,
             RaidOutdoorPropKind::Car,
             RaidOutdoorPropKind::Truck,
             RaidOutdoorPropKind::RoadBarrier,
             RaidOutdoorPropKind::Debris})
        EXPECT_TRUE(std::any_of(
            layout.props.begin(), layout.props.end(),
            [kind](const RaidOutdoorPropSnapshot &prop)
            { return prop.kind == kind; }));
}
