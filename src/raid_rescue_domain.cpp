#include "raid_rescue_domain.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base_population_domain.h"

namespace
{
const RaidRescueDefinition *findRescueDefinition(
    const ContentRegistry &content,
    const RescueDefinitionId &id) noexcept
{
    for (const MapDefinition &map : content.maps())
    {
        if (map.rescue.has_value() && map.rescue->id == id)
        {
            return &*map.rescue;
        }
    }
    return nullptr;
}

OrdinarySurvivorAdmissionPlan failurePlan(
    const ProfileState &profile,
    RaidRescueError error,
    std::string message)
{
    return OrdinarySurvivorAdmissionPlan{
        false,
        false,
        error,
        std::move(message),
        profile.revision,
        profile.basePopulation.ordinaryResidents,
        profile.basePopulation.ordinaryResidents,
        profile.basePopulation.bedCapacity,
        profile.basePopulation.ordinaryResidents >
                profile.basePopulation.bedCapacity
            ? profile.basePopulation.ordinaryResidents -
                  profile.basePopulation.bedCapacity
            : 0U,
        profile.basePopulation.ordinaryResidents};
}
}

OrdinarySurvivorAdmissionPlan queryOrdinarySurvivorAdmission(
    const ProfileState &profile,
    const OrdinarySurvivorAdmissionCommand &command)
{
    if (!command.rescueDefinitionId.valid() ||
        command.ordinaryResidentCount == 0U)
    {
        return failurePlan(
            profile,
            RaidRescueError::InvalidCommand,
            "ordinary survivor admission command is invalid");
    }
    if (profile.committedRescues.contains(command.rescueDefinitionId))
    {
        OrdinarySurvivorAdmissionPlan plan = failurePlan(
            profile,
            RaidRescueError::None,
            {});
        plan.canCommit = true;
        plan.alreadyCommitted = true;
        return plan;
    }
    if (profile.basePopulation.ordinaryResidents >
            kMaximumOrdinaryResidents ||
        command.ordinaryResidentCount >
            kMaximumOrdinaryResidents -
                profile.basePopulation.ordinaryResidents)
    {
        return failurePlan(
            profile,
            RaidRescueError::PopulationOverflow,
            "ordinary resident count cannot advance");
    }
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failurePlan(
            profile,
            RaidRescueError::RevisionOverflow,
            "profile revision cannot advance");
    }

    const std::uint32_t after =
        profile.basePopulation.ordinaryResidents +
        command.ordinaryResidentCount;
    return OrdinarySurvivorAdmissionPlan{
        true,
        false,
        RaidRescueError::None,
        {},
        profile.revision,
        profile.basePopulation.ordinaryResidents,
        after,
        profile.basePopulation.bedCapacity,
        after > profile.basePopulation.bedCapacity
            ? after - profile.basePopulation.bedCapacity
            : 0U,
        after};
}

OrdinarySurvivorAdmissionReceipt executeOrdinarySurvivorAdmission(
    ProfileState &profile,
    const ContentRegistry &content,
    const OrdinarySurvivorAdmissionCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return {false, false, RaidRescueError::InvalidCommand,
                "rescue transaction ID is empty", profile.revision};
    }
    const RaidRescueDefinition *definition = findRescueDefinition(
        content, command.rescueDefinitionId);
    if (definition == nullptr ||
        definition->subjectKind != RaidRescueSubjectKind::OrdinaryResidents ||
        definition->ordinaryResidentCount != command.ordinaryResidentCount)
    {
        return {false, false, RaidRescueError::InvalidCommand,
                "rescue definition does not match published content",
                profile.revision};
    }
    if (profile.committedTransactions.contains(context.transactionId) ||
        profile.committedRescues.contains(command.rescueDefinitionId))
    {
        const auto plan = queryOrdinarySurvivorAdmission(profile, command);
        return {true, true, RaidRescueError::None, {}, profile.revision, 0U,
                profile.basePopulation.ordinaryResidents,
                plan.bedShortfallAfter,
                plan.dailyRationsAfter};
    }
    if (context.expectedRevision != profile.revision)
    {
        return {false, false, RaidRescueError::StaleRevision,
                "profile revision is stale", profile.revision};
    }

    const OrdinarySurvivorAdmissionPlan plan =
        queryOrdinarySurvivorAdmission(profile, command);
    if (!plan.canCommit)
    {
        return {false, false, plan.error, plan.message, profile.revision};
    }

    ProfileState candidate = profile;
    candidate.basePopulation.ordinaryResidents = plan.residentsAfter;
    candidate.committedRescues.insert(command.rescueDefinitionId);
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const ProfileValidationResult validation =
        validateProfileState(candidate, content);
    if (!validation.valid)
    {
        return {false, false, RaidRescueError::InvalidProfile,
                validation.message, profile.revision};
    }
    profile = std::move(candidate);
    return {true, false, RaidRescueError::None, {}, profile.revision,
            command.ordinaryResidentCount, plan.residentsAfter,
            plan.bedShortfallAfter, plan.dailyRationsAfter};
}
