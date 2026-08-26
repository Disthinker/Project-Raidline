#include <gtest/gtest.h>

#include "base_workforce_domain.h"

#include "base_construction_domain.h"
#include "base_resident_medical_domain.h"

namespace
{
AssetInstanceId createStashAsset(
    ProfileState &profile,
    const ItemDefinitionId &definitionId,
    std::uint32_t quantity)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ItemDefinition &definition = content.item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    EXPECT_TRUE(origin.has_value());
    return profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        quantity);
}
}

TEST(BaseResidentMedicalDomainTest,
     AuthorizedWholeItemsArePreviewedAndConsumedAtomically)
{
    ProfileState profile = makeNewAlphaProfile(
        "resident-treatment-start", publishedContentRegistry());
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    const ItemDefinitionId toiletPaper{"item.loot.toilet_paper"};
    const AssetInstanceId first = createStashAsset(profile, toiletPaper, 2U);
    const AssetInstanceId second = createStashAsset(profile, toiletPaper, 2U);
    profile.baseSupplyPolicy.assignments[toiletPaper] =
        BaseSupplyCategory::Medical;

    const ResidentTreatmentPlan plan = queryStartResidentTreatment(
        profile, publishedContentRegistry());
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_EQ(plan.supplies.size(), 2U);
    EXPECT_EQ(plan.requiredContribution, 10U);
    EXPECT_EQ(plan.plannedContribution, 12U);
    EXPECT_EQ(plan.supplies[0].assetId, first);
    EXPECT_EQ(plan.supplies[0].quantity, 2U);
    EXPECT_EQ(plan.supplies[1].assetId, second);
    EXPECT_EQ(plan.supplies[1].quantity, 2U);

    const ProfileRevision revision = profile.revision;
    const ResidentTreatmentReceipt receipt = executeStartResidentTreatment(
        profile,
        publishedContentRegistry(),
        StartResidentTreatmentCommand{},
        CommandContext{revision, "start-resident-treatment"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.consumedContribution, 12U);
    EXPECT_EQ(profile.assets.find(first), nullptr);
    EXPECT_EQ(profile.assets.find(second), nullptr);
    ASSERT_TRUE(profile.residentMedical.activeTreatment.has_value());
    EXPECT_EQ(profile.basePopulation.injuredResidents, 1U);
    EXPECT_EQ(profile.revision, revision + 1U);

    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const ResidentTreatmentReceipt replay = executeStartResidentTreatment(
        profile,
        publishedContentRegistry(),
        StartResidentTreatmentCommand{},
        CommandContext{profile.revision, "start-resident-treatment"});
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(BaseResidentMedicalDomainTest, TreatmentCompletesOnlyAtWorldTimeBoundary)
{
    ProfileState profile = makeNewAlphaProfile(
        "resident-treatment-complete", publishedContentRegistry());
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    const ItemDefinitionId medkit{"item.medical.medkit_alpha"};
    static_cast<void>(createStashAsset(profile, medkit, 1U));
    profile.baseSupplyPolicy.assignments[medkit] =
        BaseSupplyCategory::Medical;
    const ResidentTreatmentPlan plan = queryStartResidentTreatment(
        profile, publishedContentRegistry());
    ASSERT_TRUE(plan.canCommit);
    ASSERT_EQ(plan.supplies.size(), 1U);
    const AssetInstanceId consumed = plan.supplies.front().assetId;
    ASSERT_TRUE(executeStartResidentTreatment(
        profile,
        publishedContentRegistry(),
        StartResidentTreatmentCommand{},
        CommandContext{profile.revision, "start-boundary-treatment"})
                    .succeeded);
    EXPECT_EQ(profile.assets.find(consumed), nullptr);

    static_cast<void>(advanceWorldClock(profile.worldClock, 359U));
    EXPECT_FALSE(applyResidentTreatmentThrough(profile).completed);
    EXPECT_EQ(profile.basePopulation.injuredResidents, 1U);
    static_cast<void>(advanceWorldClock(profile.worldClock, 1U));
    const ResidentTreatmentAdvanceResult completed =
        applyResidentTreatmentThrough(profile);
    EXPECT_TRUE(completed.completed);
    EXPECT_EQ(profile.basePopulation.injuredResidents, 0U);
    EXPECT_FALSE(profile.residentMedical.activeTreatment.has_value());
    EXPECT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
}

TEST(BaseResidentMedicalDomainTest, RejectionsPreserveFingerprint)
{
    ProfileState profile = makeNewAlphaProfile(
        "resident-treatment-reject", publishedContentRegistry());
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    const ItemDefinitionId medkit{"item.medical.medkit_alpha"};
    static_cast<void>(createStashAsset(profile, medkit, 1U));
    profile.baseSupplyPolicy.assignments[medkit] = BaseSupplyCategory::Food;
    const std::uint64_t before = profileStateFingerprint(profile);

    const ResidentTreatmentReceipt insufficient =
        executeStartResidentTreatment(
            profile,
            publishedContentRegistry(),
            StartResidentTreatmentCommand{},
            CommandContext{profile.revision, "reject-unassigned-medical"});
    EXPECT_FALSE(insufficient.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    profile.baseSupplyPolicy.assignments[medkit] =
        BaseSupplyCategory::Medical;
    const std::uint64_t assigned = profileStateFingerprint(profile);
    const ResidentTreatmentReceipt stale = executeStartResidentTreatment(
        profile,
        publishedContentRegistry(),
        StartResidentTreatmentCommand{},
        CommandContext{profile.revision + 1U, "reject-stale-treatment"});
    EXPECT_FALSE(stale.succeeded);
    EXPECT_EQ(stale.error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), assigned);

    profile.pendingRaid = PendingRaidSnapshot{};
    EXPECT_FALSE(queryStartResidentTreatment(
        profile, publishedContentRegistry()).canCommit);
}

TEST(BaseResidentMedicalDomainTest, InjuredResidentsAreNotAvailableWorkers)
{
    ProfileState profile = makeNewAlphaProfile(
        "resident-treatment-workers", publishedContentRegistry());
    profile.basePopulation = BasePopulationState{8U, 10U, 3U};
    const BaseResidentMedicalProjection medical =
        projectBaseResidentMedical(profile);
    const BaseConstructionProjection construction = projectBaseConstruction(
        profile, publishedContentRegistry());

    EXPECT_EQ(medical.healthyResidents, 5U);
    EXPECT_EQ(construction.totalWorkers, 5U);
    EXPECT_EQ(construction.availableWorkers, 3U);
}
