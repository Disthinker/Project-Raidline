#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <utility>

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
    EXPECT_FALSE(cabinet.isSearched());
}

TEST(StorageCabinetTest, CommitsOneCompleteSameSizeSearchResult)
{
    StorageCabinet cabinet{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};
    GridInventory generated{{6, 6}};
    ASSERT_TRUE(generated.tryPlace(
        ItemInstance{41, ItemId::Medkit},
        {2, 3}));

    EXPECT_TRUE(cabinet.tryCommitSearchResult(
        std::move(generated)));
    EXPECT_TRUE(cabinet.isSearched());
    ASSERT_EQ(cabinet.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        cabinet.inventory().placedItems().front().item.instanceId(),
        41U);
    EXPECT_EQ(
        cabinet.inventory().placedItems().front().origin,
        (GridPosition{2, 3}));
}

TEST(StorageCabinetTest, RejectsSecondSearchResultWithoutReplacingInventory)
{
    StorageCabinet cabinet{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};
    GridInventory first{{6, 6}};
    GridInventory second{{6, 6}};
    ASSERT_TRUE(first.tryPlace(
        ItemInstance{51, ItemId::Cola},
        {0, 0}));
    ASSERT_TRUE(second.tryPlace(
        ItemInstance{52, ItemId::Pistol},
        {1, 1}));
    ASSERT_TRUE(cabinet.tryCommitSearchResult(
        std::move(first)));

    EXPECT_FALSE(cabinet.tryCommitSearchResult(
        std::move(second)));
    ASSERT_EQ(cabinet.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        cabinet.inventory().placedItems().front().item.instanceId(),
        51U);
}

TEST(StorageCabinetTest, RejectsWrongSizeSearchResultWithoutMarkingSearched)
{
    StorageCabinet cabinet{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};
    GridInventory wrongSize{{5, 6}};

    EXPECT_FALSE(cabinet.tryCommitSearchResult(
        std::move(wrongSize)));
    EXPECT_FALSE(cabinet.isSearched());
    EXPECT_TRUE(cabinet.inventory().placedItems().empty());
}

TEST(StorageCabinetTest, RejectsSearchWhenUnsearchedInventoryWasExternallyFilled)
{
    StorageCabinet cabinet{
        {960.0F, 296.0F},
        {96.0F, 128.0F},
        64.0F,
        {6, 6}};
    ASSERT_TRUE(cabinet.inventory().tryPlace(
        ItemInstance{61, ItemId::Cola},
        {0, 0}));
    GridInventory generated{{6, 6}};

    EXPECT_FALSE(cabinet.tryCommitSearchResult(
        std::move(generated)));
    EXPECT_FALSE(cabinet.isSearched());
    ASSERT_EQ(cabinet.inventory().placedItems().size(), 1U);
    EXPECT_EQ(
        cabinet.inventory().placedItems().front().item.instanceId(),
        61U);
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
