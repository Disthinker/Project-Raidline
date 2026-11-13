#pragma once

#include "raid_tactical_map.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

inline constexpr ContentRect kRaidTacticalChart{{144.0F, 114.0F}, {992.0F, 504.0F}};

struct RaidTacticalMapChart
{
    Vec2 worldSize;

    [[nodiscard]] Vec2 screenPoint(Vec2 worldPoint) const noexcept;
    [[nodiscard]] std::optional<Vec2> worldPoint(Vec2 screenPoint) const noexcept;
};

enum class RaidChalkSpace { Outdoor, HospitalInterior };

struct RaidChalkStroke
{
    std::vector<Vec2> points;
    double length{};
};

// Client annotations belong to a single live Raid and never modify map data.
class RaidChalkRoutes
{
public:
    static constexpr double kLengthLimit = 4000.0;
    static constexpr double kMinimumSegmentLength = 8.0;
    static constexpr std::size_t kMaximumStrokePoints =
        static_cast<std::size_t>(kLengthLimit / kMinimumSegmentLength) + 2;

    void setSpace(std::optional<RaidChalkSpace> space) noexcept;
    void beginStroke(Vec2 point) noexcept;
    void extendStroke(Vec2 point);
    void endStroke() noexcept;
    void undo(RaidChalkSpace space) noexcept;
    void clear(RaidChalkSpace space) noexcept;

    [[nodiscard]] bool drawing() const noexcept { return lastPoint_.has_value(); }
    [[nodiscard]] std::span<const RaidChalkStroke> strokes(RaidChalkSpace space) const noexcept;
    [[nodiscard]] double remainingLength() const noexcept;

private:
    std::array<std::vector<RaidChalkStroke>, 2> strokes_;
    std::optional<RaidChalkSpace> space_;
    std::optional<Vec2> lastPoint_;
    double usedLength_{};
    bool hasSegment_{};
};

enum class RaidTacticalMapPresentationMode
{
    FogOfWar,
    FullStaticMap
};

[[nodiscard]] bool tacticalMapCellVisible(
    const RaidTacticalMapState &map,
    int column,
    int row,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapPointVisible(
    const RaidTacticalMapState &map,
    Vec2 point,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapExtractionVisible(
    const RaidTacticalMapState &map,
    RaidMapExtractionKind kind,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapAdvancedResourceVisible(
    const RaidTacticalMapState &map,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapResourcePointVisible(
    const RaidTacticalMapState &map,
    const RaidTacticalResourcePoint &resourcePoint,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapSpecialLocationVisible(
    const RaidSpecialLocationMapState &location,
    RaidTacticalMapPresentationMode mode) noexcept;

[[nodiscard]] bool tacticalMapObjectiveVisible(
    const RaidTacticalMapState &map,
    const RaidTacticalObjective &objective,
    RaidTacticalMapPresentationMode mode) noexcept;

// The full-static-map debug mode never grants or emulates enemy intelligence.
[[nodiscard]] bool tacticalMapEnemyDeploymentVisible(
    const RaidTacticalMapState &map) noexcept;
