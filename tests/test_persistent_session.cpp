#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "alpha_content_ids.h"
#include "base_resource_domain.h"
#include "base_world.h"
#include "game_session.h"

namespace
{
class SessionSaveDirectory
{
public:
    SessionSaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-session-test-" + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count()))}
    {
    }

    ~SessionSaveDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

AssetInstanceId findDefinition(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            return id;
        }
    }
    return 0;
}

AssetInstanceId addPendingItem(
    ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const ItemDefinition &definition =
        publishedContentRegistry().item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::baseIntake(),
        definition,
        ItemOrientation::Degrees0);
    EXPECT_TRUE(origin.has_value());
    return profile.assets.create(
        definition,
        StoredAssetLocation{
            ProfileContainerId::baseIntake(), *origin},
        1);
}
}

TEST(PersistentSessionTest, AutosavedInventoryCommandSurvivesNewProcessSession)
{
    SessionSaveDirectory temporary;
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-profile"));
    const AssetInstanceId rifle = findDefinition(
        first.profile(),
        alpha_content::rifle);
    ASSERT_NE(rifle, 0U);

    const InventoryReceipt receipt = first.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "persistent-equip");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const std::uint64_t fingerprint = profileStateFingerprint(first.profile());

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), fingerprint);
    EXPECT_EQ(
        equippedAsset(reopened.profile(), EquipmentSlotKind::PrimaryWeapon),
        rifle);
}

TEST(PersistentSessionTest, BaseWorldClockCheckpointsWithoutRevisionChurn)
{
    SessionSaveDirectory temporary;
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-world-clock"));
    const ProfileRevision revision = first.profile().revision;

    first.advanceBaseWorldClock(60.0F);
    EXPECT_EQ(
        first.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute + 60U);
    EXPECT_EQ(first.profile().revision, revision);
    ASSERT_TRUE(first.checkpointWorldClock()) << first.persistenceMessage();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(
        reopened.profile().worldClock,
        first.profile().worldClock);
    EXPECT_EQ(reopened.profile().revision, revision);
}

TEST(PersistentSessionTest, UnsettledRaidClockRollsBackWithPreRaidSave)
{
    SessionSaveDirectory temporary;
    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.startNewProfile("rollback-world-clock"));
    ASSERT_TRUE(active.deployAlpha(99101));

    active.update(GameplayInput{}, 1.0F);
    const std::uint32_t outboundMinutes = publishedContentRegistry().map(
        MapDefinitionId{"map.v0.test"}).travel.outboundMinutes;
    EXPECT_EQ(
        active.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute + outboundMinutes + 1U);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(
        reopened.profile().worldClock,
        WorldClockState{});
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
}

TEST(PersistentSessionTest, SettledRaidCommitsElapsedWorldTime)
{
    SessionSaveDirectory temporary;
    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.startNewProfile("settled-world-clock"));
    ASSERT_TRUE(active.deployAlpha(99102));
    active.update(GameplayInput{}, 1.0F);
    ASSERT_TRUE(active.activeQuitAlphaRaid());
    const RaidTravelDefinition travel = publishedContentRegistry().map(
        MapDefinitionId{"map.v0.test"}).travel;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(
        reopened.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute + travel.outboundMinutes + 1U +
            travel.returnMinutes);
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
}

TEST(PersistentSessionTest, EnvironmentObjectiveAdvancesWithoutBlockingPlay)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("tutorial-profile"));

    session.noteBaseFacility(BaseFacilityKind::Storage);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::PrepareLoadout);

    const AssetInstanceId rifle = findDefinition(
        session.profile(),
        alpha_content::rifle);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "tutorial-equip").succeeded);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::FindRaidGate);

    session.noteBaseFacility(BaseFacilityKind::RaidGate);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::Complete);
}

