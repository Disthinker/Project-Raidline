#pragma once

#include <algorithm>
#include <cstddef>

struct BaseBuildCatalogPage
{
    std::size_t pageIndex{};
    std::size_t pageCount{1U};
    std::size_t firstEntry{};
    std::size_t visibleEntryCount{};
};

[[nodiscard]] inline BaseBuildCatalogPage projectBaseBuildCatalogPage(
    std::size_t entryCount,
    std::size_t requestedPage,
    std::size_t entriesPerPage) noexcept
{
    if (entriesPerPage == 0U)
        return {};
    const std::size_t pageCount = entryCount == 0U
        ? 1U
        : 1U + (entryCount - 1U) / entriesPerPage;
    const std::size_t pageIndex = std::min(
        requestedPage, pageCount - 1U);
    const std::size_t firstEntry = pageIndex * entriesPerPage;
    return BaseBuildCatalogPage{
        pageIndex,
        pageCount,
        firstEntry,
        std::min(entriesPerPage, entryCount -
            std::min(firstEntry, entryCount))};
}
