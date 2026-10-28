#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#include "game_session.h"
#include "raid_credential_legacy_fixture.h"
#include "raid_runner_legacy_fixture.h"
#include "alpha_content_ids.h"
#include "base_world.h"

namespace
{
    const ItemDefinitionId kBaseStorageCrate{
        "item.container.base_storage_crate"};
    class SequenceLootRandomSource final
        : public LootRandomSource
    {
    public:
        explicit SequenceLootRandomSource(
            std::vector<std::uint32_t> values)
            : values_{std::move(values)}
        {
        }

        std::uint32_t next(
            std::uint32_t upperExclusive) override
        {
            if (position_ >= values_.size())
            {
                throw std::runtime_error{
                    "SequenceLootRandomSource exhausted"};
            }

            const std::uint32_t value =
                values_[position_++];

            if (value >= upperExclusive)
            {
                throw std::out_of_range{
                    "SequenceLootRandomSource value is out of range"};
            }

            return value;
        }

    private:
        std::vector<std::uint32_t> values_;
        std::size_t position_{};
    };

    GameplayInput moveLeftInput()
    {
        GameplayInput input{};
        input.moveLeft = true;
        return input;
    }

    GameplayInput moveDownInput()
    {
        GameplayInput input{};
        input.moveDown = true;
        return input;
    }

    AssetInstanceId firstAsset(
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
        return 0U;
    }

    std::size_t totalAmmunition(
        const ProfileState &profile,
        const ItemDefinitionId &definitionId)
    {
        std::size_t total{};
        for (const auto &[id, asset] : profile.assets.records())
        {
            static_cast<void>(id);
            if (asset.definitionId == definitionId)
            {
                total += asset.quantity;
            }
            total += static_cast<std::size_t>(std::count_if(
                asset.magazineRounds.begin(),
                asset.magazineRounds.end(),
                [&](const MagazineRoundRecord &round)
                { return round.definitionId == definitionId; }));
            if (asset.chamberedRound.has_value() &&
                asset.chamberedRound->definitionId == definitionId)
            {
                ++total;
            }
        }
        return total;
    }

    void pickUpDefaultMedkit(
        GameSession &session)
    {
        session.update(moveLeftInput(), 0.5F);
        session.update(moveDownInput(), 0.25F);

        GameplayInput interact{};
        interact.interactJustPressed = true;
        session.update(interact, 0.0F);

        ASSERT_EQ(
            session.world().inventory().placedItems().size(),
            1U);
        EXPECT_EQ(
            session.world().inventory().placedItems().front()
                .item.definitionId(),
            ItemId::Medkit);
    }

    void extractFromDefaultMedkitPosition(
        GameSession &session)
    {
        session.update(moveLeftInput(), 1.5F);
        session.update(moveDownInput(), 0.45F);
        session.update(GameplayInput{}, 3.0F);

        ASSERT_EQ(
            session.world().raidSession().state(),
            RaidSessionState::Extracted);
    }

    void moveFromCabinetToExtraction(
        GameSession &session)
    {
        session.update(moveLeftInput(), 3.0F);
        session.update(moveDownInput(), 0.65F);
        session.update(GameplayInput{}, 3.0F);

        ASSERT_EQ(
            session.world().raidSession().state(),
            RaidSessionState::Extracted);
    }
}

TEST(GameSessionTest, BasePlaceablePurchaseReturnsCreatedStableAsset)
{
    GameSession session;
    const AssetInstanceId expectedId = session.profile().assets.nextAssetId();
    const std::uint32_t currencyBefore = session.profile().currency;

    const auto receipt = session.purchaseBasePlaceable(
        kBaseStorageCrate, "purchase-placeable-from-build-panel");

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.assetId, expectedId);
    const AssetRecord *created = session.profile().assets.find(expectedId);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->definitionId, kBaseStorageCrate);
    ASSERT_TRUE(std::holds_alternative<StoredAssetLocation>(
        created->location));
    EXPECT_EQ(
        std::get<StoredAssetLocation>(created->location).container,
        ProfileContainerId::stash());
    EXPECT_LT(session.profile().currency, currencyBefore);
}