TEST(PersistentSessionTest, SaveFailureDoesNotSwapCandidateIntoMemory)
{
    SessionSaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }

    GameSession session;
    session.configurePersistence(invalidDirectory);
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const AssetInstanceId rifle = findDefinition(
        session.profile(),
        alpha_content::rifle);

    const InventoryReceipt receipt = session.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}

TEST(PersistentSessionTest, BasePrioritySubmissionPersistsAcrossProcess)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "persistent-base-priority",
        publishedContentRegistry());
    const AssetInstanceId cola = addPendingItem(
        initial,
        alpha_content::lootCola);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    const BasePriorityReceipt receipt =
        active.executeBasePrioritySubmission(
            cola,
            "persistent-fulfill-base-priority");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(active.profile().basePriority.fulfilled);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(reopened.profile().basePriority.fulfilled);
    EXPECT_EQ(reopened.profile().assets.find(cola), nullptr);
    EXPECT_EQ(
        reopened.profile().baseResources.pool.morale,
        52U);
}

TEST(PersistentSessionTest, BasePrioritySaveFailurePreservesMemory)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-base-priority-save",
        publishedContentRegistry());
    const AssetInstanceId cola = addPendingItem(
        initial,
        alpha_content::lootCola);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const BasePriorityReceipt receipt =
        session.executeBasePrioritySubmission(
            cola,
            "priority-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_NE(session.profile().assets.find(cola), nullptr);
    EXPECT_FALSE(session.profile().basePriority.fulfilled);
}

TEST(PersistentSessionTest, GunsmithMaintenancePersistsImmediately)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "persistent-gunsmith",
        publishedContentRegistry());
    const AssetInstanceId rifle = findDefinition(
        initial, alpha_content::rifle);
    ASSERT_NE(rifle, 0U);
    AssetRecord *damaged = initial.assets.findMutable(rifle);
    ASSERT_NE(damaged, nullptr);
    initial.currency = 1000U;
    damaged->currentDurability = 3000U;
    damaged->currentMaximumDurability = 4500U;
    damaged->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    const AssetLocation originalLocation = damaged->location;
    const std::uint64_t originalWorldMinute =
        initial.worldClock.elapsedWorldMinutes;

    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    const GunsmithMaintenanceReceipt receipt =
        active.executeBaseGunsmithMaintenance(
            rifle,
            "persistent-instant-gunsmith");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.weaponAssetId, rifle);
    EXPECT_EQ(receipt.currencyPaid, 220U);
    EXPECT_FALSE(active.profile().gunsmithMaintenanceJob.has_value());
    EXPECT_EQ(
        active.profile().worldClock.elapsedWorldMinutes,
        originalWorldMinute);
    EXPECT_EQ(active.profile().assets.find(rifle)->location, originalLocation);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    const AssetRecord *restored = reopened.profile().assets.find(rifle);
    ASSERT_NE(restored, nullptr);
    const std::uint32_t factoryDurability = publishedContentRegistry()
        .item(alpha_content::rifle)
        .weaponCondition->maximumDurabilityCenti;
    EXPECT_EQ(restored->currentDurability, factoryDurability);
    EXPECT_EQ(restored->currentMaximumDurability, factoryDurability);
    EXPECT_EQ(restored->weaponMalfunction, WeaponMalfunctionType::None);
    EXPECT_EQ(restored->location, originalLocation);
    EXPECT_EQ(reopened.profile().currency, 780U);
    EXPECT_EQ(
        reopened.profile().worldClock.elapsedWorldMinutes,
        originalWorldMinute);
    EXPECT_FALSE(reopened.profile().gunsmithMaintenanceJob.has_value());
}

AssetInstanceId addStashItem(
    ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const ItemDefinition &definition =
        publishedContentRegistry().item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    EXPECT_TRUE(origin.has_value());
    return profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        1);
}

