#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

#include "storage_cabinet.h"

TEST(StorageCabinetTest, OwnsWorldBoundsAndSixBySixInventory)
{
    StorageCabinet cabinet{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};

    EXPECT_EQ(cabinet.position().x, 960.0F);
    EXPECT_EQ(cabinet.position().y, 296.0F);
    EXPECT_EQ(cabinet.size().x, 96.0F);
    EXPECT_EQ(cabinet.size().y, 128.0F);
    EXPECT_EQ(cabinet.inventory().width(), 6);
    EXPECT_EQ(cabinet.inventory().height(), 6);
    EXPECT_TRUE(cabinet.inventory().placedItems().empty());
}

TEST(StorageCabinetTest, InteractionUsesExpandedWorldBounds)
{
    const StorageCabinet cabinet{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};

    EXPECT_TRUE(cabinet.canInteract(
        Rect{{880.0F, 360.0F}, {32.0F, 32.0F}}));
    EXPECT_FALSE(cabinet.canInteract(
        Rect{{640.0F, 360.0F}, {32.0F, 32.0F}}));
}

TEST(StorageCabinetTest, RejectsInvalidWorldGeometry)
{
    EXPECT_THROW(
        (StorageCabinet{
            {960.0F, 296.0F},
            {0.0F, 128.0F},
            64.0F,
            {6, 6}}),
        std::invalid_argument);
    EXPECT_THROW(
        (StorageCabinet{
            {960.0F, 296.0F},
            {96.0F, 128.0F},
            -1.0F,
            {6, 6}}),
        std::invalid_argument);
    EXPECT_THROW(
        (StorageCabinet{
            {960.0F, 296.0F},
            {96.0F, 128.0F},
            std::numeric_limits<float>::max(),
            {6, 6}}),
        std::invalid_argument);
}
