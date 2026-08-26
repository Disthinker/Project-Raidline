#include "raid_space_spatial_index.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "raid_space_query.h"

namespace
{
    bool finite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool finitePositive(Vec2 value) noexcept
    {
        return finite(value) && value.x > 0.0F && value.y > 0.0F;
    }
}

std::optional<RaidSpaceBlockerIndex> RaidSpaceBlockerIndex::build(
    Vec2 worldSize,
    std::span<const BallisticBlocker> blockers,
    float cellSize)
{
    if (!finitePositive(worldSize) || !std::isfinite(cellSize) ||
        cellSize <= 0.0F)
    {
        return std::nullopt;
    }

    RaidSpaceBlockerIndex index;
    index.worldSize_ = worldSize;
    index.cellSize_ = cellSize;
    index.columns_ = static_cast<std::size_t>(
        std::max(1.0F, std::ceil(worldSize.x / cellSize)));
    index.rows_ = static_cast<std::size_t>(
        std::max(1.0F, std::ceil(worldSize.y / cellSize)));
    index.cells_.resize(index.columns_ * index.rows_);
    index.blockerBounds_.reserve(blockers.size());

    for (const BallisticBlocker &blocker : blockers)
    {
        if (!finite(blocker.bounds.position) ||
            !finitePositive(blocker.bounds.size))
        {
            return std::nullopt;
        }
        const std::size_t blockerIndex = index.blockerBounds_.size();
        index.blockerBounds_.push_back(blocker.bounds);
        index.maximumHalfExtent_.x = std::max(
            index.maximumHalfExtent_.x,
            blocker.bounds.size.x * 0.5F);
        index.maximumHalfExtent_.y = std::max(
            index.maximumHalfExtent_.y,
            blocker.bounds.size.y * 0.5F);
        const Vec2 center{
            blocker.bounds.position.x + blocker.bounds.size.x * 0.5F,
            blocker.bounds.position.y + blocker.bounds.size.y * 0.5F};
        const std::size_t column = index.clampedColumn(center.x);
        const std::size_t row = index.clampedRow(center.y);
        index.cells_[row * index.columns_ + column].push_back(blockerIndex);
    }
    return index;
}

bool RaidSpaceBlockerIndex::hasLineOfSight(
    Vec2 start,
    Vec2 end,
    std::size_t *blockerTests) const noexcept
{
    if (blockerTests != nullptr)
    {
        *blockerTests = 0U;
    }
    if (!finite(start) || !finite(end))
    {
        return false;
    }

    const float minimumX = std::min(start.x, end.x) - maximumHalfExtent_.x;
    const float minimumY = std::min(start.y, end.y) - maximumHalfExtent_.y;
    const float maximumX = std::max(start.x, end.x) + maximumHalfExtent_.x;
    const float maximumY = std::max(start.y, end.y) + maximumHalfExtent_.y;
    const std::size_t firstColumn = clampedColumn(minimumX);
    const std::size_t lastColumn = clampedColumn(maximumX);
    const std::size_t firstRow = clampedRow(minimumY);
    const std::size_t lastRow = clampedRow(maximumY);
    for (std::size_t row = firstRow; row <= lastRow; ++row)
    {
        for (std::size_t column = firstColumn;
             column <= lastColumn;
             ++column)
        {
            const std::vector<std::size_t> &cell =
                cells_[row * columns_ + column];
            if (cell.empty())
            {
                continue;
            }
            const Rect expandedCell{
                Vec2{
                    static_cast<float>(column) * cellSize_ -
                        maximumHalfExtent_.x,
                    static_cast<float>(row) * cellSize_ -
                        maximumHalfExtent_.y},
                Vec2{
                    cellSize_ + maximumHalfExtent_.x * 2.0F,
                    cellSize_ + maximumHalfExtent_.y * 2.0F}};
            if (!raidSpaceSegmentIntersectsBlockerInterior(
                    start,
                    end,
                    expandedCell))
            {
                continue;
            }
            for (const std::size_t candidate : cell)
            {
                if (blockerTests != nullptr)
                {
                    ++*blockerTests;
                }
                if (raidSpaceSegmentIntersectsBlockerInterior(
                        start,
                        end,
                        blockerBounds_[candidate]))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

void RaidSpaceBlockerIndex::queryCandidateIndices(
    Rect bounds,
    std::vector<std::size_t> &output) const
{
    output.clear();
    if (!finite(bounds.position) || !finite(bounds.size) ||
        bounds.size.x < 0.0F || bounds.size.y < 0.0F || cells_.empty())
    {
        return;
    }

    const float minimumX = bounds.position.x - maximumHalfExtent_.x;
    const float minimumY = bounds.position.y - maximumHalfExtent_.y;
    const float maximumX = bounds.position.x + bounds.size.x +
        maximumHalfExtent_.x;
    const float maximumY = bounds.position.y + bounds.size.y +
        maximumHalfExtent_.y;
    const std::size_t firstColumn = clampedColumn(minimumX);
    const std::size_t lastColumn = clampedColumn(maximumX);
    const std::size_t firstRow = clampedRow(minimumY);
    const std::size_t lastRow = clampedRow(maximumY);
    for (std::size_t row = firstRow; row <= lastRow; ++row)
    {
        for (std::size_t column = firstColumn;
             column <= lastColumn;
             ++column)
        {
            const std::vector<std::size_t> &cell =
                cells_[row * columns_ + column];
            output.insert(output.end(), cell.begin(), cell.end());
        }
    }
    std::sort(output.begin(), output.end());
}

const Rect &RaidSpaceBlockerIndex::blockerBounds(std::size_t index) const
{
    if (index >= blockerBounds_.size())
    {
        throw std::out_of_range{"Raid blocker index is out of range"};
    }
    return blockerBounds_[index];
}

std::size_t RaidSpaceBlockerIndex::blockerCount() const noexcept
{
    return blockerBounds_.size();
}

Vec2 RaidSpaceBlockerIndex::worldSize() const noexcept
{
    return worldSize_;
}

float RaidSpaceBlockerIndex::cellSize() const noexcept
{
    return cellSize_;
}

std::size_t RaidSpaceBlockerIndex::clampedColumn(float x) const noexcept
{
    const float raw = std::floor(x / cellSize_);
    return static_cast<std::size_t>(std::clamp(
        raw,
        0.0F,
        static_cast<float>(columns_ - 1U)));
}

std::size_t RaidSpaceBlockerIndex::clampedRow(float y) const noexcept
{
    const float raw = std::floor(y / cellSize_);
    return static_cast<std::size_t>(std::clamp(
        raw,
        0.0F,
        static_cast<float>(rows_ - 1U)));
}
