#include <gtest/gtest.h>

#include "base_workforce_domain.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "alpha_content_ids.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_resource_domain.h"
#include "base_service_domain.h"
#include "economy_domain.h"
#include "raid_lifecycle.h"
#include "raid_map_generation.h"
#include "raid_rescue_domain.h"
#include "recovery_task_domain.h"
#include "save_repository.h"
#include "weapon_ammo_domain.h"

namespace
{
class TemporarySaveDirectory
{
public:
    TemporarySaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-save-test-" + std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch().count()))}
    {
    }

    ~TemporarySaveDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::string testSaveChecksum(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4U;
    }
    return result;
}
}

TEST(SaveRepositoryTest, SchemaV11RoundTripPreservesClockResourcesPriorityAndIntake)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::armorMaintenanceKit)
        {
            profile.assets.findMutable(id)->remainingCharges = 3210;
        }
        if (asset.definitionId == alpha_content::bodyArmor)
        {
            AssetRecord *armor = profile.assets.findMutable(id);
            armor->currentMaximumDurability = 111;
            armor->currentDurability = 72;
        }
    }
    profile.baseResources = BaseResourceState{
        BaseResourceBundle{23, 45, 67, 89},
        BaseResourceBundle{1, 2, 3, 4},
        7};
    profile.worldClock.elapsedWorldMinutes =
        7U * kWorldMinutesPerDay + kInitialWorldMinute;
    static_cast<void>(synchronizeBasePriorityThrough(
        profile,
        publishedContentRegistry()));
    profile.baseMorale.resolvedDayCount =
        projectWorldClock(profile.worldClock).completedDays;
    profile.baseCommunityEvent = {};
    static_cast<void>(synchronizeBaseCommunityEventThrough(
        profile,
        publishedContentRegistry()));
    const ItemDefinition &cola = publishedContentRegistry().item(
        alpha_content::lootCola);
    static_cast<void>(profile.assets.create(
        cola,
        StoredAssetLocation{
            ProfileContainerId::baseIntake(), GridPosition{0, 0}},
        1));
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);
    const SaveLoadResult loaded = repository.load(publishedContentRegistry());

    ASSERT_EQ(loaded.status, SaveLoadStatus::LoadedPrimary);
    ASSERT_TRUE(loaded.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV11AcceptsPreviousContentVersions)
{
    for (const std::string &contentVersion : {
             std::string{"base-periodic-wishes-content-16"},
             std::string{"base-operational-readiness-content-17"},
             std::string{"base-instant-gunsmith-content-18"}})
    {
        ProfileState profile = makeNewAlphaProfile(
            "save-v11-operations-content-migration",
            publishedContentRegistry());
        profile.baseResources.pool = BaseResourceBundle{7, 18, 35, 28};
        const std::uint64_t fingerprint = profileStateFingerprint(profile);

        const SaveLoadResult loaded = deserializeProfileEnvelope(
            serializeProfileEnvelope(profile, contentVersion, 11),
            publishedContentRegistry());

        ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
        EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
        EXPECT_EQ(
            loaded.profile->baseResources.pool,
            (BaseResourceBundle{7, 18, 35, 28}));
    }
}

TEST(SaveRepositoryTest, SchemaV12PersistsPopulationAndMigratesV11Defaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v12-population", publishedContentRegistry());
    profile.basePopulation = BasePopulationState{17, 12};

    const SaveLoadResult roundTrip = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    ASSERT_TRUE(roundTrip.profile.has_value()) << roundTrip.message;
    EXPECT_EQ(
        roundTrip.profile->basePopulation,
        (BasePopulationState{17, 12}));

    profile.basePopulation = BasePopulationState{};
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, "base-paid-medical-content-19", 11),
        publishedContentRegistry());
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(
        migrated.profile->basePopulation,
        (BasePopulationState{8, 10}));
}

TEST(SaveRepositoryTest, SchemaV13PersistsRescueSnapshotAndCommittedLedger)
{
    ProfileState pending = makeNewAlphaProfile(
        "save-v13-pending-rescue", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        pending,
        publishedContentRegistry(),
        DeployCommand{
            "save-v13-raid",
            "save-v13-settlement",
            9917U,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{pending.revision, "save-v13-deploy"}).succeeded);
    ASSERT_TRUE(pending.pendingRaid.has_value());
    ASSERT_TRUE(pending.pendingRaid->rescue.has_value());

    const SaveLoadResult pendingRoundTrip = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            pending, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    ASSERT_TRUE(pendingRoundTrip.profile.has_value())
        << pendingRoundTrip.message;
    ASSERT_TRUE(pendingRoundTrip.profile->pendingRaid.has_value());
    ASSERT_TRUE(pendingRoundTrip.profile->pendingRaid->rescue.has_value());
    const RaidRescueSnapshot &loadedRescue =
        *pendingRoundTrip.profile->pendingRaid->rescue;
    const RaidRescueSnapshot &sourceRescue = *pending.pendingRaid->rescue;
    EXPECT_EQ(loadedRescue.definitionId, sourceRescue.definitionId);
    EXPECT_EQ(loadedRescue.subjectKind, sourceRescue.subjectKind);
    EXPECT_EQ(loadedRescue.transferPoint.position.x,
              sourceRescue.transferPoint.position.x);
    EXPECT_EQ(loadedRescue.transferPoint.position.y,
              sourceRescue.transferPoint.position.y);
    EXPECT_EQ(loadedRescue.transferPoint.size.x,
              sourceRescue.transferPoint.size.x);
    EXPECT_EQ(loadedRescue.transferPoint.size.y,
              sourceRescue.transferPoint.size.y);
    EXPECT_EQ(loadedRescue.interactionDurationSeconds,
              sourceRescue.interactionDurationSeconds);
    EXPECT_EQ(loadedRescue.ordinaryResidentCount,
              sourceRescue.ordinaryResidentCount);
    EXPECT_EQ(loadedRescue.secured, sourceRescue.secured);

    ProfileState committed = makeNewAlphaProfile(
        "save-v13-committed-rescue", publishedContentRegistry());
    const OrdinarySurvivorAdmissionReceipt admission =
        executeOrdinarySurvivorAdmission(
            committed,
            publishedContentRegistry(),
            OrdinarySurvivorAdmissionCommand{
                RescueDefinitionId{"rescue.ordinary.greyline_depot"},
                1U},
            CommandContext{committed.revision, "save-v13-rescue"});
    ASSERT_TRUE(admission.succeeded) << admission.message;

    const SaveLoadResult committedRoundTrip = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            committed, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    ASSERT_TRUE(committedRoundTrip.profile.has_value())
        << committedRoundTrip.message;
    EXPECT_EQ(
        committedRoundTrip.profile->basePopulation,
        (BasePopulationState{9U, 10U}));
    EXPECT_TRUE(committedRoundTrip.profile->committedRescues.contains(
        RescueDefinitionId{"rescue.ordinary.greyline_depot"}));
}

