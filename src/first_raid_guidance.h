#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "profile_state.h"
#include "raid_session.h"
#include "raid_tactical_map.h"

class GameSession;

enum class FirstRaidGuidanceStage { Hidden, Survey, Preparation, Raid, Return };

[[nodiscard]] FirstRaidGuidanceStage
firstRaidGuidanceStage(const ProfileState &profile) noexcept;

// Stable English source fragments; the client uses its ordinary localization.
// No selection, mutation, time progression, rewards, or extra save state.
[[nodiscard]] std::vector<std::string>
projectFirstRaidReturnGuidance(const ProfileState &profile);
[[nodiscard]] std::vector<std::string>
projectFirstRaidMapBriefing(const MapDefinition &map);

struct FirstRaidRiskGuidanceProjection {
  bool visible{};
  bool highRisk{};
  bool normalOpen{};
  bool normalGrace{};
  bool signalKnown{};
  bool signalOpen{};
  bool conditionalKnown{};
  bool conditionalOpen{};
  bool conditionalEligible{};
  float regularSecondsRemaining{};
  float extractionSecondsRemaining{};
  std::uint64_t carriedWeightGrams{};
  std::uint64_t conditionalWeightLimitGrams{};
  std::vector<std::string> lines;
};

[[nodiscard]] FirstRaidRiskGuidanceProjection projectFirstRaidRiskGuidance(
    const ProfileState &profile, const RaidSession &raid,
    const RaidTacticalMapState &tacticalMap, std::uint64_t carriedWeightGrams,
    std::uint64_t conditionalWeightLimitGrams);
[[nodiscard]] FirstRaidRiskGuidanceProjection
projectFirstRaidRiskGuidance(const GameSession &session);
