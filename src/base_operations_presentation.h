#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "base_facility_management.h"

enum class BaseOperationsOverviewFilter
{
    All,
    Attention,
    InProgress,
    Ready
};

enum class BaseWorldInformationDetail
{
    Marker,
    Summary,
    Detail
};

inline constexpr std::size_t kBaseOperationsEntriesPerPage{4U};

struct BaseOperationsOverviewPage
{
    BaseOperationsOverviewFilter filter{BaseOperationsOverviewFilter::All};
    std::size_t pageIndex{};
    std::size_t pageCount{1U};
    std::size_t matchingEntryCount{};
    std::array<std::size_t, kBaseOperationsEntriesPerPage> entryIndices{};
    std::size_t visibleEntryCount{};
};

[[nodiscard]] inline bool baseOperationMatchesFilter(
    BaseOperationOverviewKind kind,
    BaseOperationsOverviewFilter filter) noexcept
{
    if (filter == BaseOperationsOverviewFilter::All)
        return true;
    if (filter == BaseOperationsOverviewFilter::Ready)
        return kind == BaseOperationOverviewKind::OutputReady;
    if (filter == BaseOperationsOverviewFilter::InProgress)
    {
        return kind == BaseOperationOverviewKind::Construction ||
            kind == BaseOperationOverviewKind::ResidentTreatment ||
            kind == BaseOperationOverviewKind::Manufacturing;
    }
    return kind == BaseOperationOverviewKind::StaffingGap ||
        kind == BaseOperationOverviewKind::ResourceShortage ||
        kind == BaseOperationOverviewKind::ResidentPressure;
}

[[nodiscard]] inline BaseOperationsOverviewPage
projectBaseOperationsOverviewPage(
    const BaseOperationsOverviewProjection &projection,
    BaseOperationsOverviewFilter filter,
    std::size_t requestedPage) noexcept
{
    BaseOperationsOverviewPage result;
    result.filter = filter;
    for (const BaseOperationOverviewEntry &entry : projection.entries)
    {
        if (baseOperationMatchesFilter(entry.kind, filter))
            ++result.matchingEntryCount;
    }
    result.pageCount = result.matchingEntryCount == 0U
        ? 1U
        : 1U + (result.matchingEntryCount - 1U) /
            kBaseOperationsEntriesPerPage;
    result.pageIndex = std::min(requestedPage, result.pageCount - 1U);
    const std::size_t firstMatching =
        result.pageIndex * kBaseOperationsEntriesPerPage;
    std::size_t matchingIndex{};
    for (std::size_t index{}; index < projection.entries.size(); ++index)
    {
        if (!baseOperationMatchesFilter(
                projection.entries[index].kind, filter))
        {
            continue;
        }
        if (matchingIndex >= firstMatching &&
            result.visibleEntryCount < kBaseOperationsEntriesPerPage)
        {
            result.entryIndices[result.visibleEntryCount++] = index;
        }
        ++matchingIndex;
    }
    return result;
}

[[nodiscard]] inline BaseWorldInformationDetail
projectBaseWorldInformationDetail(float zoom, bool selected) noexcept
{
    if (selected)
        return BaseWorldInformationDetail::Detail;
    if (!std::isfinite(zoom) || zoom <= 0.75F)
        return BaseWorldInformationDetail::Marker;
    if (zoom < 1.25F)
        return BaseWorldInformationDetail::Summary;
    return BaseWorldInformationDetail::Detail;
}