TEST(GameSessionTest, DefaultSessionStartsRaidOneWithEmptyStash)
{
    GameSession session;

    EXPECT_EQ(session.state(), GameSessionState::InRaid);
    EXPECT_EQ(session.raidNumber(), 1U);
    EXPECT_EQ(
        session.world().raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_EQ(session.stash().stackCount(), 0U);
    EXPECT_EQ(session.nextItemInstanceId(), 7U);
    EXPECT_FALSE(session.canStartNextRaid());
}

TEST(GameSessionTest, BaseClockUsesScaledSimulationTimeAndDailyDemand)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("base-clock-daily-demand"));
    const ProfileRevision revision = session.profile().revision;

    session.advanceBaseWorldClock(0.5F);
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute);
    session.advanceBaseWorldClock(0.5F);
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute + 1U);

    session.advanceBaseWorldClock(959.0F);
    const WorldClockProjection clock = session.worldClockProjection();
    EXPECT_EQ(clock.day, 2U);
    EXPECT_EQ(clock.hour, 0U);
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(
        session.profile().baseResources.pool,
        (BaseResourceBundle{32, 34, 35, 36}));
    EXPECT_EQ(
        session.profile().baseResources.resolvedDemandCycleCount,
        1U);
    EXPECT_EQ(session.profile().baseMorale.resolvedDayCount, 1U);
    EXPECT_EQ(session.profile().baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(session.profile().baseMorale.trend, BaseMoraleTrend::Steady);
}

TEST(GameSessionTest, BaseWorldFireConsumesRealAmmoAndWeaponCondition)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("base-shared-shooting"));
    const AssetInstanceId rifle = firstAsset(
        session.profile(), alpha_content::rifle);
    const AssetInstanceId magazine = firstAsset(
        session.profile(), alpha_content::magazine);
    const AssetInstanceId ammunition = firstAsset(
        session.profile(), alpha_content::ammunition);
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(magazine, 0U);
    ASSERT_NE(ammunition, 0U);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazine, ammunition, 10U},
        "base-load-magazine").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{
            rifle, EquipmentSlotKind::PrimaryWeapon},
        "base-equip-rifle").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        InstallMagazineAndChamberCommand{rifle, magazine},
        "base-install-magazine").succeeded);

    const std::size_t roundsBefore = totalAmmunition(
        session.profile(), alpha_content::ammunition);
    const std::uint32_t durabilityBefore =
        session.profile().assets.find(rifle)->currentDurability;
    BaseWorld world;
    const Vec2 player = world.playerPosition();
    GameplayInput fire{};
    fire.aimWorldPosition = Vec2{player.x + 900.0F, player.y};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    static_cast<void>(session.updateBaseWorld(world, fire, 1.0F / 60.0F));

    EXPECT_TRUE(world.shotFiredLastUpdate());
    EXPECT_EQ(
        totalAmmunition(session.profile(), alpha_content::ammunition),
        roundsBefore - 1U);
    EXPECT_LT(
        session.profile().assets.find(rifle)->currentDurability,
        durabilityBefore);
    EXPECT_FALSE(session.profile().pendingRaid.has_value());
    EXPECT_FALSE(session.profile().lastRaidResult.has_value());
}