TEST(SaveRepositoryTest, SchemaV13MigratesSchemaV12WithEmptyRescueLedger)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v12-rescue-migration", publishedContentRegistry());
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-residents-beds-sleep-content-20",
            12),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_TRUE(migrated.profile->committedRescues.empty());
    EXPECT_EQ(
        migrated.profile->basePopulation,
        (BasePopulationState{8U, 10U}));
}

TEST(SaveRepositoryTest, SchemaV10MigratesCurrentBasePriority)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v10-priority-migration",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes = 8000U;
    static_cast<void>(synchronizeBasePriorityThrough(
        profile, publishedContentRegistry()));

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-gunsmith-service-content-15",
            10),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(loaded.profile->basePriority.cycleIndex, 1U);
    EXPECT_EQ(
        loaded.profile->basePriority.definitionId,
        BasePriorityDefinitionId{"base_priority.reinforce_perimeter"});
    EXPECT_FALSE(loaded.profile->basePriority.fulfilled);
}

TEST(SaveRepositoryTest, SchemaV10RoundTripsActiveGunsmithJob)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v10-gunsmith",
        publishedContentRegistry());
    profile.currency = 1000;
    AssetInstanceId rifleId{};
    AssetInstanceId magazineId{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifleId = id;
        }
        else if (asset.definitionId == alpha_content::magazine &&
                 magazineId == 0)
        {
            magazineId = id;
        }
    }
    ASSERT_NE(rifleId, 0U);
    ASSERT_NE(magazineId, 0U);
    AssetRecord *rifle = profile.assets.findMutable(rifleId);
    rifle->currentMaximumDurability = 7600;
    rifle->currentDurability = 4300;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    profile.assets.findMutable(magazineId)->location =
        InstalledMagazineLocation{rifleId};
    const StoredAssetLocation returnLocation =
        std::get<StoredAssetLocation>(rifle->location);
    const BaseServiceJobId jobId = profile.nextBaseServiceJobId++;
    rifle->location = BaseServiceAssetLocation{jobId};
    profile.gunsmithMaintenanceJob = GunsmithMaintenanceJob{
        jobId,
        rifleId,
        returnLocation.origin,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 240U,
        130U,
        10000U};
    ASSERT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->nextBaseServiceJobId,
              profile.nextBaseServiceJobId);
    EXPECT_EQ(loaded.profile->gunsmithMaintenanceJob,
              profile.gunsmithMaintenanceJob);
    EXPECT_EQ(installedMagazine(*loaded.profile, rifleId), magazineId);
}

TEST(SaveRepositoryTest, SchemaV9MigratesWithoutBaseServiceJob)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v9-service-migration",
        publishedContentRegistry());

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-travel-time-content-14",
            9),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->nextBaseServiceJobId, 1U);
    EXPECT_FALSE(migrated.profile->gunsmithMaintenanceJob.has_value());
}

TEST(SaveRepositoryTest, SchemaV8AcceptsPreviousContentAndClockState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v8-content-migration",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        3U * kWorldMinutesPerDay + 17U * 60U + 25U;
    profile.baseResources.resolvedDemandCycleCount = 3U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-resource-pressure-content-13",
            8),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->worldClock, profile.worldClock);
    EXPECT_EQ(migrated.profile->baseResources, profile.baseResources);
}

TEST(SaveRepositoryTest, SchemaV8PendingRaidMigratesToZeroTravelRollback)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v8-pending-travel-migration",
        publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "legacy-raid",
            "legacy-settlement",
            91231,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "legacy-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    profile.pendingRaid->rulesVersion = "raid-conditional-extraction-3";
    profile.worldClock = profile.pendingRaid->travel.startingWorldClock;
    profile.baseResources = profile.pendingRaid->travel.startingBaseResources;
    const WorldClockState expectedClock = profile.worldClock;
    const BaseResourceState expectedResources = profile.baseResources;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-resource-pressure-content-13",
            8),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    ASSERT_TRUE(migrated.profile->pendingRaid.has_value());
    EXPECT_EQ(migrated.profile->pendingRaid->travel.outboundMinutes, 0U);
    EXPECT_EQ(migrated.profile->pendingRaid->travel.startingWorldClock,
              expectedClock);
    ProfileState rolledBack = *migrated.profile;
    ASSERT_TRUE(rollbackPendingRaidToBase(
        rolledBack, publishedContentRegistry()).succeeded);
    EXPECT_EQ(rolledBack.worldClock, expectedClock);
    EXPECT_EQ(rolledBack.baseResources, expectedResources);
}

TEST(SaveRepositoryTest, SchemaV9RoundTripsSettlementTravelReceipt)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v9-settlement-travel",
        publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-travel-raid",
            "save-travel-settlement",
            91232,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "save-travel-deploy"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(
        profile,
        publishedContentRegistry(),
        "save-travel-settlement",
        RaidResultOutcome::ActiveQuit).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->lastRaidResult.has_value());
    EXPECT_EQ(loaded.profile->lastRaidResult->travelMinutesApplied, 45U);
}

