#include "alpha_content_ids.h"
#include "base_migration_domain.h"
#include "base_morale_domain.h"
#include "base_population_domain.h"
#include "collision.h"
#include "game_flow.h"
#include "home_founding_domain.h"
#include "raid_lifecycle.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <queue>

namespace {
const auto &content = publishedContentRegistry();
auto fresh() { return makeNewHomeProfile("founding-tests", content); }
auto assets(const ProfileState &profile) {
  return nlohmann::json::parse(
             serializeProfileEnvelope(profile, content.contentVersion()))
      .at("payload")
      .at("assets");
}
auto found(ProfileState &profile, std::size_t index = 0,
           std::string tx = "found") {
  const auto &plot = homePlotDefinitions()[index];
  return executeHomeFounding(profile, content, plot.id, kFoundingRegion,
                             plot.corePosition,
                             {profile.revision, std::move(tx)});
}
} // namespace

TEST(HomeFoundingTest, SurveyIsRealUnestablishedStateWithFiniteAssets) {
  const auto profile = fresh();
  EXPECT_TRUE(validateProfileState(profile, content).valid);
  EXPECT_FALSE(profile.homeFounding.established);
  EXPECT_EQ(profile.basePopulation.ordinaryResidents, 0U);
  EXPECT_TRUE(profile.baseConstruction.facilities.empty());
  EXPECT_FALSE(
      profile.regionalOperations.technologyCore.baseSiteDefinitionId.valid());
  EXPECT_EQ(assets(profile),
            assets(makeNewAlphaProfile("founding-tests", content)));
  auto restored = deserializeProfileEnvelope(
      serializeProfileEnvelope(profile, content.contentVersion()), content);
  ASSERT_TRUE(restored.profile) << restored.message;
  EXPECT_EQ(profileStateFingerprint(profile),
            profileStateFingerprint(*restored.profile));
}

