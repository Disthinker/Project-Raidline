#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "raid_map_generation.h"

enum class HomeRegionDistrictKind : std::uint8_t
{
    Base,
    Industrial,
    Logistics,
    Highway,
    OpenGround,
    Greenbelt,
    RoadsideService
};

struct HomeRegionDistrictSnapshot
{
    std::uint16_t instanceId{};
    HomeRegionDistrictKind kind{HomeRegionDistrictKind::OpenGround};
    ContentRect bounds;
    Vec2 labelPosition{};

    friend bool operator==(
        const HomeRegionDistrictSnapshot &left,
        const HomeRegionDistrictSnapshot &right)
    {
        return left.instanceId == right.instanceId &&
            left.kind == right.kind && left.bounds == right.bounds &&
            left.labelPosition.x == right.labelPosition.x &&
            left.labelPosition.y == right.labelPosition.y;
    }
};

struct HomeRegionLayout
{
    std::uint32_t layoutVersion{1U};
    std::string siteDefinitionId;
    Vec2 worldSize{12800.0F, 7200.0F};
    std::uint32_t columns{160U};
    std::uint32_t rows{90U};
    std::uint32_t chunkSizeCells{16U};
    ContentRect baseParcel;
    std::vector<HomeRegionDistrictSnapshot> districts;
    std::vector<RaidTerrainSpan> terrainSpans;
    std::vector<RaidOutdoorRoadCell> roadCells;
    std::vector<RaidOutdoorPropSnapshot> props;
    std::vector<ContentRect> movementBlockers;
    std::uint64_t layoutHash{};

};

[[nodiscard]] HomeRegionLayout generateHomeRegionLayout(
    std::string_view siteDefinitionId);

[[nodiscard]] std::uint64_t homeRegionLayoutHash(
    const HomeRegionLayout &layout) noexcept;

[[nodiscard]] const char *homeRegionDistrictName(
    HomeRegionDistrictKind kind) noexcept;