TEST(SaveRepositoryTest, SchemaV7MigratesToInitialClockWithoutReplayingRaids)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v7-world-clock-migration",
        publishedContentRegistry());
    profile.baseResources.pool = BaseResourceBundle{17, 19, 23, 29};
    profile.baseResources.lastShortfall = BaseResourceBundle{1, 2, 3, 4};

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            7),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->worldClock, WorldClockState{});
    EXPECT_EQ(
        migrated.profile->baseResources.pool,
        (BaseResourceBundle{17, 19, 23, 29}));
    EXPECT_EQ(
        migrated.profile->baseResources.lastShortfall,
        (BaseResourceBundle{1, 2, 3, 4}));
    EXPECT_EQ(
        migrated.profile->baseResources.resolvedDemandCycleCount,
        0U);
    EXPECT_TRUE(validateProfileState(
        *migrated.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV9RejectsDemandCycleAheadOfWorldClock)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-invalid-demand-cycle",
        publishedContentRegistry());
    profile.baseResources.resolvedDemandCycleCount = 1U;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_NE(
        loaded.message.find("Base demand cycle is ahead"),
        std::string::npos);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousMultiWeaponContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-content-migration",
        publishedContentRegistry());
    AssetInstanceId armorKit{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::armorMaintenanceKit)
        {
            armorKit = id;
            break;
        }
    }
    ASSERT_NE(armorKit, 0U);
    ASSERT_TRUE(profile.assets.erase(armorKit));

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "survival-loadout-content-4",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
    EXPECT_EQ(
        loaded.profile->assets.find(armorKit),
        nullptr);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousArmorMaintenanceContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-aim-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "survival-loadout-content-5",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousCombatAimContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-combat-input-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "combat-aim-content-6",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousCombatInputContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-combat-ballistics-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "combat-input-content-7",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousCombatBallisticsContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-raid-fixed-maps-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "combat-ballistics-content-8",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV6AcceptsPreviousFixedMapsContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-raid-pressure-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-fixed-maps-content-9",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest,
    SchemaV6AcceptsPreviousRaidPressureContentVersion)
{
    ProfileState profile =
        makeNewAlphaProfile("save-v6-control-resource-content-migration",
                            publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, "raid-pressure-content-10", 6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(
        validateProfileState(*loaded.profile, publishedContentRegistry())
            .valid);
}

TEST(SaveRepositoryTest,
    SchemaV6AcceptsPreviousControlResourceContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-conditional-extraction-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-control-resource-content-11",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest,
    SchemaV6AcceptsPreviousConditionalExtractionContentVersion)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-allocation-content-migration",
        publishedContentRegistry());

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-conditional-extraction-content-12",
            6),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(
        loaded.profile->baseResources,
        BaseResourceState{});
    EXPECT_TRUE(validateProfileState(
        *loaded.profile, publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV1MigratesToCurrentProfileDefaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v1-migration",
        publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const std::string text = serializeProfileEnvelope(
        profile,
        "core-alpha-content-1",
        1);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(profileStateFingerprint(*migrated.profile), fingerprint);
    EXPECT_EQ(migrated.profile->currentHealth, 100);
    EXPECT_FALSE(migrated.profile->pendingRaid.has_value());
}

TEST(SaveRepositoryTest, SchemaV2MigratesArmorToFullDurability)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v2-armor-migration",
        publishedContentRegistry());
    const std::string text = serializeProfileEnvelope(
        profile,
        "core-alpha-content-2",
        2);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    for (const auto &[id, asset] : migrated.profile->assets.records())
    {
        static_cast<void>(id);
        const ItemDefinition &definition = publishedContentRegistry().item(
            asset.definitionId);
        if (!definition.armorProtection.has_value())
        {
            continue;
        }
        EXPECT_EQ(
            asset.currentMaximumDurability,
            definition.armorProtection->maximumDurability);
        EXPECT_EQ(asset.currentDurability, asset.currentMaximumDurability);
    }
}

TEST(SaveRepositoryTest, SchemaV4PreservesArmorDurability)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-armor-durability",
        publishedContentRegistry());
    AssetRecord *armor{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::bodyArmor)
        {
            armor = profile.assets.findMutable(id);
            break;
        }
    }
    ASSERT_NE(armor, nullptr);
    armor->currentMaximumDurability = 110;
    armor->currentDurability = 37;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV4PreservesPendingRaidWeaponAndMedicalState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-pending-raid",
        publishedContentRegistry());
    const auto find = [&profile](const ItemDefinitionId &definitionId)
    {
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (asset.definitionId == definitionId)
            {
                return id;
            }
        }
        return AssetInstanceId{};
    };
    const AssetInstanceId rifle = find(alpha_content::rifle);
    const AssetInstanceId magazine = find(alpha_content::magazine);
    const AssetInstanceId ammunition = find(alpha_content::ammunition);
    const AssetInstanceId chest = find(alpha_content::chestRig);
    const AssetInstanceId backpack = find(alpha_content::backpack);
    for (const auto &[assetId, slot, transaction] :
         std::vector<std::tuple<AssetInstanceId, EquipmentSlotKind, std::string>>{
             {rifle, EquipmentSlotKind::PrimaryWeapon, "save-equip-rifle"},
             {chest, EquipmentSlotKind::ChestRig, "save-equip-chest"},
             {backpack, EquipmentSlotKind::Backpack, "save-equip-backpack"}})
    {
        ASSERT_TRUE(executeInventory(
            profile,
            publishedContentRegistry(),
            InventoryEquipCommand{assetId, slot},
            CommandContext{profile.revision, transaction}).succeeded);
    }
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazine, ammunition, 30},
        CommandContext{profile.revision, "save-load-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        InstallMagazineCommand{rifle, magazine},
        CommandContext{profile.revision, "save-install-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        FireWeaponCommand{rifle},
        CommandContext{profile.revision, "save-chamber"}).succeeded);
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-raid",
            "save-settlement",
            7319,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "save-deploy"}).succeeded);
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Heavy,
        0,
        275,
        123000,
        19000};
    profile.pendingRaid->startingMedicalStatus = MedicalStatusState{
        BleedingSeverity::Light,
        32000,
        800,
        0,
        17000};
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(std::count_if(loaded.profile->pendingRaid->loot.begin(),
                            loaded.profile->pendingRaid->loot.end(),
                            [](const RaidLootSnapshot &loot)
                            { return loot.requiresHighRisk; }),
              2);
    EXPECT_EQ(installedMagazine(*loaded.profile, rifle), magazine);
    EXPECT_EQ(magazineRoundCount(*loaded.profile, magazine), 29U);
    EXPECT_TRUE(loaded.profile->assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(loaded.profile->medicalStatus, profile.medicalStatus);
    EXPECT_EQ(
        loaded.profile->pendingRaid->startingMedicalStatus,
        profile.pendingRaid->startingMedicalStatus);
}

TEST(SaveRepositoryTest, SchemaV3MigratesMedicalStateToHealthyDefaults)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v3-medical-migration",
        publishedContentRegistry());
    const std::string text = serializeProfileEnvelope(
        profile,
        "survival-loadout-content-1",
        3);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->medicalStatus, MedicalStatusState{});
}

