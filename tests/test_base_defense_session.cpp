#include "alpha_content_ids.h"
#include "base_siege_domain.h"
#include "game_flow.h"
#include "home_founding_types.h"
#include "home_perimeter_domain.h"
#include "weapon_ammo_domain.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {
struct SaveFixture {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("raidline-defense-session-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  ~SaveFixture() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
void begin(GameFlow &flow) {
  ASSERT_TRUE(flow.startNewGame("defense-session", false));
  ASSERT_TRUE(flow.gameSession().triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()))
      << flow.gameSession().persistenceMessage();
}

AssetInstanceId assetFor(const ProfileState &profile,
                         const ItemDefinitionId &definition) {
  const auto found =
      std::find_if(profile.assets.records().begin(),
                   profile.assets.records().end(), [&](const auto &entry) {
                     return entry.second.definitionId == definition;
                   });
  return found == profile.assets.records().end() ? 0U : found->first;
}

void makeLoadedTerminal(ProfileState &profile, BaseDefenseEndReason reason) {
  const auto &content = publishedContentRegistry();
  const auto rifle = assetFor(profile, alpha_content::rifle);
  const auto magazine = assetFor(profile, alpha_content::magazine);
  const auto ammunition = assetFor(profile, alpha_content::ammunition);
  ASSERT_NE(rifle, 0U);
  ASSERT_NE(magazine, 0U);
  ASSERT_NE(ammunition, 0U);
  ASSERT_TRUE(executeInventory(profile, content,
                               InventoryEquipCommand{
                                   rifle, EquipmentSlotKind::PrimaryWeapon},
                               {profile.revision, "terminal-fixture-equip"})
                  .succeeded);
  ASSERT_TRUE(executeWeaponAmmo(profile, content,
                                LoadMagazineCommand{magazine, ammunition, 12},
                                {profile.revision, "terminal-fixture-load"})
                  .succeeded);
  ASSERT_TRUE(
      executeWeaponAmmo(profile, content,
                        InstallMagazineAndChamberCommand{rifle, magazine},
                        {profile.revision, "terminal-fixture-install"})
          .succeeded);
  ASSERT_TRUE(
      queryFireWeapon(profile, content, FireWeaponCommand{rifle}).canCommit);
  ASSERT_TRUE(profile.assets.find(rifle)->chamberedRound);
  ASSERT_TRUE(profile.activeBaseDefense);
  auto &checkpoint = *profile.activeBaseDefense;
  checkpoint.enemies.clear();
  checkpoint.contacts.clear();
  checkpoint.reservedAttackers.clear();
  checkpoint.killedIds.clear();
  checkpoint.breachedIds.clear();
  if (reason == BaseDefenseEndReason::Completed) {
    for (const auto &wave : checkpoint.wavePlans)
      checkpoint.killedIds.insert(checkpoint.killedIds.end(),
                                  wave.enemyIds.begin(), wave.enemyIds.end());
    checkpoint.spawnedEnemyCount =
        static_cast<std::uint32_t>(checkpoint.killedIds.size());
    checkpoint.currentWave =
        static_cast<std::uint32_t>(checkpoint.wavePlans.size());
    checkpoint.elapsedSeconds =
        checkpoint.wavePlans.back().releaseSeconds + 5.0F;
  } else if (reason == BaseDefenseEndReason::Breached) {
    const auto &ids = checkpoint.wavePlans.front().enemyIds;
    ASSERT_GE(ids.size(), checkpoint.breachLimit);
    checkpoint.breachedIds.assign(ids.begin(),
                                  ids.begin() + checkpoint.breachLimit);
    checkpoint.spawnedEnemyCount = checkpoint.breachLimit;
    checkpoint.elapsedSeconds = 5.0F;
  } else {
    ASSERT_EQ(reason, BaseDefenseEndReason::PlayerDown);
    profile.currentHealth = 0;
  }
  ASSERT_TRUE(validateProfileState(profile, content).valid);
}

nlohmann::json assetsJson(const ProfileState &profile) {
  return nlohmann::json::parse(serializeProfileEnvelope(
      profile,
      publishedContentRegistry().contentVersion()))["payload"]["assets"];
}

void checkTerminalRecovery(BaseDefenseEndReason reason, bool failFirstWrite) {
  SaveFixture save;
  GameFlow source;
  begin(source);
  ASSERT_TRUE(source.gameSession().profile().activeBaseDefense);
  ProfileState terminal = source.gameSession().profile();
  makeLoadedTerminal(terminal, reason);
  ASSERT_TRUE(validateProfileState(terminal, publishedContentRegistry()).valid);
  const auto event = terminal.activeBaseDefense->eventId;
  const auto sequence = terminal.activeBaseDefense->siegeSequence;
  const auto expectedAssets = assetsJson(terminal);
  const auto startingTime = terminal.worldClock.elapsedWorldMinutes;
  const auto startingMaterials = terminal.baseConstruction.materialUnits;
  const auto expectedOutcome = reason == BaseDefenseEndReason::Completed
                                   ? BaseSiegeOutcome::Defended
                                   : BaseSiegeOutcome::SoftFailure;
  SaveRepository repository(save.path);
  const auto written =
      repository.save(terminal, publishedContentRegistry().contentVersion());
  ASSERT_TRUE(written.succeeded) << written.message;
  GameFlow resumed;
  resumed.configurePersistence(save.path);
  ASSERT_TRUE(resumed.continueGame())
      << resumed.gameSession().persistenceMessage();
  ASSERT_TRUE(resumed.gameSession().baseDefenseActive());
  ASSERT_EQ(resumed.gameSession().profile().activeBaseDefense->eventId, event);
  const auto previousRevision = resumed.gameSession().profile().revision;
  const auto previousOutcome =
      resumed.gameSession().profile().baseSiege.lastOutcome;
  const auto previousLedger =
      resumed.gameSession().profile().committedTransactions;
  const auto previousResolved =
      resumed.gameSession().profile().baseSiege.lastResolvedSequence;
  const auto previousCurrency = resumed.gameSession().profile().currency;
  const auto temporaryObstruction = save.path / "profile.tmp.json";
  if (failFirstWrite)
    ASSERT_TRUE(std::filesystem::create_directory(temporaryObstruction));

  // This input would move the player and consume a real chambered round if
  // a terminal recovery were incorrectly advanced before settlement.
  BaseInput input;
  input.moveRight = true;
  input.fireJustPressed = true;
  input.firePressed = true;
  input.aimWorldPosition = Vec2{resumed.baseWorld().playerPosition().x + 500.0F,
                                resumed.baseWorld().playerPosition().y};
  resumed.updateBase(input, 0.1F);
  EXPECT_EQ(assetsJson(resumed.gameSession().profile()), expectedAssets);
  EXPECT_FALSE(resumed.baseWorld().shotFiredLastUpdate());

  if (failFirstWrite) {
    EXPECT_TRUE(resumed.gameSession().baseDefenseActive());
    EXPECT_TRUE(resumed.gameSession().baseDefenseSaveBlocked());
    EXPECT_EQ(resumed.gameSession().profile().revision, previousRevision);
    EXPECT_EQ(resumed.gameSession().profile().baseSiege.lastOutcome,
              previousOutcome);
    EXPECT_EQ(resumed.gameSession().profile().baseSiege.lastResolvedSequence,
              previousResolved);
    EXPECT_EQ(resumed.gameSession().profile().committedTransactions,
              previousLedger);
    EXPECT_EQ(resumed.gameSession().profile().worldClock.elapsedWorldMinutes,
              startingTime);
    EXPECT_EQ(resumed.gameSession().profile().currency, previousCurrency);
    const auto stillDurable = repository.load(publishedContentRegistry());
    ASSERT_TRUE(stillDurable.profile) << stillDurable.message;
    ASSERT_TRUE(stillDurable.profile->activeBaseDefense);
    EXPECT_EQ(stillDurable.profile->activeBaseDefense->eventId, event);
    EXPECT_EQ(assetsJson(*stillDurable.profile), expectedAssets);
    ASSERT_TRUE(std::filesystem::remove(temporaryObstruction));
    ASSERT_TRUE(resumed.gameSession().retryBaseDefenseSave())
        << resumed.gameSession().persistenceMessage();
  }

  EXPECT_FALSE(resumed.gameSession().baseDefenseActive());
  EXPECT_FALSE(resumed.gameSession().baseDefenseSaveBlocked());
  EXPECT_EQ(resumed.gameSession().profile().baseSiege.lastResolvedSequence,
            sequence);
  EXPECT_EQ(resumed.gameSession().profile().baseSiege.lastOutcome,
            expectedOutcome);
  EXPECT_EQ(assetsJson(resumed.gameSession().profile()), expectedAssets);
  EXPECT_EQ(resumed.gameSession().profile().worldClock.elapsedWorldMinutes,
            startingTime +
                (reason == BaseDefenseEndReason::PlayerDown ? 240U : 0U));
  if (reason == BaseDefenseEndReason::Completed)
    EXPECT_EQ(resumed.gameSession().profile().baseConstruction.materialUnits,
              startingMaterials + 8U);
  if (reason == BaseDefenseEndReason::PlayerDown)
    EXPECT_EQ(resumed.gameSession().profile().currentHealth, 35);
  const auto after = profileStateFingerprint(resumed.gameSession().profile());
  EXPECT_TRUE(resumed.gameSession().retryBaseDefenseSave());
  EXPECT_EQ(profileStateFingerprint(resumed.gameSession().profile()), after);
  const auto durable = repository.load(publishedContentRegistry());
  ASSERT_TRUE(durable.profile) << durable.message;
  EXPECT_FALSE(durable.profile->activeBaseDefense);
  EXPECT_EQ(durable.profile->baseSiege.lastResolvedSequence, sequence);
  EXPECT_EQ(profileStateFingerprint(*durable.profile), after);
}
} // namespace