TEST(GameSessionTest, WalkingOutAndBackCommitsOneLocalOuting)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("base-perimeter-outing"));
    BaseWorld world;
    static_cast<void>(session.updateBaseWorld(
        world, GameplayInput{}, 1.0F / 60.0F));
    const RegionalBaseSiteDefinitionId site{world.siteDefinitionId()};

    GameplayInput outward;
    outward.moveDown = true;
    outward.sprint = true;
    for (int step{}; step < 420 &&
         !homePerimeterOutingActive(session.profile(), site); ++step)
    {
        static_cast<void>(session.updateBaseWorld(
            world, outward, 1.0F / 60.0F));
    }
    ASSERT_TRUE(homePerimeterOutingActive(session.profile(), site));
    const std::uint64_t beforeReturn =
        session.profile().worldClock.elapsedWorldMinutes;

    GameplayInput inward;
    inward.moveUp = true;
    inward.sprint = true;
    for (int step{}; step < 420 &&
         homePerimeterOutingActive(session.profile(), site); ++step)
    {
        static_cast<void>(session.updateBaseWorld(
            world, inward, 1.0F / 60.0F));
    }

    EXPECT_FALSE(homePerimeterOutingActive(session.profile(), site));
    EXPECT_EQ(session.profile().worldClock.elapsedWorldMinutes,
              beforeReturn + kHomePerimeterReturnMinutes);
    EXPECT_FALSE(session.profile().pendingRaid.has_value());
    EXPECT_FALSE(session.profile().lastRaidResult.has_value());
    EXPECT_TRUE(session.profile().lostRaidRecords.empty());
}

TEST(GameSessionTest, RaidTravelPreviewProjectsSelectedMapWithoutMutation)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("session-travel-preview"));
    const std::uint64_t fingerprint =
        profileStateFingerprint(session.profile());

    const auto preview = session.raidTravelPreview(
        MapDefinitionId{"map.raid.riverside"});

    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->outboundMinutes, 90U);
    EXPECT_EQ(preview->arrival.hour, 9U);
    EXPECT_EQ(preview->arrival.minute, 30U);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_FALSE(session.raidTravelPreview(
        MapDefinitionId{"map.raid.missing"}).has_value());
}

TEST(GameSessionTest, ActiveRaidRejectsRestartWithoutMutation)
{
    GameSession session;
    const GameplayWorld *const originalWorld =
        &session.world();
    const ItemInstanceId originalNextId =
        session.nextItemInstanceId();

    EXPECT_FALSE(session.startNextRaid());
    EXPECT_EQ(&session.world(), originalWorld);
    EXPECT_EQ(session.raidNumber(), 1U);
    EXPECT_EQ(session.nextItemInstanceId(), originalNextId);
    EXPECT_EQ(session.state(), GameSessionState::InRaid);
}

TEST(GameSessionTest, ExtractionPersistsStashAndStartsFreshSecondRaid)
{
    GameSession session;
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);

    ASSERT_EQ(session.state(), GameSessionState::BetweenRaids);
    ASSERT_EQ(session.stash().stackCount(), 1U);
    ASSERT_EQ(session.stash().unitCount(), 1U);
    const ItemInstanceId storedId =
        session.stash().inventory().placedItems().front()
            .item.instanceId();
    const ItemInstanceId secondRaidFirstId =
        session.nextItemInstanceId();

    ASSERT_TRUE(session.canStartNextRaid());
    ASSERT_TRUE(session.startNextRaid());

    EXPECT_EQ(session.state(), GameSessionState::InRaid);
    EXPECT_EQ(session.raidNumber(), 2U);
    EXPECT_FALSE(session.canStartNextRaid());
    EXPECT_EQ(
        session.world().raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(
        session.world().raidSession().raidTimeRemaining(),
        180.0F);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_FALSE(session.world().storageCabinet().isSearched());
    EXPECT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(
        session.stash().inventory().placedItems().front()
            .item.instanceId(),
        storedId);

    ASSERT_FALSE(session.world().groundItems().empty());
    EXPECT_EQ(
        session.world().groundItems().front()
            .item().instanceId(),
        secondRaidFirstId);
    EXPECT_TRUE(std::all_of(
        session.world().groundItems().begin(),
        session.world().groundItems().end(),
        [storedId](const GroundItem &groundItem)
        {
            return groundItem.item().instanceId() > storedId;
        }));
    EXPECT_EQ(
        session.nextItemInstanceId(),
        secondRaidFirstId + 6U);
}

