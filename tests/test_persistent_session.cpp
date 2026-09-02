#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

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

std::size_t countStashDefinition(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    return static_cast<std::size_t>(std::count_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&definitionId](const auto &entry)
        {
            const auto *stored =
                std::get_if<StoredAssetLocation>(&entry.second.location);
            return entry.second.definitionId == definitionId &&
                stored != nullptr &&
                stored->container == ProfileContainerId::stash();
        }));
}

std::size_t countAllAmmunition(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    std::size_t total{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        if (asset.definitionId == definitionId)
            total += asset.quantity;
        total += static_cast<std::size_t>(std::count_if(
            asset.magazineRounds.begin(), asset.magazineRounds.end(),
            [&](const MagazineRoundRecord &round)
            { return round.definitionId == definitionId; }));
        if (asset.chamberedRound.has_value() &&
            asset.chamberedRound->definitionId == definitionId)
            ++total;
    }
    return total;
}

TEST(PersistentSessionTest, BaseShootingCheckpointPersistsAmmoAndDurability)
{
    SessionSaveDirectory temporary;
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-base-shooting"));
    const AssetInstanceId rifle = findDefinition(
        first.profile(), alpha_content::rifle);
    const AssetInstanceId magazine = findDefinition(
        first.profile(), alpha_content::magazine);
    const AssetInstanceId ammunition = findDefinition(
        first.profile(), alpha_content::ammunition);
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(magazine, 0U);
    ASSERT_NE(ammunition, 0U);
    ASSERT_TRUE(first.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazine, ammunition, 10U},
        "persistent-base-load").succeeded);
    ASSERT_TRUE(first.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "persistent-base-equip").succeeded);
    ASSERT_TRUE(first.executeProfileWeaponAmmo(
        InstallMagazineAndChamberCommand{rifle, magazine},
        "persistent-base-install").succeeded);

    BaseWorld world;
    GameplayInput fire{};
    fire.aimWorldPosition = Vec2{
        world.playerPosition().x + 700.0F,
        world.playerPosition().y};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    static_cast<void>(first.updateBaseWorld(world, fire, 1.0F / 60.0F));
    ASSERT_TRUE(world.shotFiredLastUpdate());
    const std::size_t roundsAfter = countAllAmmunition(
        first.profile(), alpha_content::ammunition);
    const std::uint32_t durabilityAfter =
        first.profile().assets.find(rifle)->currentDurability;
    ASSERT_TRUE(first.checkpointWorldClock()) << first.persistenceMessage();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(
        countAllAmmunition(reopened.profile(), alpha_content::ammunition),
        roundsAfter);
    ASSERT_NE(reopened.profile().assets.find(rifle), nullptr);
    EXPECT_EQ(
        reopened.profile().assets.find(rifle)->currentDurability,
        durabilityAfter);
}

TEST(PersistentSessionTest, PreviousContentReceivesWarehouseCatalogOnce)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState legacy = makeNewPublishedProfile(
        "persistent-warehouse-catalog", content);
    legacy.committedTransactions.erase(
        "bootstrap.warehouse_catalog.content_54");
    legacy.committedTransactions.insert(
        "bootstrap.warehouse_catalog.content_53");
    const ItemDefinitionId restoredDefinition{
        "item.protective_gear.body_armor_heavy"};
    std::vector<AssetInstanceId> removed;
    for (const auto &[assetId, asset] : legacy.assets.records())
    {
        if (asset.definitionId == restoredDefinition)
        {
            removed.push_back(assetId);
        }
    }
    ASSERT_EQ(removed.size(), 1U);
    ASSERT_TRUE(legacy.assets.erase(removed.front()));
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        legacy,
        "content-beta-warehouse-catalog-content-53").succeeded);

    GameSession migrated;
    migrated.configurePersistence(temporary.path());
    ASSERT_TRUE(migrated.continueProfile()) << migrated.persistenceMessage();
    EXPECT_TRUE(migrated.profile().committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
    EXPECT_EQ(countStashDefinition(
                  migrated.profile(), restoredDefinition),
              1U);
    const std::size_t migratedAssetCount =
        migrated.profile().assets.records().size();
    const ProfileRevision migratedRevision = migrated.profile().revision;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().assets.records().size(), migratedAssetCount);
    EXPECT_EQ(reopened.profile().revision, migratedRevision);
    EXPECT_EQ(countStashDefinition(
                  reopened.profile(), restoredDefinition),
              1U);
}

