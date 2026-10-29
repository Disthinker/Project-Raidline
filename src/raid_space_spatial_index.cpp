#include "raid_space_spatial_index.h"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <limits>
#include <stdexcept>

#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
#include <xmmintrin.h>
#endif

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

    bool preparedSightEnvironment() noexcept
    {
#if (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)) && FLT_EVAL_METHOD == 0
        // Cache only the environment whose binary32 arithmetic was prepared.
        // Different rounding, FTZ/DAZ or unmasked traps use the original path.
        return sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
            std::numeric_limits<float>::radix == 2 &&
            std::fegetround() == FE_TONEAREST &&
            (_mm_getcsr() & 0xffc0U) == 0x1f80U;
#else
        return false;
#endif
    }
}

RaidSpaceBlockerIndex::SightBounds
RaidSpaceBlockerIndex::prepareSightBounds(Rect bounds) noexcept
{
    SightBounds prepared;
    if (!finite(bounds.position) || !finitePositive(bounds.size))
        return prepared;
    // Preserve the original left association and the exact epsilon.
    prepared.lowX = bounds.position.x + kRaidSightGeometryEpsilon;
    prepared.highX = bounds.position.x + bounds.size.x - kRaidSightGeometryEpsilon;
    prepared.lowY = bounds.position.y + kRaidSightGeometryEpsilon;
    prepared.highY = bounds.position.y + bounds.size.y - kRaidSightGeometryEpsilon;
    if (!std::isfinite(prepared.lowX) || !std::isfinite(prepared.highX) ||
        !std::isfinite(prepared.lowY) || !std::isfinite(prepared.highY))
        return prepared;
    prepared.empty = prepared.lowX >= prepared.highX || prepared.lowY >= prepared.highY;
    prepared.ready = true;
    return prepared;
}

