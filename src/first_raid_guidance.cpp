#include "first_raid_guidance.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "game_session.h"

namespace {
std::string seconds(float value) {
  const double finite = std::isfinite(value) ? value : 0.0;
  return std::to_string(static_cast<std::uint32_t>(std::clamp(
      std::ceil(finite), 0.0,
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()))));
}
} // namespace

FirstRaidGuidanceStage
firstRaidGuidanceStage(const ProfileState &profile) noexcept {
  if (profile.homeFounding.hintsDismissed)
    return FirstRaidGuidanceStage::Hidden;
  if (!profile.homeFounding.established)
    return FirstRaidGuidanceStage::Survey;
  if (profile.pendingRaid)
    return FirstRaidGuidanceStage::Raid;
  if (profile.lastRaidResult &&
      profile.lastRaidResult->outcome != RaidResultOutcome::AbnormalQuit &&
      profile.committedSettlements.contains(
          profile.lastRaidResult->settlementId))
    return FirstRaidGuidanceStage::Return;
  return FirstRaidGuidanceStage::Preparation;
}

std::vector<std::string>
projectFirstRaidReturnGuidance(const ProfileState &profile) {
  if (firstRaidGuidanceStage(profile) != FirstRaidGuidanceStage::Return)
    return {};
  const auto &result = *profile.lastRaidResult;
  if (result.outcome == RaidResultOutcome::Extracted)
    return {
        "EXTRACTED: ITEMS STAY IN THEIR EQUIPMENT AND EXACT GRID POSITIONS",
        "RETURNED ITEMS INCLUDE YOUR ORIGINAL GEAR; THEY ARE NOT NET NEW LOOT",
        "KEEP OR USE THEM; BASE SUPPLY AND WISHES REQUIRE YOUR CHOICE",
        "RETURN TO BASE, OPEN TAB, AND PREPARE YOUR NEXT RAID"};
  std::vector<std::string> lines{
      "BASE REMAINS; LOST CARRIED ITEMS DO NOT RETURN TO YOUR INVENTORY"};
  if (result.lostRaidRecordId &&
      profile.lostRaidRecords.contains(*result.lostRaidRecordId))
    lines.emplace_back(
        "USE THE EXISTING LOST-ITEM RECORD FOR NPC OR SELF RECOVERY");
  lines.emplace_back(
      "CHECK BASE SUPPLY AND CONDITIONAL RELIEF TO PREPARE AGAIN");
  lines.emplace_back("RETURN TO BASE AND CHOOSE YOUR NEXT LOADOUT");
  return lines;
}

std::vector<std::string> projectFirstRaidMapBriefing(const MapDefinition &map) {
  if (!map.highRisk.enabled)
    return {"NO HARD TIME LIMIT | NORMAL EXTRACTION: " +
                seconds(map.raidRules.extractionDurationSeconds) + " S",
            "M MAP SHOWS ONLY DISCOVERED OR BRIEFED EXTRACTION INFORMATION"};
  return {
      "REGULAR PHASE: " + seconds(map.highRisk.regularPhaseDurationSeconds) +
          " S | NORMAL EXTRACTION: " +
          seconds(map.raidRules.extractionDurationSeconds) + " S",
      "ZERO TIME STARTS ONGOING HIGH RISK; IT IS NOT A RAID FAILURE",
      "NORMAL CLOSES AT HIGH RISK; AN ACTIVE EXTRACT CAN FINISH",
      "ALTERNATIVES REQUIRE DISCOVERY OR INTELLIGENCE; CHECK M MAP"};
}

FirstRaidRiskGuidanceProjection projectFirstRaidRiskGuidance(
    const ProfileState &profile, const RaidSession &raid,
    const RaidTacticalMapState &tacticalMap, std::uint64_t carriedWeightGrams,
    std::uint64_t conditionalWeightLimitGrams) {
  FirstRaidRiskGuidanceProjection result;
  if (firstRaidGuidanceStage(profile) != FirstRaidGuidanceStage::Raid ||
      !raid.isActive())
    return result;
  result.visible = true;
  result.highRisk = raid.phase() == RaidPhase::HighRisk;
  result.normalOpen = raid.normalExtractionOpen();
  result.normalGrace = raid.normalExtractionGraceActive();
  result.regularSecondsRemaining = raid.raidTimeRemaining();
  result.extractionSecondsRemaining =
      raid.state() == RaidSessionState::Extracting
          ? std::max(0.0F,
                     raid.extractionDuration() - raid.extractionTimeElapsed())
          : 0.0F;
  result.signalKnown =
      tacticalMap.emergencyExtraction().has_value() &&
      tacticalMap.extractionVisible(RaidMapExtractionKind::EmergencySignal);
  result.signalOpen = result.signalKnown && raid.emergencyExtractionOpen();
  result.conditionalKnown = tacticalMap.conditionalExtraction().has_value() &&
                            tacticalMap.extractionVisible(
                                RaidMapExtractionKind::EmergencyConditional);
  result.conditionalOpen =
      result.conditionalKnown && raid.conditionalExtractionOpen();
  if (result.conditionalKnown) {
    result.carriedWeightGrams = carriedWeightGrams;
    result.conditionalWeightLimitGrams = conditionalWeightLimitGrams;
    result.conditionalEligible =
        conditionalWeightLimitGrams > 0U &&
        carriedWeightGrams <= conditionalWeightLimitGrams;
  }
  if (result.highRisk)
    result.lines.emplace_back(
        "HIGH RISK CONTINUES; ZERO TIME DOES NOT END THIS RAID");
  else if (tacticalMap.emergencyExtraction())
    result.lines.emplace_back(
        "REGULAR TIME LEFT: " + seconds(result.regularSecondsRemaining) + " S");
  else
    result.lines.emplace_back("NO HARD TIME LIMIT; CHOOSE WHEN TO EXTRACT");
  if (result.normalGrace)
    result.lines.emplace_back(
        "FINISH NORMAL EXTRACTION WITHOUT LEAVING; ITS GRACE WILL END");
  else if (result.normalOpen)
    result.lines.emplace_back(
        "NORMAL EXTRACTION: HOLD POSITION UNTIL THE BAR FILLS");
  else
    result.lines.emplace_back(
        "NORMAL EXTRACTION CLOSED; CHECK KNOWN ALTERNATIVES");
  if (result.signalOpen)
    result.lines.emplace_back(
        "SIGNAL EXTRACTION AVAILABLE; EXPECT ENEMY PRESSURE");
  if (result.conditionalOpen)
    result.lines.emplace_back(
        std::string{result.conditionalEligible
                        ? "LIGHT EXTRACTION: READY | CARRY "
                        : "LIGHT EXTRACTION: TOO HEAVY | CARRY "} +
        std::to_string(carriedWeightGrams) + " G / LIMIT " +
        std::to_string(conditionalWeightLimitGrams) + " G");
  if (!result.signalOpen && !result.conditionalOpen)
    result.lines.emplace_back(
        "M MAP SHOWS ONLY DISCOVERED OR BRIEFED EXTRACTION INFORMATION");
  return result;
}

FirstRaidRiskGuidanceProjection
projectFirstRaidRiskGuidance(const GameSession &session) {
  if (!session.alphaRaidActive())
    return {};
  return projectFirstRaidRiskGuidance(
      session.profile(), session.world().raidSession(),
      session.world().tacticalMap(), session.currentRaidCarriedWeightGrams(),
      session.conditionalExtractionWeightLimitGrams());
}