TEST(GameSessionTest, SearchTransferExtractionAndRestartFormOneSuccessSlice)
{
    GameSession session{std::vector<EnemySpawn>{}};

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    session.update(moveRight, 1.0F);
    ASSERT_TRUE(
        session.world().canInteractWithContainer());

    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(
        session.world().searchStorageCabinet(random));
    ASSERT_EQ(
        session.world().containerInventory()
            .placedItems()
            .size(),
        2U);

    const PlacedItem &cabinetItem =
        session.world().containerInventory()
            .placedItems()
            .front();
    const ItemInstanceId transferredId =
        cabinetItem.item.instanceId();
    const std::uint32_t transferredQuantity =
        cabinetItem.item.quantity();

    ASSERT_TRUE(
        session.world().transferInventoryItemQuantity(
            false,
            transferredId,
            transferredQuantity));
    ASSERT_EQ(
        session.world().inventory()
            .placedItems()
            .size(),
        1U);

    moveFromCabinetToExtraction(session);

    ASSERT_EQ(
        session.state(),
        GameSessionState::BetweenRaids);
    ASSERT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(session.stash().unitCount(), 1U);
    EXPECT_EQ(
        session.stash().inventory()
            .placedItems()
            .front()
            .item.instanceId(),
        transferredId);

    ASSERT_TRUE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 2U);
    EXPECT_EQ(session.world().player().health(), 3);
    EXPECT_EQ(session.world().player().maxHealth(), 3);
    EXPECT_TRUE(
        session.world().inventory()
            .placedItems()
            .empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
}

TEST(GameSessionTest, SecondRaidLossDoesNotRemovePreviousStash)
{
    GameSession session;
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);
    ASSERT_TRUE(session.startNextRaid());

    pickUpDefaultMedkit(session);
    ASSERT_TRUE(session.world().damagePlayer(3));
    session.update(GameplayInput{}, 0.0F);

    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_EQ(
        session.settlement().state(),
        RaidSettlementState::PlayerDead);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(session.stash().unitCount(), 1U);

    ASSERT_TRUE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 3U);
    EXPECT_EQ(session.world().player().health(), 3);
    EXPECT_TRUE(
        session.world().inventory()
            .placedItems()
            .empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
}

TEST(GameSessionTest, BlockedSettlementCannotStartAnotherRaid)
{
    GameSession session{{1, 1}};
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);

    // 1x2 Medkit cannot fit into the 1x1 Stash, so extraction remains
    // retryable and the player inventory is preserved.
    EXPECT_EQ(
        session.state(),
        GameSessionState::SettlementBlocked);
    EXPECT_EQ(
        session.settlement().state(),
        RaidSettlementState::Blocked);
    EXPECT_FALSE(session.canStartNextRaid());
    EXPECT_FALSE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 1U);
    EXPECT_EQ(
        session.world().inventory().placedItems().size(),
        1U);
    EXPECT_EQ(session.stash().stackCount(), 0U);
}

TEST(GameSessionTest, RestartIsAcceptedOnlyOncePerCompletedRaid)
{
    GameSession session;
    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);

    ASSERT_TRUE(session.startNextRaid());
    EXPECT_FALSE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 2U);
}

TEST(GameSessionTest, NextRaidStartsWithFreshWeaponFireState)
{
    GameSession freshSession;
    const float freshSpread =
        freshSession.world().weaponSpreadDegrees();
    GameSession session;
    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    session.update(fire, 0.0F);

    ASSERT_GT(session.world().weaponSpreadDegrees(), 0.0F);
    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);
    ASSERT_TRUE(session.startNextRaid());

    EXPECT_FLOAT_EQ(session.world().weaponSpreadDegrees(), freshSpread);
    EXPECT_FLOAT_EQ(session.world().weaponVisualRecoilPixels(), 0.0F);
}

TEST(GameSessionTest, StateNamesAreStableForDebugOutput)
{
    EXPECT_STREQ(
        gameSessionStateName(GameSessionState::InRaid),
        "InRaid");
    EXPECT_STREQ(
        gameSessionStateName(
            GameSessionState::SettlementBlocked),
        "SettlementBlocked");
    EXPECT_STREQ(
        gameSessionStateName(GameSessionState::BetweenRaids),
        "BetweenRaids");
}

