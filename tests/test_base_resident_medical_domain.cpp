#include <gtest/gtest.h>

#include "base_workforce_domain.h"

#include "base_construction_domain.h"
#include "base_emergency_power_domain.h"
#include "base_medical_service_domain.h"
#include "base_resident_medical_domain.h"
#include "raid_optics_legacy_fixture.h"

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

AssetInstanceId createCabinetAsset(
    ProfileState &profile, const ItemDefinitionId &id, GridPosition origin)
{
    return profile.assets.create(publishedContentRegistry().item(id),
        StoredAssetLocation{ProfileContainerId::medicalReserve(), origin});
}

void injure(ProfileState &profile, BaseResidentProfession profession)
{
    ++profile.basePopulation.injuredResidents;
    ++profile.basePopulation.injuredByProfession[baseProfessionIndex(profession)];
}
}

TEST(BaseResidentMedicalDomainTest,
     AuthorizedWholeItemsArePreviewedAndConsumedAtomically)
{
    const auto historical = ContentRegistry::fromJson(contentBeforeHospitalMedicalReserveJson().dump());
    ProfileState profile = makeNewAlphaProfile(
        "resident-treatment-start", historical);
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    const ItemDefinitionId toiletPaper{"item.loot.toilet_paper"};
    const AssetInstanceId first = createStashAsset(profile, toiletPaper, 2U);
    const AssetInstanceId second = createStashAsset(profile, toiletPaper, 2U);
    profile.baseSupplyPolicy.assignments[toiletPaper] =
        BaseSupplyCategory::Medical;

    const ResidentTreatmentPlan plan = queryStartResidentTreatment(
        profile, historical);
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
        historical,
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
        historical,
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
    ASSERT_NE(createCabinetAsset(profile, kHospitalFirstAidStockId, {0, 0}), 0U);
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

TEST(BaseResidentMedicalDomainTest, CabinetMovesPreserveOriginalInstanceAndRejectOccupiedCells)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("resident-cabinet-transfer", content);
    const ItemDefinitionId roll{"item.medical.bandage_roll_basic"};
    const auto medicine = createStashAsset(profile, roll, 1U);
    const auto source = std::get<StoredAssetLocation>(profile.assets.find(medicine)->location);
    profile.assets.findMutable(medicine)->remainingCharges = 1U;
    const auto occupied = createCabinetAsset(profile, kHospitalFirstAidStockId, {0, 0});
    ASSERT_NE(occupied, 0U);
    const StoredAssetLocation blocked{ProfileContainerId::medicalReserve(), {0, 0}};
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeMoveMedicalReserveAsset(profile, content, {medicine, source, blocked},
        {profile.revision, "cabinet-blocked"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    const StoredAssetLocation destination{ProfileContainerId::medicalReserve(), {0, 2}};
    const auto deposit = executeMoveMedicalReserveAsset(profile, content, {medicine, source, destination},
        {profile.revision, "cabinet-move"});
    ASSERT_TRUE(deposit.succeeded) << deposit.message;
    EXPECT_EQ(profile.assets.find(medicine)->remainingCharges, 1U);
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(medicine)->location), destination);
    EXPECT_TRUE(executeMoveMedicalReserveAsset(profile, content, {medicine, source, destination},
        {profile.revision, "cabinet-move"}).alreadyCommitted);

    const auto fitting = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
        content.item(roll), ItemOrientation::Degrees0);
    ASSERT_TRUE(fitting);
    const auto returned = executeMoveMedicalReserveAsset(profile, content,
        {medicine, destination, {ProfileContainerId::stash(), *fitting}}, {profile.revision, "cabinet-return"});
    ASSERT_TRUE(returned.succeeded) << returned.message;
    EXPECT_EQ(profile.assets.find(medicine)->remainingCharges, 1U);
    EXPECT_TRUE(assetIsBaseAccessible(profile, medicine));
}

TEST(BaseResidentMedicalDomainTest, TreatmentPrefersStockThenConsumesExactlyTwoCabinetCharges)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("resident-cabinet-cost", content);
    injure(profile, BaseResidentProfession::General);
    const auto personal = createStashAsset(profile, kHospitalFirstAidStockId, 1U);
    const ItemDefinitionId roll{"item.medical.bandage_roll_basic"};
    const auto twoUses = createCabinetAsset(profile, roll, {0, 0});
    const auto firstUse = createCabinetAsset(profile, roll, {1, 0});
    const auto secondUse = createCabinetAsset(profile, roll, {2, 0});
    profile.assets.findMutable(firstUse)->remainingCharges = 1U;
    profile.assets.findMutable(secondUse)->remainingCharges = 1U;
    const auto stock = createCabinetAsset(profile, kHospitalFirstAidStockId, {0, 1});

    const auto withStock = queryStartResidentTreatment(profile, content);
    ASSERT_TRUE(withStock.canCommit) << withStock.message;
    ASSERT_EQ(withStock.supplies.size(), 1U);
    EXPECT_EQ(withStock.supplies[0].assetId, stock);
    EXPECT_EQ(withStock.supplies[0].quantity, 1U);
    ASSERT_TRUE(profile.assets.erase(stock));
    const auto changed = profileStateFingerprint(profile);
    EXPECT_FALSE(executeStartResidentTreatment(profile, content,
        {withStock.patientProfession, withStock.supplies}, {profile.revision, "stale-cabinet-quote"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), changed);

    const auto chargePlan = queryStartResidentTreatment(profile, content);
    ASSERT_TRUE(chargePlan.canCommit) << chargePlan.message;
    ASSERT_EQ(chargePlan.supplies.size(), 2U);
    EXPECT_EQ(chargePlan.supplies[0].assetId, firstUse);
    EXPECT_EQ(chargePlan.supplies[1].assetId, secondUse);
    EXPECT_EQ(chargePlan.supplies[0].charges, 1U);
    EXPECT_EQ(chargePlan.supplies[1].charges, 1U);
    const auto started = executeStartResidentTreatment(profile, content,
        {chargePlan.patientProfession, chargePlan.supplies}, {profile.revision, "pay-cabinet-charges"});
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(profile.residentMedical.activeTreatment);
    EXPECT_TRUE(profile.residentMedical.activeTreatment->usesMedicalReserve);
    EXPECT_EQ(profile.assets.find(firstUse), nullptr);
    EXPECT_EQ(profile.assets.find(secondUse), nullptr);
    ASSERT_NE(profile.assets.find(twoUses), nullptr);
    EXPECT_EQ(profile.assets.find(twoUses)->remainingCharges, content.item(roll).maximumCharges);
    EXPECT_NE(profile.assets.find(personal), nullptr);
}