TEST(PersistentSessionTest, NewPlayableProfileStartsWithFiniteStarterAssets)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("finite-starter"));

    const ProfileState &profile = session.profile();
    EXPECT_NE(findDefinition(profile, alpha_content::rifle), 0U);
    EXPECT_NE(findDefinition(profile, alpha_content::pistol), 0U);
    EXPECT_NE(findDefinition(profile, alpha_content::chestRig), 0U);
    EXPECT_NE(findDefinition(profile, alpha_content::backpack), 0U);
    EXPECT_EQ(findDefinition(
                  profile,
                  ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}),
              0U);
    EXPECT_EQ(findDefinition(
                  profile,
                  ItemDefinitionId{"item.protective_gear.body_armor_heavy"}),
              0U);
    EXPECT_EQ(findDefinition(
                  profile,
                  ItemDefinitionId{"item.container.backpack_expedition"}),
              0U);
    EXPECT_LT(
        profile.assets.records().size(),
        publishedContentRegistry().items().size());
    EXPECT_TRUE(profile.committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
    EXPECT_FALSE(profile.committedTransactions.contains(
        "developer.warehouse_catalog.content_56"));
}

TEST(PersistentSessionTest, DeveloperCatalogGrantPersistsAndIsIdempotent)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("developer-catalog"));

    const WarehouseCatalogGrantReceipt granted =
        session.grantDeveloperWarehouseCatalog();
    ASSERT_TRUE(granted.succeeded) << granted.message;
    EXPECT_FALSE(granted.alreadyGranted);
    EXPECT_GT(granted.addedDefinitionCount, 0U);
    EXPECT_TRUE(session.developerWarehouseCatalogGranted());
    EXPECT_NE(findDefinition(
                  session.profile(),
                  ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}),
              0U);
    const std::uint64_t grantedFingerprint =
        profileStateFingerprint(session.profile());

    const WarehouseCatalogGrantReceipt replay =
        session.grantDeveloperWarehouseCatalog();
    EXPECT_TRUE(replay.succeeded);
    EXPECT_TRUE(replay.alreadyGranted);
    EXPECT_EQ(profileStateFingerprint(session.profile()), grantedFingerprint);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(reopened.developerWarehouseCatalogGranted());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), grantedFingerprint);
}

TEST(PersistentSessionTest, CurrentStarterProfileReopensWithoutCatalogMutation)
{
    SessionSaveDirectory temporary;
    ProfileState starter = makeNewAlphaProfile(
        "current-starter-reopen", publishedContentRegistry());
    const std::uint64_t before = profileStateFingerprint(starter);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        starter, publishedContentRegistry().contentVersion()).succeeded);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), before);
    EXPECT_FALSE(reopened.developerWarehouseCatalogGranted());
    EXPECT_EQ(findDefinition(
                  reopened.profile(),
                  ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}),
              0U);
}

TEST(PersistentSessionTest, DeveloperCatalogSaveFailurePreservesMemory)
{
    SessionSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("developer-catalog-save-failure"));
    const std::uint64_t before = profileStateFingerprint(session.profile());

    const std::filesystem::path invalidDirectory =
        temporary.path() / "developer-catalog-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const WarehouseCatalogGrantReceipt rejected =
        session.grantDeveloperWarehouseCatalog();
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.developerWarehouseCatalogGranted());
}

TEST(PersistentSessionTest, FullLegacyWarehouseLoadsWithoutPartialCatalogGrant)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState legacy = makeNewAlphaProfile(
        "persistent-full-warehouse", content);
    legacy.committedTransactions.erase(
        "bootstrap.warehouse_catalog.content_54");
    legacy.committedTransactions.insert(
        "bootstrap.warehouse_catalog.content_53");
    std::vector<AssetInstanceId> existing;
    for (const auto &[assetId, asset] : legacy.assets.records())
    {
        static_cast<void>(asset);
        existing.push_back(assetId);
    }
    for (AssetInstanceId assetId : existing)
    {
        ASSERT_TRUE(legacy.assets.erase(assetId));
    }
    const ItemDefinition &ammunition =
        content.item(alpha_content::ammunition);
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 24; ++x)
        {
            static_cast<void>(legacy.assets.create(
                ammunition,
                StoredAssetLocation{
                    ProfileContainerId::stash(), GridPosition{x, y}},
                1U));
        }
    }
    const std::uint64_t before = profileStateFingerprint(legacy);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        legacy,
        "content-beta-warehouse-catalog-content-53").succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_NE(session.persistenceMessage().find("clear Stash space"),
              std::string::npos);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().committedTransactions.contains(
        "bootstrap.warehouse_catalog.content_54"));
}