TEST(PersistentSessionTest, ConstructionCommandsPersistAcrossProcessSessions)
{
    SessionSaveDirectory temporary;
    ProfileState prepared = makeNewAlphaProfile(
        "persistent-construction-commands",
        publishedContentRegistry());
    const AssetInstanceId scrap = addPendingItem(
        prepared,
        ItemDefinitionId{"item.loot.scrap_parts"});
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        prepared,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.continueProfile()) << first.persistenceMessage();
    ASSERT_TRUE(first.executeConstructionMaterialContribution(
        scrap,
        "persistent-process-material").succeeded);
    ASSERT_TRUE(first.executeStartBaseConstruction(
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        "persistent-start-construction").succeeded);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.profile().baseConstruction.activeProject.has_value());
    EXPECT_EQ(reopened.profile().baseConstruction.materialUnits, 0U);

    ASSERT_TRUE(reopened.executeCancelBaseConstruction(
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        "persistent-cancel-construction").succeeded);
    GameSession cancelled;
    cancelled.configurePersistence(temporary.path());
    ASSERT_TRUE(cancelled.continueProfile()) << cancelled.persistenceMessage();
    EXPECT_FALSE(cancelled.profile().baseConstruction.activeProject.has_value());
    EXPECT_EQ(cancelled.profile().baseConstruction.materialUnits, 4U);
}

TEST(PersistentSessionTest, WorkforceAssignmentsPersistAcrossProcessSessions)
{
    SessionSaveDirectory temporary;
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-workforce"));

    ASSERT_TRUE(first.executeClearBaseWorker(
        BaseFacilityStaffingKind::Workshop,
        "persistent-clear-workshop-worker").succeeded);

    GameSession cleared;
    cleared.configurePersistence(temporary.path());
    ASSERT_TRUE(cleared.continueProfile()) << cleared.persistenceMessage();
    EXPECT_FALSE(cleared.profile().baseWorkforce.workshopWorker.has_value());

    ASSERT_TRUE(cleared.executeAssignBestBaseWorker(
        BaseFacilityStaffingKind::Workshop,
        "persistent-assign-workshop-worker").succeeded);

    GameSession assigned;
    assigned.configurePersistence(temporary.path());
    ASSERT_TRUE(assigned.continueProfile()) << assigned.persistenceMessage();
    ASSERT_TRUE(assigned.profile().baseWorkforce.workshopWorker.has_value());
    EXPECT_EQ(
        *assigned.profile().baseWorkforce.workshopWorker,
        BaseResidentProfession::Engineering);
}

TEST(PersistentSessionTest, BaseClockCompletesAndPersistsDormitoryExpansion)
{
    SessionSaveDirectory temporary;
    ProfileState prepared = makeNewAlphaProfile(
        "persistent-construction-completion",
        publishedContentRegistry());
    prepared.baseConstruction.activeProject =
        ActiveBaseConstructionProject{
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"},
            4U,
            3U,
            prepared.worldClock.elapsedWorldMinutes,
            prepared.worldClock.elapsedWorldMinutes + 360U};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        prepared,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const ProfileRevision startedRevision = session.profile().revision;
    session.advanceBaseWorldClock(359.0F);
    EXPECT_TRUE(session.profile().baseConstruction.activeProject.has_value());
    session.advanceBaseWorldClock(1.0F);
    EXPECT_FALSE(session.profile().baseConstruction.activeProject.has_value());
    EXPECT_EQ(session.profile().baseConstruction.dormitoryLevel, 2U);
    EXPECT_EQ(session.profile().basePopulation.bedCapacity, 14U);
    EXPECT_EQ(session.profile().revision, startedRevision + 1U);
    ASSERT_TRUE(session.checkpointWorldClock()) << session.persistenceMessage();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().baseConstruction.dormitoryLevel, 2U);
    EXPECT_EQ(reopened.profile().basePopulation.bedCapacity, 14U);
}