TEST(SaveRepositoryTest, SchemaV6PreservesWeaponConditionAndMalfunction)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v5-weapon-condition", publishedContentRegistry());
    AssetRecord *rifle{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = profile.assets.findMutable(id);
            break;
        }
    }
    ASSERT_NE(rifle, nullptr);
    rifle->currentMaximumDurability = 8750;
    rifle->currentDurability = 4321;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest, SchemaV5MigratesNewlyDurablePistolOnly)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v5-pistol-migration", publishedContentRegistry());
    AssetRecord *rifle{};
    AssetRecord *pistol{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = profile.assets.findMutable(id);
        }
        else if (asset.definitionId == alpha_content::pistol)
        {
            pistol = profile.assets.findMutable(id);
        }
    }
    ASSERT_NE(rifle, nullptr);
    ASSERT_NE(pistol, nullptr);
    rifle->currentMaximumDurability = 8750;
    rifle->currentDurability = 4321;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    pistol->currentMaximumDurability = 0;
    pistol->currentDurability = 0;
    pistol->weaponMalfunction = WeaponMalfunctionType::None;

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, "survival-loadout-content-3", 5),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    const AssetRecord *loadedRifle = loaded.profile->assets.find(
        rifle->instanceId);
    const AssetRecord *loadedPistol = loaded.profile->assets.find(
        pistol->instanceId);
    ASSERT_NE(loadedRifle, nullptr);
    ASSERT_NE(loadedPistol, nullptr);
    EXPECT_EQ(loadedRifle->currentMaximumDurability, 8750U);
    EXPECT_EQ(loadedRifle->currentDurability, 4321U);
    EXPECT_EQ(
        loadedRifle->weaponMalfunction,
        WeaponMalfunctionType::Stovepipe);
    EXPECT_EQ(loadedPistol->currentMaximumDurability, 10000U);
    EXPECT_EQ(loadedPistol->currentDurability, 10000U);
    EXPECT_EQ(
        loadedPistol->weaponMalfunction,
        WeaponMalfunctionType::None);
}

TEST(SaveRepositoryTest, SchemaV6RoundTripsNewWeaponSlots)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v6-weapon-slots", publishedContentRegistry());
    AssetInstanceId rifle{};
    AssetInstanceId pistol{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle) rifle = id;
        if (asset.definitionId == alpha_content::pistol) pistol = id;
    }
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(pistol, 0U);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{rifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "save-equip-secondary"}).succeeded);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{pistol, EquipmentSlotKind::Sidearm},
        CommandContext{profile.revision, "save-equip-sidearm"}).succeeded);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(
        equippedAsset(*loaded.profile, EquipmentSlotKind::SecondaryWeapon),
        rifle);
    EXPECT_EQ(
        equippedAsset(*loaded.profile, EquipmentSlotKind::Sidearm),
        pistol);
}

TEST(SaveRepositoryTest, SchemaV4MigratesWeaponConditionToFactoryState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-v4-weapon-migration", publishedContentRegistry());
    AssetRecord *rifle{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::rifle)
        {
            rifle = profile.assets.findMutable(id);
            break;
        }
    }
    ASSERT_NE(rifle, nullptr);
    rifle->currentDurability = 1234;
    rifle->weaponMalfunction = WeaponMalfunctionType::Stovepipe;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, "survival-loadout-content-2", 4),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    const AssetRecord *loadedRifle = migrated.profile->assets.find(
        rifle->instanceId);
    ASSERT_NE(loadedRifle, nullptr);
    EXPECT_EQ(loadedRifle->currentMaximumDurability, 10000U);
    EXPECT_EQ(loadedRifle->currentDurability, 10000U);
    EXPECT_EQ(loadedRifle->weaponMalfunction, WeaponMalfunctionType::None);
}

TEST(SaveRepositoryTest, FirstSuccessfulSaveAlsoCreatesRecoveryBackup)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);

    std::ofstream corrupt(repository.primaryPath(), std::ios::trunc);
    corrupt << "corrupt";
    corrupt.close();

    const SaveLoadResult recovered = repository.load(publishedContentRegistry());
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*recovered.profile), fingerprint);
}

TEST(SaveRepositoryTest, CorruptPrimaryRecoversMostRecentValidBackup)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState first = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        first,
        publishedContentRegistry().contentVersion()).succeeded);
    const std::uint64_t firstFingerprint = profileStateFingerprint(first);

    ProfileState second = first;
    ASSERT_TRUE(executeEconomy(
        second,
        publishedContentRegistry(),
        PurchaseCommand{alpha_content::ammunition, 1},
        CommandContext{second.revision, "buy-before-backup"}).succeeded);
    ASSERT_TRUE(repository.save(
        second,
        publishedContentRegistry().contentVersion()).succeeded);

    std::ofstream corrupt(repository.primaryPath(), std::ios::trunc);
    corrupt << "{truncated";
    corrupt.close();

    const SaveLoadResult recovered = repository.load(publishedContentRegistry());
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*recovered.profile), firstFingerprint);
}

TEST(SaveRepositoryTest, SavingOverCorruptPrimaryPreservesExistingValidBackup)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState first = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        first,
        publishedContentRegistry().contentVersion()).succeeded);
    const std::uint64_t firstFingerprint = profileStateFingerprint(first);

    std::ofstream corrupt(repository.primaryPath(), std::ios::trunc);
    corrupt << "corrupt";
    corrupt.close();

    ProfileState replacement = first;
    ASSERT_TRUE(executeEconomy(
        replacement,
        publishedContentRegistry(),
        PurchaseCommand{alpha_content::ammunition, 1},
        CommandContext{replacement.revision, "replace-corrupt-primary"})
                    .succeeded);
    ASSERT_TRUE(repository.save(
        replacement,
        publishedContentRegistry().contentVersion()).succeeded);

    std::ofstream corruptReplacement(repository.primaryPath(), std::ios::trunc);
    corruptReplacement << "corrupt-again";
    corruptReplacement.close();

    const SaveLoadResult recovered = repository.load(publishedContentRegistry());
    ASSERT_EQ(recovered.status, SaveLoadStatus::RecoveredBackup);
    ASSERT_TRUE(recovered.profile.has_value());
    EXPECT_EQ(profileStateFingerprint(*recovered.profile), firstFingerprint);
}

TEST(SaveRepositoryTest, ChecksumMismatchIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    std::string text = serializeProfileEnvelope(
        profile,
        publishedContentRegistry().contentVersion());
    const std::size_t position = text.find("save-test");
    ASSERT_NE(position, std::string::npos);
    text.replace(position, 9, "save-tampered");

    const SaveLoadResult result = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());
    EXPECT_EQ(result.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(result.profile.has_value());
}

TEST(SaveRepositoryTest, UnsupportedContentVersionIsRejected)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-test",
        publishedContentRegistry());
    const std::string text = serializeProfileEnvelope(
        profile,
        "future-content-version");

    const SaveLoadResult result = deserializeProfileEnvelope(
        text,
        publishedContentRegistry());
    EXPECT_EQ(result.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(result.profile.has_value());
}

TEST(SaveRepositoryTest, CorruptPrimaryAndBackupFailExplicitly)
{
    TemporarySaveDirectory temporary;
    SaveRepository repository{temporary.path()};
    ProfileState profile = makeNewAlphaProfile(
        "save-double-corrupt",
        publishedContentRegistry());
    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);

    for (const std::filesystem::path &path : {
             repository.primaryPath(),
             repository.backupPath()})
    {
        std::ofstream corrupt(path, std::ios::trunc);
        corrupt << "corrupt";
    }

    const SaveLoadResult result = repository.load(
        publishedContentRegistry());
    EXPECT_EQ(result.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(result.profile.has_value());
}