bool RaidSpaceBlockerIndex::intersectsSightBounds(
    Vec2 start, Vec2 delta, bool nearX, bool nearY,
    const SightBounds &bounds) noexcept
{
    if (bounds.empty)
        return false;
    float enter{}, leave{1};
    if (nearX)
    {
        if (!(start.x > bounds.lowX && start.x < bounds.highX))
            return false;
    }
    else
    {
        float first = (bounds.lowX - start.x) / delta.x;
        float second = (bounds.highX - start.x) / delta.x;
        if (first > second) std::swap(first, second);
        enter = std::max(enter, first);
        leave = std::min(leave, second);
        if (!(enter <= leave)) return false;
    }
    if (nearY)
    {
        if (!(start.y > bounds.lowY && start.y < bounds.highY))
            return false;
    }
    else
    {
        float first = (bounds.lowY - start.y) / delta.y;
        float second = (bounds.highY - start.y) / delta.y;
        if (first > second) std::swap(first, second);
        enter = std::max(enter, first);
        leave = std::min(leave, second);
        if (!(enter <= leave)) return false;
    }
    return leave >= 0 && enter <= 1 && leave - enter > kRaidSightGeometryEpsilon;
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
    if (preparedSightEnvironment())
    {
        index.sightBlockers_.reserve(index.blockerBounds_.size());
        for (const Rect bounds : index.blockerBounds_)
            index.sightBlockers_.push_back(prepareSightBounds(bounds));
        index.sightCells_.resize(index.cells_.size());
        // maximumHalfExtent_ is final before any expanded cell is prepared.
        for (std::size_t row = 0; row < index.rows_; ++row)
            for (std::size_t column = 0; column < index.columns_; ++column)
            {
                const std::size_t cell = row * index.columns_ + column;
                if (index.cells_[cell].empty()) continue;
                const Rect expandedCell{
                    {static_cast<float>(column) * index.cellSize_ - index.maximumHalfExtent_.x,
                     static_cast<float>(row) * index.cellSize_ - index.maximumHalfExtent_.y},
                    {index.cellSize_ + index.maximumHalfExtent_.x * 2.0F,
                     index.cellSize_ + index.maximumHalfExtent_.y * 2.0F}};
                index.sightCells_[cell] = prepareSightBounds(expandedCell);
            }
        index.sightPrepared_ = true;
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

    bool prepared = sightPrepared_ && preparedSightEnvironment();
    Vec2 delta{};
    bool nearX{}, nearY{};
    if (prepared)
    {
        delta = {end.x - start.x, end.y - start.y};
        prepared = finite(delta);
        if (prepared)
        {
            nearX = std::abs(delta.x) <= kRaidSightGeometryEpsilon;
            nearY = std::abs(delta.y) <= kRaidSightGeometryEpsilon;
        }
    }

    // Cell membership is deliberately conservative. Dense cells can contain
    // many walls whose bounds do not meet this ray at all; preserve the exact
    // interior predicate and boundary semantics for every remaining wall.
    const float rayLeft=std::min(start.x,end.x),rayRight=std::max(start.x,end.x);
    const float rayTop=std::min(start.y,end.y),rayBottom=std::max(start.y,end.y);
    const auto *const blockerData=blockerBounds_.data();

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
            const std::size_t cellIndex = row * columns_ + column;
            if (prepared && sightCells_[cellIndex].ready)
            {
                if (!intersectsSightBounds(start, delta, nearX, nearY, sightCells_[cellIndex]))
                    continue;
            }
            else
            {
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
            }
            const auto *const endCandidate=cell.data()+cell.size();
            for (const auto *cursor=cell.data(); cursor!=endCandidate; ++cursor)
            {
                const auto candidate=*cursor;
                if (blockerTests != nullptr)
                {
                    ++*blockerTests;
                }
                const Rect &bounds=blockerData[candidate];
                if(bounds.position.x+bounds.size.x<rayLeft || bounds.position.x>rayRight ||
                   bounds.position.y+bounds.size.y<rayTop || bounds.position.y>rayBottom)continue;
                if (prepared && sightBlockers_[candidate].ready)
                {
                    if (intersectsSightBounds(start, delta, nearX, nearY, sightBlockers_[candidate]))
                        return false;
                }
                else if (raidSpaceSegmentIntersectsBlockerInterior(
                        start,
                        end,
                        bounds))
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
    std::size_t nonemptyCells{};
    struct Range { const std::size_t *first, *last; };
    std::array<Range, 8> ranges;
    std::size_t rangeCount{}, candidateCount{};
    bool directMerge = true;
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
            if (!cell.empty()) {
                ++nonemptyCells;
                if (directMerge && rangeCount < ranges.size() &&
                    cell.size() <= 256U - candidateCount) {
                    // The immutable index owns these already sorted ranges;
                    // output growth cannot invalidate any borrowed pointer.
                    ranges[rangeCount++] = {cell.data(), cell.data() + cell.size()};
                    candidateCount += cell.size();
                } else {
                    if (directMerge) {
                        // Crossing either small-query limit preserves every
                        // earlier cell before continuing the original fallback.
                        for (std::size_t i = 0; i < rangeCount; ++i)
                            output.insert(output.end(), ranges[i].first, ranges[i].last);
                        directMerge = false;
                    }
                    output.insert(output.end(), cell.data(), cell.data() + cell.size());
                }
            }
        }
    }
    // build() visits original indices in ascending order. Preserve the direct
    // single-cell copy and the general insert/sort path for larger queries.
    if (!directMerge) {
        if (nonemptyCells > 1) std::sort(output.begin(), output.end());
    } else if (rangeCount == 1) {
        output.insert(output.end(), ranges[0].first, ranges[0].last);
    } else if (rangeCount > 1) {
        // Establish every output element's lifetime once, then merge directly
        // from the source cells without an intermediate candidate copy.
        output.resize(candidateCount);
        auto *const cursors = ranges.data();
        std::size_t active = rangeCount;
        auto *write = output.data();
        while (active) {
            std::size_t selected{};
            for (std::size_t i = 1; i < active; ++i)
                if (*cursors[i].first < *cursors[selected].first) selected = i;
            *write++ = *cursors[selected].first++;
            if (cursors[selected].first == cursors[selected].last)
                cursors[selected] = cursors[--active];
        }
    }
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