TEST(PersistentSessionTest, SiegeWarningCountdownPersistsAndFirstTimeoutWaits)
{
    SessionSaveDirectory temporary;
    ProfileState profile = makeNewAlphaProfile(
        "persistent-siege-warning", publishedContentRegistry());
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    profile.baseSiege.warningActive = true;
    profile.baseSiege.warningRemainingSeconds = 3U;
    profile.baseSiege.siegeSequence = 1U;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        profile, publishedContentRegistry().contentVersion()).succeeded);

    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.continueProfile()) << first.persistenceMessage();
    first.advanceBaseWorldClock(2.2F);
    EXPECT_EQ(first.baseThreatProjection().warningRemainingSeconds, 1U);
    ASSERT_TRUE(first.checkpointWorldClock()) << first.persistenceMessage();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.baseThreatProjection().warningRemainingSeconds, 1U);
    reopened.advanceBaseWorldClock(2.0F);
    EXPECT_TRUE(reopened.baseThreatProjection().warningActive);
    EXPECT_EQ(reopened.baseThreatProjection().warningRemainingSeconds, 0U);
    EXPECT_TRUE(reopened.baseThreatProjection().requiresFirstPreset);

    const BaseAutoDefenseReceipt receipt = reopened.executeBaseAutoDefense(
        "persistent-first-auto-defense");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_FALSE(reopened.baseThreatProjection().warningActive);
    EXPECT_EQ(receipt.outcome, BaseSiegeOutcome::Defended);
}

TEST(PersistentSessionTest, EligibleSiegeWarningActivationIsImmediatelyDurable)
{
    SessionSaveDirectory temporary;
    ProfileState profile = makeNewAlphaProfile(
        "persistent-siege-activation", publishedContentRegistry());
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    profile.baseSiege.safeUntilWorldMinute =
        profile.worldClock.elapsedWorldMinutes;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        profile, publishedContentRegistry().contentVersion()).succeeded);

    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    active.advanceBaseWorldClock(0.1F);
    ASSERT_TRUE(active.baseThreatProjection().warningActive);
    EXPECT_EQ(active.baseThreatProjection().warningRemainingSeconds,
              kBaseSiegeWarningSeconds);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(reopened.baseThreatProjection().warningActive);
    EXPECT_EQ(reopened.baseThreatProjection().warningRemainingSeconds,
              kBaseSiegeWarningSeconds);
}

TEST(PersistentSessionTest, SavedAutoDefensePresetResolvesAtTimeout)
{
    SessionSaveDirectory temporary;
    ProfileState profile = makeNewAlphaProfile(
        "persistent-siege-preset", publishedContentRegistry());
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    profile.baseSiege.warningActive = true;
    profile.baseSiege.warningRemainingSeconds = 1U;
    profile.baseSiege.siegeSequence = 4U;
    profile.baseSiege.autoDefensePresetSaved = true;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        profile, publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    session.advanceBaseWorldClock(1.0F);

    EXPECT_FALSE(session.baseThreatProjection().warningActive);
    EXPECT_EQ(session.profile().baseSiege.lastOutcome,
              BaseSiegeOutcome::Defended);
    EXPECT_TRUE(session.profile().committedTransactions.contains(
        "persistent-siege-preset-base-siege-auto-4"));
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

TEST(PersistentSessionTest,
     RaidBaseCompletionNoticeWaitsForSuccessfulSettlement)
{
    SessionSaveDirectory temporary;
    ProfileState prepared = makeNewAlphaProfile(
        "deferred-raid-base-completion",
        publishedContentRegistry());
    const std::uint32_t outboundMinutes = publishedContentRegistry().map(
        MapDefinitionId{"map.v0.test"}).travel.outboundMinutes;
    prepared.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        4U,
        3U,
        prepared.worldClock.elapsedWorldMinutes + outboundMinutes + 1U - 360U,
        prepared.worldClock.elapsedWorldMinutes + outboundMinutes + 1U};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        prepared,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    ASSERT_TRUE(active.deployAlpha(99103));
    active.update(GameplayInput{}, 1.0F);
    EXPECT_EQ(active.profile().baseConstruction.dormitoryLevel, 2U);
    EXPECT_TRUE(active.takePresentationEvents().empty());

    ASSERT_TRUE(active.activeQuitAlphaRaid());
    EXPECT_EQ(
        active.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::BaseDormitoryUpgradeCompleted});
}

