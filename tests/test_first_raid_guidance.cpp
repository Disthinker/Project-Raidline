#include "first_raid_guidance.h"

#include <algorithm>
#include <gtest/gtest.h>

#include "game_session.h"
#include "home_founding_domain.h"
#include "save_repository.h"

namespace {
ProfileState preparing() {
  auto profile =
      makeNewAlphaProfile("first-guidance", publishedContentRegistry());
  profile.homeFounding.hintsDismissed = false;
  return profile;
}
ProfileState pending() {
  auto profile = preparing();
  profile.pendingRaid = PendingRaidSnapshot{};
  profile.pendingRaid->raidId = "raid-first";
  profile.pendingRaid->settlementId = "settlement-first";
  return profile;
}
RaidSession activeRaid(float regularSeconds = 1200.0F) {
  RaidSession raid{RaidSessionConfig{
      0.0F, 3.0F, false,
      HighRiskRaidSessionConfig{true, regularSeconds, 8.0F, 4.0F}}};
  EXPECT_TRUE(raid.start());
  return raid;
}
RaidTacticalMapState tactical(bool known) {
  RaidTacticalMapState map;
  RaidIntelligenceLoadout intel;
  intel.set(RaidIntelligenceCategory::Transport, known);
  map.configure({1280, 720}, intel, {{20, 20}, {30, 30}},
                ContentRect{{700, 100}, {30, 30}},
                ContentRect{{1000, 400}, {30, 30}}, std::nullopt, {{543, 321}});
  return map;
}
bool contains(const std::vector<std::string> &lines, std::string_view text) {
  return std::any_of(lines.begin(), lines.end(), [&](const auto &line) {
    return line.find(text) != std::string::npos;
  });
}
} // namespace

TEST(FirstRaidGuidanceTest, LegacySaveIsHiddenAndDoesNotReplay) {
  const auto &content = publishedContentRegistry();
  auto legacy = makeNewAlphaProfile("old-first-guidance", content);
  auto read = deserializeProfileEnvelope(
      serializeProfileEnvelope(legacy, content.contentVersion(), 44), content);
  ASSERT_TRUE(read.profile) << read.message;
  EXPECT_EQ(firstRaidGuidanceStage(*read.profile),
            FirstRaidGuidanceStage::Hidden);
  EXPECT_TRUE(projectFirstRaidReturnGuidance(*read.profile).empty());
}

TEST(FirstRaidGuidanceTest, StagesRequireHomeAndCommittedNonAbnormalResult) {
  auto profile =
      makeNewHomeProfile("survey-first-guidance", publishedContentRegistry());
  EXPECT_EQ(firstRaidGuidanceStage(profile), FirstRaidGuidanceStage::Survey);
  profile = preparing();
  EXPECT_EQ(firstRaidGuidanceStage(profile),
            FirstRaidGuidanceStage::Preparation);
  profile.lastRaidResult = LastRaidResult{};
  profile.lastRaidResult->settlementId = "fake";
  EXPECT_EQ(firstRaidGuidanceStage(profile),
            FirstRaidGuidanceStage::Preparation);
  EXPECT_TRUE(projectFirstRaidReturnGuidance(profile).empty());
  profile.committedSettlements.insert("fake");
  EXPECT_EQ(firstRaidGuidanceStage(profile), FirstRaidGuidanceStage::Return);
  profile.lastRaidResult->outcome = RaidResultOutcome::AbnormalQuit;
  EXPECT_EQ(firstRaidGuidanceStage(profile),
            FirstRaidGuidanceStage::Preparation);
  profile.pendingRaid = PendingRaidSnapshot{};
  EXPECT_EQ(firstRaidGuidanceStage(profile), FirstRaidGuidanceStage::Raid);
  profile.homeFounding.hintsDismissed = true;
  EXPECT_EQ(firstRaidGuidanceStage(profile), FirstRaidGuidanceStage::Hidden);
}

