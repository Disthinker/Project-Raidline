#include "alpha_content_ids.h"
#include "game_flow.h"
#include "home_founding_domain.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {
const auto &content = publishedContentRegistry();

class FirstRaidSaveDirectory {
public:
  FirstRaidSaveDirectory()
      : path_{std::filesystem::temp_directory_path() /
              ("raidline-first-loop-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))} {}
  ~FirstRaidSaveDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

ProfileState foundedProfile(std::string id) {
  auto profile = makeNewHomeProfile(std::move(id), content);
  const auto &plot = homePlotDefinitions().front();
  const auto found = executeHomeFounding(
      profile, content, plot.id, kFoundingRegion, plot.corePosition,
      {profile.revision, "found-first-loop"});
  if (!found.succeeded)
    throw std::runtime_error(found.message);
  const auto backpack = std::find_if(
      profile.assets.records().begin(), profile.assets.records().end(),
      [](const auto &entry) {
        return entry.second.definitionId == alpha_content::backpack;
      });
  if (backpack == profile.assets.records().end())
    throw std::runtime_error("missing initial backpack");
  const auto equipped = executeInventory(
      profile, content,
      InventoryEquipCommand{backpack->first, EquipmentSlotKind::Backpack},
      {profile.revision, "equip-first-loop-backpack"});
  if (!equipped.succeeded)
    throw std::runtime_error(equipped.message);
  return profile;
}

void saveProfile(const FirstRaidSaveDirectory &directory,
                 const ProfileState &profile) {
  const auto saved =
      SaveRepository{directory.path()}.save(profile, content.contentVersion());
  ASSERT_TRUE(saved.succeeded) << saved.message;
}

void loadFlow(GameFlow &flow, const FirstRaidSaveDirectory &directory) {
  flow.configurePersistence(directory.path());
  ASSERT_TRUE(flow.continueGame()) << flow.gameSession().persistenceMessage();
  ASSERT_EQ(flow.state(), GameFlowState::Base);
}

void finishRaid(GameFlow &flow, RaidResultOutcome outcome) {
  ASSERT_TRUE(flow.deploy());
  auto &session = flow.gameSession();
  if (outcome == RaidResultOutcome::Extracted) {
    for (const auto &enemy : session.world().enemies())
      static_cast<void>(
          const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
    const Rect extraction = session.world().extractionPoint().bounds();
    ASSERT_TRUE(const_cast<Player &>(session.world().player())
                    .setPosition(extraction.position));
    for (int step = 0; step < 50 && flow.state() == GameFlowState::Raid; ++step)
      flow.update({}, 0.1F);
  } else if (outcome == RaidResultOutcome::PlayerDead) {
    ASSERT_TRUE(session.world().damagePlayer(100));
    flow.update({}, 0.0F);
  } else {
    ASSERT_TRUE(session.activeQuitAlphaRaid());
    flow.update({}, 0.0F);
  }
  ASSERT_EQ(flow.state(), GameFlowState::RaidResult);
  ASSERT_TRUE(session.profile().lastRaidResult);
  ASSERT_EQ(session.profile().lastRaidResult->outcome, outcome);
}
} // namespace

TEST(FirstRaidLifecycleTest,
     NewGuideDoesNotFinishAtGateAndLegacyRemainsDismissed) {
  FirstRaidSaveDirectory directory;
  auto profile = foundedProfile("first-gate");
  profile.tutorial = TutorialProgress::FindRaidGate;
  saveProfile(directory, profile);
  GameSession session;
  session.configurePersistence(directory.path());
  ASSERT_TRUE(session.continueProfile());
  const auto before = profileStateFingerprint(session.profile());
  session.noteBaseFacility(BaseFacilityKind::RaidGate);
  EXPECT_EQ(profileStateFingerprint(session.profile()), before);
  EXPECT_FALSE(session.finishFirstRaidHints());
  EXPECT_FALSE(session.profile().homeFounding.hintsDismissed);

  auto legacy = makeNewAlphaProfile("legacy-first-loop", content);
  legacy.tutorial = TutorialProgress::FindRaidGate;
  saveProfile(directory, legacy);
  ASSERT_TRUE(session.continueProfile());
  session.noteBaseFacility(BaseFacilityKind::RaidGate);
  EXPECT_TRUE(session.profile().homeFounding.hintsDismissed);
  EXPECT_EQ(session.profile().tutorial, TutorialProgress::Complete);
}

TEST(FirstRaidLifecycleTest,
     EveryCommittedOutcomeCanFinishOnReturnWithoutChangingAssets) {
  for (const auto outcome :
       {RaidResultOutcome::Extracted, RaidResultOutcome::PlayerDead,
        RaidResultOutcome::ActiveQuit}) {
    FirstRaidSaveDirectory directory;
    saveProfile(directory,
                foundedProfile("first-result-" +
                               std::to_string(static_cast<int>(outcome))));
    GameFlow flow;
    loadFlow(flow, directory);
    finishRaid(flow, outcome);
    auto expected = flow.gameSession().profile();
    ASSERT_FALSE(expected.homeFounding.hintsDismissed);
    expected.homeFounding.hintsDismissed = true;
    expected.tutorial = TutorialProgress::Complete;
    ++expected.revision;
    ASSERT_TRUE(flow.returnToBase());
    EXPECT_EQ(flow.state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()),
              profileStateFingerprint(expected));
    const auto completed =
        profileStateFingerprint(flow.gameSession().profile());
    EXPECT_TRUE(flow.gameSession().finishFirstRaidHints());
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), completed);
    GameFlow reopened;
    loadFlow(reopened, directory);
    EXPECT_TRUE(reopened.gameSession().profile().homeFounding.hintsDismissed);
    EXPECT_EQ(profileStateFingerprint(reopened.gameSession().profile()),
              completed);
  }
}