namespace
{
class StashMigrationSaveDirectory
{
public:
    StashMigrationSaveDirectory()
        : path{std::filesystem::temp_directory_path() /
               ("raidline-stash-migration-session-" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()))}
    { std::filesystem::create_directories(path); }
    ~StashMigrationSaveDirectory()
    { std::error_code error; std::filesystem::remove_all(path, error); }
    std::filesystem::path path;
};
std::string migrationFileBytes(const std::filesystem::path &path)
{
    std::ifstream input{path, std::ios::binary};
    std::ostringstream bytes; bytes << input.rdbuf(); return bytes.str();
}
void writeMigrationFixture(const std::filesystem::path &path, std::string_view bytes)
{
    std::ofstream output{path, std::ios::binary};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
std::vector<std::filesystem::path> stashMigrationBackups(const std::filesystem::path &directory)
{
    std::vector<std::filesystem::path> result;
    for (const auto &entry : std::filesystem::directory_iterator{directory})
        if (entry.path().filename().string().find("pre-stash-pages-v1.") != std::string::npos)
            result.push_back(entry.path());
    return result;
}
ProfileState crossedLegacySessionProfile()
{
    const auto &content = publishedContentRegistry();
    auto legacy = makeNewAlphaProfile("legacy-session-crossed-pages", content);
    legacy.stashLayoutVersion = 0;
    legacy.assets = AssetRegistry{};
    const auto weapon = legacy.assets.create(content.item(alpha_content::rifle),
        StoredAssetLocation{ProfileContainerId::stash(), {7, 0}});
    legacy.assets.findMutable(weapon)->currentDurability = 6320;
    legacy.assets.findMutable(weapon)->chamberedRound =
        MagazineRoundRecord{alpha_content::ammunition, std::nullopt};
    const auto magazine = legacy.assets.create(content.item(alpha_content::magazine),
        InstalledMagazineLocation{weapon});
    legacy.assets.findMutable(magazine)->magazineRounds.assign(
        3, MagazineRoundRecord{alpha_content::ammunition, std::nullopt});
    const auto bag = legacy.assets.create(content.item(alpha_content::backpack),
        StoredAssetLocation{ProfileContainerId::stash(), {7, 7}});
    const auto medkit = legacy.assets.create(content.item(alpha_content::medkit),
        StoredAssetLocation{ProfileContainerId::compartment(bag, 0), {0, 0}});
    legacy.assets.findMutable(medkit)->remainingCharges = 1;
    static_cast<void>(legacy.assets.create(content.item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::stash(), {16, 12}}, 17));
    legacy.assets.setNextAssetIdForLoad(555555U);
    return legacy;
}
}

