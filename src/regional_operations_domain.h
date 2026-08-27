#pragma once

#include <string>
#include <vector>

#include "inventory_domain.h"

struct RegionalRoutePlan
{
    bool reachable{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    MapDefinitionId mapDefinitionId;
    RegionNodeDefinitionId destinationNodeId;
    std::uint32_t travelMinutes{};
    std::vector<RegionRouteDefinitionId> routeIds;
    bool usesOnlineOutpost{};
};

[[nodiscard]] std::uint32_t assignedRegionalOutpostStaff(
    const RegionalOutpostState &state) noexcept;

[[nodiscard]] bool regionalOutpostOnline(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RegionalOutpostDefinitionId &outpostId) noexcept;

[[nodiscard]] RegionalRoutePlan queryRegionalRoute(
    const ProfileState &profile,
    const ContentRegistry &content,
    const MapDefinitionId &mapDefinitionId) noexcept;