TEST(FirstRaidGuidanceTest,
     SuccessExplainsExactLocationsAndNoAutomaticContribution) {
  auto profile = preparing();
  profile.lastRaidResult = LastRaidResult{};
  profile.lastRaidResult->settlementId = "first-ok";
  profile.lastRaidResult->outcome = RaidResultOutcome::Extracted;
  profile.committedSettlements.insert("first-ok");
  const auto before = profileStateFingerprint(profile);
  const auto lines = projectFirstRaidReturnGuidance(profile);
  EXPECT_TRUE(contains(lines, "EXACT GRID POSITIONS"));
  EXPECT_TRUE(contains(lines, "NOT NET NEW LOOT"));
  EXPECT_TRUE(contains(lines, "REQUIRE YOUR CHOICE"));
  EXPECT_EQ(profileStateFingerprint(profile), before);
  for (const auto &line : lines)
    EXPECT_LT(line.size(), 85U);
}

TEST(FirstRaidGuidanceTest,
     BothRealFailurePathsKeepBaseAndUseExistingRecoveryOnly) {
  for (const auto outcome :
       {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit}) {
    auto profile = preparing();
    profile.lastRaidResult = LastRaidResult{};
    profile.lastRaidResult->settlementId = "first-failed";
    profile.lastRaidResult->outcome = outcome;
    profile.lastRaidResult->lostRaidRecordId = "lost";
    profile.committedSettlements.insert("first-failed");
    const auto noRecord = projectFirstRaidReturnGuidance(profile);
    EXPECT_TRUE(contains(noRecord, "BASE REMAINS"));
    EXPECT_TRUE(contains(noRecord, "CONDITIONAL RELIEF"));
    EXPECT_FALSE(contains(noRecord, "NPC OR SELF"));
    profile.lostRaidRecords.emplace("lost", LostRaidRecord{});
    EXPECT_TRUE(
        contains(projectFirstRaidReturnGuidance(profile), "NPC OR SELF"));
  }
}

TEST(FirstRaidGuidanceTest,
     MapBriefingUsesActualTwentyMinutesAndOtherDefinitions) {
  const auto &content = publishedContentRegistry();
  const auto &frontier =
      content.map(MapDefinitionId{"map.raid.frontier_exchange"});
  ASSERT_FLOAT_EQ(frontier.highRisk.regularPhaseDurationSeconds, 1200.0F);
  EXPECT_TRUE(contains(projectFirstRaidMapBriefing(frontier), "1200 S"));
  auto different = frontier;
  different.highRisk.regularPhaseDurationSeconds = 420.0F;
  different.raidRules.extractionDurationSeconds = 6.0F;
  EXPECT_TRUE(contains(projectFirstRaidMapBriefing(different), "420 S"));
  EXPECT_TRUE(
      contains(projectFirstRaidMapBriefing(different), "EXTRACTION: 6 S"));
  EXPECT_TRUE(
      contains(projectFirstRaidMapBriefing(different), "NOT A RAID FAILURE"));
}

TEST(FirstRaidGuidanceTest, RuntimeUsesActualClockWithoutMutatingProfileOrMap) {
  const auto profile = pending();
  const auto before = profileStateFingerprint(profile);
  auto raid = activeRaid(420.0F);
  raid.update(7.0F, false);
  const auto map = tactical(false);
  const auto result =
      projectFirstRaidRiskGuidance(profile, raid, map, 1000U, 22000U);
  EXPECT_TRUE(result.visible);
  EXPECT_FLOAT_EQ(result.regularSecondsRemaining, 413.0F);
  EXPECT_TRUE(contains(result.lines, "413 S"));
  EXPECT_FLOAT_EQ(raid.raidTimeRemaining(), 413.0F);
  EXPECT_EQ(profileStateFingerprint(profile), before);
  EXPECT_FALSE(map.cellRevealed(0, 0));
  EXPECT_FALSE(
      map.extractionVisible(RaidMapExtractionKind::EmergencyConditional));
}

TEST(FirstRaidGuidanceTest,
     UnknownAlternativesNeverExposeWeightRulesOrEnemyCoordinates) {
  const auto profile = pending();
  auto raid = activeRaid();
  ASSERT_TRUE(raid.triggerHighRisk());
  const auto result = projectFirstRaidRiskGuidance(
      profile, raid, tactical(false), 333U, 54321U);
  EXPECT_TRUE(result.highRisk);
  EXPECT_FALSE(result.signalKnown);
  EXPECT_FALSE(result.signalOpen);
  EXPECT_FALSE(result.conditionalKnown);
  EXPECT_EQ(result.conditionalWeightLimitGrams, 0U);
  EXPECT_FALSE(contains(result.lines, "54321"));
  EXPECT_FALSE(contains(result.lines, "543"));
  EXPECT_FALSE(contains(result.lines, "LIGHT EXTRACTION"));
  EXPECT_TRUE(contains(result.lines, "DISCOVERED OR BRIEFED"));
}