TEST(PersistentSessionTest, BaseClockCompletesAndPersistsWorkshopUpgrade)
{
    SessionSaveDirectory temporary;
    ProfileState prepared = makeNewAlphaProfile(
        "persistent-workshop-upgrade",
        publishedContentRegistry());
    prepared.baseConstruction.materialUnits = 6U;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        prepared,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    ASSERT_TRUE(session.executeStartBaseConstruction(
        BaseConstructionProjectDefinitionId{
            "base_construction.workshop.level_2"},
        "persistent-start-workshop-upgrade").succeeded);

    session.advanceBaseWorldClock(719.0F);
    EXPECT_EQ(session.profile().baseConstruction.workshopLevel, 1U);
    EXPECT_TRUE(session.profile().baseConstruction.activeProject.has_value());
    session.advanceBaseWorldClock(1.0F);
    EXPECT_EQ(session.profile().baseConstruction.workshopLevel, 2U);
    EXPECT_FALSE(session.profile().baseConstruction.activeProject.has_value());
    ASSERT_TRUE(session.checkpointWorldClock()) << session.persistenceMessage();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().baseConstruction.workshopLevel, 2U);
    EXPECT_FALSE(reopened.profile().baseConstruction.activeProject.has_value());
}

TEST(PersistentSessionTest, ConstructionSaveFailurePreservesInMemoryProfile)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-construction-save",
        publishedContentRegistry());
    initial.baseConstruction.materialUnits = 4U;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "construction-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const BaseConstructionReceipt receipt =
        session.executeStartBaseConstruction(
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"},
            "construction-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().baseConstruction.activeProject.has_value());
    EXPECT_EQ(session.profile().baseConstruction.materialUnits, 4U);
}

TEST(PersistentSessionTest,
     ConstructionCompletionSaveFailurePreservesProjectClockAndBeds)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-construction-completion-save",
        publishedContentRegistry());
    initial.baseConstruction.activeProject =
        ActiveBaseConstructionProject{
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"},
            4U,
            3U,
            initial.worldClock.elapsedWorldMinutes,
            initial.worldClock.elapsedWorldMinutes + 360U};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "completion-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    session.advanceBaseWorldClock(360.0F);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    ASSERT_TRUE(session.profile().baseConstruction.activeProject.has_value());
    EXPECT_EQ(session.profile().baseConstruction.dormitoryLevel, 1U);
    EXPECT_EQ(session.profile().basePopulation.bedCapacity, 10U);
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        initial.worldClock.elapsedWorldMinutes);
    EXPECT_FALSE(session.persistenceMessage().empty());
}

TEST(PersistentSessionTest, GunsmithSaveFailurePreservesInMemoryProfile)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-gunsmith-save",
        publishedContentRegistry());
    const AssetInstanceId rifle = findDefinition(
        initial, alpha_content::rifle);
    ASSERT_NE(rifle, 0U);
    AssetRecord *damaged = initial.assets.findMutable(rifle);
    ASSERT_NE(damaged, nullptr);
    initial.currency = 1000U;
    damaged->currentDurability = 3000U;
    damaged->currentMaximumDurability = 4500U;

    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());

    const std::filesystem::path invalidDirectory =
        temporary.path() / "not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);
    const GunsmithMaintenanceReceipt receipt =
        session.executeBaseGunsmithMaintenance(
            rifle,
            "gunsmith-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().gunsmithMaintenanceJob.has_value());
    const AssetRecord *unchanged = session.profile().assets.find(rifle);
    ASSERT_NE(unchanged, nullptr);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        unchanged->location));
}

