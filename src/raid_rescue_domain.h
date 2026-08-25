#pragma once

#include <cstdint>
#include <string>

#include "inventory_domain.h"

enum class RaidRescueError
{
    None,
    InvalidCommand,
    StaleRevision,
    PopulationOverflow,
    RevisionOverflow,
    InvalidProfile
};

struct OrdinarySurvivorAdmissionCommand
{
    RescueDefinitionId rescueDefinitionId;
    std::uint32_t ordinaryResidentCount{};
};

struct OrdinarySurvivorAdmissionPlan
{
    bool canCommit{};
    bool alreadyCommitted{};
    RaidRescueError error{RaidRescueError::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t residentsBefore{};
    std::uint32_t residentsAfter{};
    std::uint32_t bedCapacity{};
    std::uint32_t bedShortfallAfter{};
    std::uint32_t dailyRationsAfter{};
};

struct OrdinarySurvivorAdmissionReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    RaidRescueError error{RaidRescueError::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t admittedResidents{};
    std::uint32_t residentsAfter{};
    std::uint32_t bedShortfallAfter{};
    std::uint32_t dailyRationsAfter{};
};

[[nodiscard]] OrdinarySurvivorAdmissionPlan
queryOrdinarySurvivorAdmission(
    const ProfileState &profile,
    const OrdinarySurvivorAdmissionCommand &command);

[[nodiscard]] OrdinarySurvivorAdmissionReceipt
executeOrdinarySurvivorAdmission(
    ProfileState &profile,
    const ContentRegistry &content,
    const OrdinarySurvivorAdmissionCommand &command,
    const CommandContext &context);
