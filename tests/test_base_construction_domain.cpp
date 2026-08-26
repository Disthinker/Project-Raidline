#include <limits>

#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_construction_domain.h"
#include "world_clock.h"

namespace {
const BaseConstructionProjectDefinitionId kDormitoryExpansion{
    "base_construction.dormitory.level_2"};

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