TEST(PersistentSessionTest, PaidBaseMedicalServicePersistsAcrossProcess)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "persistent-paid-base-medical",
        publishedContentRegistry());
    initial.currency = 1000U;
    initial.currentHealth = 55;
    initial.medicalStatus = MedicalStatusState{
        BleedingSeverity::Heavy, 0U, 400U, 80000U, 12000U};
    const AssetInstanceId medkit = findDefinition(
        initial, alpha_content::medkit);
    ASSERT_NE(medkit, 0U);
    const std::uint32_t medkitCharges =
        initial.assets.find(medkit)->remainingCharges;
    const AssetInstanceId nextAssetId = initial.assets.nextAssetId();

    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    const BaseMedicalServiceReceipt receipt =
        active.executeBasePaidMedicalService(
            "persistent-paid-base-medical-treatment");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.currencyPaid, 195U);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().currency, 805U);
    EXPECT_EQ(reopened.profile().currentHealth, 100);
    EXPECT_EQ(
        reopened.profile().medicalStatus.bleeding,
        BleedingSeverity::None);
    EXPECT_EQ(
        reopened.profile().medicalStatus.painkillerRemainingMs,
        80000U);
    EXPECT_EQ(reopened.profile().assets.nextAssetId(), nextAssetId);
    ASSERT_NE(reopened.profile().assets.find(medkit), nullptr);
    EXPECT_EQ(
        reopened.profile().assets.find(medkit)->remainingCharges,
        medkitCharges);
}

TEST(PersistentSessionTest, PaidBaseMedicalSaveFailurePreservesProfile)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-paid-base-medical-save",
        publishedContentRegistry());
    initial.currency = 1000U;
    initial.currentHealth = 70;
    initial.medicalStatus = MedicalStatusState{
        BleedingSeverity::Light, 30000U, 800U, 0U, 12000U};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const BaseMedicalServiceReceipt receipt =
        session.executeBasePaidMedicalService(
            "paid-base-medical-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}

TEST(PersistentSessionTest, BaseRestPersistsClockPopulationAndDailyNeeds)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("persistent-base-rest"));

    ASSERT_TRUE(session.executeBaseRest(12, "rest-to-evening").succeeded);
    const BaseRestReceipt crossed = session.executeBaseRest(
        6, "rest-across-midnight");
    ASSERT_TRUE(crossed.succeeded) << crossed.message;
    EXPECT_EQ(crossed.dailyCyclesResolved, 1U);
    EXPECT_EQ(
        session.profile().baseResources.pool,
        (BaseResourceBundle{32, 34, 35, 36}));
    const std::uint64_t fingerprint = profileStateFingerprint(
        session.profile());

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), fingerprint);
    EXPECT_EQ(
        reopened.profile().basePopulation,
        (BasePopulationState{8, 10}));
    EXPECT_EQ(reopened.worldClockProjection().day, 2U);
    EXPECT_EQ(reopened.worldClockProjection().hour, 2U);
}

TEST(PersistentSessionTest, BaseRestSaveFailurePreservesProfile)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("failed-base-rest-save"));
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const BaseRestReceipt receipt = session.executeBaseRest(
        12, "base-rest-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}

TEST(PersistentSessionTest,
     ResidentTreatmentPersistsAndRestCompletesRecovery)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "persistent-resident-treatment",
        publishedContentRegistry());
    initial.basePopulation.injuredResidents = 1U;
    initial.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    initial.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.medical.medkit_alpha"},
        BaseSupplyCategory::Medical);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const ResidentTreatmentReceipt started =
        session.executeStartResidentTreatment(
            "persistent-start-resident-treatment");
    ASSERT_TRUE(started.succeeded) << started.message;
    ASSERT_TRUE(session.profile().residentMedical.activeTreatment.has_value());

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.profile().residentMedical.activeTreatment.has_value());
    ASSERT_TRUE(reopened.executeBaseRest(
        6U, "rest-completes-resident-treatment").succeeded);
    EXPECT_EQ(reopened.profile().basePopulation.injuredResidents, 0U);
    EXPECT_FALSE(reopened.profile().residentMedical.activeTreatment.has_value());

    GameSession completed;
    completed.configurePersistence(temporary.path());
    ASSERT_TRUE(completed.continueProfile()) << completed.persistenceMessage();
    EXPECT_EQ(completed.profile().basePopulation.injuredResidents, 0U);
    EXPECT_FALSE(completed.profile().residentMedical.activeTreatment.has_value());
}

