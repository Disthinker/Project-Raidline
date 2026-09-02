#include <algorithm>
#include <limits>

#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_construction_domain.h"
#include "base_siege_domain.h"
#include "world_clock.h"

namespace {
const BaseConstructionProjectDefinitionId kDormitoryExpansion{
    "base_construction.dormitory.level_2"};
const BaseConstructionProjectDefinitionId kKitchenWaterBuild{
    "base_construction.kitchen_water.level_1"};

AssetInstanceId addPendingSalvage(ProfileState &profile,
                                  const ItemDefinitionId &definitionId,
                                  GridPosition origin = {0, 0}) {
  const ItemDefinition &definition =
      publishedContentRegistry().item(definitionId);
  return profile.assets.create(
      definition,
      StoredAssetLocation{ProfileContainerId::baseIntake(), origin});
}

AssetInstanceId addStashSalvage(ProfileState &profile,
                                const ItemDefinitionId &definitionId) {
  const ItemDefinition &definition =
      publishedContentRegistry().item(definitionId);
  const auto origin = findFirstProfileFit(
      profile, publishedContentRegistry(), ProfileContainerId::stash(),
      definition, ItemOrientation::Degrees0);
  EXPECT_TRUE(origin.has_value());
  return profile.assets.create(
      definition,
      StoredAssetLocation{ProfileContainerId::stash(), *origin});
}

void grantConstructionMaterial(ProfileState &profile) {
  const AssetInstanceId scrap =
      addPendingSalvage(profile, ItemDefinitionId{"item.loot.scrap_parts"});
  ASSERT_TRUE(executeConstructionMaterialContribution(
                  profile, publishedContentRegistry(),
                  ContributeConstructionMaterialCommand{scrap},
                  CommandContext{profile.revision, "process-scrap"})
                  .succeeded);
}
} // namespace

TEST(BaseConstructionDomainTest,
     SalvageBecomesIndependentConstructionMaterial) {
  ProfileState profile =
      makeNewAlphaProfile("construction-material", publishedContentRegistry());
  const AssetInstanceId scrap =
      addStashSalvage(profile, ItemDefinitionId{"item.loot.scrap_parts"});
  const ConstructionMaterialPlan plan = queryConstructionMaterialContribution(
      profile, publishedContentRegistry(),
      ContributeConstructionMaterialCommand{scrap});
  ASSERT_TRUE(plan.canCommit);
  EXPECT_EQ(plan.materialUnits, 4U);

  const ProfileRevision revision = profile.revision;
  const ConstructionMaterialReceipt receipt =
      executeConstructionMaterialContribution(
          profile, publishedContentRegistry(),
          ContributeConstructionMaterialCommand{scrap},
          CommandContext{revision, "process-scrap"});
  EXPECT_TRUE(receipt.succeeded);
  EXPECT_EQ(receipt.materialUnits, 4U);
  EXPECT_EQ(profile.baseConstruction.materialUnits, 4U);
  EXPECT_EQ(profile.revision, revision + 1U);
  EXPECT_EQ(profile.assets.find(scrap), nullptr);
  EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}

