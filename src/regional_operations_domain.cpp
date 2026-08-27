#include "regional_operations_domain.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>

namespace
{
constexpr std::uint64_t kUnreachable =
    std::numeric_limits<std::uint64_t>::max();

RegionalRoutePlan failure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message,
    MapDefinitionId mapDefinitionId = {})
{
    return {false, error, std::move(message), profile.revision,
            std::move(mapDefinitionId)};
}

RegionalOutpostPlan outpostFailure(
    const ProfileState &profile,
    DomainErrorCode error,
    std::string message,
    RegionalOutpostDefinitionId definitionId = {})
{
    return {false, error, std::move(message), profile.revision,
            std::move(definitionId)};
}
}

std::uint32_t assignedRegionalOutpostStaff(
    const RegionalOutpostState &state) noexcept
{
    std::uint64_t total{};
    for (std::uint32_t count : state.assignedStaff)
    {
        total += count;
    }
    return total > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(total);
}

bool regionalOutpostOnline(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RegionalOutpostDefinitionId &outpostId) noexcept
{
    try
    {
        const RegionalOutpostDefinition &definition =
            content.regionalOutpost(outpostId);
        const auto state = profile.regionalOperations.outposts.find(outpostId);
        return state != profile.regionalOperations.outposts.end() &&
            state->second.unlocked && state->second.established &&
            !state->second.disrupted &&
            assignedRegionalOutpostStaff(state->second) ==
                definition.requiredStaff;
    }
    catch (...)
    {
        return false;
    }
}

RegionalRoutePlan queryRegionalRoute(
    const ProfileState &profile,
    const ContentRegistry &content,
    const MapDefinitionId &mapDefinitionId) noexcept
{
    try
    {
        static_cast<void>(content.map(mapDefinitionId));
        const RegionalOperationsDefinition &regional =
            content.regionalOperations();
        const auto destination = std::find_if(
            regional.nodes.begin(), regional.nodes.end(),
            [&](const RegionNodeDefinition &node)
            {
                return node.mapDefinitionId ==
                    std::optional<MapDefinitionId>{mapDefinitionId};
            });
        if (destination == regional.nodes.end())
        {
            return failure(
                profile, DomainErrorCode::InvalidProfile,
                "Raid map has no regional destination", mapDefinitionId);
        }
        static_cast<void>(content.regionNode(
            profile.regionalOperations.activeBaseNodeId));

        std::map<RegionNodeDefinitionId, std::uint64_t> distance;
        std::map<RegionNodeDefinitionId, bool> visited;
        std::map<RegionNodeDefinitionId, RegionNodeDefinitionId> previousNode;
        std::map<RegionNodeDefinitionId, RegionRouteDefinitionId> previousRoute;
        for (const RegionNodeDefinition &node : regional.nodes)
        {
            distance.emplace(node.id, kUnreachable);
            visited.emplace(node.id, false);
        }
        distance.at(profile.regionalOperations.activeBaseNodeId) = 0U;

        for (std::size_t pass{}; pass < regional.nodes.size(); ++pass)
        {
            std::optional<RegionNodeDefinitionId> current;
            for (const auto &[nodeId, candidateDistance] : distance)
            {
                if (visited.at(nodeId) || candidateDistance == kUnreachable)
                {
                    continue;
                }
                if (!current.has_value() ||
                    candidateDistance < distance.at(*current) ||
                    (candidateDistance == distance.at(*current) &&
                     nodeId < *current))
                {
                    current = nodeId;
                }
            }
            if (!current.has_value())
            {
                break;
            }
            visited.at(*current) = true;
            if (*current == destination->id)
            {
                break;
            }
            for (const RegionRouteDefinition &route : regional.routes)
            {
                if (route.requiredOnlineOutpostId.has_value() &&
                    !regionalOutpostOnline(
                        profile, content,
                        *route.requiredOnlineOutpostId))
                {
                    continue;
                }
                std::optional<RegionNodeDefinitionId> neighbor;
                if (route.from == *current)
                {
                    neighbor = route.to;
                }
                else if (route.to == *current)
                {
                    neighbor = route.from;
                }
                if (!neighbor.has_value() || visited.at(*neighbor))
                {
                    continue;
                }
                const std::uint64_t candidate =
                    distance.at(*current) + route.travelMinutes;
                const auto oldRoute = previousRoute.find(*neighbor);
                if (candidate < distance.at(*neighbor) ||
                    (candidate == distance.at(*neighbor) &&
                     (oldRoute == previousRoute.end() ||
                      route.id < oldRoute->second)))
                {
                    distance.at(*neighbor) = candidate;
                    previousNode[*neighbor] = *current;
                    previousRoute[*neighbor] = route.id;
                }
            }
        }

        if (distance.at(destination->id) == kUnreachable ||
            distance.at(destination->id) >
                std::numeric_limits<std::uint32_t>::max())
        {
            return failure(
                profile, DomainErrorCode::IllegalDestination,
                "Raid destination has no active regional route",
                mapDefinitionId);
        }
        std::vector<RegionRouteDefinitionId> routeIds;
        RegionNodeDefinitionId cursor = destination->id;
        bool usesOnlineOutpost{};
        while (cursor != profile.regionalOperations.activeBaseNodeId)
        {
            const auto route = previousRoute.find(cursor);
            const auto node = previousNode.find(cursor);
            if (route == previousRoute.end() || node == previousNode.end())
            {
                return failure(
                    profile, DomainErrorCode::InvalidProfile,
                    "regional route reconstruction failed", mapDefinitionId);
            }
            routeIds.push_back(route->second);
            usesOnlineOutpost = usesOnlineOutpost ||
                content.regionRoute(route->second)
                    .requiredOnlineOutpostId.has_value();
            cursor = node->second;
        }
        std::reverse(routeIds.begin(), routeIds.end());
        return {true,
                DomainErrorCode::None,
                {},
                profile.revision,
                mapDefinitionId,
                destination->id,
                static_cast<std::uint32_t>(distance.at(destination->id)),
                std::move(routeIds),
                usesOnlineOutpost};
    }
    catch (const std::exception &error)
    {
        return failure(
            profile, DomainErrorCode::InvalidProfile,
            error.what(), mapDefinitionId);
    }
}