TEST(FirstRaidLifecycleTest,
     FailedReturnAcknowledgmentStaysOnResultAndCanRetry) {
  FirstRaidSaveDirectory directory;
  saveProfile(directory, foundedProfile("failed-return-ack"));
  GameFlow flow;
  loadFlow(flow, directory);
  finishRaid(flow, RaidResultOutcome::ActiveQuit);
  const auto before = profileStateFingerprint(flow.gameSession().profile());
  std::filesystem::create_directory(directory.path() / "profile.tmp.json");
  EXPECT_FALSE(flow.returnToBase());
  EXPECT_EQ(flow.state(), GameFlowState::RaidResult);
  EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
  const auto saved = SaveRepository{directory.path()}.load(content);
  ASSERT_TRUE(saved.profile);
  EXPECT_EQ(profileStateFingerprint(*saved.profile), before);
  std::filesystem::remove(directory.path() / "profile.tmp.json");
  EXPECT_TRUE(flow.returnToBase());
  EXPECT_TRUE(flow.gameSession().profile().homeFounding.hintsDismissed);
}

TEST(FirstRaidLifecycleTest,
     ActiveRaidCannotSaveHintPreferenceOrCollectedLoot) {
  FirstRaidSaveDirectory directory;
  saveProfile(directory, foundedProfile("no-active-hint-save"));
  GameSession session;
  session.configurePersistence(directory.path());
  ASSERT_TRUE(session.continueProfile());
  const auto cleanFingerprint = profileStateFingerprint(session.profile());
  ASSERT_TRUE(session.deployAlpha(199));
  const auto &loot = session.profile().pendingRaid->loot;
  const auto selected =
      std::find_if(loot.begin(), loot.end(), [](const auto &entry) {
        return entry.spaceId == outdoorRaidSpaceId() &&
               !entry.requiresHighRisk &&
               content.item(entry.definitionId).maxStackSize == 1U;
      });
  ASSERT_NE(selected, loot.end());
  const auto assetId = selected->assetId;
  const Vec2 position = selected->position;
  const float half = session.world().player().size() * 0.5F;
  ASSERT_TRUE(const_cast<Player &>(session.world().player())
                  .setPosition({position.x - half, position.y - half}));
  GameplayInput pickup{};
  pickup.interactJustPressed = true;
  session.update(pickup, 0.0F);
  ASSERT_FALSE(std::holds_alternative<RaidGroundAssetLocation>(
      session.profile().assets.find(assetId)->location));
  const auto activeFingerprint = profileStateFingerprint(session.profile());
  EXPECT_FALSE(session.dismissHomeHints());
  EXPECT_FALSE(session.finishFirstRaidHints());
  EXPECT_EQ(profileStateFingerprint(session.profile()), activeFingerprint);
  const auto saved = SaveRepository{directory.path()}.load(content);
  ASSERT_TRUE(saved.profile);
  EXPECT_FALSE(saved.profile->pendingRaid);
  EXPECT_EQ(profileStateFingerprint(*saved.profile), cleanFingerprint);
  GameFlow reopened;
  loadFlow(reopened, directory);
  EXPECT_FALSE(reopened.gameSession().profile().homeFounding.hintsDismissed);
  EXPECT_FALSE(reopened.gameSession().profile().lastRaidResult);
  EXPECT_EQ(profileStateFingerprint(reopened.gameSession().profile()),
            cleanFingerprint);
}