TEST(BaseResidentMedicalDomainTest, PriorityChangesLeaveCurrentTreatmentAndApplyToNextAdmission)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("resident-cabinet-priority", content);
    profile.basePopulation.ordinaryResidents += 2U;
    profile.basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::Medical)] += 2U;
    injure(profile, BaseResidentProfession::General);
    injure(profile, BaseResidentProfession::Medical);
    injure(profile, BaseResidentProfession::Medical);
    const auto first = createCabinetAsset(profile, kHospitalFirstAidStockId, {0, 0});
    const auto second = createCabinetAsset(profile, kHospitalFirstAidStockId, {2, 0});
    const auto start = executeStartResidentTreatment(profile, content, {}, {profile.revision, "manual-priority"});
    ASSERT_TRUE(start.succeeded) << start.message;
    ASSERT_TRUE(profile.residentMedical.activeTreatment);
    const auto current = *profile.residentMedical.activeTreatment;
    EXPECT_EQ(current.patientProfession, BaseResidentProfession::Medical);
    EXPECT_EQ(profile.assets.find(first), nullptr);

    auto policy = profile.residentMedicalReservePolicy;
    policy.automaticAdmission = true;
    std::swap(policy.professionPriority.front(), policy.professionPriority.back());
    const auto changed = executeSetResidentMedicalReservePolicy(profile, content, {policy},
        {profile.revision, "change-cabinet-priority"});
    ASSERT_TRUE(changed.succeeded) << changed.message;
    EXPECT_EQ(profile.residentMedical.activeTreatment, current);
    EXPECT_FALSE(tryStartAutomaticResidentTreatment(profile, content));
    EXPECT_NE(profile.assets.find(second), nullptr);

    static_cast<void>(advanceWorldClock(profile.worldClock, current.completionWorldMinute - current.startedWorldMinute));
    ASSERT_TRUE(applyResidentTreatmentThrough(profile).completed);
    EXPECT_EQ(profile.basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::Medical)], 1U);
    profile.pendingRaid.emplace();
    EXPECT_FALSE(tryStartAutomaticResidentTreatment(profile, content));
    EXPECT_NE(profile.assets.find(second), nullptr);
    profile.pendingRaid.reset();
    ASSERT_TRUE(tryStartAutomaticResidentTreatment(profile, content));
    ASSERT_TRUE(profile.residentMedical.activeTreatment);
    EXPECT_EQ(profile.residentMedical.activeTreatment->patientProfession, BaseResidentProfession::General);
    EXPECT_EQ(profile.residentMedical.activeTreatment->startedWorldMinute, profile.worldClock.elapsedWorldMinutes);
    EXPECT_EQ(profile.assets.find(second), nullptr);
}

TEST(BaseResidentMedicalDomainTest, CabinetStockIsExcludedFromDailySupplyAndManufacturing)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("resident-cabinet-supply-isolation", content);
    const auto stock = createCabinetAsset(profile, kHospitalFirstAidStockId, {0, 0});
    const ItemDefinitionId roll{"item.medical.bandage_roll_basic"};
    const auto bandage = createCabinetAsset(profile, roll, {2, 0});
    profile.baseSupplyPolicy.assignments[roll] = BaseSupplyCategory::Medical;
    profile.baseResources.pool.hygiene = 0U;
    profile.currentHealth = 90;
    static_cast<void>(advanceWorldClock(profile.worldClock, kWorldMinutesPerDay));
    const auto beforeBandage = *profile.assets.find(bandage);
    const auto beforeStock = *profile.assets.find(stock);

    const auto demand = applyBaseDailyDemandWithSupplyThrough(profile, content,
        projectWorldClock(profile.worldClock).completedDays);
    EXPECT_GT(demand.latestShortfall.hygiene, 0U);
    EXPECT_FALSE(queryStartBaseManufacturing(profile, content,
        {BaseManufacturingRecipeDefinitionId{"base_manufacturing.bandage_roll_basic_repack"}}).canCommit);
    const auto paid = executeBaseMedicalService(profile, content, {},
        {profile.revision, "paid-care-keeps-cabinet"});
    ASSERT_TRUE(paid.succeeded) << paid.message;
    EXPECT_EQ(profile.currentHealth, 100);
    ASSERT_NE(profile.assets.find(stock), nullptr);
    ASSERT_NE(profile.assets.find(bandage), nullptr);
    EXPECT_EQ(profile.assets.find(stock)->quantity, beforeStock.quantity);
    EXPECT_EQ(profile.assets.find(bandage)->remainingCharges, beforeBandage.remainingCharges);
}