TEST(GameSessionTest, LegacyStashPagesMigrateOncePreservingAssetsStateAndImmutableSource)
{
    StashMigrationSaveDirectory temporary;
    const auto &content = publishedContentRegistry();
    auto legacy = crossedLegacySessionProfile();
    ASSERT_TRUE(validateProfileState(legacy, content).valid);
    const auto original = serializeProfileEnvelope(legacy, content.contentVersion(), 54) + "\r\n";
    writeMigrationFixture(temporary.path / "profile.json", original);
    const auto count = legacy.assets.records().size();
    GameSession session;
    session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_EQ(session.profile().stashLayoutVersion, 1U);
    EXPECT_EQ(session.profile().revision, legacy.revision + 1U);
    EXPECT_EQ(session.profile().assets.nextAssetId(), legacy.assets.nextAssetId());
    EXPECT_EQ(session.profile().assets.records().size(), count);
    for (const auto &[id, oldAsset] : legacy.assets.records())
    {
        const auto *restored = session.profile().assets.find(id);
        ASSERT_NE(restored, nullptr);
        EXPECT_EQ(restored->definitionId, oldAsset.definitionId);
        EXPECT_EQ(restored->quantity, oldAsset.quantity);
        EXPECT_EQ(restored->orientation, oldAsset.orientation);
        EXPECT_EQ(restored->remainingCharges, oldAsset.remainingCharges);
        EXPECT_EQ(restored->currentDurability, oldAsset.currentDurability);
        EXPECT_EQ(restored->currentMaximumDurability, oldAsset.currentMaximumDurability);
        EXPECT_EQ(restored->magazineRounds, oldAsset.magazineRounds);
        EXPECT_EQ(restored->chamberedRound, oldAsset.chamberedRound);
        EXPECT_EQ(restored->reliefBatchId, oldAsset.reliefBatchId);
        EXPECT_EQ(restored->weaponMalfunction, oldAsset.weaponMalfunction);
        EXPECT_EQ(restored->deviceState, oldAsset.deviceState);
        if (const auto *oldPlace = std::get_if<StoredAssetLocation>(&oldAsset.location);
            oldPlace && oldPlace->container == ProfileContainerId::stash())
        {
            const auto &place = std::get<StoredAssetLocation>(restored->location);
            EXPECT_TRUE(stashFootprintFitsPage(place.origin, inventoryFootprint(
                content.item(restored->definitionId), restored->orientation)));
            if (stashFootprintFitsPage(oldPlace->origin, inventoryFootprint(
                    content.item(oldAsset.definitionId), oldAsset.orientation)))
                EXPECT_EQ(restored->location, oldAsset.location);
        }
        else EXPECT_EQ(restored->location, oldAsset.location);
    }
    auto backups = stashMigrationBackups(temporary.path);
    ASSERT_EQ(backups.size(), 1U);
    EXPECT_EQ(migrationFileBytes(backups.front()), original);
    const auto committed = migrationFileBytes(temporary.path / "profile.json");
    const auto fingerprint = profileStateFingerprint(session.profile());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_EQ(migrationFileBytes(temporary.path / "profile.json"), committed);
    EXPECT_EQ(stashMigrationBackups(temporary.path).size(), backups.size());
    EXPECT_EQ(migrationFileBytes(backups.front()), original);
}

TEST(GameSessionTest, LegacyPageMigrationSaveFailurePreservesDiskAndRunningSession)
{
    StashMigrationSaveDirectory temporary;
    const auto &content = publishedContentRegistry();
    const auto legacy = crossedLegacySessionProfile();
    const auto original = serializeProfileEnvelope(legacy, content.contentVersion(), 54);
    writeMigrationFixture(temporary.path / "profile.json", original);
    writeMigrationFixture(temporary.path / "profile.backup.json", original + "\n");
    std::filesystem::create_directories(temporary.path / "profile.tmp.json");
    GameSession running;
    ASSERT_TRUE(running.startNewProfile("unrelated-running-state"));
    const auto before = serializeProfileEnvelope(running.profile(), content.contentVersion());
    const auto state = running.state();
    const auto world = &running.world();
    const auto recovered = running.recoveredAbandonedRaid();
    running.configurePersistence(temporary.path);
    EXPECT_FALSE(running.continueProfile());
    EXPECT_EQ(serializeProfileEnvelope(running.profile(), content.contentVersion()), before);
    EXPECT_EQ(running.state(), state);
    EXPECT_EQ(&running.world(), world);
    EXPECT_EQ(running.recoveredAbandonedRaid(), recovered);
    EXPECT_EQ(migrationFileBytes(temporary.path / "profile.json"), original);
    EXPECT_EQ(migrationFileBytes(temporary.path / "profile.backup.json"), original + "\n");
    EXPECT_EQ(stashMigrationBackups(temporary.path).size(), 2U);
    std::filesystem::remove(temporary.path / "profile.tmp.json");
    ASSERT_TRUE(running.continueProfile()) << running.persistenceMessage();
    EXPECT_EQ(running.profile().stashLayoutVersion, 1U);
    EXPECT_EQ(stashMigrationBackups(temporary.path).size(), 2U);
}