TEST(FirstRaidLifecycleTest,
     UnacknowledgedResultReopensAndSecondDeployCompletesAtomically) {
  FirstRaidSaveDirectory directory;
  saveProfile(directory, foundedProfile("second-deploy-guide"));
  GameFlow first;
  loadFlow(first, directory);
  finishRaid(first, RaidResultOutcome::ActiveQuit);
  GameFlow reopened;
  loadFlow(reopened, directory);
  ASSERT_TRUE(reopened.gameSession().profile().lastRaidResult);
  ASSERT_FALSE(reopened.gameSession().profile().homeFounding.hintsDismissed);
  const auto before = profileStateFingerprint(reopened.gameSession().profile());
  EXPECT_FALSE(reopened.deploy(MapDefinitionId{"map.not_a_map"}));
  EXPECT_EQ(profileStateFingerprint(reopened.gameSession().profile()), before);
  std::filesystem::create_directory(directory.path() / "profile.tmp.json");
  EXPECT_FALSE(reopened.deploy());
  EXPECT_EQ(reopened.state(), GameFlowState::Base);
  EXPECT_EQ(profileStateFingerprint(reopened.gameSession().profile()), before);
  std::filesystem::remove(directory.path() / "profile.tmp.json");
  ASSERT_TRUE(reopened.deploy());
  EXPECT_TRUE(reopened.gameSession().profile().homeFounding.hintsDismissed);
  EXPECT_EQ(reopened.gameSession().profile().tutorial,
            TutorialProgress::Complete);
  const auto saved = SaveRepository{directory.path()}.load(content);
  ASSERT_TRUE(saved.profile);
  EXPECT_FALSE(saved.profile->pendingRaid);
  EXPECT_TRUE(saved.profile->lastRaidResult);
  EXPECT_TRUE(saved.profile->homeFounding.hintsDismissed);
  GameFlow afterClose;
  loadFlow(afterClose, directory);
  EXPECT_EQ(profileStateFingerprint(afterClose.gameSession().profile()),
            profileStateFingerprint(*saved.profile));
}

TEST(FirstRaidLifecycleTest,
     LegacyPendingRecoveryGoesDirectlyToBaseWithoutFinishing) {
  FirstRaidSaveDirectory directory;
  auto profile = foundedProfile("legacy-pending-first-guide");
  const auto founding = profile.homeFounding;
  ASSERT_TRUE(executeDeploy(profile, content,
                            {"legacy-first", "legacy-first-settle", 807,
                             MapDefinitionId{"map.v0.test"}},
                            {profile.revision, "legacy-first-deploy"})
                  .succeeded);
  saveProfile(directory, profile);
  GameFlow flow;
  loadFlow(flow, directory);
  EXPECT_TRUE(flow.gameSession().recoveredAbandonedRaid());
  EXPECT_FALSE(flow.gameSession().profile().pendingRaid);
  EXPECT_FALSE(flow.gameSession().profile().lastRaidResult);
  EXPECT_TRUE(flow.gameSession().profile().committedSettlements.empty());
  EXPECT_EQ(flow.gameSession().profile().homeFounding, founding);
  EXPECT_FALSE(flow.gameSession().finishFirstRaidHints());
}

TEST(FirstRaidLifecycleTest,
     DismissAtBasePersistsWithoutPretendingARaidWasCompleted) {
  FirstRaidSaveDirectory directory;
  saveProfile(directory, foundedProfile("base-dismiss-first-guide"));
  GameSession session;
  session.configurePersistence(directory.path());
  ASSERT_TRUE(session.continueProfile());
  const auto originalTutorial = session.profile().tutorial;
  ASSERT_TRUE(session.dismissHomeHints());
  EXPECT_EQ(session.profile().tutorial, originalTutorial);
  EXPECT_FALSE(session.profile().lastRaidResult);
  EXPECT_TRUE(session.profile().committedSettlements.empty());
  const auto saved = SaveRepository{directory.path()}.load(content);
  ASSERT_TRUE(saved.profile);
  EXPECT_TRUE(saved.profile->homeFounding.hintsDismissed);
  EXPECT_EQ(profileStateFingerprint(*saved.profile),
            profileStateFingerprint(session.profile()));
}