TEST(FirstRaidGuidanceTest,
     KnownConditionalExtractionReportsRealWeightAndPhase) {
  const auto profile = pending();
  auto raid = activeRaid();
  const auto map = tactical(true);
  auto result =
      projectFirstRaidRiskGuidance(profile, raid, map, 22000U, 22000U);
  EXPECT_TRUE(result.conditionalKnown);
  EXPECT_TRUE(result.conditionalEligible);
  EXPECT_FALSE(result.conditionalOpen);
  ASSERT_TRUE(raid.triggerHighRisk());
  result = projectFirstRaidRiskGuidance(profile, raid, map, 22000U, 22000U);
  EXPECT_TRUE(result.signalOpen);
  EXPECT_TRUE(result.conditionalOpen);
  EXPECT_TRUE(contains(result.lines, "LIGHT EXTRACTION: READY"));
  result = projectFirstRaidRiskGuidance(profile, raid, map, 22001U, 22000U);
  EXPECT_FALSE(result.conditionalEligible);
  EXPECT_TRUE(contains(result.lines, "TOO HEAVY"));
  EXPECT_TRUE(contains(result.lines, "22001 G / LIMIT 22000 G"));
}

TEST(FirstRaidGuidanceTest,
     CrossingPhasePreservesRealNormalGraceAndDoesNotFailRaid) {
  const auto profile = pending();
  auto raid = activeRaid(5.0F);
  const auto map = tactical(true);
  raid.update(4.0F, false);
  raid.update(1.5F, true);
  const auto result =
      projectFirstRaidRiskGuidance(profile, raid, map, 0U, 22000U);
  EXPECT_TRUE(result.highRisk);
  EXPECT_FALSE(result.normalOpen);
  EXPECT_TRUE(result.normalGrace);
  EXPECT_FLOAT_EQ(result.extractionSecondsRemaining, 1.5F);
  EXPECT_TRUE(contains(result.lines, "WITHOUT LEAVING"));
  EXPECT_TRUE(contains(result.lines, "DOES NOT END"));
  EXPECT_TRUE(raid.isActive());
  raid.update(0.0F, false);
  EXPECT_FALSE(
      projectFirstRaidRiskGuidance(profile, raid, map, 0U, 22000U).normalGrace);
}

TEST(FirstRaidGuidanceTest, HiddenAndTerminalSessionsHaveNoRiskHints) {
  auto profile = pending();
  auto raid = activeRaid();
  const auto map = tactical(true);
  profile.homeFounding.hintsDismissed = true;
  EXPECT_FALSE(
      projectFirstRaidRiskGuidance(profile, raid, map, 0U, 22000U).visible);
  profile.homeFounding.hintsDismissed = false;
  ASSERT_TRUE(raid.markPlayerDead());
  EXPECT_TRUE(projectFirstRaidRiskGuidance(profile, raid, map, 0U, 22000U)
                  .lines.empty());
}

TEST(FirstRaidGuidanceTest,
     ProductionSessionWrapperReadsActiveRuntimeNotPreview) {
  GameSession session;
  ASSERT_TRUE(session.startNewProfile("first-guidance-session", true));
  const auto &plot = homePlotDefinitions()[0];
  ASSERT_TRUE(
      session.establishHome(plot.id, kFoundingRegion, plot.corePosition));
  EXPECT_TRUE(projectFirstRaidRiskGuidance(session).lines.empty());
  ASSERT_TRUE(
      session.deployAlpha(722U, MapDefinitionId{"map.raid.industrial"}));
  const auto before = profileStateFingerprint(session.profile());
  const auto result = projectFirstRaidRiskGuidance(session);
  EXPECT_TRUE(result.visible);
  EXPECT_FLOAT_EQ(result.regularSecondsRemaining,
                  session.world().raidSession().raidTimeRemaining());
  EXPECT_EQ(profileStateFingerprint(session.profile()), before);
  EXPECT_FALSE(session.dismissHomeHints());
  EXPECT_EQ(profileStateFingerprint(session.profile()), before);
  EXPECT_FALSE(projectFirstRaidRiskGuidance(session).lines.empty());
}