TEST(PersistentSessionTest,
     ResidentTreatmentSaveFailurePreservesAssetsAndPopulation)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-resident-treatment-save",
        publishedContentRegistry());
    initial.basePopulation.injuredResidents = 1U;
    initial.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    initial.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.medical.medkit_alpha"},
        BaseSupplyCategory::Medical);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "resident-treatment-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const ResidentTreatmentReceipt receipt =
        session.executeStartResidentTreatment(
            "resident-treatment-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().residentMedical.activeTreatment.has_value());
    EXPECT_EQ(session.profile().basePopulation.injuredResidents, 1U);
}

TEST(PersistentSessionTest,
     ManufacturingStartCompletionAndRealOutputPersistAcrossProcesses)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "persistent-manufacturing",
        publishedContentRegistry());
    addStashItem(initial, ItemDefinitionId{"item.loot.scrap_parts"});
    addStashItem(initial, ItemDefinitionId{"item.loot.electronics"});
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession started;
    started.configurePersistence(temporary.path());
    ASSERT_TRUE(started.continueProfile()) << started.persistenceMessage();
    const BaseManufacturingReceipt receipt =
        started.executeStartBaseManufacturing(
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"},
            "persistent-start-manufacturing");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;

    GameSession processing;
    processing.configurePersistence(temporary.path());
    ASSERT_TRUE(processing.continueProfile()) << processing.persistenceMessage();
    ASSERT_TRUE(processing.profile().baseManufacturing.activeOrder.has_value());
    processing.advanceBaseWorldClock(360.0F);
    EXPECT_FALSE(processing.profile().baseManufacturing.activeOrder.has_value());

    GameSession completed;
    completed.configurePersistence(temporary.path());
    ASSERT_TRUE(completed.continueProfile()) << completed.persistenceMessage();
    const AssetRecord *output = completed.profile().assets.find(
        *receipt.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(
        output->definitionId,
        ItemDefinitionId{"item.maintenance.weapon_kit_basic"});
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
}

TEST(PersistentSessionTest,
     ManufacturingSaveFailurePreservesInputsAndStableIdentities)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "failed-manufacturing-save",
        publishedContentRegistry());
    addStashItem(initial, ItemDefinitionId{"item.loot.scrap_parts"});
    addStashItem(initial, ItemDefinitionId{"item.loot.electronics"});
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "manufacturing-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const BaseManufacturingReceipt receipt =
        session.executeStartBaseManufacturing(
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"},
            "manufacturing-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().baseManufacturing.activeOrder.has_value());
}

TEST(PersistentSessionTest, LegacyPendingRaidSaveRestoresLoadoutWithoutLoss)
{
    SessionSaveDirectory temporary;
    ProfileState legacy = makeNewAlphaProfile(
        "legacy-pending-profile",
        publishedContentRegistry());
    const AssetInstanceId rifle = findDefinition(
        legacy, alpha_content::rifle);
    ASSERT_TRUE(executeInventory(
        legacy,
        publishedContentRegistry(),
        InventoryEquipCommand{
            rifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{legacy.revision, "legacy-equip-rifle"}).succeeded);
    ASSERT_TRUE(executeDeploy(
        legacy,
        publishedContentRegistry(),
        DeployCommand{
            "legacy-pending-raid",
            "legacy-pending-settlement",
            88771,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{legacy.revision, "legacy-deploy"}).succeeded);
    ASSERT_TRUE(legacy.pendingRaid.has_value());
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        legacy,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();

    EXPECT_TRUE(reopened.recoveredAbandonedRaid());
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
    EXPECT_EQ(equippedAsset(
        reopened.profile(), EquipmentSlotKind::PrimaryWeapon), rifle);
    EXPECT_TRUE(reopened.profile().committedSettlements.empty());
    EXPECT_FALSE(reopened.profile().lastRaidResult.has_value());
}