TEST(SaveRepositoryTest, SchemaV16RoundTripsResidentMedicalAndSupplyPolicy)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-base-supply-v15",
        publishedContentRegistry());
    profile.baseConstruction.materialUnits = 7U;
    profile.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        4U,
        3U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U};
    profile.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.medical.medkit_alpha"},
        BaseSupplyCategory::Medical);
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    profile.nextBaseServiceJobId = 2U;
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        1U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U,
        14U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-resident-medical-content-24",
            16),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->baseConstruction.materialUnits, 7U);
    ASSERT_TRUE(loaded.profile->baseConstruction.activeProject.has_value());
    EXPECT_EQ(
        loaded.profile->baseConstruction.activeProject->committedWorkers,
        3U);
    EXPECT_EQ(
        loaded.profile->baseSupplyPolicy.assignments.at(
            ItemDefinitionId{"item.medical.medkit_alpha"}),
        BaseSupplyCategory::Medical);
    EXPECT_EQ(loaded.profile->basePopulation.injuredResidents, 1U);
    ASSERT_TRUE(loaded.profile->residentMedical.activeTreatment.has_value());
    EXPECT_EQ(
        loaded.profile->residentMedical.activeTreatment
            ->completionWorldMinute,
        profile.worldClock.elapsedWorldMinutes + 360U);
}

TEST(SaveRepositoryTest, SchemaV17RoundTripsActiveManufacturingOwnership)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-manufacturing-v17",
        publishedContentRegistry());
    const auto addInput = [&](const ItemDefinitionId &definitionId)
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(definitionId);
        const auto origin = findFirstProfileFit(
            profile,
            publishedContentRegistry(),
            ProfileContainerId::stash(),
            definition,
            ItemOrientation::Degrees0);
        ASSERT_TRUE(origin.has_value());
        profile.assets.create(
            definition,
            StoredAssetLocation{ProfileContainerId::stash(), *origin});
    };
    addInput(ItemDefinitionId{"item.loot.scrap_parts"});
    addInput(ItemDefinitionId{"item.loot.electronics"});
    const BaseManufacturingReceipt started = executeStartBaseManufacturing(
        profile,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"}},
        CommandContext{profile.revision, "save-start-manufacturing"});
    ASSERT_TRUE(started.succeeded) << started.message;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            17),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->baseManufacturing.activeOrder.has_value());
    EXPECT_EQ(
        loaded.profile->baseManufacturing.activeOrder->outputAssetId,
        started.outputAssetId);
}

TEST(SaveRepositoryTest, SchemaV18RoundTripsMoraleEventAndRaidRollback)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-morale-v18",
        publishedContentRegistry());
    profile.baseMorale.pendingFulfilledWishCount = 2U;
    profile.baseMorale.pendingNegativeEventCount = 1U;
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-morale-raid",
            "save-morale-settlement",
            19001U,
            MapDefinitionId{"map.v0.test"}},
        CommandContext{profile.revision, "save-morale-deploy"}).succeeded);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            18),
        publishedContentRegistry());
    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingBaseMorale,
        profile.pendingRaid->travel.startingBaseMorale);
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingBaseCommunityEvent,
        profile.pendingRaid->travel.startingBaseCommunityEvent);
}

TEST(SaveRepositoryTest, SchemaV17MigratesToStableCurrentMoraleEvent)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-morale-v17-migration",
        publishedContentRegistry());
    profile.worldClock.elapsedWorldMinutes =
        12U * kWorldMinutesPerDay + kInitialWorldMinute;
    profile.baseResources.resolvedDemandCycleCount = 12U;
    profile.baseMorale.resolvedDayCount = 12U;
    profile.basePriority = {};
    static_cast<void>(synchronizeBasePriorityThrough(
        profile,
        publishedContentRegistry()));
    profile.baseCommunityEvent = {};
    static_cast<void>(synchronizeBaseCommunityEventThrough(
        profile,
        publishedContentRegistry()));
    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-basic-manufacturing-content-25",
            17),
        publishedContentRegistry());
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(migrated.profile->baseMorale.resolvedDayCount, 12U);
    EXPECT_EQ(migrated.profile->baseCommunityEvent.cycleIndex, 2U);
    EXPECT_TRUE(validateProfileState(
        *migrated.profile,
        publishedContentRegistry()).valid);
}

TEST(SaveRepositoryTest, SchemaV18RejectsInvalidMoraleEventSnapshot)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-morale-invalid",
        publishedContentRegistry());
    profile.baseCommunityEvent.cycleIndex = 99U;
    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion(),
            18),
        publishedContentRegistry());
    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_NE(loaded.message.find("morale"), std::string::npos);
}

TEST(SaveRepositoryTest, SchemaV19RoundTripsProfessionsStaffingAndFacilityLevels)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-workforce-facilities-v19",
        publishedContentRegistry());
    profile.basePopulation = BasePopulationState{10U, 14U, 2U};
    profile.basePopulation.professionResidents = {6U, 2U, 1U, 1U};
    profile.basePopulation.injuredByProfession = {1U, 0U, 1U, 0U};
    profile.baseWorkforce.workshopWorker = BaseResidentProfession::General;
    profile.baseWorkforce.medicalWorker = BaseResidentProfession::Medical;
    profile.baseConstruction.dormitoryLevel = 2U;
    profile.baseConstruction.workshopLevel = 2U;
    profile.baseConstruction.medicalLevel = 2U;
    ASSERT_TRUE(validateProfileState(
        profile, publishedContentRegistry()).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(
        loaded.profile->basePopulation.professionResidents,
        profile.basePopulation.professionResidents);
    EXPECT_EQ(loaded.profile->baseWorkforce, profile.baseWorkforce);
    EXPECT_EQ(loaded.profile->baseConstruction.workshopLevel, 2U);
    EXPECT_EQ(loaded.profile->baseConstruction.medicalLevel, 2U);
}

