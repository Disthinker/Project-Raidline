#include <gtest/gtest.h>

#include <limits>

#include "base_operations_presentation.h"

namespace
{
BaseOperationsOverviewProjection sampleProjection()
{
    return BaseOperationsOverviewProjection{{
        {BaseFacilityKind::Workshop,
         BaseOperationOverviewKind::OutputReady},
        {BaseFacilityKind::Dormitory,
         BaseOperationOverviewKind::Construction},
        {BaseFacilityKind::Medical,
         BaseOperationOverviewKind::ResidentTreatment},
        {BaseFacilityKind::Workshop,
         BaseOperationOverviewKind::Manufacturing},
        {BaseFacilityKind::Medical,
         BaseOperationOverviewKind::StaffingGap},
        {BaseFacilityKind::KitchenWater,
         BaseOperationOverviewKind::ResourceShortage},
        {BaseFacilityKind::Dormitory,
         BaseOperationOverviewKind::ResidentPressure},
        {BaseFacilityKind::Allocation,
         BaseOperationOverviewKind::BaseWish}}};
}
}

TEST(BaseOperationsPresentationTest, FiltersStableOperationCategories)
{
    const BaseOperationsOverviewProjection projection = sampleProjection();

    const BaseOperationsOverviewPage attention =
        projectBaseOperationsOverviewPage(
            projection, BaseOperationsOverviewFilter::Attention, 0U);
    EXPECT_EQ(attention.matchingEntryCount, 4U);
    ASSERT_EQ(attention.visibleEntryCount, 4U);
    EXPECT_EQ(attention.entryIndices[0], 4U);
    EXPECT_EQ(attention.entryIndices[1], 5U);
    EXPECT_EQ(attention.entryIndices[2], 6U);

    const BaseOperationsOverviewPage inProgress =
        projectBaseOperationsOverviewPage(
            projection, BaseOperationsOverviewFilter::InProgress, 0U);
    EXPECT_EQ(inProgress.matchingEntryCount, 3U);
    ASSERT_EQ(inProgress.visibleEntryCount, 3U);
    EXPECT_EQ(inProgress.entryIndices[0], 1U);
    EXPECT_EQ(inProgress.entryIndices[2], 3U);

    const BaseOperationsOverviewPage ready =
        projectBaseOperationsOverviewPage(
            projection, BaseOperationsOverviewFilter::Ready, 0U);
    EXPECT_EQ(ready.matchingEntryCount, 1U);
    ASSERT_EQ(ready.visibleEntryCount, 1U);
    EXPECT_EQ(ready.entryIndices[0], 0U);
}

TEST(BaseOperationsPresentationTest, AllFilterPagesAndClampsStalePage)
{
    const BaseOperationsOverviewProjection projection = sampleProjection();
    const BaseOperationsOverviewPage first =
        projectBaseOperationsOverviewPage(
            projection, BaseOperationsOverviewFilter::All, 0U);
    EXPECT_EQ(first.matchingEntryCount, 8U);
    EXPECT_EQ(first.pageCount, 2U);
    EXPECT_EQ(first.visibleEntryCount, 4U);
    EXPECT_EQ(first.entryIndices[0], 0U);
    EXPECT_EQ(first.entryIndices[3], 3U);

    const BaseOperationsOverviewPage last =
        projectBaseOperationsOverviewPage(
            projection,
            BaseOperationsOverviewFilter::All,
            std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(last.pageIndex, 1U);
    ASSERT_EQ(last.visibleEntryCount, 4U);
    EXPECT_EQ(last.entryIndices[0], 4U);
    EXPECT_EQ(last.entryIndices[3], 7U);
}

TEST(BaseOperationsPresentationTest, EmptyFilterKeepsOneEmptyPage)
{
    BaseOperationsOverviewProjection projection;
    projection.entries.push_back(BaseOperationOverviewEntry{
        BaseFacilityKind::Workshop,
        BaseOperationOverviewKind::Manufacturing});
    const BaseOperationsOverviewPage page =
        projectBaseOperationsOverviewPage(
            projection, BaseOperationsOverviewFilter::Ready, 9U);
    EXPECT_EQ(page.pageIndex, 0U);
    EXPECT_EQ(page.pageCount, 1U);
    EXPECT_EQ(page.matchingEntryCount, 0U);
    EXPECT_EQ(page.visibleEntryCount, 0U);
}

TEST(BaseOperationsPresentationTest, ZoomAndSelectionChooseInformationDensity)
{
    EXPECT_EQ(
        projectBaseWorldInformationDetail(0.60F, false),
        BaseWorldInformationDetail::Marker);
    EXPECT_EQ(
        projectBaseWorldInformationDetail(0.75F, false),
        BaseWorldInformationDetail::Marker);
    EXPECT_EQ(
        projectBaseWorldInformationDetail(1.00F, false),
        BaseWorldInformationDetail::Summary);
    EXPECT_EQ(
        projectBaseWorldInformationDetail(1.25F, false),
        BaseWorldInformationDetail::Detail);
    EXPECT_EQ(
        projectBaseWorldInformationDetail(0.60F, true),
        BaseWorldInformationDetail::Detail);
}