TEST(PersistentSessionTest,
     RaidRollbackDiscardsDeferredBaseCompletionNotice)
{
    SessionSaveDirectory temporary;
    ProfileState prepared = makeNewAlphaProfile(
        "discarded-raid-base-completion",
        publishedContentRegistry());
    const std::uint32_t outboundMinutes = publishedContentRegistry().map(
        MapDefinitionId{"map.v0.test"}).travel.outboundMinutes;
    prepared.baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{
            "base_construction.dormitory.level_2"},
        4U,
        3U,
        prepared.worldClock.elapsedWorldMinutes + outboundMinutes + 1U - 360U,
        prepared.worldClock.elapsedWorldMinutes + outboundMinutes + 1U};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        prepared,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession active;
    active.configurePersistence(temporary.path());
    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    ASSERT_TRUE(active.deployAlpha(99104));
    active.update(GameplayInput{}, 1.0F);
    EXPECT_EQ(active.profile().baseConstruction.dormitoryLevel, 2U);
    EXPECT_TRUE(active.takePresentationEvents().empty());

    ASSERT_TRUE(active.continueProfile()) << active.persistenceMessage();
    EXPECT_EQ(active.profile().baseConstruction.dormitoryLevel, 1U);
    EXPECT_TRUE(active.takePresentationEvents().empty());
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
    const AssetInstanceId bodyArmor = findDefinition(
        session.profile(),
        alpha_content::bodyArmor);
    const AssetInstanceId rig = findDefinition(
        session.profile(),
        alpha_content::chestRig);
    const AssetInstanceId backpack = findDefinition(
        session.profile(),
        alpha_content::backpack);
    const AssetInstanceId ammunition = findDefinition(
        session.profile(),
        alpha_content::ammunition);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        "tutorial-equip").succeeded);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::PrepareLoadout);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{bodyArmor, EquipmentSlotKind::BodyArmor},
        "tutorial-equip-armor").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{rig, EquipmentSlotKind::ChestRig},
        "tutorial-equip-rig").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        "tutorial-equip-pack").succeeded);
    EXPECT_EQ(session.profile().tutorial, TutorialProgress::PrepareLoadout);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            ammunition,
            0U,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0U),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "tutorial-carry-ammo").succeeded);
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