TEST(SaveRepositoryTest, SchemaV20RoundTripsRaidIntelligenceAndPendingLoadout)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-intelligence-v20", publishedContentRegistry());
    const MapDefinitionId mapId{"map.v0.test"};
    auto &counts = profile.raidIntelligence.counts[mapId];
    counts[raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Transport)] = 2U;
    counts[raidIntelligenceCategoryIndex(
        RaidIntelligenceCategory::Resource)] = 1U;
    RaidIntelligenceLoadout loadout;
    loadout.set(RaidIntelligenceCategory::Transport, true);
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{"save-intelligence-raid", "save-intelligence-settle",
                      99441U, mapId, loadout},
        {profile.revision, "save-intelligence-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion(), 20),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(loaded.profile->pendingRaid->intelligence, loadout);
    EXPECT_EQ(
        loaded.profile->pendingRaid->travel.startingRaidIntelligence.count(
            mapId, RaidIntelligenceCategory::Transport),
        2U);
    EXPECT_EQ(loaded.profile->raidIntelligence.count(
        mapId, RaidIntelligenceCategory::Transport), 1U);
}

TEST(SaveRepositoryTest, SchemaV23FreezesInteriorSpacesAndSpatialLayout)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-procedural-layout-v21", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-procedural-raid",
            "save-procedural-settle",
            783311U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-procedural-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_FALSE(
        profile.pendingRaid->spatialLayout.ballisticBlockers.empty());
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_EQ(loaded.profile->pendingRaid->spatialLayout,
              profile.pendingRaid->spatialLayout);
    EXPECT_EQ(loaded.profile->pendingRaid->interiors,
              profile.pendingRaid->interiors);
    ASSERT_FALSE(loaded.profile->pendingRaid->interiors.empty());
    EXPECT_EQ(
        loaded.profile->pendingRaid->interiors.front().id,
        RaidSpaceDefinitionId{"raid_space.frontier_exchange.office"});

    ++profile.pendingRaid->spatialLayout.layoutHash;
    const SaveLoadResult corrupt = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    EXPECT_FALSE(corrupt.profile.has_value());
    EXPECT_EQ(corrupt.status, SaveLoadStatus::Failed);
}

TEST(SaveRepositoryTest, SchemaV24RoundTripsLostRaidOwnershipAndReceipt)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-lost-raid-v24", content);
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    const std::string recordId{"lost-save-settlement"};
    profile.committedSettlements.insert(recordId);
    profile.lostRaidRecords.emplace(
        recordId,
        LostRaidRecord{
            recordId,
            "lost-save-raid",
            recordId,
            MapDefinitionId{"map.raid.riverside"},
            "MODERATE",
            RaidResultOutcome::PlayerDead,
            profile.worldClock.elapsedWorldMinutes,
            2U});
    profile.assets.findMutable(rifle->first)->location =
        LostRaidAssetLocation{recordId, EquipmentSlotKind::PrimaryWeapon};
    profile.lastRaidResult = LastRaidResult{};
    profile.lastRaidResult->settlementId = recordId;
    profile.lastRaidResult->outcome = RaidResultOutcome::PlayerDead;
    profile.lastRaidResult->lostRaidRecordId = recordId;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, content.contentVersion(), 24),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->lostRaidRecords.contains(recordId));
    EXPECT_EQ(
        loaded.profile->lostRaidRecords.at(recordId)
            .subsequentRaidSettlementCount,
        2U);
    ASSERT_TRUE(loaded.profile->lastRaidResult.has_value());
    EXPECT_EQ(
        loaded.profile->lastRaidResult->lostRaidRecordId,
        std::optional<std::string>{recordId});
    const auto *location = std::get_if<LostRaidAssetLocation>(
        &loaded.profile->assets.find(rifle->first)->location);
    ASSERT_NE(location, nullptr);
    EXPECT_EQ(location->recordId, recordId);
}

TEST(SaveRepositoryTest, SchemaV23MigratesToEmptyLostRecordState)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-lost-raid-v23-migration", content);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-second-representative-location-content-33",
            23),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->lostRaidRecords.empty());
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
}

TEST(SaveRepositoryTest,
     SchemaV25RoundTripsRecoveryTaskOwnershipFrozenResultAndHighWater)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-recovery-task-v25", content);
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    const AssetInstanceId rifleId = rifle->first;
    const std::string recordId{"save-recovery-settlement"};
    profile.committedSettlements.insert(recordId);
    profile.lostRaidRecords.emplace(
        recordId,
        LostRaidRecord{
            recordId,
            "save-recovery-raid",
            recordId,
            MapDefinitionId{"map.raid.riverside"},
            "MODERATE",
            RaidResultOutcome::ActiveQuit,
            profile.worldClock.elapsedWorldMinutes,
            1U});
    profile.assets.findMutable(rifleId)->location =
        LostRaidAssetLocation{
            recordId, EquipmentSlotKind::PrimaryWeapon};
    ASSERT_TRUE(executeStartRecoveryTask(
        profile,
        content,
        recordId,
        CommandContext{profile.revision, "save-recovery-start"})
                    .succeeded);
    ASSERT_TRUE(profile.recoveryTask.has_value());
    profile.worldClock.elapsedWorldMinutes =
        profile.recoveryTask->completionWorldMinute;
    ASSERT_TRUE(applyRecoveryTaskThrough(profile).becameReady);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(profile, content.contentVersion()),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    ASSERT_TRUE(loaded.profile->recoveryTask.has_value());
    EXPECT_TRUE(loaded.profile->recoveryTask->readyForCollection);
    EXPECT_EQ(
        loaded.profile->recoveryTask->recoveredAssetIds,
        profile.recoveryTask->recoveredAssetIds);
    EXPECT_EQ(
        loaded.profile->nextRecoveryTaskId,
        profile.nextRecoveryTaskId);
    EXPECT_TRUE(std::holds_alternative<RecoveryTaskAssetLocation>(
        loaded.profile->assets.find(rifleId)->location));
}

TEST(SaveRepositoryTest,
     SchemaV24PreviousContentMigratesToEmptyRecoveryTaskState)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeNewAlphaProfile(
        "save-recovery-v24-migration", content);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, "regional-loss-record-content-34", 24),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_FALSE(loaded.profile->recoveryTask.has_value());
    EXPECT_EQ(loaded.profile->nextRecoveryTaskId, 1U);
}

TEST(SaveRepositoryTest, SchemaV24RejectsUnknownLostRecordMap)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "save-lost-raid-invalid-map", content);
    const auto rifle = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    profile.committedSettlements.insert("lost-invalid-settlement");
    profile.lostRaidRecords.emplace(
        "lost-invalid-settlement",
        LostRaidRecord{
            "lost-invalid-settlement",
            "lost-invalid-raid",
            "lost-invalid-settlement",
            MapDefinitionId{"map.raid.unknown"},
            "UNKNOWN",
            RaidResultOutcome::ActiveQuit,
            profile.worldClock.elapsedWorldMinutes,
            0U});
    profile.assets.findMutable(rifle->first)->location =
        LostRaidAssetLocation{
            "lost-invalid-settlement",
            EquipmentSlotKind::PrimaryWeapon};

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, content.contentVersion(), 24),
        content);

    EXPECT_FALSE(loaded.profile.has_value());
    EXPECT_EQ(loaded.status, SaveLoadStatus::Failed);
    EXPECT_EQ(loaded.message, "lost Raid record is invalid");
}

TEST(SaveRepositoryTest,
     SchemaV23RoundTripsPermanentInteriorIntelligenceAndDeployKnowledge)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};
    ProfileState profile = makeNewAlphaProfile(
        "save-interior-intelligence-v23", content);
    profile.raidInteriorIntelligence.knownLayouts.insert(interiorId);
    ASSERT_TRUE(executeDeploy(
        profile,
        content,
        DeployCommand{
            "save-known-interior-raid",
            "save-known-interior-settle",
            783312U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-known-interior-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    ASSERT_EQ(profile.pendingRaid->interiors.size(), 2U);
    ASSERT_TRUE(profile.pendingRaid->interiors.front().layoutKnown);
    EXPECT_FALSE(profile.pendingRaid->interiors[1].layoutKnown);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, content.contentVersion(), 23),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), fingerprint);
    EXPECT_TRUE(loaded.profile->raidInteriorIntelligence.knows(interiorId));
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_TRUE(loaded.profile->pendingRaid->interiors.front().layoutKnown);
    ASSERT_EQ(loaded.profile->pendingRaid->interiors.size(), 2U);
    EXPECT_FALSE(loaded.profile->pendingRaid->interiors[1].layoutKnown);
}

TEST(SaveRepositoryTest,
     SchemaV22MigratesWithNoPermanentInteriorIntelligence)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};
    ProfileState profile = makeNewAlphaProfile(
        "save-interior-intelligence-v22", content);
    profile.raidInteriorIntelligence.knownLayouts.insert(interiorId);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-special-location-placement-content-31",
            22),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    EXPECT_TRUE(loaded.profile->raidInteriorIntelligence.knownLayouts.empty());
}

TEST(SaveRepositoryTest,
     SchemaV23RejectsUnknownOrDuplicateInteriorIntelligenceIds)
{
    const ContentRegistry &content = publishedContentRegistry();
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};

    ProfileState unknown = makeNewAlphaProfile(
        "save-unknown-interior-intelligence-v23", content);
    unknown.raidInteriorIntelligence.knownLayouts.insert(
        RaidSpaceDefinitionId{"raid_space.unknown.office"});
    const SaveLoadResult unknownResult = deserializeProfileEnvelope(
        serializeProfileEnvelope(unknown, content.contentVersion(), 23),
        content);
    EXPECT_FALSE(unknownResult.profile.has_value());

    ProfileState valid = makeNewAlphaProfile(
        "save-duplicate-interior-intelligence-v23", content);
    valid.raidInteriorIntelligence.knownLayouts.insert(interiorId);
    nlohmann::json envelope = nlohmann::json::parse(
        serializeProfileEnvelope(valid, content.contentVersion(), 23));
    envelope["payload"]["raid_interior_intelligence"].push_back(
        interiorId.value());
    envelope["payload_checksum"] = testSaveChecksum(
        envelope["payload"].dump());

    const SaveLoadResult duplicateResult = deserializeProfileEnvelope(
        envelope.dump(), content);
    EXPECT_FALSE(duplicateResult.profile.has_value());
}

TEST(SaveRepositoryTest, SchemaV22RejectsUnknownInteriorActorSpace)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-interior-space-v22", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-interior-raid",
            "save-interior-settle",
            883311U,
            MapDefinitionId{"map.raid.frontier_exchange"},
            {}},
        {profile.revision, "save-interior-deploy"}).succeeded);
    ASSERT_TRUE(profile.pendingRaid.has_value());
    const auto enemy = std::find_if(
        profile.pendingRaid->enemies.begin(),
        profile.pendingRaid->enemies.end(),
        [](const RaidEnemySnapshot &candidate)
        { return candidate.spaceId != outdoorRaidSpaceId(); });
    ASSERT_NE(enemy, profile.pendingRaid->enemies.end());
    enemy->spaceId = RaidSpaceDefinitionId{"raid_space.unknown.room"};

    const SaveLoadResult corrupt = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, publishedContentRegistry().contentVersion()),
        publishedContentRegistry());
    EXPECT_FALSE(corrupt.profile.has_value());
    EXPECT_EQ(corrupt.status, SaveLoadStatus::Failed);
}

