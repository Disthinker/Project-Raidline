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
        profile.basePopulation.injuredResidents,
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
        command.ordinaryResidentCount == 0U ||
        command.injuredResidentCount > command.ordinaryResidentCount)
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
    if (profile.basePopulation.injuredResidents >
            kMaximumOrdinaryResidents ||
        command.injuredResidentCount >
            kMaximumOrdinaryResidents -
                profile.basePopulation.injuredResidents)
    {
        return failurePlan(
            profile,
            RaidRescueError::PopulationOverflow,
            "injured resident count cannot advance");
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
        profile.basePopulation.injuredResidents +
            command.injuredResidentCount,
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
        definition->ordinaryResidentCount != command.ordinaryResidentCount ||
        definition->injuredResidentCount != command.injuredResidentCount)
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
                0U,
                profile.basePopulation.ordinaryResidents,
                profile.basePopulation.injuredResidents,
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
    candidate.basePopulation.injuredResidents =
        plan.injuredResidentsAfter;
    const std::size_t professionIndex = static_cast<std::size_t>(
        definition->profession);
    if (professionIndex >= kBaseResidentProfessionCount)
    {
        return {false, false, RaidRescueError::InvalidCommand,
                "rescue profession is invalid", profile.revision};
    }
    candidate.basePopulation.professionResidents[professionIndex] +=
        command.ordinaryResidentCount;
    candidate.basePopulation.injuredByProfession[professionIndex] +=
        command.injuredResidentCount;
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
            command.ordinaryResidentCount,
            command.injuredResidentCount,
            plan.residentsAfter,
            plan.injuredResidentsAfter,
            plan.bedShortfallAfter, plan.dailyRationsAfter};
}