TEST(PersistentSessionTest, GroundContainerSaveFailurePreservesMemory)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewAlphaProfile(
        "failed-ground-container-save", content);
    const AssetInstanceId carriedBackpack = findDefinition(
        initial, alpha_content::backpack);
    ASSERT_NE(carriedBackpack, 0U);
    ASSERT_TRUE(executeInventory(
        initial,
        content,
        InventoryEquipCommand{
            carriedBackpack, EquipmentSlotKind::Backpack},
        CommandContext{initial.revision, "equip-save-failure-pack"})
                    .succeeded);
    const RegionalBaseSiteDefinitionId greyline{
        "regional_base_site.greyline_yard"};
    const AssetInstanceId groundPack = initial.assets.create(
        content.item(alpha_content::backpack),
        BaseGroundAssetLocation{greyline, Vec2{160.0F, 100.0F}});
    const AssetInstanceId medkit = initial.assets.create(
        content.item(alpha_content::medkit),
        StoredAssetLocation{
            ProfileContainerId::compartment(groundPack, 0),
            GridPosition{0, 0}});
    ASSERT_TRUE(validateProfileState(initial, content).valid);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial, content.contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "ground-container-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);
    const InventoryReceipt receipt =
        session.executeBaseGroundContainerInventory(
            groundPack,
            BaseGroundAccess{
                greyline,
                Vec2{120.0F, 100.0F},
                Vec2{160.0F, 100.0F},
                false,
                84.0F},
            InventoryMoveCommand{
                medkit,
                0,
                StoredAssetLocation{
                    ProfileContainerId::compartment(carriedBackpack, 0),
                    GridPosition{0, 0}},
                ItemOrientation::Degrees0},
            "ground-container-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}

TEST(PersistentSessionTest, PlaceableStorageSaveFailurePreservesMemory)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewAlphaProfile(
        "failed-placeable-storage-save", content);
    const ItemDefinition &definition = content.item(
        ItemDefinitionId{"item.container.base_storage_crate"});
    const auto origin = findFirstProfileFit(
        initial,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId crate = initial.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(initial, content.contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "placeable-storage-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);
    const RegionalBaseSiteDefinitionId greyline{
        "regional_base_site.greyline_yard"};
    const BaseGroundReceipt receipt = session.executeBaseGroundAsset(
        DropBaseGroundAssetCommand{
            crate,
            0,
            ItemOrientation::Degrees0,
            BaseGroundAccess{
                greyline,
                Vec2{400.0F, 400.0F},
                Vec2{700.0F, 600.0F},
                true,
                84.0F,
                BaseGroundPlacementContext{
                    ContentRect{{200.0F, 200.0F}, {1200.0F, 900.0F}},
                    {}}}},
        "placeable-storage-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}

TEST(PersistentSessionTest,
     CoreFacilityRepositionPersistsAndSaveFailurePreservesMemory)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewPublishedProfile(
        "persistent-facility-layout", content);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial, content.contentVersion()).succeeded);

    const RegionalBaseSiteDefinitionId greyline{
        "regional_base_site.greyline_yard"};
    const BaseFacilityDefinitionId warehouse{
        "base_facility.warehouse"};
    const RepositionBaseFacilityCommand command{
        warehouse,
        Vec2{400.0F, 400.0F},
        Vec2{300.0F, 220.0F},
        BaseFacilityLayoutAccess{
            greyline,
            ContentRect{{0.0F, 0.0F}, {1600.0F, 1120.0F}},
            {}}};

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const BaseFacilityLayoutReceipt moved =
        session.executeBaseFacilityLayout(command, "persist-core-facility");
    ASSERT_TRUE(moved.succeeded) << moved.message;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    const Vec2 persisted = reopened.profile().baseFacilityLayout
        .placements.at(greyline).at(warehouse);
    EXPECT_FLOAT_EQ(persisted.x, 0.25F);
    EXPECT_FLOAT_EQ(persisted.y, 400.0F / 1120.0F);

    const std::uint64_t before =
        profileStateFingerprint(reopened.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "facility-layout-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    reopened.configurePersistence(invalidDirectory);
    RepositionBaseFacilityCommand rejected = command;
    rejected.worldCenter = Vec2{700.0F, 600.0F};
    const BaseFacilityLayoutReceipt failed =
        reopened.executeBaseFacilityLayout(
            rejected, "failed-core-facility-save");
    EXPECT_FALSE(failed.succeeded);
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), before);
}

TEST(PersistentSessionTest,
     ReserveFacilityPlacementRestoresAndPersistsInOneCommand)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewPublishedProfile(
        "persistent-facility-install", content);
    const RegionalBaseSiteDefinitionId greyline{
        "regional_base_site.greyline_yard"};
    const BaseFacilityDefinitionId workshop{"base_facility.workshop"};
    initial.baseConstruction.facilities[workshop] =
        BaseConstructionState::FacilityPlacement::Reserve;
    initial.baseConstruction.facilityReserveStartedWorldMinutes[workshop] =
        initial.worldClock.elapsedWorldMinutes;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial, content.contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const BaseFacilityLayoutReceipt installed =
        session.executeInstallBaseFacilityAt(
            InstallBaseFacilityAtCommand{
                workshop,
                Vec2{800.0F, 600.0F},
                Vec2{270.0F, 170.0F},
                BaseFacilityLayoutAccess{
                    greyline,
                    ContentRect{{0.0F, 0.0F}, {1600.0F, 1120.0F}},
                    {}}},
            "persist-install-core-facility");
    ASSERT_TRUE(installed.succeeded) << installed.message;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(baseFacilityInstalled(reopened.profile(), workshop));
    EXPECT_FALSE(reopened.profile().baseConstruction
                     .facilityReserveStartedWorldMinutes.contains(workshop));
    const Vec2 persisted = reopened.profile().baseFacilityLayout
        .placements.at(greyline).at(workshop);
    EXPECT_FLOAT_EQ(persisted.x, 0.5F);
    EXPECT_FLOAT_EQ(persisted.y, 600.0F / 1120.0F);
}

TEST(PersistentSessionTest,
     KitchenWaterConstructionCompletesToReserveThenPlacementPersists)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewPublishedProfile(
        "persistent-kitchen-spatial", content);
    initial.baseConstruction.materialUnits = 5U;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial, content.contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    ASSERT_TRUE(session.executeStartBaseConstruction(
        BaseConstructionProjectDefinitionId{
            "base_construction.kitchen_water.level_1"},
        "persistent-start-kitchen").succeeded);
    session.advanceBaseWorldClock(480.0F);

    const BaseFacilityDefinitionId kitchenWater{
        "base_facility.kitchen_water"};
    ASSERT_EQ(
        session.profile().baseConstruction.facilities.at(kitchenWater),
        BaseConstructionState::FacilityPlacement::Reserve);
    ASSERT_TRUE(session.profile().baseConstruction
                    .facilityReserveStartedWorldMinutes.contains(
                        kitchenWater));

    const RegionalBaseSiteDefinitionId greyline{
        "regional_base_site.greyline_yard"};
    const BaseFacilityLayoutReceipt installed =
        session.executeInstallBaseFacilityAt(
            InstallBaseFacilityAtCommand{
                kitchenWater,
                Vec2{600.0F, 400.0F},
                Vec2{300.0F, 180.0F},
                BaseFacilityLayoutAccess{
                    greyline,
                    ContentRect{{0.0F, 0.0F}, {1600.0F, 1120.0F}},
                    {}}},
            "persistent-place-kitchen");
    ASSERT_TRUE(installed.succeeded) << installed.message;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().baseConstruction.kitchenWaterLevel, 1U);
    EXPECT_TRUE(baseFacilityInstalled(reopened.profile(), kitchenWater));
    EXPECT_FALSE(reopened.profile().baseConstruction
                     .facilityReserveStartedWorldMinutes.contains(
                         kitchenWater));
    const Vec2 persisted = reopened.profile().baseFacilityLayout
        .placements.at(greyline).at(kitchenWater);
    EXPECT_FLOAT_EQ(persisted.x, 0.375F);
    EXPECT_FLOAT_EQ(persisted.y, 400.0F / 1120.0F);
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

TEST(PersistentSessionTest,
     PermanentInteriorIntelligencePurchaseSurvivesNewProcessSession)
{
    SessionSaveDirectory temporary;
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-interior-intelligence"));
    const std::uint32_t startingCurrency = first.profile().currency;

    const RaidInteriorIntelligencePurchaseReceipt receipt =
        first.purchaseRaidInteriorIntelligence(
            {interiorId}, "persistent-interior-plan-purchase");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(first.profile().currency, startingCurrency - 180U);
    EXPECT_TRUE(first.profile().raidInteriorIntelligence.knows(interiorId));

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().currency, startingCurrency - 180U);
    EXPECT_TRUE(reopened.profile().raidInteriorIntelligence.knows(interiorId));
}

TEST(PersistentSessionTest,
     PermanentInteriorIntelligenceSaveFailurePreservesMemory)
{
    SessionSaveDirectory temporary;
    std::filesystem::create_directories(temporary.path());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "interior-intelligence-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    GameSession session;
    session.configurePersistence(invalidDirectory);
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const RaidSpaceDefinitionId interiorId{
        "raid_space.frontier_exchange.office"};

    const RaidInteriorIntelligencePurchaseReceipt receipt =
        session.purchaseRaidInteriorIntelligence(
            {interiorId}, "interior-plan-save-must-not-commit");

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(session.profile().raidInteriorIntelligence.knows(interiorId));
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
    EXPECT_TRUE(session.takePresentationEvents().empty());
    session.advanceBaseWorldClock(1.0F);
    EXPECT_FALSE(session.profile().baseConstruction.activeProject.has_value());
    EXPECT_EQ(session.profile().baseConstruction.dormitoryLevel, 2U);
    EXPECT_EQ(session.profile().basePopulation.bedCapacity, 14U);
    EXPECT_EQ(session.profile().revision, startedRevision + 1U);
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::BaseDormitoryUpgradeCompleted});
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
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::BaseWorkshopUpgradeCompleted});
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
    EXPECT_TRUE(session.takePresentationEvents().empty());
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
    EXPECT_EQ(
        reopened.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::BaseResidentTreatmentCompleted});

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
     RecoveryTaskPersistsAcrossRestartAndBaseRestMakesItCollectable)
{
    SessionSaveDirectory temporary;
    ProfileState initial = makeNewAlphaProfile(
        "persistent-recovery-task",
        publishedContentRegistry());
    const auto rifle = std::find_if(
        initial.assets.records().begin(),
        initial.assets.records().end(),
        [](const auto &entry)
        { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, initial.assets.records().end());
    const std::string recordId{"persistent-recovery-settlement"};
    initial.committedSettlements.insert(recordId);
    initial.lostRaidRecords.emplace(
        recordId,
        LostRaidRecord{
            recordId,
            "persistent-recovery-raid",
            recordId,
            MapDefinitionId{"map.v0.test"},
            "LOW",
            RaidResultOutcome::PlayerDead,
            initial.worldClock.elapsedWorldMinutes,
            0U});
    initial.assets.findMutable(rifle->first)->location =
        LostRaidAssetLocation{
            recordId, EquipmentSlotKind::PrimaryWeapon};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        initial,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const RecoveryTaskReceipt started = session.startRecoveryTask(
        recordId, "persistent-recovery-start");
    ASSERT_TRUE(started.succeeded) << started.message;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.recoveryTaskProjection().has_value());
    EXPECT_FALSE(reopened.recoveryTaskProjection()->readyForCollection);
    ASSERT_TRUE(reopened.executeBaseRest(
        6U, "persistent-recovery-rest").succeeded);
    ASSERT_TRUE(reopened.recoveryTaskProjection().has_value());
    EXPECT_TRUE(reopened.recoveryTaskProjection()->readyForCollection);

    GameSession ready;
    ready.configurePersistence(temporary.path());
    ASSERT_TRUE(ready.continueProfile()) << ready.persistenceMessage();
    ASSERT_TRUE(ready.recoveryTaskProjection().has_value());
    const RecoveryTaskReceipt collected = ready.collectRecoveryTask(
        "persistent-recovery-collect");
    ASSERT_TRUE(collected.succeeded) << collected.message;
    EXPECT_FALSE(ready.recoveryTaskProjection().has_value());
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
    EXPECT_EQ(
        processing.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::BaseManufacturingCompleted});

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

