#pragma once

#include <string>
#include <string_view>

#include "profile_state.h"

inline constexpr std::string_view kRaidCredentialExtractionRulesVersion{
    "credential-extraction-1"};

// A completed observation produced by the live world after its uninterrupted
// investigation interaction. The service consumes this fact; UI never grants it.
struct RaidCredentialInvestigationFact
{
    std::string investigationPointId;
    RaidSpaceDefinitionId spaceId;
    Vec2 playerPosition{};
};

[[nodiscard]] ProfileValidationResult queryRaidCredentialInvestigation(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidCredentialInvestigationFact &fact);

// Changes the in-memory pending Raid only. Settlement owns permanent permission
// registration, and SaveRepository remains the service's atomic boundary.
[[nodiscard]] ProfileValidationResult recordRaidCredentialInvestigation(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidCredentialInvestigationFact &fact);
