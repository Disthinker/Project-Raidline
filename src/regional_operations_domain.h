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

struct EstablishRegionalOutpostCommand
{
    RegionalOutpostDefinitionId definitionId;
};

struct RegionalOutpostPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RegionalOutpostDefinitionId definitionId;
    bool unlocked{};
    bool established{};
    bool online{};
    std::uint32_t requiredStaff{};
    std::uint32_t assignedStaff{};
};

struct RegionalOutpostReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RegionalOutpostDefinitionId definitionId;
    bool established{};
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

[[nodiscard]] RegionalOutpostPlan queryEstablishRegionalOutpost(
    const ProfileState &profile,
    const ContentRegistry &content,
    const EstablishRegionalOutpostCommand &command) noexcept;

[[nodiscard]] RegionalOutpostReceipt executeEstablishRegionalOutpost(
    ProfileState &profile,
    const ContentRegistry &content,
    const EstablishRegionalOutpostCommand &command,
    const CommandContext &context);