TEST(HomeFoundingTest, AllPlotsEstablishOnceAndPreserveEveryAsset) {
  for (std::size_t i = 0; i < 3; ++i) {
    auto profile = fresh();
    const auto before = assets(profile);
    const auto highWater = profile.assets.nextAssetId();
    auto receipt = found(profile, i);
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(assets(profile), before);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_EQ(profile.homeFounding.plots.at(kFoundingRegion),
              homePlotDefinitions()[i].id);
    EXPECT_EQ(profile.basePopulation.ordinaryResidents, 8U);
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_TRUE(found(profile, i).alreadyCommitted);
    EXPECT_FALSE(found(profile, (i + 1) % 3, "second").succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
  }
}

TEST(HomeFoundingTest, RejectionsAreNonMutating) {
  auto profile = fresh();
  const auto fingerprint = profileStateFingerprint(profile);
  const auto &plot = homePlotDefinitions()[0];
  EXPECT_FALSE(executeHomeFounding(profile, content, "bad", kFoundingRegion,
                                   plot.corePosition, {profile.revision, "bad"})
                   .succeeded);
  EXPECT_FALSE(executeHomeFounding(profile, content, plot.id, kFoundingRegion,
                                   {0, 0}, {profile.revision, "far"})
                   .succeeded);
  EXPECT_FALSE(executeHomeFounding(profile, content, plot.id, kFoundingRegion,
                                   plot.corePosition,
                                   {profile.revision + 1, "stale"})
                   .succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
  const auto deployed = executeDeploy(
      profile, content, {"r", "s", 1, MapDefinitionId{"map.v0.test"}},
      {profile.revision, "deploy"});
  EXPECT_FALSE(deployed.succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(HomeFoundingTest, LegacySaveKeepsBaseAndExactAssets) {
  auto profile = makeNewAlphaProfile("legacy-home", content);
  const auto text =
      serializeProfileEnvelope(profile, content.contentVersion(), 44);
  auto restored = deserializeProfileEnvelope(text, content);
  ASSERT_TRUE(restored.profile) << restored.message;
  EXPECT_TRUE(restored.profile->homeFounding.established);
  EXPECT_TRUE(restored.profile->homeFounding.hintsDismissed);
  EXPECT_TRUE(restored.profile->homeFounding.plots.empty());
  EXPECT_EQ(profileStateFingerprint(profile),
            profileStateFingerprint(*restored.profile));
}

TEST(HomeFoundingTest, SelectionNeverRerollsPropsAndAllFacilitiesHaveSpace) {
  const auto survey =
      generateFoundingHomeRegionLayout(kFoundingRegion.value(), "survey");
  for (const auto &plot : homePlotDefinitions()) {
    auto profile = fresh();
    ASSERT_TRUE(executeHomeFounding(profile, content, plot.id, kFoundingRegion,
                                    plot.corePosition, {profile.revision, "x"})
                    .succeeded);
    const auto layout =
        generateFoundingHomeRegionLayout(kFoundingRegion.value(), plot.id);
    EXPECT_EQ(layout.props, survey.props);
    EXPECT_EQ(layout.roadCells, survey.roadCells);
    EXPECT_EQ(layout.baseParcel, plot.bounds);
    BaseWorld world;
    world.configureSite(kFoundingRegion.value(), {}, std::string{plot.id});
    for (const auto &facility : world.facilities()) {
      const auto access = baseFacilityAccessGeometry(facility);
      for (const auto &blocker : layout.movementBlockers) {
        EXPECT_FALSE(
            isCollision(facility.bounds, Rect{blocker.position, blocker.size}))
            << plot.id;
        EXPECT_FALSE(
            isCollision(Rect{access.workZone.position, access.workZone.size},
                        Rect{blocker.position, blocker.size}))
            << plot.id << " facility " << static_cast<int>(facility.kind)
            << " blocker " << blocker.position.x << "," << blocker.position.y;
      }
    }
  }
}

TEST(HomeFoundingTest,
     ProductionNewFlowPausesSurveyAndRestoresAcrossProcesses) {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("raidline-founding-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  GameFlow flow;
  flow.configurePersistence(dir);
  ASSERT_TRUE(flow.startNewGame("flow-found", true));
  EXPECT_TRUE(flow.baseWorld().surveying());
  EXPECT_TRUE(flow.baseWorld().canAccessStash());
  const auto before = profileStateFingerprint(flow.gameSession().profile());
  for (int i = 0; i < 100; ++i) {
    flow.updateBase({}, 0.1F);
    flow.gameSession().advanceBaseWorldClock(1.0F);
  }
  EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
  GameFlow reload;
  reload.configurePersistence(dir);
  ASSERT_TRUE(reload.continueGame());
  EXPECT_TRUE(reload.baseWorld().surveying());
  EXPECT_TRUE(reload.baseWorld().perimeterEnemies().empty());
  std::filesystem::remove_all(dir);
}

TEST(HomeFoundingTest, SurveyCannotOperateBaseOrHideOperatingState) {
  auto profile = fresh();
  const auto hash = profileStateFingerprint(profile);
  EXPECT_FALSE(queryBaseRest(profile, {1U}).canCommit);
  static_cast<void>(synchronizeBaseDailySystemsThrough(profile, content));
  EXPECT_EQ(profileStateFingerprint(profile), hash);
  profile.baseConstruction.workshopLevel = 1;
  EXPECT_FALSE(validateProfileState(profile, content).valid);
  profile = fresh();
  profile.baseResources.pool.food = 1;
  EXPECT_FALSE(validateProfileState(profile, content).valid);
  profile = fresh();
  ++profile.worldClock.elapsedWorldMinutes;
  EXPECT_FALSE(validateProfileState(profile, content).valid);
  profile = fresh();
  profile.homeFounding.layoutVersion = 123;
  EXPECT_FALSE(validateProfileState(profile, content).valid);
}

TEST(HomeFoundingTest, GroundItemsBlockBurialAndStayAtTheirExactLocations) {
  auto profile = fresh();
  const auto &plot = homePlotDefinitions()[0];
  const auto id = profile.assets.create(
      content.item(alpha_content::ammunition),
      BaseGroundAssetLocation{kFoundingRegion, plot.corePosition}, 2);
  const auto before = profileStateFingerprint(profile);
  EXPECT_FALSE(found(profile).succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), before);
  profile.assets.findMutable(id)->location =
      BaseGroundAssetLocation{kFoundingRegion, {3000, 2400}};
  const auto beforeAssets = assets(profile);
  ASSERT_TRUE(found(profile).succeeded);
  EXPECT_EQ(assets(profile), beforeAssets);
}

TEST(HomeFoundingTest, SaveFailureLeavesSurveyBudgetAndRevisionUnchanged) {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("raidline-founding-failure-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  GameSession session;
  session.configurePersistence(dir);
  ASSERT_TRUE(session.startNewProfile("save-failure", true));
  const auto before = profileStateFingerprint(session.profile());
  std::filesystem::create_directory(dir / "profile.tmp.json");
  const auto &plot = homePlotDefinitions()[1];
  EXPECT_FALSE(
      session.establishHome(plot.id, kFoundingRegion, plot.corePosition));
  EXPECT_EQ(profileStateFingerprint(session.profile()), before);
  std::filesystem::remove(dir / "profile.tmp.json");
  ASSERT_TRUE(
      session.establishHome(plot.id, kFoundingRegion, plot.corePosition));
  ASSERT_TRUE(session.dismissHomeHints());
  GameSession reloaded;
  reloaded.configurePersistence(dir);
  ASSERT_TRUE(reloaded.continueProfile());
  EXPECT_TRUE(reloaded.profile().homeFounding.established);
  EXPECT_TRUE(reloaded.profile().homeFounding.hintsDismissed);
  EXPECT_EQ(profileStateFingerprint(reloaded.profile()),
            profileStateFingerprint(session.profile()));
  std::filesystem::remove_all(dir);
}

TEST(HomeFoundingTest, FirstRaidAllOutcomesAndPendingRecoveryKeepChosenPlot) {
  for (const auto outcome :
       {RaidResultOutcome::Extracted, RaidResultOutcome::PlayerDead,
        RaidResultOutcome::ActiveQuit}) {
    auto profile = fresh();
    ASSERT_TRUE(found(profile, 1).succeeded);
    const auto state = profile.homeFounding;
    const auto deployed = executeDeploy(
        profile, content,
        {"first", "settlement", 100, MapDefinitionId{"map.v0.test"}},
        {profile.revision, "deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    const auto pending = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()), content);
    ASSERT_TRUE(pending.profile) << pending.message;
    auto rolled = *pending.profile;
    ASSERT_TRUE(rollbackPendingRaidToBase(rolled, content).succeeded);
    EXPECT_EQ(rolled.homeFounding, state);
    const auto settled =
        settlePendingRaid(profile, content, "settlement", outcome);
    ASSERT_TRUE(settled.succeeded)
        << settled.message << " outcome " << static_cast<int>(outcome);
    EXPECT_EQ(profile.homeFounding, state);
    EXPECT_TRUE(settlePendingRaid(profile, content, "settlement", outcome)
                    .alreadyCommitted);
  }
}

TEST(HomeFoundingTest, LegacyPendingRaidRemainsFrozenAfterSchema44Load) {
  auto profile = makeNewAlphaProfile("legacy-pending", content);
  ASSERT_TRUE(
      executeDeploy(profile, content,
                    {"old", "old-settle", 101, MapDefinitionId{"map.v0.test"}},
                    {profile.revision, "deploy"})
          .succeeded);
  auto restored = deserializeProfileEnvelope(
      serializeProfileEnvelope(profile, content.contentVersion(), 44), content);
  ASSERT_TRUE(restored.profile) << restored.message;
  EXPECT_EQ(profileStateFingerprint(profile),
            profileStateFingerprint(*restored.profile));
  EXPECT_TRUE(restored.profile->homeFounding.plots.empty());
}

TEST(HomeFoundingTest, MovingRegionalBaseNeverForgetsTheOriginalLocalPlot) {
  auto profile = fresh();
  ASSERT_TRUE(found(profile, 2).succeeded);
  const RegionalBaseSiteDefinitionId ash{
      "regional_base_site.ashworks_logistics_yard"};
  profile.regionalOperations.baseSites.at(ash).unlocked = true;
  auto &outpost = profile.regionalOperations.outposts.at(
      RegionalOutpostDefinitionId{"regional_outpost.ashworks_logistics_yard"});
  outpost.unlocked = true;
  outpost.established = true;
  profile.baseConstruction.kitchenWaterLevel = 1;
  profile.baseConstruction
      .facilities[BaseFacilityDefinitionId{"base_facility.kitchen_water"}] =
      BaseConstructionState::FacilityPlacement::Installed;
  initializeBaseFacilityLayouts(profile, content);
  const auto selected = profile.homeFounding;
  auto receipt =
      executeBaseMigration(profile, content, {ash}, {profile.revision, "away"});
  ASSERT_TRUE(receipt.succeeded) << receipt.message;
  receipt = executeBaseMigration(profile, content, {kFoundingRegion},
                                 {profile.revision, "back"});
  ASSERT_TRUE(receipt.succeeded) << receipt.message;
  EXPECT_EQ(profile.homeFounding, selected);
}

TEST(HomeFoundingTest, CampAndEveryPlotAreWalkablyConnectedWithActorClearance) {
  // A conservative 40-unit test grid, expanded by the full player body.
  // Production movement continues to use the existing swept collision code.
  constexpr int columns = 200, rows = 75;
  const auto layout =
      generateFoundingHomeRegionLayout(kFoundingRegion.value(), "survey");
  std::vector<bool> blocked(columns * rows), reached(columns * rows);
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < columns; ++x) {
      const Rect body{
          {static_cast<float>(x * 40), static_cast<float>(y * 40 - 6)},
          {40, 52}};
      for (const auto &obstacle : layout.movementBlockers)
        if (isCollision(body, Rect{obstacle.position, obstacle.size})) {
          blocked[y * columns + x] = true;
          break;
        }
    }
  const auto cell = [](Vec2 p) {
    return static_cast<int>(p.y / 40) * columns + static_cast<int>(p.x / 40);
  };
  std::queue<int> queue;
  const int start = cell({2940, 2110});
  ASSERT_FALSE(blocked[start]);
  reached[start] = true;
  queue.push(start);
  while (!queue.empty()) {
    const int current = queue.front();
    queue.pop();
    const int x = current % columns, y = current / columns;
    for (const auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
      const int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= columns || ny >= rows)
        continue;
      const int n = ny * columns + nx;
      if (!blocked[n] && !reached[n]) {
        reached[n] = true;
        queue.push(n);
      }
    }
  }
  for (const auto &plot : homePlotDefinitions())
    EXPECT_TRUE(reached[cell(plot.corePosition)]) << plot.id;
}

TEST(HomeFoundingTest, RealMovementCanInspectFoundAndUseTheSelectedBase) {
  GameFlow flow;
  ASSERT_TRUE(flow.startNewGame("real-flow", true));
  auto walk = [&](Vec2 target) {
    for (int i = 0; i < 2000; ++i) {
      const auto pos = flow.baseWorld().playerPosition();
      const Vec2 center{pos.x + 20, pos.y + 26};
      const float dx = target.x - center.x, dy = target.y - center.y;
      if (std::abs(dx) <= 18 && std::abs(dy) <= 18)
        return true;
      BaseInput input{};
      input.moveLeft = dx < -18;
      input.moveRight = dx > 18;
      input.moveUp = dy < -18;
      input.moveDown = dy > 18;
      flow.updateBase(input, 0.05F);
    }
    ADD_FAILURE() << "Stopped at " << flow.baseWorld().playerPosition().x << ","
                  << flow.baseWorld().playerPosition().y;
    return false;
  };
  ASSERT_TRUE(walk({2960, 2360}));
  EXPECT_FALSE(flow.baseWorld().canAccessStash());
  ASSERT_TRUE(walk({4160, 2360}));
  ASSERT_TRUE(walk(homePlotDefinitions()[1].corePosition));
  ASSERT_TRUE(flow.establishHome(homePlotDefinitions()[1].id));
  EXPECT_EQ(flow.baseWorld().baseParcel(), homePlotDefinitions()[1].bounds);
  EXPECT_TRUE(flow.baseWorld().canAccessStash());
  EXPECT_FALSE(flow.baseWorld().surveying());
  EXPECT_TRUE(flow.openBaseFacilityForManagement(BaseFacilityKind::Workshop));
  flow.closeBaseFacility();
  ASSERT_TRUE(flow.deploy());
  EXPECT_EQ(flow.state(), GameFlowState::Raid);
  ASSERT_TRUE(flow.returnToMainMenu());
}

TEST(HomeFoundingTest,
     MalformedFoundingSavesAreRejectedEvenWithValidChecksums) {
  auto profile = fresh();
  ASSERT_TRUE(found(profile).succeeded);
  const auto original = nlohmann::json::parse(
      serializeProfileEnvelope(profile, content.contentVersion()));
  auto signedEnvelope = [](nlohmann::json envelope) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : envelope["payload"].dump()) {
      hash ^= c;
      hash *= 1099511628211ULL;
    }
    std::string checksum(16, '0');
    constexpr char hex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; --i) {
      checksum[i] = hex[hash & 15];
      hash >>= 4;
    }
    envelope["payload_checksum"] = checksum;
    return envelope.dump();
  };
  auto corrupt = original;
  corrupt["payload"]["home_founding"]["plots"][0]["plot"] = "home_plot.unknown";
  EXPECT_FALSE(
      deserializeProfileEnvelope(signedEnvelope(corrupt), content).profile);
  corrupt = original;
  const auto duplicate = corrupt["payload"]["home_founding"]["plots"][0];
  corrupt["payload"]["home_founding"]["plots"].push_back(duplicate);
  EXPECT_FALSE(
      deserializeProfileEnvelope(signedEnvelope(corrupt), content).profile);
  corrupt = original;
  corrupt["payload"]["home_founding"]["established"] = false;
  EXPECT_FALSE(
      deserializeProfileEnvelope(signedEnvelope(corrupt), content).profile);
  corrupt = original;
  corrupt["payload"]["home_founding"]["layout_version"] = 99;
  EXPECT_FALSE(
      deserializeProfileEnvelope(signedEnvelope(corrupt), content).profile);
  const auto text = original.dump();
  EXPECT_FALSE(
      deserializeProfileEnvelope(text.substr(0, text.size() / 2), content)
          .profile);
  EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(
                   profile, content.contentVersion(), 44)),
               std::invalid_argument);
}

