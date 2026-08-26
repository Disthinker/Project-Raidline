#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "hit_resolution.h"
#include "rect.h"
#include "vec2.h"

// Immutable broad-phase index for static blocker geometry in one Raid space.
// Each blocker is assigned to exactly one cell by its center. Queries expand
// by the largest blocker half-extent before visiting center cells, so every
// possible overlap remains a candidate without duplicate blocker entries.
class RaidSpaceBlockerIndex
{
public:
    [[nodiscard]] static std::optional<RaidSpaceBlockerIndex> build(
        Vec2 worldSize,
        std::span<const BallisticBlocker> blockers,
        float cellSize = 128.0F);

    [[nodiscard]] bool hasLineOfSight(
        Vec2 start,
        Vec2 end,
        std::size_t *blockerTests = nullptr) const noexcept;

    void queryCandidateIndices(
        Rect bounds,
        std::vector<std::size_t> &output) const;

    [[nodiscard]] const Rect &blockerBounds(std::size_t index) const;
    [[nodiscard]] std::size_t blockerCount() const noexcept;
    [[nodiscard]] Vec2 worldSize() const noexcept;
    [[nodiscard]] float cellSize() const noexcept;

private:
    Vec2 worldSize_{};
    float cellSize_{};
    std::size_t columns_{};
    std::size_t rows_{};
    Vec2 maximumHalfExtent_{};
    std::vector<Rect> blockerBounds_;
    std::vector<std::vector<std::size_t>> cells_;

    [[nodiscard]] std::size_t clampedColumn(float x) const noexcept;
    [[nodiscard]] std::size_t clampedRow(float y) const noexcept;
};