TEST(PersistentSessionTest,
     MainBaseMigrationAndFacilityReinstallPersistAcrossProcesses)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewAlphaProfile(
        "persistent-main-base-migration", content);
    const RegionalBaseSiteDefinitionId ashworksSite{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId ashworksOutpost{
        "regional_outpost.ashworks_logistics_yard"};
    const BaseFacilityDefinitionId kitchenWater{
        "base_facility.kitchen_water"};
    const BaseFacilityDefinitionId workshop{
        "base_facility.workshop"};
    initial.regionalOperations.baseSites.at(ashworksSite).unlocked = true;
    initial.regionalOperations.outposts.at(ashworksOutpost).unlocked = true;
    initial.regionalOperations.outposts.at(ashworksOutpost).established = true;
    initial.baseConstruction.kitchenWaterLevel = 1U;
    initial.baseConstruction.facilities[kitchenWater] =
        BaseConstructionState::FacilityPlacement::Installed;
    initializeBaseFacilityLayouts(initial, content);
    ASSERT_TRUE(validateProfileState(initial, content).valid);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(initial, content.contentVersion()).succeeded);

    GameSession migrated;
    migrated.configurePersistence(temporary.path());
    ASSERT_TRUE(migrated.continueProfile()) << migrated.persistenceMessage();
    const BaseMigrationReceipt receipt = migrated.executeBaseMigration(
        ashworksSite, "persistent-migrate-main-base");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_FALSE(baseFacilityInstalled(migrated.profile(), workshop));

    GameSession packed;
    packed.configurePersistence(temporary.path());
    ASSERT_TRUE(packed.continueProfile()) << packed.persistenceMessage();
    EXPECT_EQ(packed.profile().regionalOperations.activeBaseNodeId,
              RegionNodeDefinitionId{
                  "region_node.base.ashworks_logistics_yard"});
    EXPECT_FALSE(baseFacilityInstalled(packed.profile(), workshop));
    const InstallBaseFacilityReceipt installed =
        packed.executeInstallBaseFacility(
            workshop, "persistent-install-workshop");
    ASSERT_TRUE(installed.succeeded) << installed.message;

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(baseFacilityInstalled(reopened.profile(), workshop));
    EXPECT_EQ(reopened.profile().regionalOperations.technologyCore
                  .baseSiteDefinitionId,
              ashworksSite);
}