TEST(BaseDefenseSessionTest, ManualStartExcludesRaidAndAutoDefense) {
  GameFlow flow;
  begin(flow);
  auto &session = flow.gameSession();
  ASSERT_TRUE(session.baseDefenseActive());
  const auto before = profileStateFingerprint(session.profile());
  EXPECT_FALSE(session.executeBaseAutoDefense("not-a-second-mode").succeeded);
  EXPECT_FALSE(flow.deploy(MapDefinitionId{"map.frontier_exchange"}));
  EXPECT_EQ(before, profileStateFingerprint(session.profile()));
  EXPECT_EQ(session.profile().activeBaseDefense->siegeSequence,
            session.profile().baseSiege.siegeSequence);
}

TEST(BaseDefenseSessionTest,
     RealAndInfiniteAmmunitionFireAtCoreDoesNotPauseDefense) {
  for (const bool infinite : {false, true}) {
    SCOPED_TRACE(infinite);
    SaveFixture save;
    GameFlow flow;
    flow.configurePersistence(save.path);
    ASSERT_TRUE(flow.startNewGame("defense-continuous-fire", false));
    auto &session = flow.gameSession();
    const auto rifle = assetFor(session.profile(), alpha_content::rifle);
    const auto magazine = assetFor(session.profile(), alpha_content::magazine);
    const auto ammo = assetFor(session.profile(), alpha_content::ammunition);
    ASSERT_TRUE(
        session
            .executeProfileInventory(
                InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
                "equip")
            .succeeded);
    ASSERT_TRUE(session
                    .executeProfileWeaponAmmo(
                        LoadMagazineCommand{magazine, ammo, 30}, "load")
                    .succeeded);
    ASSERT_TRUE(
        session
            .executeProfileWeaponAmmo(
                InstallMagazineAndChamberCommand{rifle, magazine}, "install")
            .succeeded);
    ASSERT_TRUE(session.triggerDeveloperBaseSiegeWarning());
    ASSERT_TRUE(session.startBaseRealtimeDefense(flow.baseWorld()));
    unsigned shots = 0;
    for (unsigned frame = 0; frame < 3000 && session.baseDefenseActive();
         ++frame) {
      BaseInput input;
      input.firePressed = true;
      input.developerInfiniteAmmo = infinite;
      input.fireJustPressed = frame % 10 == 0;
      const auto &enemies = flow.baseWorld().baseDefenseEnemies();
      if (!enemies.empty()) {
        const auto &enemy = enemies.front();
        const Vec2 target{enemy.position().x + enemy.size().x / 2,
                          enemy.position().y + enemy.size().y / 2};
        const auto aim = flow.baseWorld().weaponAimWorldPosition();
        input.aimWorldPosition = target;
        input.aimMotionDelta = Vec2{target.x - aim.x, target.y - aim.y};
      }
      flow.updateBase(input, 0.02F);
      shots += flow.baseWorld().shotFiredLastUpdate() ? 1 : 0;
      ASSERT_FALSE(session.baseDefenseSaveBlocked())
          << "frame=" << frame << " shots=" << shots << " "
          << session.persistenceMessage();
    }
    EXPECT_GE(shots, 10U);
  }
}