TEST(SaveRepositoryTest, SchemaV22LoadsLegacyFixedInteriorPlacement)
{
    const ContentRegistry &content = publishedContentRegistry();
    const MapDefinitionId mapId{"map.raid.frontier_exchange"};
    const MapDefinition &map = content.map(mapId);
    const RaidInteriorDefinition &definition = map.interiors.front();
    ProfileState legacyProfile = makeNewAlphaProfile(
        "save-legacy-interior-v22", content);
    ASSERT_TRUE(executeDeploy(
        legacyProfile,
        content,
        DeployCommand{
            "save-legacy-interior-raid",
            "save-legacy-interior-settle",
            78123U,
            mapId,
            {}},
        {legacyProfile.revision, "save-legacy-interior-deploy"})
                    .succeeded);
    ASSERT_TRUE(legacyProfile.pendingRaid.has_value());
    PendingRaidSnapshot &raid = *legacyProfile.pendingRaid;
    const RaidSpaceDefinitionId secondInteriorId = map.interiors[1].id;
    std::vector<AssetInstanceId> removedLootAssets;
    for (const RaidLootSnapshot &loot : raid.loot)
    {
        if (loot.spaceId == secondInteriorId)
        {
            removedLootAssets.push_back(loot.assetId);
        }
    }
    std::erase_if(
        raid.enemies,
        [&](const RaidEnemySnapshot &enemy)
        { return enemy.spaceId == secondInteriorId; });
    std::erase_if(
        raid.loot,
        [&](const RaidLootSnapshot &loot)
        { return loot.spaceId == secondInteriorId; });
    for (AssetInstanceId assetId : removedLootAssets)
    {
        ASSERT_TRUE(legacyProfile.assets.erase(assetId));
    }
    raid.interiors.resize(1U);
    raid.rulesVersion = "raid-interior-spaces-12";
    raid.interiors.front().exteriorEntrance = definition.exteriorEntrance;
    raid.interiors.front().exteriorReturn = definition.exteriorReturn;

    RaidMapGenerationAnchors anchors;
    anchors.playerSpawn = raid.playerSpawn;
    anchors.extractionPoint = raid.extractionPoint;
    anchors.occupiedRegions = {
        map.highRisk.emergencyExtractionPoint,
        map.highRisk.conditionalExtractionPoint,
        map.highRisk.activationControlPoint,
        map.highRisk.advancedResourceArea};
    const auto addRegion = [&anchors](ContentRect region)
    {
        anchors.reachablePoints.push_back(
            {region.position.x + region.size.x * 0.5F,
             region.position.y + region.size.y * 0.5F});
    };
    for (const RaidEnemySnapshot &enemy : raid.enemies)
    {
        if (enemy.spaceId == outdoorRaidSpaceId())
        {
            anchors.occupiedRegions.push_back(
                {enemy.position, enemy.size});
            addRegion({enemy.position, enemy.size});
        }
    }
    for (const RaidLootSnapshot &loot : raid.loot)
    {
        if (loot.spaceId == outdoorRaidSpaceId())
        {
            anchors.reachablePoints.push_back(loot.position);
        }
    }
    for (const EnemySpawnDefinition &spawn : map.highRisk.pressureSpawns)
    {
        anchors.occupiedRegions.push_back({spawn.position, spawn.size});
    }
    addRegion(map.highRisk.emergencyExtractionPoint);
    addRegion(map.highRisk.conditionalExtractionPoint);
    addRegion(map.highRisk.activationControlPoint);
    addRegion(map.highRisk.advancedResourceArea);
    if (raid.rescue.has_value())
    {
        anchors.occupiedRegions.push_back(raid.rescue->transferPoint);
        addRegion(raid.rescue->transferPoint);
    }
    anchors.occupiedRegions.push_back(definition.exteriorEntrance);
    addRegion(definition.exteriorEntrance);
    raid.spatialLayout = generateRaidMapLayout(map, raid.seed, anchors);

    const SaveLoadResult loaded = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            legacyProfile,
            "raid-interior-spaces-content-30",
            22),
        content);

    ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
    ASSERT_TRUE(loaded.profile->pendingRaid.has_value());
    EXPECT_EQ(
        loaded.profile->pendingRaid->interiors.front().exteriorEntrance,
        definition.exteriorEntrance);
    EXPECT_FLOAT_EQ(
        loaded.profile->pendingRaid->interiors.front().exteriorReturn.x,
        definition.exteriorReturn.x);
    EXPECT_FLOAT_EQ(
        loaded.profile->pendingRaid->interiors.front().exteriorReturn.y,
        definition.exteriorReturn.y);
}

TEST(SaveRepositoryTest, SchemaV21MigratesRootActorsWithoutInteriorState)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-root-space-v21", publishedContentRegistry());
    ASSERT_TRUE(executeDeploy(
        profile,
        publishedContentRegistry(),
        DeployCommand{
            "save-root-raid",
            "save-root-settle",
            883312U,
            MapDefinitionId{"map.v0.test"},
            {}},
        {profile.revision, "save-root-deploy"}).succeeded);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile, "procedural-outdoor-layout-content-29", 21),
        publishedContentRegistry());
    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    ASSERT_TRUE(migrated.profile->pendingRaid.has_value());
    EXPECT_TRUE(migrated.profile->pendingRaid->interiors.empty());
    EXPECT_TRUE(std::all_of(
        migrated.profile->pendingRaid->enemies.begin(),
        migrated.profile->pendingRaid->enemies.end(),
        [](const RaidEnemySnapshot &enemy)
        { return enemy.spaceId == outdoorRaidSpaceId(); }));
}

TEST(SaveRepositoryTest, SchemaV19MigratesToEmptyRaidIntelligenceArchive)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-intelligence-v19", publishedContentRegistry());
    profile.raidIntelligence.counts[MapDefinitionId{"map.v0.test"}]
        [raidIntelligenceCategoryIndex(RaidIntelligenceCategory::Enemy)] = 3U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-workforce-facilities-content-27",
            19),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_TRUE(migrated.profile->raidIntelligence.counts.empty());
}

TEST(SaveRepositoryTest, SchemaV15MigratesWithoutRetroactiveResidentInjury)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-resident-medical-v15",
        publishedContentRegistry());
    profile.baseSupplyPolicy.assignments.emplace(
        ItemDefinitionId{"item.medical.medkit_alpha"},
        BaseSupplyCategory::Medical);
    profile.basePopulation.injuredResidents = 1U;
    profile.basePopulation.injuredByProfession[baseProfessionIndex(
        BaseResidentProfession::General)] = 1U;
    profile.nextBaseServiceJobId = 2U;
    profile.residentMedical.activeTreatment = ActiveResidentTreatment{
        1U,
        profile.worldClock.elapsedWorldMinutes,
        profile.worldClock.elapsedWorldMinutes + 360U,
        14U,
        BaseResidentProfession::General,
        BaseResidentProfession::Medical};

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-supply-policy-content-23",
            15),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->basePopulation.injuredResidents, 0U);
    EXPECT_FALSE(migrated.profile->residentMedical.activeTreatment.has_value());
    EXPECT_EQ(
        migrated.profile->baseSupplyPolicy.assignments.at(
            ItemDefinitionId{"item.medical.medkit_alpha"}),
        BaseSupplyCategory::Medical);
}

TEST(SaveRepositoryTest, SchemaV14MigratesToEmptySupplyPolicy)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-base-supply-v14",
        publishedContentRegistry());
    profile.baseConstruction.materialUnits = 7U;
    profile.baseSupplyPolicy.assignments.emplace(
        alpha_content::lootCola,
        BaseSupplyCategory::Food);

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "base-dormitory-expansion-content-22",
            14),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->baseConstruction.materialUnits, 7U);
    EXPECT_TRUE(migrated.profile->baseSupplyPolicy.assignments.empty());
}

TEST(SaveRepositoryTest, SchemaV13MigratesToDefaultBaseConstruction)
{
    ProfileState profile = makeNewAlphaProfile(
        "save-base-construction-v13",
        publishedContentRegistry());
    profile.baseConstruction.materialUnits = 9U;

    const SaveLoadResult migrated = deserializeProfileEnvelope(
        serializeProfileEnvelope(
            profile,
            "raid-ordinary-survivor-rescue-content-21",
            13),
        publishedContentRegistry());

    ASSERT_TRUE(migrated.profile.has_value()) << migrated.message;
    EXPECT_EQ(migrated.profile->baseConstruction.materialUnits, 0U);
    EXPECT_EQ(migrated.profile->baseConstruction.dormitoryLevel, 1U);
    EXPECT_FALSE(migrated.profile->baseConstruction.activeProject.has_value());
    // Population was already part of schema v13 and remains authoritative.
    EXPECT_EQ(migrated.profile->basePopulation.bedCapacity, 10U);
}