TEST(HomeFoundingTest, ExistingConstructionAndPerimeterUseSelectedGeometry) {
  auto profile = fresh();
  ASSERT_TRUE(found(profile, 2).succeeded);
  const auto &plot = homePlotDefinitions()[2];
  for (int i = 0; i < 2; ++i) {
    const auto &definition =
        content.item(ItemDefinitionId{"item.loot.scrap_parts"});
    const auto origin =
        findFirstProfileFit(profile, content, ProfileContainerId::stash(),
                            definition, ItemOrientation::Degrees0);
    ASSERT_TRUE(origin);
    const auto id = profile.assets.create(
        definition, StoredAssetLocation{ProfileContainerId::stash(), *origin});
    ASSERT_TRUE(executeConstructionMaterialContribution(
                    profile, content, {id},
                    {profile.revision, "scrap" + std::to_string(i)})
                    .succeeded);
  }
  ASSERT_TRUE(executeStartBaseConstruction(
                  profile, content,
                  {BaseConstructionProjectDefinitionId{
                      "base_construction.kitchen_water.level_1"}},
                  {profile.revision, "kitchen"})
                  .succeeded);
  static_cast<void>(advanceWorldClock(profile.worldClock, 480U));
  ASSERT_TRUE(applyBaseConstructionThrough(profile, content).completed);
  EXPECT_TRUE(validateProfileState(profile, content).valid);
  const auto layout =
      generateFoundingHomeRegionLayout(kFoundingRegion.value(), plot.id);
  const auto perimeter = ensureHomePerimeterSnapshot(
      profile, content,
      {kFoundingRegion, layout.worldSize, plot.bounds, layout.movementBlockers},
      {profile.revision, "perimeter"});
  ASSERT_TRUE(perimeter.succeeded) << perimeter.message;
  for (const auto &enemy :
       profile.homePerimeter.sites.at(kFoundingRegion).enemies)
    EXPECT_EQ(queryHomeRegionSafetyZone(enemy.position, plot.bounds),
              HomeRegionSafetyZone::Perimeter);
  EXPECT_NE(queryHomeRegionSafetyZone(homePlotDefinitions()[0].corePosition,
                                      plot.bounds),
            HomeRegionSafetyZone::SafeCore);
  EXPECT_EQ(profile.homeFounding.plots.at(kFoundingRegion), plot.id);
}