TEST(PersistentSessionTest, MainBaseMigrationSaveFailureIsZeroCommit)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewAlphaProfile(
        "failed-main-base-migration", content);
    const RegionalBaseSiteDefinitionId ashworksSite{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId ashworksOutpost{
        "regional_outpost.ashworks_logistics_yard"};
    initial.regionalOperations.baseSites.at(ashworksSite).unlocked = true;
    initial.regionalOperations.outposts.at(ashworksOutpost).unlocked = true;
    initial.regionalOperations.outposts.at(ashworksOutpost).established = true;
    initial.baseConstruction.kitchenWaterLevel = 1U;
    initial.baseConstruction.facilities[
        BaseFacilityDefinitionId{"base_facility.kitchen_water"}] =
        BaseConstructionState::FacilityPlacement::Installed;
    initializeBaseFacilityLayouts(initial, content);
    ASSERT_TRUE(validateProfileState(initial, content).valid);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(initial, content.contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const std::uint64_t before = profileStateFingerprint(session.profile());
    const std::filesystem::path invalidDirectory =
        temporary.path() / "migration-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    session.configurePersistence(invalidDirectory);

    const BaseMigrationReceipt receipt = session.executeBaseMigration(
        ashworksSite, "migration-save-must-not-commit");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
}

TEST(PersistentSessionTest,
     BaseSiteFeatureRepairPersistsAndSaveFailureIsZeroCommit)
{
    SessionSaveDirectory temporary;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState initial = makeNewAlphaProfile(
        "persistent-base-site-feature", content);
    const RegionalBaseSiteDefinitionId ashworksSite{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId ashworksOutpost{
        "regional_outpost.ashworks_logistics_yard"};
    initial.regionalOperations.baseSites.at(ashworksSite).unlocked = true;
    initial.regionalOperations.outposts.at(ashworksOutpost).unlocked = true;
    initial.regionalOperations.activeBaseNodeId =
        RegionNodeDefinitionId{"region_node.base.ashworks_logistics_yard"};
    initial.regionalOperations.technologyCore.baseSiteDefinitionId =
        ashworksSite;
    initial.baseConstruction.materialUnits = 20U;
    ASSERT_TRUE(validateProfileState(initial, content).valid);
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(initial, content.contentVersion()).succeeded);

    GameSession repaired;
    repaired.configurePersistence(temporary.path());
    ASSERT_TRUE(repaired.continueProfile()) << repaired.persistenceMessage();
    const BaseSiteFeatureRepairReceipt receipt =
        repaired.executeBaseSiteFeatureRepair(
            ashworksSite, "persistent-repair-site-feature");
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(repaired.profile().baseConstruction.materialUnits, 5U);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(reopened.profile().regionalOperations.baseSites.at(
        ashworksSite).uniqueFeatureRepaired);
    EXPECT_EQ(reopened.profile().baseConstruction.materialUnits, 5U);

    SessionSaveDirectory failedTemporary;
    SaveRepository failedRepository{failedTemporary.path()};
    ASSERT_TRUE(failedRepository.save(initial, content.contentVersion())
                    .succeeded);
    GameSession failedSession;
    failedSession.configurePersistence(failedTemporary.path());
    ASSERT_TRUE(failedSession.continueProfile())
        << failedSession.persistenceMessage();
    const std::uint64_t before =
        profileStateFingerprint(failedSession.profile());
    const std::filesystem::path invalidDirectory =
        failedTemporary.path() / "feature-not-a-directory";
    {
        std::ofstream file(invalidDirectory);
        file << "occupied";
    }
    failedSession.configurePersistence(invalidDirectory);

    const BaseSiteFeatureRepairReceipt failed =
        failedSession.executeBaseSiteFeatureRepair(
            ashworksSite, "site-feature-save-must-not-commit");
    EXPECT_FALSE(failed.succeeded);
    EXPECT_EQ(profileStateFingerprint(failedSession.profile()), before);
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
TEST(PersistentSessionTest, HomePerimeterSnapshotSurvivesProcessRestart)
{
    SessionSaveDirectory temporary;
    GameSession first;
    first.configurePersistence(temporary.path());
    ASSERT_TRUE(first.startNewProfile("persistent-home-perimeter"));
    BaseWorld firstWorld;
    static_cast<void>(first.updateBaseWorld(
        firstWorld, GameplayInput{}, 1.0F / 60.0F));
    const RegionalBaseSiteDefinitionId site{
        firstWorld.siteDefinitionId()};
    ASSERT_TRUE(first.profile().homePerimeter.sites.contains(site));
    const HomePerimeterSiteSnapshot expected =
        first.profile().homePerimeter.sites.at(site);
    ASSERT_GE(expected.enemies.size(), 7U);
    ASSERT_GE(expected.lootAssetIds.size(), 2U);

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.profile().homePerimeter.sites.contains(site));
    EXPECT_EQ(reopened.profile().homePerimeter.sites.at(site), expected);
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
}