TEST(BaseConstructionDomainTest, StartLocksMaterialAndCompletionAddsBedsOnce) {
  ProfileState profile = makeNewAlphaProfile("construction-completion",
                                             publishedContentRegistry());
  grantConstructionMaterial(profile);
  const std::uint64_t started = profile.worldClock.elapsedWorldMinutes;

  const BaseConstructionReceipt startedReceipt = executeStartBaseConstruction(
      profile, publishedContentRegistry(),
      StartBaseConstructionCommand{kDormitoryExpansion},
      CommandContext{profile.revision, "start-dormitory"});
  ASSERT_TRUE(startedReceipt.succeeded);
  EXPECT_EQ(profile.baseConstruction.materialUnits, 0U);
  ASSERT_TRUE(profile.baseConstruction.activeProject.has_value());
  EXPECT_EQ(profile.baseConstruction.activeProject->committedWorkers, 3U);
  EXPECT_EQ(profile.basePopulation.bedCapacity, 10U);

  static_cast<void>(advanceWorldClock(profile.worldClock, 359U));
  EXPECT_FALSE(applyBaseConstructionThrough(profile, publishedContentRegistry())
                   .completed);
  EXPECT_EQ(profile.basePopulation.bedCapacity, 10U);

  static_cast<void>(advanceWorldClock(profile.worldClock, 1U));
  const BaseConstructionAdvanceResult completed =
      applyBaseConstructionThrough(profile, publishedContentRegistry());
  EXPECT_TRUE(completed.completed);
  EXPECT_EQ(completed.releasedWorkers, 3U);
  EXPECT_EQ(profile.baseConstruction.dormitoryLevel, 2U);
  EXPECT_EQ(profile.basePopulation.bedCapacity, 14U);
  EXPECT_FALSE(profile.baseConstruction.activeProject.has_value());
  EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, started + 360U);
  EXPECT_FALSE(applyBaseConstructionThrough(profile, publishedContentRegistry())
                   .completed);
}

