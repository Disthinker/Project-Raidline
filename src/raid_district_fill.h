#pragma once

#include "raid_map_generation.h"

// Immutable fill policy, separate from district allocation, collision and
// random streams. Defaults preserve the accepted Frontier v4 draw order.
struct RaidDistrictFill
{
    RaidTerrainKind terrain{RaidTerrainKind::Dirt};
    bool largeBuildings{};
    RaidOutdoorPropKind building{RaidOutdoorPropKind::Warehouse};
    std::uint32_t minimumWidthCells{10U};
    std::uint32_t widthVariation{7U};
    std::uint32_t minimumHeightCells{6U};
    std::uint32_t heightVariation{5U};
    std::uint32_t equipmentPeriod{};

    [[nodiscard]] constexpr RaidOutdoorPropKind smallBlocker(
        std::size_t ordinal) const noexcept
    {
        return equipmentPeriod != 0U && ordinal % equipmentPeriod == 0U
            ? RaidOutdoorPropKind::EngineeringEquipment
            : RaidOutdoorPropKind::Container;
    }
};

[[nodiscard]] constexpr RaidDistrictFill raidDistrictFill(
    RaidDistrictKind kind) noexcept
{
    switch (kind)
    {
    case RaidDistrictKind::Industrial:
        return {RaidTerrainKind::Concrete, true, RaidOutdoorPropKind::Factory,
                12U, 7U, 7U, 5U, 3U};
    case RaidDistrictKind::Logistics:
        return {RaidTerrainKind::Concrete, true};
    case RaidDistrictKind::Highway:
        return {RaidTerrainKind::Asphalt};
    case RaidDistrictKind::Greenbelt:
        return {RaidTerrainKind::Grass};
    case RaidDistrictKind::RoadsideService:
        return {RaidTerrainKind::Concrete, false,
                RaidOutdoorPropKind::Warehouse, 10U, 7U, 6U, 5U, 2U};
    case RaidDistrictKind::OpenGround:
        return {};
    }
    return {};
}