TEST(BaseDefenseSessionTest,
     NewlyEquippedWeaponIsConfiguredBeforeDefenseSnapshot) {
  GameFlow flow;
  ASSERT_TRUE(flow.startNewGame("defense-new-weapon", false));
  auto &session = flow.gameSession();
  const auto rifle = assetFor(session.profile(), alpha_content::rifle);
  ASSERT_TRUE(
      session
          .executeProfileInventory(
              InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
              "equip-before-warning")
          .succeeded);
  ASSERT_TRUE(session.triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(session.startBaseRealtimeDefense(flow.baseWorld()));
  const auto expected =
      publishedContentRegistry().item(alpha_content::rifle).weaponUse;
  ASSERT_TRUE(expected);
  EXPECT_EQ(flow.baseWorld().baseDefenseCheckpoint()->shooting.weaponDamage,
            expected->baseDamage);
  flow.updateBase({}, 0.02F);
  EXPECT_EQ(flow.baseWorld().baseDefenseCheckpoint()->shooting.weaponDamage,
            expected->baseDamage);
}

TEST(BaseDefenseSessionTest, AbandonUsesOneSoftResultWithoutPersonalAssetLoss) {
  GameFlow flow;
  begin(flow);
  auto &session = flow.gameSession();
  const auto assets = nlohmann::json::parse(serializeProfileEnvelope(
      session.profile(),
      publishedContentRegistry().contentVersion()))["payload"]["assets"];
  const auto lost = session.profile().lostRaidRecords.size();
  const auto security = session.profile().baseResources.pool.security;
  const auto residents = session.profile().basePopulation.ordinaryResidents;
  ASSERT_TRUE(session.abandonBaseRealtimeDefense())
      << session.persistenceMessage();
  EXPECT_FALSE(session.baseDefenseActive());
  EXPECT_EQ(session.profile().baseSiege.lastOutcome,
            BaseSiegeOutcome::SoftFailure);
  EXPECT_EQ(session.profile().baseResources.pool.security, security);
  EXPECT_EQ(session.profile().lostRaidRecords.size(), lost);
  EXPECT_GE(session.profile().basePopulation.ordinaryResidents, residents - 1U);
  EXPECT_EQ(
      nlohmann::json::parse(serializeProfileEnvelope(
          session.profile(),
          publishedContentRegistry().contentVersion()))["payload"]["assets"],
      assets);
  const auto after = profileStateFingerprint(session.profile());
  EXPECT_FALSE(session.abandonBaseRealtimeDefense());
  EXPECT_EQ(profileStateFingerprint(session.profile()), after);
}

TEST(BaseDefenseSessionTest,
     NormalQuitRestoresSameEventAndFullCombatCheckpoint) {
  SaveFixture save;
  GameFlow flow;
  flow.configurePersistence(save.path);
  begin(flow);
  for (int i{}; i < 100; ++i)
    flow.updateBase({}, 0.016F);
  ASSERT_TRUE(flow.gameSession().checkpointWorldClock())
      << flow.gameSession().persistenceMessage();
  const auto saved =
      serializeProfileEnvelope(flow.gameSession().profile(),
                               publishedContentRegistry().contentVersion());
  const auto event = flow.gameSession().profile().activeBaseDefense->eventId;
  ASSERT_TRUE(flow.returnToMainMenu());
  ASSERT_TRUE(flow.continueGame()) << flow.gameSession().persistenceMessage();
  ASSERT_TRUE(flow.gameSession().baseDefenseActive());
  EXPECT_EQ(flow.gameSession().profile().activeBaseDefense->eventId, event);
  EXPECT_EQ(
      serializeProfileEnvelope(flow.gameSession().profile(),
                               publishedContentRegistry().contentVersion()),
      saved);
  EXPECT_EQ(flow.baseWorld().playerPosition().x,
            flow.gameSession().profile().activeBaseDefense->playerPosition.x);
  EXPECT_EQ(flow.baseWorld().playerPosition().y,
            flow.gameSession().profile().activeBaseDefense->playerPosition.y);
  EXPECT_FALSE(flow.gameSession().profile().pendingRaid);
}

TEST(BaseDefenseSessionTest, OpeningFacilityDoesNotFreezeAttack) {
  GameFlow flow;
  begin(flow);
  ASSERT_TRUE(flow.openBaseFacilityForManagement(BaseFacilityKind::Supply));
  const auto before = flow.baseWorld().baseDefenseState()->elapsedSeconds;
  BaseInput uiInput;
  uiInput.inventoryOpen = true;
  flow.updateBase(uiInput, 0.1F);
  EXPECT_GT(flow.baseWorld().baseDefenseState()->elapsedSeconds, before);
  EXPECT_EQ(flow.activeBaseFacility(), BaseFacilityKind::Supply);
}

TEST(BaseDefenseSessionTest, SameProcessAndFreshLoadPreserveOwnFlightAndWeaponConfiguration) {
  SaveFixture save;
  GameFlow flow;
  flow.configurePersistence(save.path);
  ASSERT_TRUE(flow.startNewGame("boundary-defense-flight"));
  auto &session = flow.gameSession();
  const auto rifle = assetFor(session.profile(), alpha_content::rifle);
  const auto magazine = assetFor(session.profile(), alpha_content::magazine);
  const auto ammo = assetFor(session.profile(), alpha_content::ammunition);
  ASSERT_TRUE(session.executeProfileInventory(
      InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon}, "equip").succeeded);
  ASSERT_TRUE(session.executeProfileWeaponAmmo(
      LoadMagazineCommand{magazine, ammo, 12}, "load").succeeded);
  ASSERT_TRUE(session.executeProfileWeaponAmmo(
      InstallMagazineAndChamberCommand{rifle, magazine}, "install").succeeded);
  ASSERT_TRUE(session.triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(session.startBaseRealtimeDefense(flow.baseWorld()));
  BaseInput fire;
  fire.firePressed = fire.fireJustPressed = true;
  const auto position = flow.baseWorld().playerPosition();
  fire.aimWorldPosition = Vec2{position.x + 500, position.y};
  flow.updateBase(fire, 0.000001F);
  ASSERT_TRUE(flow.baseWorld().shotFiredLastUpdate());
  ASSERT_FALSE(flow.baseWorld().baseDefenseCheckpoint()->shooting.flights.empty());
  const auto checkpoint = *flow.baseWorld().baseDefenseCheckpoint();
  const auto assets = assetsJson(session.profile());
  ASSERT_TRUE(flow.returnToMainMenu());
  ASSERT_TRUE(flow.continueGame()) << session.persistenceMessage();
  EXPECT_EQ(flow.baseWorld().baseDefenseCheckpoint()->shooting, checkpoint.shooting);
  EXPECT_EQ(assetsJson(session.profile()), assets);
  EXPECT_FALSE(flow.baseWorld().shotFiredLastUpdate());

  GameFlow fresh;
  fresh.configurePersistence(save.path);
  ASSERT_TRUE(fresh.continueGame());
  EXPECT_EQ(baseDefenseCheckpointHash(*flow.baseWorld().baseDefenseCheckpoint()),
            baseDefenseCheckpointHash(*fresh.baseWorld().baseDefenseCheckpoint()));
  flow.updateBase({}, 0.016F);
  fresh.updateBase({}, 0.016F);
  EXPECT_EQ(baseDefenseCheckpointHash(*flow.baseWorld().baseDefenseCheckpoint()),
            baseDefenseCheckpointHash(*fresh.baseWorld().baseDefenseCheckpoint()));
  EXPECT_EQ(assetsJson(session.profile()), assetsJson(fresh.gameSession().profile()));
  EXPECT_EQ(flow.baseWorld().baseDefenseCheckpoint()->shooting.weaponDamage,
            publishedContentRegistry().item(alpha_content::rifle).weaponUse->baseDamage);
}

TEST(BaseDefenseSessionTest, StartSaveFailureLeavesWarningAndProfileUntouched) {
  SaveFixture save;
  GameFlow flow;
  ASSERT_TRUE(flow.startNewGame("failure", false));
  ASSERT_TRUE(flow.gameSession().triggerDeveloperBaseSiegeWarning());
  std::ofstream obstruction(save.path);
  obstruction << "not a directory";
  obstruction.close();
  flow.configurePersistence(save.path);
  const auto before = profileStateFingerprint(flow.gameSession().profile());
  EXPECT_FALSE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
  EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
  EXPECT_TRUE(flow.gameSession().profile().baseSiege.warningActive);
  EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
}

TEST(BaseDefenseSessionTest, UnfoundedSurveyCannotCreateCombat) {
  GameFlow flow;
  ASSERT_TRUE(flow.startNewGame("survey", true));
  EXPECT_FALSE(flow.gameSession().triggerDeveloperBaseSiegeWarning());
  EXPECT_FALSE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
}

TEST(BaseDefenseSessionTest, LoadedVictorySettlesBeforeSimulation) {
  checkTerminalRecovery(BaseDefenseEndReason::Completed, false);
}
TEST(BaseDefenseSessionTest, LoadedPlayerDownSettlesBeforeSimulation) {
  checkTerminalRecovery(BaseDefenseEndReason::PlayerDown, false);
}
TEST(BaseDefenseSessionTest, LoadedBreachSettlesBeforeSimulation) {
  checkTerminalRecovery(BaseDefenseEndReason::Breached, false);
}
TEST(BaseDefenseSessionTest, FailedVictorySaveRetriesWithoutDoubleReward) {
  checkTerminalRecovery(BaseDefenseEndReason::Completed, true);
}
TEST(BaseDefenseSessionTest, FailedDownSaveRetriesWithoutDoubleRescueTime) {
  checkTerminalRecovery(BaseDefenseEndReason::PlayerDown, true);
}
TEST(BaseDefenseSessionTest, FailedBreachSaveRetriesWithoutDoublePublicLoss) {
  checkTerminalRecovery(BaseDefenseEndReason::Breached, true);
}

TEST(BaseDefenseSessionTest,
     SafeCoreMedicalTimersContinueAcrossCheckpointResume) {
  SaveFixture initialSave, resumedSave;
  GameFlow source;
  begin(source);
  ASSERT_TRUE(source.gameSession().baseDefenseActive());
  ProfileState wounded = source.gameSession().profile();
  wounded.medicalStatus =
      MedicalStatusState{BleedingSeverity::Light, 40000U, 1000U, 350U, 500U};
  ASSERT_TRUE(validateProfileState(wounded, publishedContentRegistry()).valid);
  const auto saved =
      SaveRepository(initialSave.path)
          .save(wounded, publishedContentRegistry().contentVersion());
  ASSERT_TRUE(saved.succeeded) << saved.message;
  GameFlow uninterrupted;
  uninterrupted.configurePersistence(initialSave.path);
  ASSERT_TRUE(uninterrupted.continueGame())
      << uninterrupted.gameSession().persistenceMessage();
  const auto position = uninterrupted.baseWorld().playerPosition();
  const auto size = uninterrupted.baseWorld().playerSize();
  EXPECT_EQ(queryHomeRegionSafetyZone(
                {position.x + size.x * 0.5F, position.y + size.y * 0.5F},
                uninterrupted.baseWorld().baseParcel()),
            HomeRegionSafetyZone::SafeCore);
  for (int i = 0; i < 17; ++i) {
    uninterrupted.updateBase({}, 0.07F);
    ASSERT_TRUE(uninterrupted.gameSession().baseDefenseActive());
    EXPECT_EQ(uninterrupted.baseWorld().baseDefenseDamageLastUpdate(), 0);
  }
  EXPECT_EQ(uninterrupted.gameSession().profile().currentHealth, 99);
  EXPECT_EQ(
      uninterrupted.gameSession().profile().medicalStatus.painkillerRemainingMs,
      0U);
  EXPECT_LT(uninterrupted.gameSession()
                .profile()
                .medicalStatus.lightBleedingRemainingMs,
            40000U);
  EXPECT_GT(
      uninterrupted.gameSession().profile().medicalStatus.painScreamRemainingMs,
      500U);
  ASSERT_TRUE(uninterrupted.gameSession().checkpointWorldClock())
      << uninterrupted.gameSession().persistenceMessage();
  const ProfileState midpoint = uninterrupted.gameSession().profile();
  ASSERT_TRUE(midpoint.activeBaseDefense);
  EXPECT_GT(midpoint.activeBaseDefense->medicalTickAccumulatorSeconds, 0.0F);
  EXPECT_GT(midpoint.activeBaseDefense->medicalRandomSequence, 0U);
  const auto copied =
      SaveRepository(resumedSave.path)
          .save(midpoint, publishedContentRegistry().contentVersion());
  ASSERT_TRUE(copied.succeeded) << copied.message;
  GameFlow resumed;
  resumed.configurePersistence(resumedSave.path);
  ASSERT_TRUE(resumed.continueGame())
      << resumed.gameSession().persistenceMessage();
  ASSERT_TRUE(resumed.gameSession().profile().activeBaseDefense);
  EXPECT_EQ(
      resumed.gameSession().profile().activeBaseDefense->medicalRandomSequence,
      midpoint.activeBaseDefense->medicalRandomSequence);
  EXPECT_EQ(resumed.gameSession().profile().medicalStatus,
            midpoint.medicalStatus);
  for (int i = 0; i < 40; ++i) {
    uninterrupted.updateBase({}, 0.07F);
    resumed.updateBase({}, 0.07F);
    ASSERT_TRUE(uninterrupted.gameSession().baseDefenseActive());
    ASSERT_TRUE(resumed.gameSession().baseDefenseActive());
    EXPECT_EQ(uninterrupted.baseWorld().baseDefenseDamageLastUpdate(), 0);
    EXPECT_EQ(resumed.baseWorld().baseDefenseDamageLastUpdate(), 0);
    EXPECT_EQ(resumed.gameSession().profile().currentHealth,
              uninterrupted.gameSession().profile().currentHealth);
    EXPECT_EQ(resumed.gameSession().profile().medicalStatus,
              uninterrupted.gameSession().profile().medicalStatus);
  }
  ASSERT_TRUE(uninterrupted.gameSession().checkpointWorldClock());
  ASSERT_TRUE(resumed.gameSession().checkpointWorldClock());
  const auto &expected =
      *uninterrupted.gameSession().profile().activeBaseDefense;
  const auto &actual = *resumed.gameSession().profile().activeBaseDefense;
  EXPECT_EQ(actual.medicalRandomSequence, expected.medicalRandomSequence);
  EXPECT_FLOAT_EQ(actual.medicalTickAccumulatorSeconds,
                  expected.medicalTickAccumulatorSeconds);
  EXPECT_DOUBLE_EQ(actual.pendingWorldSeconds, expected.pendingWorldSeconds);
  EXPECT_LT(resumed.gameSession().profile().currentHealth,
            midpoint.currentHealth);
  EXPECT_FALSE(resumed.gameSession().profile().pendingRaid);
}

TEST(BaseDefenseSessionTest,
     NewSurveyAfterActiveMenuExitClearsOldDefenseRuntime) {
  SaveFixture save;
  GameFlow flow;
  flow.configurePersistence(save.path);
  begin(flow);
  flow.updateBase({}, 0.1F);
  ASSERT_TRUE(flow.baseWorld().baseDefenseActive());
  ASSERT_TRUE(flow.returnToMainMenu())
      << flow.gameSession().persistenceMessage();
  ASSERT_TRUE(flow.startNewGame("new-survey-after-defense", true))
      << flow.gameSession().persistenceMessage();
  EXPECT_FALSE(flow.gameSession().baseDefenseActive());
  EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
  EXPECT_TRUE(flow.baseWorld().baseDefenseEnemies().empty());
  EXPECT_FALSE(flow.gameSession().profile().homeFounding.established);
  EXPECT_TRUE(flow.baseWorld().surveying());
  EXPECT_EQ(flow.baseWorld().plotId(), "survey");
  EXPECT_EQ(flow.baseWorld().siteDefinitionId(), kFoundingRegion.value());
  const auto actual = flow.baseWorld().baseParcel();
  EXPECT_FLOAT_EQ(actual.position.x, kFoundingCamp.position.x);
  EXPECT_FLOAT_EQ(actual.position.y, kFoundingCamp.position.y);
  EXPECT_FLOAT_EQ(actual.size.x, kFoundingCamp.size.x);
  EXPECT_FLOAT_EQ(actual.size.y, kFoundingCamp.size.y);
  flow.updateBase({}, 0.1F);
  EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
  EXPECT_TRUE(flow.baseWorld().surveying());
}

TEST(BaseDefenseSessionTest,
     ConsecutiveEventsAcrossRestartUseDistinctTransactions) {
  SaveFixture save;
  GameFlow first;
  first.configurePersistence(save.path);
  begin(first);
  ASSERT_TRUE(first.gameSession().baseDefenseActive());
  const auto firstId = first.gameSession().profile().activeBaseDefense->eventId;
  const auto firstSequence =
      first.gameSession().profile().activeBaseDefense->siegeSequence;
  ASSERT_TRUE(first.gameSession().abandonBaseRealtimeDefense())
      << first.gameSession().persistenceMessage();
  EXPECT_EQ(first.gameSession().profile().baseSiege.lastResolvedSequence,
            firstSequence);
  const auto firstResult =
      profileStateFingerprint(first.gameSession().profile());
  EXPECT_FALSE(first.gameSession().abandonBaseRealtimeDefense());
  EXPECT_EQ(profileStateFingerprint(first.gameSession().profile()),
            firstResult);
  ASSERT_TRUE(first.returnToMainMenu());

  GameFlow second;
  second.configurePersistence(save.path);
  ASSERT_TRUE(second.continueGame())
      << second.gameSession().persistenceMessage();
  EXPECT_FALSE(second.gameSession().baseDefenseActive());
  EXPECT_EQ(second.gameSession().profile().baseSiege.lastResolvedSequence,
            firstSequence);
  ASSERT_TRUE(second.gameSession().triggerDeveloperBaseSiegeWarning());
  const auto secondSequence =
      second.gameSession().profile().baseSiege.siegeSequence;
  EXPECT_GT(secondSequence, firstSequence);
  ASSERT_TRUE(second.gameSession().startBaseRealtimeDefense(second.baseWorld()))
      << second.gameSession().persistenceMessage();
  ASSERT_TRUE(second.gameSession().baseDefenseActive());
  const auto secondId =
      second.gameSession().profile().activeBaseDefense->eventId;
  EXPECT_NE(firstId, secondId);
  ASSERT_TRUE(second.gameSession().abandonBaseRealtimeDefense())
      << second.gameSession().persistenceMessage();
  EXPECT_EQ(second.gameSession().profile().baseSiege.lastResolvedSequence,
            secondSequence);
  EXPECT_EQ(second.gameSession().profile().baseSiege.lastOutcome,
            BaseSiegeOutcome::SoftFailure);
  const auto secondResult =
      profileStateFingerprint(second.gameSession().profile());
  EXPECT_FALSE(second.gameSession().abandonBaseRealtimeDefense());
  EXPECT_EQ(profileStateFingerprint(second.gameSession().profile()),
            secondResult);
  const auto loaded =
      SaveRepository(save.path).load(publishedContentRegistry());
  ASSERT_TRUE(loaded.profile) << loaded.message;
  EXPECT_FALSE(loaded.profile->activeBaseDefense);
  EXPECT_EQ(loaded.profile->baseSiege.lastResolvedSequence, secondSequence);
  EXPECT_EQ(profileStateFingerprint(*loaded.profile), secondResult);
}