TEST(GameSessionTest, LegacyMigrationRecoversBackupAndPreservesBothOriginalSources)
{
    StashMigrationSaveDirectory temporary;
    const auto &content = publishedContentRegistry();
    const auto legacy = crossedLegacySessionProfile();
    const auto original = serializeProfileEnvelope(legacy, content.contentVersion(), 54);
    writeMigrationFixture(temporary.path / "profile.json", "{original truncated save");
    writeMigrationFixture(temporary.path / "profile.backup.json", original);
    GameSession session;
    session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_EQ(session.lastSaveLoadStatus(), SaveLoadStatus::RecoveredBackup);
    EXPECT_EQ(session.profile().stashLayoutVersion, 1U);
    const auto backups = stashMigrationBackups(temporary.path);
    ASSERT_EQ(backups.size(), 2U);
    bool foundPrimary{}, foundBackup{};
    for (const auto &path : backups)
    {
        const auto bytes = migrationFileBytes(path);
        foundPrimary = foundPrimary || bytes == "{original truncated save";
        foundBackup = foundBackup || bytes == original;
    }
    EXPECT_TRUE(foundPrimary);
    EXPECT_TRUE(foundBackup);
}

TEST(GameSessionTest, LegacyMigrationDoesNotReplaceExistingImmutableBackup)
{
    StashMigrationSaveDirectory temporary;
    const auto &content = publishedContentRegistry();
    const auto original = serializeProfileEnvelope(crossedLegacySessionProfile(), content.contentVersion(), 54);
    writeMigrationFixture(temporary.path / "profile.json", original);
    SaveRepository repository{temporary.path};
    ASSERT_TRUE(repository.preserveStashMigrationSources(SaveLoadStatus::LoadedPrimary).succeeded);
    const auto backups = stashMigrationBackups(temporary.path);
    ASSERT_EQ(backups.size(), 1U);
    writeMigrationFixture(backups.front(), "existing immutable data");
    GameSession session;
    const auto before = profileStateFingerprint(session.profile());
    session.configurePersistence(temporary.path);
    EXPECT_FALSE(session.continueProfile());
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_EQ(migrationFileBytes(temporary.path / "profile.json"), original);
    EXPECT_EQ(migrationFileBytes(backups.front()), "existing immutable data");
}

TEST(GameSessionTest, LegacyPendingRaidRollsBackBeforeIndependentPageMigration)
{
    StashMigrationSaveDirectory temporary;
    const auto content = contentBeforeRunner("quick-melee-self-defense-content-66");
    auto legacy = crossedLegacySessionProfile();
    const auto originalAssets = legacy.assets;
    const auto rifle = firstAsset(legacy, alpha_content::rifle);
    ASSERT_TRUE(executeDeploy(legacy, content,
        DeployCommand{"legacy-page-raid", "legacy-page-settlement", 7301U,
                      MapDefinitionId{"map.raid.frontier_exchange"}},
        CommandContext{legacy.revision, "legacy-page-deploy"}).succeeded);
    ASSERT_TRUE(legacy.pendingRaid->startingAssets);
    stripCredentialExtractionForLegacySnapshot(legacy);
    legacy.assets.findMutable(rifle)->currentDurability = 1234;
    ASSERT_TRUE(validateProfileState(legacy, content).valid);
    const auto original = serializeProfileEnvelope(legacy, content.contentVersion(), 54);
    writeMigrationFixture(temporary.path / "profile.json", original);
    GameSession session;
    session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_TRUE(session.recoveredAbandonedRaid());
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_EQ(session.profile().stashLayoutVersion, 1U);
    EXPECT_EQ(session.profile().assets.records().size(), originalAssets.records().size());
    EXPECT_EQ(session.profile().assets.nextAssetId(), originalAssets.nextAssetId());
    EXPECT_EQ(session.profile().assets.find(rifle)->currentDurability,
              originalAssets.find(rifle)->currentDurability);
    EXPECT_EQ(session.profile().revision, legacy.revision + 2U);
    EXPECT_EQ(migrationFileBytes(stashMigrationBackups(temporary.path).front()), original);
    const auto revision = session.profile().revision;
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    EXPECT_EQ(session.profile().revision, revision);
}
