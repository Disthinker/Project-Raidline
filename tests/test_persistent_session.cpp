#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "alpha_content_ids.h"
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

TEST(PersistentSessionTest, GunsmithJobPersistsAndCompletesThroughRaidTravel)
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

    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession started;
    started.configurePersistence(temporary.path());
    ASSERT_TRUE(started.continueProfile()) << started.persistenceMessage();
    const GunsmithMaintenanceReceipt startReceipt =
        started.executeBaseGunsmithMaintenance(
            rifle,
            "persistent-start-gunsmith");
    ASSERT_TRUE(startReceipt.succeeded) << startReceipt.message;
    ASSERT_TRUE(started.profile().gunsmithMaintenanceJob.has_value());
    EXPECT_EQ(started.profile().gunsmithMaintenanceJob->weaponAssetId, rifle);

    GameSession travelling;
    travelling.configurePersistence(temporary.path());
    ASSERT_TRUE(travelling.continueProfile()) << travelling.persistenceMessage();
    ASSERT_TRUE(travelling.profile().gunsmithMaintenanceJob.has_value());
    ASSERT_TRUE(travelling.deployAlpha(
        99103,
        MapDefinitionId{"map.raid.industrial"}));
    ASSERT_TRUE(travelling.activeQuitAlphaRaid());
    EXPECT_GE(
        travelling.profile().worldClock.elapsedWorldMinutes,
        travelling.profile().gunsmithMaintenanceJob->completionWorldMinute);

    const GunsmithCollectionReceipt collectReceipt =
        travelling.collectBaseGunsmithMaintenance(
            "persistent-collect-gunsmith");
    ASSERT_TRUE(collectReceipt.succeeded) << collectReceipt.message;
    EXPECT_EQ(collectReceipt.weaponAssetId, rifle);
    EXPECT_FALSE(travelling.profile().gunsmithMaintenanceJob.has_value());

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
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(
        restored->location));
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
