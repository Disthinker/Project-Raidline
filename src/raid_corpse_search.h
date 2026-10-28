#pragma once

#include "profile_state.h"

inline constexpr std::string_view kRaidCorpseRulesVersion{"content-frontier-corpse-search-30"};
inline constexpr std::string_view kRaidRunnerRulesVersion{"content-frontier-runner-infected-31"};

[[nodiscard]] inline constexpr bool supportsRaidCorpseRules(std::string_view version) noexcept
{
    return version == kRaidCorpseRulesVersion || version == kRaidRunnerRulesVersion;
}

struct RaidCorpseDeathFact
{
    std::string sourceEnemyId;
    RaidSpaceDefinitionId spaceId;
    Vec2 position{};
};

// Freeze existing ordinary rolls, atomically. Never roll new rewards.
[[nodiscard]] ProfileValidationResult freezeRaidCorpseLoot(ProfileState &, const ContentRegistry &);
[[nodiscard]] ProfileValidationResult validateRaidCorpseBindings(const ProfileState &, const ContentRegistry &);
[[nodiscard]] bool revealRaidCorpse(ProfileState &, const ContentRegistry &, const RaidCorpseDeathFact &);
// Simulation-only: the deployed Profile has already passed full validation.
// Recheck corpse/loot participants before committing the one-time reveal.
[[nodiscard]] bool revealRaidCorpseInSimulation(ProfileState &, const ContentRegistry &, const RaidCorpseDeathFact &) noexcept;
[[nodiscard]] bool raidCorpseAccessible(const ProfileState &, AssetInstanceId, const RaidSpaceDefinitionId &, Vec2);
