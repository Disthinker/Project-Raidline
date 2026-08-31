#include <gtest/gtest.h>

#include "content_registry.h"
#include "profile_container_presentation.h"

TEST(ProfileContainerPresentationTest, UsesEveryDefinedChestRigCompartment)
{
    const ItemDefinition &definition = publishedContentRegistry().item(
        ItemDefinitionId{"item.container.chest_rig_assault"});

    const ProfileCompartmentLayout layout = layoutProfileCompartments(
        definition, 214.0F, 442.0F, 580.0F, 20.0F, false);

    ASSERT_EQ(
        layout.placements.size(),
        definition.containerCompartments.size());
    EXPECT_EQ(layout.placements.front().label, "MAG 1");
    EXPECT_EQ(layout.placements[3].label, "MAG 4");
    EXPECT_EQ(layout.placements[4].label, "UTIL 1");
    EXPECT_EQ(layout.placements.back().label, "UTIL 4");
    EXPECT_LE(layout.bottom, 506.0F);
}

TEST(ProfileContainerPresentationTest, BackpackUsesOneContinuousNamedGrid)
{
    const ItemDefinition &definition = publishedContentRegistry().item(
        ItemDefinitionId{"item.container.backpack_expedition"});

    const ProfileCompartmentLayout layout = layoutProfileCompartments(
        definition, 214.0F, 520.0F, 580.0F, 20.0F, true);

    ASSERT_EQ(layout.placements.size(), 1U);
    EXPECT_EQ(layout.placements.front().label, "BACKPACK");
    EXPECT_FLOAT_EQ(layout.bottom, 640.0F);
}

TEST(ProfileContainerPresentationTest, WrapsFutureWideCompartmentRows)
{
    ItemDefinition definition;
    definition.containerCompartments = {
        {5, 2, ContainerPocketKind::General},
        {5, 2, ContainerPocketKind::General}};

    const ProfileCompartmentLayout layout = layoutProfileCompartments(
        definition, 100.0F, 200.0F, 260.0F, 20.0F, false);

    ASSERT_EQ(layout.placements.size(), 2U);
    EXPECT_FLOAT_EQ(layout.placements.front().y, 200.0F);
    EXPECT_FLOAT_EQ(layout.placements.back().y, 264.0F);
    EXPECT_FLOAT_EQ(layout.bottom, 304.0F);
}