RegionalOutpostPlan queryEstablishRegionalOutpost(
    const ProfileState &profile,
    const ContentRegistry &content,
    const EstablishRegionalOutpostCommand &command) noexcept
{
    try
    {
        const RegionalOutpostDefinition &definition =
            content.regionalOutpost(command.definitionId);
        const auto found = profile.regionalOperations.outposts.find(
            command.definitionId);
        if (found == profile.regionalOperations.outposts.end())
        {
            return outpostFailure(
                profile, DomainErrorCode::InvalidProfile,
                "regional outpost state is missing", command.definitionId);
        }
        const RegionalOutpostState &state = found->second;
        if (profile.pendingRaid.has_value())
        {
            return outpostFailure(
                profile, DomainErrorCode::IllegalDestination,
                "regional operations are unavailable during a Raid",
                command.definitionId);
        }
        if (!state.unlocked)
        {
            return outpostFailure(
                profile, DomainErrorCode::IllegalDestination,
                "regional outpost location is locked",
                command.definitionId);
        }
        if (state.established)
        {
            return outpostFailure(
                profile, DomainErrorCode::IllegalDestination,
                "regional outpost is already established",
                command.definitionId);
        }
        const std::size_t established = static_cast<std::size_t>(
            std::count_if(
                profile.regionalOperations.outposts.begin(),
                profile.regionalOperations.outposts.end(),
                [](const auto &entry)
                { return entry.second.established; }));
        if (established >=
            content.regionalOperations().maximumEstablishedOutposts)
        {
            return outpostFailure(
                profile, DomainErrorCode::Capacity,
                "regional outpost capacity is full",
                command.definitionId);
        }
        return {true,
                DomainErrorCode::None,
                {},
                profile.revision,
                command.definitionId,
                state.unlocked,
                state.established,
                regionalOutpostOnline(profile, content, command.definitionId),
                definition.requiredStaff,
                assignedRegionalOutpostStaff(state)};
    }
    catch (const std::exception &error)
    {
        return outpostFailure(
            profile, DomainErrorCode::IllegalDestination,
            error.what(), command.definitionId);
    }
}

RegionalOutpostReceipt executeEstablishRegionalOutpost(
    ProfileState &profile,
    const ContentRegistry &content,
    const EstablishRegionalOutpostCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, DomainErrorCode::InvalidTransaction,
                "transaction ID is empty", profile.revision,
                command.definitionId, false};
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        const auto found = profile.regionalOperations.outposts.find(
            command.definitionId);
        return {true, true, DomainErrorCode::None, {}, profile.revision,
                command.definitionId,
                found != profile.regionalOperations.outposts.end() &&
                    found->second.established};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, DomainErrorCode::StaleRevision,
                "profile revision is stale", profile.revision,
                command.definitionId, false};
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return {false, false, DomainErrorCode::RevisionOverflow,
                "profile revision cannot advance", profile.revision,
                command.definitionId, false};
    }
    const RegionalOutpostPlan plan = queryEstablishRegionalOutpost(
        profile, content, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision,
                command.definitionId, false};
    }
    ProfileState candidate = profile;
    candidate.regionalOperations.outposts.at(command.definitionId)
        .established = true;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                validation.message, profile.revision,
                command.definitionId, false};
    }
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision,
            command.definitionId, true};
}