TEST(BaseConstructionDomainTest,
     FirstFacilityConstructionCompletesIntoSpatialReserve) {
  const ContentRegistry &content = publishedContentRegistry();
  ProfileState profile = makeNewAlphaProfile(
      "construction-kitchen-reserve", content);
  profile.baseConstruction.materialUnits = 5U;

  const BaseConstructionReceipt started = executeStartBaseConstruction(
      profile, content, StartBaseConstructionCommand{kKitchenWaterBuild},
      CommandContext{profile.revision, "start-kitchen-water"});
  ASSERT_TRUE(started.succeeded) << started.message;
  static_cast<void>(advanceWorldClock(profile.worldClock, 480U));
  const BaseConstructionAdvanceResult completed =
      applyBaseConstructionThrough(profile, content);

  ASSERT_TRUE(completed.completed);
  EXPECT_EQ(completed.target, BaseFacilityUpgradeTarget::KitchenWater);
  EXPECT_EQ(profile.baseConstruction.kitchenWaterLevel, 1U);
  const BaseFacilityDefinitionId kitchenWater{
      "base_facility.kitchen_water"};
  EXPECT_EQ(
      profile.baseConstruction.facilities.at(kitchenWater),
      BaseConstructionState::FacilityPlacement::Reserve);
  EXPECT_EQ(
      profile.baseConstruction.facilityReserveStartedWorldMinutes.at(
          kitchenWater),
      profile.worldClock.elapsedWorldMinutes);
  for (const auto &[site, placements] :
       profile.baseFacilityLayout.placements) {
    static_cast<void>(site);
    EXPECT_TRUE(placements.contains(kitchenWater));
  }
  EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(BaseConstructionDomainTest,
     CatalogProjectsPublishedDefinitionsInStableOrder) {
  const ContentRegistry &content = publishedContentRegistry();
  ProfileState profile = makeNewAlphaProfile(
      "construction-catalog-order", content);
  profile.baseConstruction.materialUnits = 100U;

  const auto catalog = projectBaseConstructionCatalog(profile, content);
  ASSERT_EQ(catalog.size(), content.baseConstructionProjects().size());
  for (std::size_t index{}; index < catalog.size(); ++index) {
    EXPECT_EQ(catalog[index].definitionId,
              content.baseConstructionProjects()[index].id);
    EXPECT_EQ(catalog[index].displayName,
              content.baseConstructionProjects()[index].displayName);
    EXPECT_EQ(catalog[index].action, BaseConstructionCatalogAction::Start);
    EXPECT_TRUE(catalog[index].canCommit);
  }
}

TEST(BaseConstructionDomainTest,
     CatalogUsesQueryResultsForActiveBlockedAndCompleteProjects) {
  const ContentRegistry &content = publishedContentRegistry();
  ProfileState profile = makeNewAlphaProfile(
      "construction-catalog-state", content);
  profile.baseConstruction.materialUnits = 100U;
  ASSERT_TRUE(executeStartBaseConstruction(
      profile, content, StartBaseConstructionCommand{kKitchenWaterBuild},
      CommandContext{profile.revision, "catalog-start-kitchen"}).succeeded);

  auto catalog = projectBaseConstructionCatalog(profile, content);
  auto kitchen = std::find_if(
      catalog.begin(), catalog.end(),
      [](const BaseConstructionCatalogEntry &entry) {
        return entry.definitionId == kKitchenWaterBuild;
      });
  auto dormitory = std::find_if(
      catalog.begin(), catalog.end(),
      [](const BaseConstructionCatalogEntry &entry) {
        return entry.definitionId == kDormitoryExpansion;
      });
  ASSERT_NE(kitchen, catalog.end());
  ASSERT_NE(dormitory, catalog.end());
  EXPECT_EQ(kitchen->action, BaseConstructionCatalogAction::Cancel);
  EXPECT_TRUE(kitchen->canCommit);
  EXPECT_EQ(kitchen->remainingMinutes, 480U);
  EXPECT_EQ(dormitory->action, BaseConstructionCatalogAction::Blocked);
  EXPECT_FALSE(dormitory->canCommit);
  EXPECT_EQ(dormitory->message,
            "another Base construction project is active");

  static_cast<void>(advanceWorldClock(profile.worldClock, 480U));
  ASSERT_TRUE(applyBaseConstructionThrough(profile, content).completed);
  catalog = projectBaseConstructionCatalog(profile, content);
  kitchen = std::find_if(
      catalog.begin(), catalog.end(),
      [](const BaseConstructionCatalogEntry &entry) {
        return entry.definitionId == kKitchenWaterBuild;
      });
  ASSERT_NE(kitchen, catalog.end());
  EXPECT_EQ(kitchen->action, BaseConstructionCatalogAction::Complete);
  EXPECT_FALSE(kitchen->canCommit);
}

TEST(BaseConstructionDomainTest, CancelRefundsMaterialWithoutRewindingTime) {
  ProfileState profile =
      makeNewAlphaProfile("construction-cancel", publishedContentRegistry());
  grantConstructionMaterial(profile);
  ASSERT_TRUE(executeStartBaseConstruction(
                  profile, publishedContentRegistry(),
                  StartBaseConstructionCommand{kDormitoryExpansion},
                  CommandContext{profile.revision, "start-dormitory"})
                  .succeeded);
  static_cast<void>(advanceWorldClock(profile.worldClock, 120U));
  const std::uint64_t cancellationTime = profile.worldClock.elapsedWorldMinutes;

  const BaseConstructionReceipt cancelled = executeCancelBaseConstruction(
      profile, publishedContentRegistry(),
      CancelBaseConstructionCommand{kDormitoryExpansion},
      CommandContext{profile.revision, "cancel-dormitory"});
  EXPECT_TRUE(cancelled.succeeded);
  EXPECT_EQ(profile.baseConstruction.materialUnits, 4U);
  EXPECT_FALSE(profile.baseConstruction.activeProject.has_value());
  EXPECT_EQ(profile.worldClock.elapsedWorldMinutes, cancellationTime);
  EXPECT_EQ(profile.basePopulation.bedCapacity, 10U);
}

TEST(BaseConstructionDomainTest, WorkshopAndMedicalUseTypedLinearUpgrades) {
  for (const auto &[definitionId, target] : std::array{
           std::pair{BaseConstructionProjectDefinitionId{
                         "base_construction.workshop.level_2"},
                     BaseFacilityUpgradeTarget::Workshop},
           std::pair{BaseConstructionProjectDefinitionId{
                         "base_construction.medical.level_2"},
                     BaseFacilityUpgradeTarget::Medical}}) {
    ProfileState profile = makeNewAlphaProfile(
        "typed-facility-upgrade", publishedContentRegistry());
    profile.baseConstruction.materialUnits = 6U;
    const BaseConstructionPlan plan = queryStartBaseConstruction(
        profile, publishedContentRegistry(),
        StartBaseConstructionCommand{definitionId});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.target, target);
    EXPECT_EQ(plan.currentLevel, 1U);
    EXPECT_EQ(plan.targetLevel, 2U);
    ASSERT_TRUE(executeStartBaseConstruction(
                    profile, publishedContentRegistry(),
                    StartBaseConstructionCommand{definitionId},
                    CommandContext{profile.revision, "start-typed-upgrade"})
                    .succeeded);
    static_cast<void>(advanceWorldClock(profile.worldClock, 720U));
    const BaseConstructionAdvanceResult completed =
        applyBaseConstructionThrough(profile, publishedContentRegistry());
    ASSERT_TRUE(completed.completed);
    EXPECT_EQ(completed.target, target);
    EXPECT_EQ(completed.levelBefore, 1U);
    EXPECT_EQ(completed.levelAfter, 2U);
    EXPECT_EQ(profile.basePopulation.bedCapacity, 10U);
    if (target == BaseFacilityUpgradeTarget::Workshop) {
      EXPECT_EQ(profile.baseConstruction.workshopLevel, 2U);
      EXPECT_EQ(profile.baseConstruction.medicalLevel, 1U);
    } else {
      EXPECT_EQ(profile.baseConstruction.medicalLevel, 2U);
      EXPECT_EQ(profile.baseConstruction.workshopLevel, 1U);
    }
  }
}

