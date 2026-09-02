#include <gtest/gtest.h>

#include "base_build_catalog_presentation.h"

TEST(BaseBuildCatalogPresentationTest, EmptyCatalogHasOneEmptyPage)
{
    const BaseBuildCatalogPage page = projectBaseBuildCatalogPage(0U, 9U, 4U);
    EXPECT_EQ(page.pageIndex, 0U);
    EXPECT_EQ(page.pageCount, 1U);
    EXPECT_EQ(page.firstEntry, 0U);
    EXPECT_EQ(page.visibleEntryCount, 0U);
}

TEST(BaseBuildCatalogPresentationTest, PagesEveryPublishedEntryWithoutLoss)
{
    const BaseBuildCatalogPage first = projectBaseBuildCatalogPage(9U, 0U, 4U);
    EXPECT_EQ(first.pageCount, 3U);
    EXPECT_EQ(first.firstEntry, 0U);
    EXPECT_EQ(first.visibleEntryCount, 4U);

    const BaseBuildCatalogPage last = projectBaseBuildCatalogPage(9U, 2U, 4U);
    EXPECT_EQ(last.firstEntry, 8U);
    EXPECT_EQ(last.visibleEntryCount, 1U);
}

TEST(BaseBuildCatalogPresentationTest, RemovedEntriesClampStalePage)
{
    const BaseBuildCatalogPage page = projectBaseBuildCatalogPage(3U, 7U, 4U);
    EXPECT_EQ(page.pageIndex, 0U);
    EXPECT_EQ(page.pageCount, 1U);
    EXPECT_EQ(page.visibleEntryCount, 3U);
}
