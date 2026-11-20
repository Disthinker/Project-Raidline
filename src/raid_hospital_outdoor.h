#pragma once

#include "raid_map_generation.h"

struct HospitalOutdoorPlan
{
    Vec2 playerApproach;
    std::vector<std::uint16_t> owners;
    std::vector<std::uint8_t> roadRanks;
    std::vector<RaidLandmarkPlacementSnapshot> landmarks;
};

bool usesHospitalOutdoorPlan(const MapDefinition &map) noexcept;
HospitalOutdoorPlan planHospitalOutdoor(const MapDefinition &map,
                                        const std::vector<RaidDistrictArchetypeDefinition> &districts,
                                        std::uint64_t seed, std::uint32_t attempt);

enum class HospitalParcelFamily
{
    Shopfront,
    Courtyard,
    ServiceShed
};
struct HospitalRoadsideParcel
{
    HospitalParcelFamily family{};
    ContentRect bounds;
    Vec2 gate;
    std::uint8_t quarterTurns{};
    std::array<RaidOutdoorPropSnapshot, 3> parts;
};

std::vector<HospitalRoadsideParcel> hospitalParcelCandidates(const MapDefinition &map,
                                                             const std::vector<std::uint8_t> &roadRanks,
                                                             std::uint64_t seed);
std::vector<HospitalRoadsideParcel> inspectHospitalParcels(const MapDefinition &map,
                                                           const RaidGeneratedMapLayout &layout);

struct HospitalOutdoorRouteReport
{
    std::vector<Vec2> mainRoute;
    std::vector<Vec2> bypassRoute;
    bool anchorsReachable{};
    bool passable() const noexcept
    {
        return anchorsReachable && !mainRoute.empty() && !bypassRoute.empty();
    }
};
HospitalOutdoorRouteReport inspectHospitalOutdoorRoutes(const MapDefinition &map,
                                                        const RaidGeneratedMapLayout &layout);

int hospitalEnvironmentalField(std::uint64_t seed, int column, int row) noexcept;