TEST(BaseConstructionDomainTest, RejectionsLeaveProfileUnchanged) {
  ProfileState profile =
      makeNewAlphaProfile("construction-rejected", publishedContentRegistry());
  const std::uint64_t initial = profileStateFingerprint(profile);

  EXPECT_FALSE(executeStartBaseConstruction(
                   profile, publishedContentRegistry(),
                   StartBaseConstructionCommand{kDormitoryExpansion},
                   CommandContext{profile.revision, "no-material"})
                   .succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), initial);

  const AssetInstanceId cola =
      addPendingSalvage(profile, alpha_content::lootCola);
  const std::uint64_t withCola = profileStateFingerprint(profile);
  EXPECT_FALSE(executeConstructionMaterialContribution(
                   profile, publishedContentRegistry(),
                   ContributeConstructionMaterialCommand{cola},
                   CommandContext{profile.revision, "cola-is-not-material"})
                   .succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), withCola);

  profile.revision = std::numeric_limits<ProfileRevision>::max();
  const std::uint64_t overflow = profileStateFingerprint(profile);
  EXPECT_FALSE(executeConstructionMaterialContribution(
                   profile, publishedContentRegistry(),
                   ContributeConstructionMaterialCommand{cola},
                   CommandContext{profile.revision, "overflow"})
                   .succeeded);
  EXPECT_EQ(profileStateFingerprint(profile), overflow);
}

TEST(BaseConstructionDomainTest,
     SiegeWarningBlocksNewConstructionWithoutMutation) {
  ProfileState profile = makeNewAlphaProfile(
      "construction-siege-warning", publishedContentRegistry());
  profile.baseConstruction.materialUnits = 4U;
  profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
  profile.baseSiege.safeUntilWorldMinute =
      profile.worldClock.elapsedWorldMinutes;
  ASSERT_TRUE(activateBaseSiegeWarningIfEligible(profile));
  const std::uint64_t before = profileStateFingerprint(profile);

  const BaseConstructionPlan plan = queryStartBaseConstruction(
      profile, publishedContentRegistry(),
      StartBaseConstructionCommand{kDormitoryExpansion});
  EXPECT_FALSE(plan.canCommit);
  EXPECT_EQ(plan.error, DomainErrorCode::IllegalDestination);
  const BaseConstructionReceipt receipt = executeStartBaseConstruction(
      profile, publishedContentRegistry(),
      StartBaseConstructionCommand{kDormitoryExpansion},
      CommandContext{profile.revision, "construction-during-warning"});
  EXPECT_FALSE(receipt.succeeded);
  EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
  EXPECT_EQ(profileStateFingerprint(profile), before);
}
