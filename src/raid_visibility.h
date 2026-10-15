#pragma once

#include "definition_id.h"
#include "rect.h"
#include <array>
#include <cstdint>
#include <vector>

inline constexpr float kRaidSightGeometryEpsilon{0.001F};

enum class RaidLightEnvironment
{
    DayOutdoor,
    NightOutdoor,
    WeakIndoor
};

struct RaidObservationKey
{
    RaidSpaceDefinitionId spaceId;
    Vec2 observer{};
    Vec2 direction{1, 0};
    float angleDegrees{360};
    float maximumDistance{}; // Zero retains full-space observation outside this
                             // slice.
    RaidLightEnvironment environment{RaidLightEnvironment::WeakIndoor};
    std::uint64_t geometryRevision{1};
    std::uint64_t lightAssetId{}, lightBatteryId{};
    float lightAngleDegrees{}, lightDistance{};

    friend bool operator==(const RaidObservationKey &a,
                           const RaidObservationKey &b)
    {
        return a.spaceId == b.spaceId && a.observer.x == b.observer.x &&
               a.observer.y == b.observer.y && a.direction.x == b.direction.x &&
               a.direction.y == b.direction.y &&
               a.angleDegrees == b.angleDegrees &&
               a.maximumDistance == b.maximumDistance &&
               a.environment == b.environment &&
               a.geometryRevision == b.geometryRevision && a.lightAssetId == b.lightAssetId && a.lightBatteryId == b.lightBatteryId &&
               a.lightAngleDegrees == b.lightAngleDegrees && a.lightDistance == b.lightDistance;
    }
};

struct RaidSightParameters
{
    float angleDegrees{}, maximumDistance{};
};
[[nodiscard]] RaidSightParameters
    raidSightParameters(RaidLightEnvironment) noexcept;

// Owned, transient observation data. No actor/asset pointers or save identity.
// The simulation builds it from its current space; clients never grant access.
struct RaidVisibilityProjection
{
    RaidSpaceDefinitionId spaceId;
    Vec2 observer{};
    Vec2 worldSize{};
    std::uint64_t geometryRevision{};
    std::vector<Vec2> boundary;
    bool valid{};
    Vec2 direction{1, 0};
    float angleDegrees{360};
    float maximumDistance{};
    RaidLightEnvironment environment{RaidLightEnvironment::WeakIndoor};
    std::uint64_t lightAssetId{}, lightBatteryId{};
    float lightAngleDegrees{}, lightDistance{};

    [[nodiscard]] RaidObservationKey key() const;
    // Cache comparison only. Commands must use matches(), which validates
    // geometry.
    [[nodiscard]] bool
    hasSameParameters(const RaidObservationKey &) const noexcept;
    [[nodiscard]] bool matches(const RaidObservationKey &) const noexcept;

    [[nodiscard]] bool validGeometry() const noexcept;

    [[nodiscard]] bool matches(const RaidSpaceDefinitionId &,
                               Vec2) const noexcept;
    [[nodiscard]] bool pointVisible(Vec2) const noexcept;
    [[nodiscard]] bool boundsVisible(Rect) const;
};

// Owned immutable geometry for repeated queries of one observation. Building
// this snapshot validates the public projection in full; changing the source
// afterwards cannot silently change or grant access through this query.
// Commands continue to validate their current public context with matches().
class RaidVisibilityBoundsQuery
{
  public:
    RaidVisibilityBoundsQuery() = default;
    explicit RaidVisibilityBoundsQuery(const RaidVisibilityProjection &);
    [[nodiscard]] bool boundsVisible(Rect) const noexcept;

  private:
    struct Edge
    {
        Vec2 origin, delta;
        float absoluteX{}, absoluteY{};
        [[nodiscard]] bool overlaps(Vec2 center, float hx, float hy,
                                    float sign) const noexcept;
    };
    struct Triangle
    {
        Vec2 minimum, maximum;
        float sign{};
        std::array<Edge, 3> edges;
    };
    bool valid_{}, bounded_{};
    Vec2 observer_{}, leftDirection_{}, rightDirection_{};
    float maximumDistanceSquared_{};
    std::vector<Triangle> triangles_;
};

[[nodiscard]] bool raidSightSegmentIntersectsRectInterior(Vec2, Vec2,
                                                          Rect) noexcept;
// Clip one fan triangle to an object rectangle; used by observation queries.
[[nodiscard]] std::vector<Vec2> clipRaidVisibilityTriangle(Vec2, Vec2, Vec2,
                                                           Rect);
