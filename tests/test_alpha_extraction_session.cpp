#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "alpha_content_ids.h"
#include "game_session.h"

namespace
{
class TemporarySaveDirectory
{
public:
    TemporarySaveDirectory()
        : path_{std::filesystem::temp_directory_path() /
                ("raidline-alpha-session-" + std::to_string(
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

std::vector<AssetInstanceId> assets(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            result.push_back(id);
        }
    }
    return result;
}

std::uint64_t greylineWestSpawnSeed()
{
    for (std::uint64_t seed = 1U; seed <= 64U; ++seed)
    {
        ProfileState probe = makeNewAlphaProfile(
            "rescue-seed-probe", publishedContentRegistry());
        const DeployReceipt deployed = executeDeploy(
            probe,
            publishedContentRegistry(),
            DeployCommand{
                "rescue-seed-raid",
                "rescue-seed-settlement",
                seed,
                MapDefinitionId{"map.v0.test"}},
            CommandContext{probe.revision, "rescue-seed-deploy"});
        if (!deployed.succeeded || !probe.pendingRaid.has_value() ||
            probe.pendingRaid->spawnExtractionPairId != "west_to_east")
        {
            continue;
        }
        return seed;
    }
    throw std::logic_error{"no deterministic rescue test seed was found"};
}

TEST(AlphaExtractionSessionTest, ExplicitMapSelectionBuildsSelectedRaidWorld)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-selected-map"));

    ASSERT_TRUE(session.deployAlpha(
        77233, MapDefinitionId{"map.raid.industrial"}));

    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    EXPECT_EQ(
        session.profile().pendingRaid->mapDefinitionId,
        MapDefinitionId{"map.raid.industrial"});
    EXPECT_EQ(session.world().ballisticBlockers().size(), 3U);
    EXPECT_FLOAT_EQ(
        session.world().player().position().x,
        session.profile().pendingRaid->playerSpawn.x);
    EXPECT_FLOAT_EQ(
        session.world().player().position().y,
        session.profile().pendingRaid->playerSpawn.y);
    EXPECT_TRUE(session.world().emergencyExtractionPoint().has_value());
    EXPECT_EQ(session.world().highRiskActiveEnemyCap(), 8U);
    EXPECT_EQ(
        session.profile().pendingRaid->rulesVersion,
        "procedural-playable-outdoor-layout-21");
}

TEST(AlphaExtractionSessionTest,
     BaseSiteClearingObjectiveFollowsInitialEnemiesAndFailureStaysLocked)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-base-site-clearance"));
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId outpostId{
        "regional_outpost.ashworks_logistics_yard"};
    ASSERT_TRUE(session.deployAlpha(
        77239U,
        MapDefinitionId{"map.raid.industrial"},
        {},
        std::nullopt,
        std::nullopt,
        siteId));
    ASSERT_TRUE(session.profile().pendingRaid->baseSiteClearance.has_value());
    EXPECT_FALSE(session.baseSiteClearanceObjectiveSecured());

    const Rect &extraction = session.world().extractionPoint().bounds();
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
        extraction.position));
    session.update(GameplayInput{}, 0.1F);
    EXPECT_EQ(
        session.world().raidSession().state(),
        RaidSessionState::Extracting);
    EXPECT_GT(
        session.world().raidSession().extractionProgress(), 0.0F);
    EXPECT_FALSE(session.baseSiteClearanceObjectiveSecured());

    for (const Enemy &enemy : session.world().enemies())
    {
        Enemy &mutableEnemy = const_cast<Enemy &>(enemy);
        static_cast<void>(mutableEnemy.takeDamage(mutableEnemy.maxHealth()));
    }
    ASSERT_EQ(session.world().aliveInitialEnemyCount(), 0U);
    session.update(GameplayInput{}, 0.0F);
    EXPECT_TRUE(session.baseSiteClearanceObjectiveSecured());

    ASSERT_TRUE(session.activeQuitAlphaRaid());
    EXPECT_FALSE(session.profile().regionalOperations.baseSites.at(siteId)
                     .unlocked);
    EXPECT_FALSE(session.profile().regionalOperations.outposts.at(outpostId)
                     .unlocked);
}

TEST(AlphaExtractionSessionPerformanceTest,
     ActiveFrontierClockUpdatesStayInsideOneFrameBudget)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-frontier-clock-performance"));
    const auto rifles = assets(session.profile(), alpha_content::rifle);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto ammunition = assets(
        session.profile(), alpha_content::ammunition);
    const auto backpacks = assets(
        session.profile(), alpha_content::backpack);
    ASSERT_EQ(rifles.size(), 1U);
    ASSERT_GE(magazines.size(), 1U);
    ASSERT_GE(ammunition.size(), 2U);
    ASSERT_EQ(backpacks.size(), 1U);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazines[0], ammunition[0], 30},
        "frontier-performance-load").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{
            rifles[0], EquipmentSlotKind::PrimaryWeapon},
        "frontier-performance-equip").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{
            backpacks[0], EquipmentSlotKind::Backpack},
        "frontier-performance-equip-backpack").succeeded);
    const ProfileContainerId backpackGrid =
        ProfileContainerId::compartment(backpacks[0], 0);
    const auto ammunitionFit = findFirstProfileFit(
        session.profile(),
        publishedContentRegistry(),
        backpackGrid,
        publishedContentRegistry().item(alpha_content::ammunition),
        ItemOrientation::Degrees0,
        ammunition[1]);
    ASSERT_TRUE(ammunitionFit.has_value());
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            ammunition[1],
            0,
            StoredAssetLocation{backpackGrid, *ammunitionFit},
            ItemOrientation::Degrees0},
        "frontier-performance-store-ammunition").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        InstallMagazineAndChamberCommand{rifles[0], magazines[0]},
        "frontier-performance-install").succeeded);
    ASSERT_TRUE(session.deployAlpha(
        7723401U, MapDefinitionId{"map.raid.frontier_exchange"}))
        << session.persistenceMessage();
    const std::uint64_t startingWorldMinute =
        session.profile().worldClock.elapsedWorldMinutes;

    std::chrono::microseconds slowestClockUpdate{};
    std::vector<std::chrono::microseconds> clockUpdateSamples;
    clockUpdateSamples.reserve(180U);
    for (std::size_t frame{}; frame < 180U; ++frame)
    {
        const auto started = std::chrono::steady_clock::now();
        session.update(GameplayInput{}, 1.0F / 60.0F);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);
        clockUpdateSamples.push_back(elapsed);
        slowestClockUpdate = std::max(slowestClockUpdate, elapsed);
    }
    std::sort(clockUpdateSamples.begin(), clockUpdateSamples.end());
    const auto clockP95 = clockUpdateSamples[static_cast<std::size_t>(
        std::ceil(clockUpdateSamples.size() * 0.95)) - 1U];

    std::cout << "active Frontier clock p95/max update: "
              << clockP95.count() << "/"
              << slowestClockUpdate.count() << " us\n";
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        startingWorldMinute + 3U);
    EXPECT_TRUE(validateProfileState(
        session.profile(), publishedContentRegistry()).valid);
    EXPECT_LT(clockP95.count(), 25000)
        << "Advancing one world minute must not copy and validate the full "
           "frozen megamap on the simulation thread.";
    EXPECT_LT(slowestClockUpdate.count(), 250000)
        << "A single update must not exhibit an unbounded stall.";

    const std::uint64_t inventoryFingerprint =
        profileStateFingerprint(session.profile());
    std::chrono::microseconds slowestInventoryQuery{};
    std::vector<std::chrono::microseconds> inventoryQuerySamples;
    inventoryQuerySamples.reserve(240U);
    for (std::size_t frame{}; frame < 240U; ++frame)
    {
        const auto started = std::chrono::steady_clock::now();
        const InventoryPlan movePlan = queryInventory(
            session.profile(),
            publishedContentRegistry(),
            InventoryMoveCommand{
                ammunition[1],
                0,
                StoredAssetLocation{backpackGrid, *ammunitionFit},
                ItemOrientation::Degrees0});
        const WeaponAmmoPlan loadPlan = queryWeaponAmmo(
            session.profile(),
            publishedContentRegistry(),
            LoadMagazineCommand{magazines[0], ammunition[1], 1});
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);
        ASSERT_TRUE(movePlan.canCommit) << movePlan.message;
        ASSERT_TRUE(loadPlan.canCommit) << loadPlan.message;
        inventoryQuerySamples.push_back(elapsed);
        slowestInventoryQuery = std::max(
            slowestInventoryQuery, elapsed);
    }
    std::sort(
        inventoryQuerySamples.begin(), inventoryQuerySamples.end());
    const auto inventoryQueryP95 =
        inventoryQuerySamples[static_cast<std::size_t>(
            std::ceil(inventoryQuerySamples.size() * 0.95)) - 1U];

    std::cout << "Frontier inventory preview p95/max query: "
              << inventoryQueryP95.count() << "/"
              << slowestInventoryQuery.count() << " us\n";
    EXPECT_EQ(
        profileStateFingerprint(session.profile()), inventoryFingerprint);
    EXPECT_LT(inventoryQueryP95.count(), 25000)
        << "Drag previews must inspect only inventory participants and must "
           "not copy or validate the frozen megamap each render frame.";
    EXPECT_LT(slowestInventoryQuery.count(), 250000)
        << "A single drag preview must not exhibit an unbounded stall.";

    GameplayInput fire;
    fire.fireJustPressed = true;
    fire.firePressed = true;
    fire.aimWorldPosition = Vec2{
        session.world().player().position().x + 900.0F,
        session.world().player().position().y};
    std::size_t shots{};
    std::chrono::microseconds slowestFireUpdate{};
    std::vector<std::chrono::microseconds> fireUpdateSamples;
    fireUpdateSamples.reserve(240U);
    for (std::size_t frame{}; frame < 240U; ++frame)
    {
        const auto started = std::chrono::steady_clock::now();
        session.update(fire, 1.0F / 60.0F);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);
        fireUpdateSamples.push_back(elapsed);
        slowestFireUpdate = std::max(slowestFireUpdate, elapsed);
        if (session.world().shotFiredLastUpdate())
        {
            ++shots;
        }
        fire.fireJustPressed = false;
    }
    std::sort(fireUpdateSamples.begin(), fireUpdateSamples.end());
    const auto fireP95 = fireUpdateSamples[static_cast<std::size_t>(
        std::ceil(fireUpdateSamples.size() * 0.95)) - 1U];

    std::cout << "continuous Frontier fire p95/max update: "
              << fireP95.count() << "/"
              << slowestFireUpdate.count() << " us across " << shots
              << " shots\n";
    EXPECT_GE(shots, 20U);
    EXPECT_EQ(magazineRoundCount(session.profile(), magazines[0]), 0U);
    EXPECT_FALSE(session.profile().assets.find(rifles[0])
                     ->chamberedRound.has_value());
    EXPECT_TRUE(validateProfileState(
        session.profile(), publishedContentRegistry()).valid);
    EXPECT_LT(fireP95.count(), 25000)
        << "Firing must mutate only weapon participants and must not copy or "
           "validate the frozen megamap on the simulation thread.";
    EXPECT_LT(slowestFireUpdate.count(), 250000)
        << "A single firing update must not exhibit an unbounded stall.";

    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    const auto loot = std::find_if(
        session.profile().pendingRaid->loot.begin(),
        session.profile().pendingRaid->loot.end(),
        [](const RaidLootSnapshot &entry)
        {
            return entry.spaceId == outdoorRaidSpaceId() &&
                !entry.requiresHighRisk && !entry.collected &&
                publishedContentRegistry()
                        .item(entry.definitionId)
                        .maxStackSize == 1U;
        });
    ASSERT_NE(loot, session.profile().pendingRaid->loot.end());
    const AssetInstanceId lootAssetId = loot->assetId;
    const Vec2 lootPosition = loot->position;
    const float playerHalf = session.world().player().size() * 0.5F;
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
        {lootPosition.x - playerHalf, lootPosition.y - playerHalf}));
    GameplayInput pickup;
    pickup.interactJustPressed = true;
    const auto pickupStarted = std::chrono::steady_clock::now();
    session.update(pickup, 0.0F);
    const auto pickupElapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - pickupStarted);

    const AssetRecord *pickedUp = session.profile().assets.find(lootAssetId);
    ASSERT_NE(pickedUp, nullptr);
    EXPECT_FALSE(std::holds_alternative<RaidGroundAssetLocation>(
        pickedUp->location));
    EXPECT_TRUE(std::find_if(
        session.profile().pendingRaid->loot.begin(),
        session.profile().pendingRaid->loot.end(),
        [lootAssetId](const RaidLootSnapshot &entry)
        { return entry.assetId == lootAssetId && entry.collected; }) !=
        session.profile().pendingRaid->loot.end());
    EXPECT_TRUE(validateProfileState(
        session.profile(), publishedContentRegistry()).valid);
    std::cout << "Frontier Loot pickup update: "
              << pickupElapsed.count() << " us\n";
    EXPECT_LT(pickupElapsed.count(), 25000)
        << "Picking up one item must not copy the frozen megamap.";
}

TEST(AlphaExtractionSessionTest,
     BasePerimeterSweepObjectiveFollowsInitialEnemies)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-perimeter-sweep"));
    const_cast<ProfileState &>(session.profile())
        .baseSiege.raidThreatUnits = 70U;
    const RegionalBaseSiteDefinitionId siteId{
        "regional_base_site.greyline_yard"};
    ASSERT_TRUE(session.deployAlpha(
        77240U,
        MapDefinitionId{"map.v0.test"},
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        siteId));
    ASSERT_TRUE(session.profile().pendingRaid->basePerimeterSweep.has_value());
    EXPECT_FALSE(session.basePerimeterSweepObjectiveSecured());
    EXPECT_TRUE(session.raidOperationProjection().basePerimeterSweepActive);
    EXPECT_FALSE(session.raidOperationProjection().objectiveSecured);

    for (const Enemy &enemy : session.world().enemies())
    {
        Enemy &mutableEnemy = const_cast<Enemy &>(enemy);
        static_cast<void>(mutableEnemy.takeDamage(mutableEnemy.maxHealth()));
    }
    session.update(GameplayInput{}, 0.0F);

    EXPECT_TRUE(session.basePerimeterSweepObjectiveSecured());
    EXPECT_TRUE(session.raidOperationProjection().basePerimeterSweepActive);
    EXPECT_TRUE(session.raidOperationProjection().objectiveSecured);
    ASSERT_TRUE(session.activeQuitAlphaRaid());
    EXPECT_FALSE(session.raidOperationProjection().basePerimeterSweepActive);
    EXPECT_EQ(session.profile().lastRaidResult->baseThreatReducedUnits, 0U);
}

TEST(AlphaExtractionSessionTest,
     HeldWorldInteractionOpensSelectedLostCacheThroughSessionBoundary)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-self-recovery"));
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{
            rifle, EquipmentSlotKind::PrimaryWeapon},
        "self-recovery-session-equip").succeeded);
    ASSERT_TRUE(session.deployAlpha(
        77191U, MapDefinitionId{"map.v0.test"}));
    ASSERT_TRUE(session.activeQuitAlphaRaid());
    const auto records = session.lostRaidRecordProjections();
    ASSERT_EQ(records.size(), 1U);

    ASSERT_TRUE(session.deployAlpha(
        77192U,
        MapDefinitionId{"map.v0.test"},
        {},
        records.front().recordId));
    const auto recovery = session.raidSelfRecoveryProjection();
    ASSERT_TRUE(recovery.has_value());
    ASSERT_FALSE(recovery->opened);
    const float half = session.world().player().size() * 0.5F;
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
        Vec2{recovery->cachePosition.x - half,
             recovery->cachePosition.y - half}));
    ASSERT_TRUE(session.raidSelfRecoveryProjection()->interactionInRange);

    GameplayInput held;
    held.interactPressed = true;
    session.update(held, 2.1F);

    const auto opened = session.raidSelfRecoveryProjection();
    ASSERT_TRUE(opened.has_value());
    EXPECT_TRUE(opened->opened);
    EXPECT_FALSE(session.profile().lostRaidRecords.contains(
        records.front().recordId));
    EXPECT_TRUE(std::any_of(
        session.profile().pendingRaid->loot.begin(),
        session.profile().pendingRaid->loot.end(),
        [rifle](const RaidLootSnapshot &loot)
        { return loot.assetId == rifle && !loot.collected; }));
}

TEST(AlphaExtractionSessionTest, DeployProjectsFrozenSpecialLocationToMap)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-special-location"));
    ASSERT_TRUE(session.deployAlpha(
        88123U,
        MapDefinitionId{"map.raid.frontier_exchange"}))
        << session.persistenceMessage();
    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    ASSERT_EQ(session.profile().pendingRaid->interiors.size(), 2U);
    ASSERT_EQ(session.world().tacticalMap().specialLocations().size(), 2U);

    for (std::size_t index{};
         index < session.profile().pendingRaid->interiors.size(); ++index)
    {
        const RaidInteriorSnapshot &snapshot =
            session.profile().pendingRaid->interiors[index];
        const RaidSpecialLocationMapState &projection =
            session.world().tacticalMap().specialLocations()[index];
        EXPECT_EQ(projection.id, snapshot.id);
        EXPECT_EQ(projection.displayName, snapshot.displayName);
        EXPECT_EQ(projection.entrance, snapshot.exteriorEntrance);
    }
}

TEST(AlphaExtractionSessionTest,
     FrontierProjectsFrozenHighRiskAndRescueObjectivesToTacticalMap)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-frontier-objectives"));
    ASSERT_TRUE(session.deployAlpha(
        88124U,
        MapDefinitionId{"map.raid.frontier_exchange"}))
        << session.persistenceMessage();

    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    ASSERT_TRUE(session.profile().pendingRaid->rescue.has_value());
    const auto &objectives = session.world().tacticalMap().objectives();
    ASSERT_EQ(objectives.size(), 2U);
    const auto highRisk = std::find_if(
        objectives.begin(), objectives.end(),
        [](const RaidTacticalObjective &objective)
        {
            return objective.kind ==
                RaidTacticalObjectiveKind::HighRiskControl;
        });
    const auto rescue = std::find_if(
        objectives.begin(), objectives.end(),
        [](const RaidTacticalObjective &objective)
        { return objective.kind == RaidTacticalObjectiveKind::Rescue; });
    ASSERT_NE(highRisk, objectives.end());
    ASSERT_NE(rescue, objectives.end());
    EXPECT_EQ(highRisk->visibility,
              RaidTacticalObjectiveVisibility::Explored);
    EXPECT_EQ(rescue->visibility,
              RaidTacticalObjectiveVisibility::Briefed);
    EXPECT_EQ(rescue->bounds,
              session.profile().pendingRaid->rescue->transferPoint);
    const Vec2 highRiskCenter{
        highRisk->bounds.position.x + highRisk->bounds.size.x * 0.5F,
        highRisk->bounds.position.y + highRisk->bounds.size.y * 0.5F};
    EXPECT_FALSE(session.world().tacticalMap().pointRevealed(
        highRiskCenter));
}

TEST(AlphaExtractionSessionTest, RegularPhaseExpiresIntoActiveHighRiskRaid)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-high-risk"));
    ASSERT_TRUE(session.deployAlpha(
        77234, MapDefinitionId{"map.v0.test"}));

    session.world().update(GameplayInput{}, 180.5F);

    EXPECT_EQ(session.state(), GameSessionState::InRaid);
    EXPECT_TRUE(session.profile().pendingRaid.has_value());
    EXPECT_TRUE(session.world().raidSession().isActive());
    EXPECT_EQ(session.world().raidSession().phase(), RaidPhase::HighRisk);
    EXPECT_FALSE(session.world().raidSession().normalExtractionOpen());
    EXPECT_TRUE(session.world().raidSession().emergencyExtractionOpen());
}

TEST(
    AlphaExtractionSessionTest,
    AdvancedLootIsFrozenButLockedUntilHighRisk)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-advanced-loot"));
    ASSERT_TRUE(session.deployAlpha(77235, MapDefinitionId{"map.v0.test"}));
    ASSERT_TRUE(session.profile().pendingRaid.has_value());

    std::vector<RaidLootSnapshot> advanced;
    for (const RaidLootSnapshot &loot : session.profile().pendingRaid->loot)
    {
        if (loot.requiresHighRisk)
        {
            advanced.push_back(loot);
            EXPECT_FALSE(session.raidLootAccessible(loot));
            EXPECT_NE(session.profile().assets.find(loot.assetId), nullptr);
        }
    }
    ASSERT_EQ(advanced.size(), 2U);
    const std::uint64_t fingerprintBefore =
        profileStateFingerprint(session.profile());

    session.world().update(GameplayInput{}, 180.5F);

    EXPECT_EQ(session.world().raidSession().phase(), RaidPhase::HighRisk);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprintBefore);
    for (const RaidLootSnapshot &loot : advanced)
    {
        EXPECT_TRUE(session.raidLootAccessible(loot));
    }
}

void equip(GameSession &session, AssetInstanceId id, EquipmentSlotKind slot,
           std::string transaction)
{
    const InventoryReceipt receipt = session.executeProfileInventory(
        InventoryEquipCommand{id, slot},
        std::move(transaction));
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}

void prepareArmedLoadout(GameSession &session)
{
    const auto rifles = assets(session.profile(), alpha_content::rifle);
    const auto chests = assets(session.profile(), alpha_content::chestRig);
    const auto backpacks = assets(session.profile(), alpha_content::backpack);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto ammunition = assets(session.profile(), alpha_content::ammunition);
    ASSERT_EQ(rifles.size(), 1U);
    ASSERT_EQ(chests.size(), 1U);
    ASSERT_EQ(backpacks.size(), 1U);
    ASSERT_GE(magazines.size(), 2U);
    ASSERT_GE(ammunition.size(), 2U);

    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazines[0], ammunition[0], 10},
        "alpha-load-ten").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazines[1], ammunition[1], 20},
        "alpha-load-twenty").succeeded);
    equip(session, rifles[0], EquipmentSlotKind::PrimaryWeapon,
          "alpha-equip-rifle");
    equip(session, chests[0], EquipmentSlotKind::ChestRig,
          "alpha-equip-chest");
    equip(session, backpacks[0], EquipmentSlotKind::Backpack,
          "alpha-equip-backpack");
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            magazines[0], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests[0], 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "alpha-pocket-mag-0").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            magazines[1], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests[0], 1),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "alpha-pocket-mag-1").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        InstallMagazineCommand{rifles[0], magazines[0]},
        "alpha-install-mag").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        FireWeaponCommand{rifles[0]},
        "alpha-chamber-round").succeeded);
}

struct MultiWeaponLoadout
{
    AssetInstanceId rifle{};
    AssetInstanceId pistol{};
    AssetInstanceId pistolMagazine{};
    AssetInstanceId sparePistolMagazine{};
};

void prepareMultiWeaponLoadout(
    GameSession &session,
    MultiWeaponLoadout &result)
{
    prepareArmedLoadout(session);
    const auto rifles = assets(session.profile(), alpha_content::rifle);
    const auto pistols = assets(session.profile(), alpha_content::pistol);
    const auto pistolMagazines = assets(
        session.profile(), alpha_content::pistolMagazine);
    const auto ammunition = assets(
        session.profile(), alpha_content::ammunition);
    const auto backpacks = assets(
        session.profile(), alpha_content::backpack);
    ASSERT_EQ(rifles.size(), 1U);
    ASSERT_EQ(pistols.size(), 1U);
    ASSERT_EQ(pistolMagazines.size(), 2U);
    ASSERT_FALSE(ammunition.empty());
    ASSERT_EQ(backpacks.size(), 1U);

    equip(session, pistols[0], EquipmentSlotKind::Sidearm,
          "alpha-equip-pistol");
    const WeaponAmmoReceipt loadedFirstPistolMagazine =
        session.executeProfileWeaponAmmo(
            LoadMagazineCommand{pistolMagazines[0], ammunition.front(), 5},
            "alpha-load-pistol-five");
    ASSERT_TRUE(loadedFirstPistolMagazine.succeeded)
        << loadedFirstPistolMagazine.message;
    const WeaponAmmoReceipt loadedSecondPistolMagazine =
        session.executeProfileWeaponAmmo(
            LoadMagazineCommand{pistolMagazines[1], ammunition.front(), 7},
            "alpha-load-pistol-seven");
    ASSERT_TRUE(loadedSecondPistolMagazine.succeeded)
        << loadedSecondPistolMagazine.message;
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        InstallMagazineCommand{pistols[0], pistolMagazines[0]},
        "alpha-install-pistol-mag").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        ChamberWeaponCommand{pistols[0]},
        "alpha-chamber-pistol").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            pistolMagazines[1],
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpacks[0], 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "alpha-carry-spare-pistol-mag").succeeded);
    result = MultiWeaponLoadout{
        rifles[0], pistols[0], pistolMagazines[0], pistolMagazines[1]};
}

std::uint32_t carriedLooseAmmunition(const ProfileState &profile)
{
    std::uint32_t total{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::ammunition &&
            assetIsCarried(profile, id))
        {
            total += asset.quantity;
        }
    }
    return total;
}
}

TEST(AlphaExtractionSessionTest, DeployUsesSnapshotAndRealShotConsumption)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-fire"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId magazine = *installedMagazine(
        session.profile(), rifle);
    const std::size_t roundsBefore = magazineRoundCount(
        session.profile(), magazine);

    ASSERT_TRUE(session.deployAlpha(90817));
    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    EXPECT_EQ(session.world().player().maxHealth(), 100);
    EXPECT_EQ(session.world().player().health(), 100);
    EXPECT_FLOAT_EQ(session.world().raidSession().raidTimeRemaining(), 180.0F);
    EXPECT_EQ(session.world().raidSession().phase(), RaidPhase::Regular);
    const auto &raidLoot = session.profile().pendingRaid->loot;
    EXPECT_GE(raidLoot.size(), 8U);
    EXPECT_LE(raidLoot.size(), 11U);
    EXPECT_EQ(std::count_if(raidLoot.begin(),
                            raidLoot.end(),
                            [](const RaidLootSnapshot &loot)
                            { return loot.requiresHighRisk; }),
              2);

    session.update(GameplayInput{}, 0.0F);
    const auto alertedEnemyCount = [&session]()
    {
        return std::count_if(
            session.world().enemies().begin(),
            session.world().enemies().end(),
            [](const Enemy &enemy)
            {
                return enemy.awarenessState() ==
                    EnemyAwarenessState::Alerted;
            });
    };
    const auto alertedBeforeShot = alertedEnemyCount();
    ASSERT_LT(alertedBeforeShot, session.world().enemies().size());

    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    session.update(fire, 0.0F);

    EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_GT(alertedEnemyCount(), alertedBeforeShot);
    EXPECT_EQ(magazineRoundCount(session.profile(), magazine), roundsBefore - 1U);
    EXPECT_TRUE(session.profile().assets.find(rifle)->chamberedRound.has_value());
}

TEST(AlphaExtractionSessionTest, DeployProjectsAuthoritativeLightExitWeight)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-light-exit"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(
        90818,
        MapDefinitionId{"map.raid.riverside"}));

    EXPECT_TRUE(session.world().conditionalExtractionPoint().has_value());
    EXPECT_EQ(session.conditionalExtractionWeightLimitGrams(), 22000U);
    EXPECT_EQ(
        session.currentRaidCarriedWeightGrams(),
        carriedWeightGrams(session.profile(), publishedContentRegistry()));
    EXPECT_TRUE(session.conditionalExtractionEligible());
}

TEST(AlphaExtractionSessionTest, SprintFireWaitsForReadyAndConsumesOneRound)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-sprint-fire"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId magazine = *installedMagazine(
        session.profile(), rifle);
    ASSERT_TRUE(session.deployAlpha(90820));
    const std::size_t roundsBefore = magazineRoundCount(
        session.profile(), magazine);

    GameplayInput sprintFire{};
    sprintFire.moveRight = true;
    sprintFire.sprint = true;
    sprintFire.fireJustPressed = true;
    sprintFire.firePressed = true;
    session.update(sprintFire, 0.0F);

    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        magazineRoundCount(session.profile(), magazine),
        roundsBefore);

    GameplayInput heldSprint{};
    heldSprint.moveRight = true;
    heldSprint.sprint = true;
    session.update(heldSprint, 0.30F);

    EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        magazineRoundCount(session.profile(), magazine),
        roundsBefore - 1U);
}

TEST(AlphaExtractionSessionTest, AdsSlowsMovementAndKeepsLowPowerWeakTracer)
{
    GameSession hipSession;
    ASSERT_TRUE(hipSession.startNewProfile("alpha-session-hip-move"));
    prepareArmedLoadout(hipSession);
    ASSERT_TRUE(hipSession.deployAlpha(90821));
    const float hipStart = hipSession.world().player().position().x;
    GameplayInput hipMove{};
    hipMove.moveRight = true;
    hipSession.update(hipMove, 0.20F);
    const float hipDistance =
        hipSession.world().player().position().x - hipStart;

    GameSession adsSession;
    ASSERT_TRUE(adsSession.startNewProfile("alpha-session-ads-move"));
    prepareArmedLoadout(adsSession);
    ASSERT_TRUE(adsSession.deployAlpha(90822));
    const float adsStart = adsSession.world().player().position().x;
    GameplayInput adsMove{};
    adsMove.moveRight = true;
    adsMove.aimDownSights = true;
    adsMove.aimWorldPosition = Vec2{1100.0F, 360.0F};
    adsSession.update(adsMove, 0.20F);
    const float adsDistance =
        adsSession.world().player().position().x - adsStart;

    EXPECT_GT(adsDistance, 0.0F);
    EXPECT_LT(adsDistance, hipDistance);
    EXPECT_GT(adsSession.world().weaponAimDownSightsProgress(), 0.0F);

    GameplayInput adsFire = adsMove;
    adsFire.moveRight = false;
    adsFire.fireJustPressed = true;
    adsFire.firePressed = true;
    adsSession.update(adsFire, 0.0F);
    ASSERT_TRUE(adsSession.world().shotFiredLastUpdate());
    GameplayInput holdAds{};
    holdAds.aimDownSights = true;
    holdAds.aimWorldPosition = Vec2{1100.0F, 360.0F};
    adsSession.update(holdAds, 0.005F);
    const auto shots = adsSession.world().shotPresentationSnapshots();
    ASSERT_FALSE(shots.empty());
    EXPECT_EQ(shots.back().tracerStyle, TracerStyle::Weak);
}

TEST(AlphaExtractionSessionTest, ReloadRetainsAdsAndLocksMaximumSpread)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-reload-ads"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(90823));

    GameplayInput reload{};
    reload.reloadJustPressed = true;
    reload.aimDownSights = true;
    reload.aimWorldPosition = Vec2{1000.0F, 360.0F};
    session.update(reload, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());

    GameplayInput keepAiming{};
    keepAiming.aimDownSights = true;
    keepAiming.aimWorldPosition = Vec2{1000.0F, 360.0F};
    session.update(keepAiming, 0.20F);

    EXPECT_GT(session.world().weaponAimDownSightsProgress(), 0.0F);
    EXPECT_FLOAT_EQ(
        session.world().weaponSpreadDegrees(),
        session.world().weaponAccuracyProjection().maximumSpreadDegrees);
    EXPECT_TRUE(session.raidActionState().active().has_value());
}

TEST(AlphaExtractionSessionTest,
     DeveloperWeaponTuningIsRuntimeOnlyAndResettable)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-developer-tuning"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(90824));

    const std::uint64_t fingerprint = profileStateFingerprint(session.profile());
    const ProfileRevision revision = session.profile().revision;
    const auto defaults = session.developerWeaponTuning();
    ASSERT_TRUE(defaults.has_value());
    EXPECT_FALSE(defaults->overridden);

    ASSERT_TRUE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::Stability,
        1,
        false));
    ASSERT_TRUE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::MaximumReticleSpeed,
        -1,
        true));
    ASSERT_TRUE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::ReticleControlAcceleration,
        1,
        false));
    ASSERT_TRUE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::RecoilBendDuration,
        1,
        false));
    ASSERT_TRUE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::DistanceBloomAtEffectiveRange,
        1,
        false));
    ASSERT_TRUE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::SprintingSpreadFraction,
        -1,
        false));

    const auto tuned = session.developerWeaponTuning();
    ASSERT_TRUE(tuned.has_value());
    EXPECT_TRUE(tuned->overridden);
    EXPECT_EQ(tuned->weaponAssetId, defaults->weaponAssetId);
    EXPECT_EQ(tuned->weaponUse.stability, defaults->weaponUse.stability + 1U);
    EXPECT_LT(
        tuned->handling.maximumSpreadDegrees,
        defaults->handling.maximumSpreadDegrees);
    EXPECT_FLOAT_EQ(
        tuned->handling.maximumReticleSpeed,
        defaults->handling.maximumReticleSpeed - 100.0F);
    EXPECT_FLOAT_EQ(
        tuned->handling.reticleControlAcceleration,
        defaults->handling.reticleControlAcceleration + 100.0F);
    EXPECT_FLOAT_EQ(
        tuned->handling.recoilBendDurationSeconds,
        defaults->handling.recoilBendDurationSeconds + 0.005F);
    EXPECT_FLOAT_EQ(
        tuned->handling.distanceBloomAtEffectiveRange,
        defaults->handling.distanceBloomAtEffectiveRange + 0.01F);
    EXPECT_FLOAT_EQ(
        tuned->handling.sprintingSpreadFraction,
        defaults->handling.sprintingSpreadFraction - 0.01F);
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);

    ASSERT_TRUE(session.resetDeveloperWeaponTuning());
    const auto reset = session.developerWeaponTuning();
    ASSERT_TRUE(reset.has_value());
    EXPECT_FALSE(reset->overridden);
    EXPECT_EQ(reset->weaponUse, defaults->weaponUse);
    EXPECT_FLOAT_EQ(
        reset->handling.maximumReticleSpeed,
        defaults->handling.maximumReticleSpeed);
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_FALSE(session.resetDeveloperWeaponTuning());
}

TEST(AlphaExtractionSessionTest,
     DeveloperInfiniteAmmoFiresWithoutMutatingProfileAmmunition)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-infinite-ammo"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId magazine = *installedMagazine(
        session.profile(), rifle);
    ASSERT_TRUE(session.deployAlpha(90826));

    const std::size_t roundsBefore = magazineRoundCount(
        session.profile(), magazine);
    const ProfileRevision revisionBefore = session.profile().revision;
    const std::uint64_t fingerprintBefore =
        profileStateFingerprint(session.profile());

    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    fire.developerInfiniteAmmo = true;
    session.update(fire, 0.0F);
    ASSERT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        magazineRoundCount(session.profile(), magazine), roundsBefore);
    EXPECT_EQ(session.profile().revision, revisionBefore);
    EXPECT_EQ(
        profileStateFingerprint(session.profile()), fingerprintBefore);

    session.update(GameplayInput{}, 0.20F);
    session.update(fire, 0.0F);
    ASSERT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        magazineRoundCount(session.profile(), magazine), roundsBefore);
    EXPECT_EQ(session.profile().revision, revisionBefore);
    EXPECT_EQ(
        profileStateFingerprint(session.profile()), fingerprintBefore);
}

TEST(AlphaExtractionSessionTest, DeveloperWeaponTuningRejectsInvalidAccess)
{
    GameSession session;
    EXPECT_FALSE(session.developerWeaponTuning().has_value());
    EXPECT_FALSE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::Accuracy,
        1,
        false));

    ASSERT_TRUE(session.startNewProfile("alpha-session-invalid-tuning"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(90825));
    EXPECT_FALSE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::Accuracy,
        0,
        false));
    EXPECT_FALSE(session.adjustDeveloperWeaponTuning(
        DeveloperWeaponParameter::Count,
        1,
        false));
}

TEST(AlphaExtractionSessionTest, DeploySelectsFirstOccupiedWeaponSlot)
{
    GameSession secondarySession;
    ASSERT_TRUE(secondarySession.startNewProfile(
        "alpha-session-secondary-start"));
    const AssetInstanceId rifle = assets(
        secondarySession.profile(), alpha_content::rifle).front();
    const AssetInstanceId pistol = assets(
        secondarySession.profile(), alpha_content::pistol).front();
    equip(
        secondarySession, rifle, EquipmentSlotKind::SecondaryWeapon,
        "equip-secondary-start");
    equip(
        secondarySession, pistol, EquipmentSlotKind::Sidearm,
        "equip-sidearm-fallback");
    ASSERT_TRUE(secondarySession.deployAlpha(77120));
    EXPECT_EQ(
        secondarySession.activeAlphaWeaponSlot(),
        EquipmentSlotKind::SecondaryWeapon);

    GameSession sidearmSession;
    ASSERT_TRUE(sidearmSession.startNewProfile(
        "alpha-session-sidearm-start"));
    const AssetInstanceId sidearm = assets(
        sidearmSession.profile(), alpha_content::pistol).front();
    equip(
        sidearmSession, sidearm, EquipmentSlotKind::Sidearm,
        "equip-only-sidearm");
    ASSERT_TRUE(sidearmSession.deployAlpha(77121));
    EXPECT_EQ(
        sidearmSession.activeAlphaWeaponSlot(),
        EquipmentSlotKind::Sidearm);
}

TEST(AlphaExtractionSessionTest, ReloadCommitsSelectedChestMagazineAfterTwoSeconds)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-reload"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId original = *installedMagazine(session.profile(), rifle);
    ASSERT_TRUE(session.deployAlpha(3319));

    GameplayInput reload{};
    reload.reloadJustPressed = true;
    session.update(reload, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    session.update(GameplayInput{}, 2.0F);

    const auto installed = installedMagazine(session.profile(), rifle);
    ASSERT_TRUE(installed.has_value());
    EXPECT_NE(*installed, original);
    EXPECT_EQ(magazineRoundCount(session.profile(), *installed), 20U);
}

TEST(AlphaExtractionSessionTest, RaidInventoryMovesUnequipsAndReequipsWeapon)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-raid-inventory"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId backpack = assets(
        session.profile(), alpha_content::backpack).front();
    ASSERT_TRUE(session.deployAlpha(77127));

    const ProfileContainerId backpackGrid =
        ProfileContainerId::compartment(backpack, 0);
    const auto fit = findFirstProfileFit(
        session.profile(),
        publishedContentRegistry(),
        backpackGrid,
        publishedContentRegistry().item(alpha_content::rifle),
        ItemOrientation::Degrees0,
        rifle);
    ASSERT_TRUE(fit.has_value());

    const InventoryReceipt stored = session.executeProfileInventory(
        InventoryMoveCommand{
            rifle,
            0,
            StoredAssetLocation{backpackGrid, *fit},
            ItemOrientation::Degrees0},
        "raid-ui-unequip-rifle");
    ASSERT_TRUE(stored.succeeded) << stored.message;
    EXPECT_FALSE(equippedAsset(
        session.profile(), EquipmentSlotKind::PrimaryWeapon).has_value());
    EXPECT_TRUE(assetIsCarried(session.profile(), rifle));

    const InventoryReceipt equipped = session.executeProfileInventory(
        InventoryEquipCommand{
            rifle, EquipmentSlotKind::SecondaryWeapon},
        "raid-ui-reequip-rifle");
    ASSERT_TRUE(equipped.succeeded) << equipped.message;
    session.update(GameplayInput{}, 0.0F);
    EXPECT_EQ(
        equippedAsset(
            session.profile(), EquipmentSlotKind::SecondaryWeapon),
        rifle);
    EXPECT_EQ(session.activeAlphaWeapon(), rifle);
    EXPECT_EQ(
        session.activeAlphaWeaponSlot(),
        EquipmentSlotKind::SecondaryWeapon);
}

TEST(AlphaExtractionSessionTest, TimedSwitchUsesIndependentWeaponStateAndFireMode)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-multi-weapon"));
    MultiWeaponLoadout loadout;
    prepareMultiWeaponLoadout(session, loadout);
    const std::uint32_t rifleDurabilityBefore =
        session.profile().assets.find(loadout.rifle)->currentDurability;
    const std::uint32_t pistolDurabilityBefore =
        session.profile().assets.find(loadout.pistol)->currentDurability;
    ASSERT_TRUE(session.deployAlpha(77123));

    EXPECT_EQ(
        session.activeAlphaWeaponSlot(), EquipmentSlotKind::PrimaryWeapon);
    EXPECT_EQ(session.activeAlphaWeapon(), loadout.rifle);
    EXPECT_FALSE(session.startAlphaWeaponSwitch(
        EquipmentSlotKind::PrimaryWeapon));
    EXPECT_FALSE(session.startAlphaWeaponSwitch(
        EquipmentSlotKind::SecondaryWeapon));

    GameplayInput selectPistol{};
    selectPistol.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
    session.update(selectPistol, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    EXPECT_NE(
        std::get_if<WeaponSwitchRaidAction>(
            &*session.raidActionState().active()),
        nullptr);
    session.update(GameplayInput{}, 0.36F);
    EXPECT_EQ(session.activeAlphaWeapon(), loadout.rifle);
    session.update(GameplayInput{}, 0.02F);
    EXPECT_EQ(session.activeAlphaWeapon(), loadout.pistol);

    const std::size_t pistolRoundsBefore = magazineRoundCount(
        session.profile(), loadout.pistolMagazine);
    GameplayInput firstPistolShot{};
    firstPistolShot.fireJustPressed = true;
    firstPistolShot.firePressed = true;
    session.update(firstPistolShot, 0.0F);
    EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        magazineRoundCount(session.profile(), loadout.pistolMagazine),
        pistolRoundsBefore - 1U);
    EXPECT_EQ(
        session.profile().assets.find(loadout.rifle)->currentDurability,
        rifleDurabilityBefore);
    EXPECT_LT(
        session.profile().assets.find(loadout.pistol)->currentDurability,
        pistolDurabilityBefore);

    GameplayInput heldPistolTrigger{};
    heldPistolTrigger.firePressed = true;
    session.update(heldPistolTrigger, 0.5F);
    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        magazineRoundCount(session.profile(), loadout.pistolMagazine),
        pistolRoundsBefore - 1U);

    ASSERT_TRUE(session.startAlphaReload(
        loadout.pistol, loadout.sparePistolMagazine));
    session.update(GameplayInput{}, 2.0F);
    EXPECT_EQ(
        installedMagazine(session.profile(), loadout.pistol),
        loadout.sparePistolMagazine);
    EXPECT_TRUE(
        session.profile().assets.find(loadout.pistol)->chamberedRound.has_value());

    GameplayInput selectRifle{};
    selectRifle.weaponSlotJustPressed = EquipmentSlotKind::PrimaryWeapon;
    session.update(selectRifle, 0.0F);
    session.update(GameplayInput{}, 0.66F);
    EXPECT_EQ(session.activeAlphaWeapon(), loadout.rifle);

    GameplayInput rifleTrigger{};
    rifleTrigger.fireJustPressed = true;
    rifleTrigger.firePressed = true;
    session.update(rifleTrigger, 0.0F);
    ASSERT_TRUE(session.world().shotFiredLastUpdate());
    rifleTrigger.fireJustPressed = false;
    session.update(rifleTrigger, 0.2F);
    EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_LT(
        session.profile().assets.find(loadout.rifle)->currentDurability,
        rifleDurabilityBefore);
}

TEST(AlphaExtractionSessionTest, TimedWeaponSwitchKeepsRelativeReticlePosition)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-switch-reticle"));
    MultiWeaponLoadout loadout;
    prepareMultiWeaponLoadout(session, loadout);
    ASSERT_TRUE(session.deployAlpha(77126));

    GameplayInput initializeAim{};
    initializeAim.aimWorldPosition = Vec2{900.0F, 376.0F};
    session.update(initializeAim, 0.0F);

    GameplayInput moveReticle = initializeAim;
    moveReticle.aimMotionDelta = Vec2{135.0F, -48.0F};
    session.update(moveReticle, 1.0F / 60.0F);
    const Vec2 beforeSwitch = session.world().weaponAimWorldPosition();
    ASSERT_NE(beforeSwitch.x, initializeAim.aimWorldPosition->x);

    GameplayInput selectPistol = initializeAim;
    selectPistol.aimMotionDelta = Vec2{};
    selectPistol.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
    session.update(selectPistol, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());

    GameplayInput holdAim = initializeAim;
    holdAim.aimMotionDelta = Vec2{};
    session.update(holdAim, 1.0F);

    ASSERT_EQ(session.activeAlphaWeapon(), loadout.pistol);
    EXPECT_FLOAT_EQ(
        session.world().weaponAimWorldPosition().x,
        beforeSwitch.x);
    EXPECT_FLOAT_EQ(
        session.world().weaponAimWorldPosition().y,
        beforeSwitch.y);
}

TEST(AlphaExtractionSessionTest, SprintInterruptsWeaponSwitchWithoutChangingSlot)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-switch-interrupt"));
    MultiWeaponLoadout loadout;
    prepareMultiWeaponLoadout(session, loadout);
    ASSERT_TRUE(session.deployAlpha(77124));

    GameplayInput selectPistol{};
    selectPistol.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
    session.update(selectPistol, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    GameplayInput sprint{};
    sprint.moveRight = true;
    sprint.sprint = true;
    session.update(sprint, 0.1F);

    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(
        session.activeAlphaWeaponSlot(), EquipmentSlotKind::PrimaryWeapon);
}

TEST(AlphaExtractionSessionTest, TargetedReloadIsAtomicAndChambersAfterTwoSeconds)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-targeted-reload"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId original = *installedMagazine(session.profile(), rifle);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto target = std::find_if(
        magazines.begin(),
        magazines.end(),
        [original, &session](AssetInstanceId id)
        {
            return id != original &&
                   assetIsCarried(session.profile(), id) &&
                   magazineRoundCount(session.profile(), id) == 20U;
        });
    ASSERT_NE(target, magazines.end());
    ASSERT_TRUE(session.deployAlpha(3320));
    static_cast<void>(session.takePresentationEvents());

    for (int shot = 0; shot < 10; ++shot)
    {
        ASSERT_TRUE(session.executeProfileWeaponAmmo(
            FireWeaponCommand{rifle},
            "targeted-setup-fire-" + std::to_string(shot)).succeeded);
    }
    ASSERT_FALSE(session.profile().assets.find(rifle)->chamberedRound.has_value());
    const std::uint64_t beforeInterrupted =
        profileStateFingerprint(session.profile());

    ASSERT_TRUE(session.startAlphaReload(rifle, *target));
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::ReloadStarted});
    GameplayInput inventoryOpened{};
    inventoryOpened.inventoryOpen = true;
    session.update(inventoryOpened, 0.5F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeInterrupted);
    EXPECT_EQ(installedMagazine(session.profile(), rifle), original);

    ASSERT_TRUE(session.startAlphaReload(rifle, *target));
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::ReloadStarted});
    session.update(GameplayInput{}, 2.0F);
    EXPECT_EQ(installedMagazine(session.profile(), rifle), *target);
    EXPECT_TRUE(session.profile().assets.find(rifle)->chamberedRound.has_value());
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 19U);
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::ReloadCompleted});
}

TEST(AlphaExtractionSessionTest, RaidWeaponMaintenanceAllowsSlowMovement)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-maintenance"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId backpack = assets(
        session.profile(), alpha_content::backpack).front();
    const AssetInstanceId kit = assets(
        session.profile(), alpha_content::weaponMaintenanceKit).front();
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            kit,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "maintenance-carry-kit").succeeded);
    ASSERT_TRUE(session.deployAlpha(90818));

    ASSERT_TRUE(session.executeProfileWeaponAmmo(
        FireWeaponCommand{rifle},
        "maintenance-wear-shot").succeeded);
    EXPECT_EQ(session.profile().assets.find(rifle)->currentDurability, 9990U);

    ASSERT_TRUE(session.startAlphaWeaponMaintenance(kit, rifle));
    const std::uint64_t beforeMovement =
        profileStateFingerprint(session.profile());
    const float positionBefore = session.world().player().position().x;
    GameplayInput movement{};
    movement.moveRight = true;
    session.update(movement, 0.1F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    EXPECT_NEAR(
        session.world().player().position().x - positionBefore,
        10.8F,
        0.001F);
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeMovement);
    EXPECT_EQ(session.profile().assets.find(kit)->remainingCharges, 2500U);

}

TEST(AlphaExtractionSessionTest, RaidArmorMaintenanceAllowsSlowMovementAndRemainsAtomic)
{
    TemporarySaveDirectory directory;
    ProfileState profile = makeNewAlphaProfile(
        "alpha-session-armor-maintenance", publishedContentRegistry());
    const AssetInstanceId armor = assets(
        profile, alpha_content::bodyArmor).front();
    profile.assets.findMutable(armor)->currentDurability = 60;
    SaveRepository repository{directory.path()};
    ASSERT_TRUE(repository.save(
        profile, publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile());
    prepareArmedLoadout(session);
    const AssetInstanceId backpack = assets(
        session.profile(), alpha_content::backpack).front();
    const AssetInstanceId kit = assets(
        session.profile(), alpha_content::armorMaintenanceKit).front();
    equip(session, armor, EquipmentSlotKind::BodyArmor,
          "armor-maintenance-equip");
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            kit,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "armor-maintenance-carry-kit").succeeded);
    ASSERT_TRUE(session.deployAlpha(90819));

    const float positionBefore = session.world().player().position().x;
    ASSERT_TRUE(session.startAlphaArmorMaintenance(kit, armor));
    const std::uint64_t beforeMovement =
        profileStateFingerprint(session.profile());
    GameplayInput movement{};
    movement.moveRight = true;
    session.update(movement, 0.1F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    EXPECT_NEAR(
        session.world().player().position().x - positionBefore,
        10.8F,
        0.001F);
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeMovement);
    EXPECT_EQ(session.profile().assets.find(armor)->currentDurability, 60U);
    EXPECT_EQ(session.profile().assets.find(kit)->remainingCharges, 5000U);

    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    const auto weapon = session.activeAlphaWeapon();
    ASSERT_TRUE(weapon.has_value());
    const std::uint32_t weaponConditionBefore =
        session.profile().assets.find(*weapon)->currentDurability;
    session.update(fire, 0.1F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(
        session.profile().assets.find(*weapon)->currentDurability,
        weaponConditionBefore);
    EXPECT_EQ(session.profile().assets.find(armor)->currentDurability, 60U);
    EXPECT_EQ(session.profile().assets.find(kit)->remainingCharges, 5000U);

    session.update(GameplayInput{}, 0.0F);
    ASSERT_TRUE(session.startAlphaArmorMaintenance(kit, armor));
    session.update(GameplayInput{}, 6.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(session.profile().assets.find(armor)->currentDurability, 110U);
    EXPECT_EQ(
        session.profile().assets.find(armor)->currentMaximumDurability,
        110U);
    EXPECT_EQ(session.profile().assets.find(kit), nullptr);
}

TEST(AlphaExtractionSessionTest, RaidMagazineUnloadIsInterruptibleAndAtomic)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-unload-magazine"));
    prepareArmedLoadout(session);
    const AssetInstanceId rifle = assets(
        session.profile(), alpha_content::rifle).front();
    const AssetInstanceId installed = *installedMagazine(
        session.profile(), rifle);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto target = std::find_if(
        magazines.begin(),
        magazines.end(),
        [installed, &session](AssetInstanceId id)
        {
            return id != installed &&
                   assetIsCarried(session.profile(), id) &&
                   magazineRoundCount(session.profile(), id) == 20U;
        });
    ASSERT_NE(target, magazines.end());
    ASSERT_TRUE(session.deployAlpha(3321));
    ASSERT_EQ(carriedLooseAmmunition(session.profile()), 0U);

    const ProfileState beforeInterrupted = session.profile();
    ASSERT_TRUE(session.startAlphaUnloadMagazine(*target));
    GameplayInput inventoryOpened{};
    inventoryOpened.inventoryOpen = true;
    session.update(inventoryOpened, 1.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    ProfileState afterInterrupted = session.profile();
    afterInterrupted.worldClock = beforeInterrupted.worldClock;
    EXPECT_EQ(
        profileStateFingerprint(afterInterrupted),
        profileStateFingerprint(beforeInterrupted));
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        beforeInterrupted.worldClock.elapsedWorldMinutes + 1U);
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 20U);

    ASSERT_TRUE(session.startAlphaUnloadMagazine(*target));
    session.update(GameplayInput{}, 3.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 0U);
    EXPECT_EQ(carriedLooseAmmunition(session.profile()), 20U);
}

TEST(AlphaExtractionSessionTest, RaidMagazinePackingIsInterruptibleAndAtomic)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-pack-magazine"));
    prepareArmedLoadout(session);
    const AssetInstanceId backpack = *equippedAsset(
        session.profile(), EquipmentSlotKind::Backpack);
    const auto magazines = assets(session.profile(), alpha_content::magazine);
    const auto ammunition = assets(session.profile(), alpha_content::ammunition);
    const auto target = std::find_if(
        magazines.begin(),
        magazines.end(),
        [&session](AssetInstanceId id)
        {
            return assetIsCarried(session.profile(), id) &&
                   magazineRoundCount(session.profile(), id) == 20U;
        });
    const auto source = std::find_if(
        ammunition.begin(),
        ammunition.end(),
        [&session](AssetInstanceId id)
        {
            const AssetRecord *asset = session.profile().assets.find(id);
            return asset != nullptr && asset->quantity >= 5;
        });
    ASSERT_NE(target, magazines.end());
    ASSERT_NE(source, ammunition.end());
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{
            *source,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "carry-loose-ammunition").succeeded);
    ASSERT_TRUE(session.deployAlpha(3322));

    const std::uint64_t beforeInterrupted =
        profileStateFingerprint(session.profile());
    const std::uint32_t sourceBefore =
        session.profile().assets.find(*source)->quantity;
    ASSERT_TRUE(session.startAlphaLoadMagazine(*source, *target, 5));
    GameplayInput inventoryOpened{};
    inventoryOpened.inventoryOpen = true;
    session.update(inventoryOpened, 0.5F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeInterrupted);

    ASSERT_TRUE(session.startAlphaLoadMagazine(*source, *target, 0));
    session.update(GameplayInput{}, 2.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_EQ(magazineRoundCount(session.profile(), *target), 30U);
    EXPECT_EQ(session.profile().assets.find(*source)->quantity,
              sourceBefore - 10U);
}

TEST(AlphaExtractionSessionTest, EnemyHitAtomicallyUpdatesProfileArmorAndWorldHealth)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-armor-hit"));
    prepareArmedLoadout(session);
    const AssetInstanceId helmet = assets(
        session.profile(),
        alpha_content::helmet).front();
    const AssetInstanceId bodyArmor = assets(
        session.profile(),
        alpha_content::bodyArmor).front();
    equip(session, helmet, EquipmentSlotKind::Helmet, "alpha-equip-helmet");
    equip(
        session,
        bodyArmor,
        EquipmentSlotKind::BodyArmor,
        "alpha-equip-body-armor");
    const std::uint32_t helmetBefore = session.profile().assets.find(helmet)
        ->currentDurability;
    const std::uint32_t bodyBefore = session.profile().assets.find(bodyArmor)
        ->currentDurability;
    ASSERT_TRUE(session.deployAlpha(73219));

    for (int frame = 0;
         frame < 2400 && session.profile().currentHealth == 100;
         ++frame)
    {
        ASSERT_FALSE(session.world().enemies().empty());
        const Vec2 player = session.world().player().position();
        const Vec2 enemy = session.world().enemies().front().position();
        GameplayInput approach{};
        approach.sprint = true;
        approach.moveLeft = player.x > enemy.x + 8.0F;
        approach.moveRight = player.x + 8.0F < enemy.x;
        approach.moveUp = player.y > enemy.y + 8.0F;
        approach.moveDown = player.y + 8.0F < enemy.y;
        session.update(approach, 1.0F / 60.0F);
    }

    ASSERT_LT(session.profile().currentHealth, 100);
    EXPECT_EQ(
        session.profile().currentHealth,
        session.world().player().health());
    const std::uint32_t helmetAfter = session.profile().assets.find(helmet)
        ->currentDurability;
    const std::uint32_t bodyAfter = session.profile().assets.find(bodyArmor)
        ->currentDurability;
    EXPECT_TRUE(helmetAfter < helmetBefore || bodyAfter < bodyBefore);
}

TEST(AlphaExtractionSessionTest, MedkitHealsContinuouslyAndFireMustBeRepressed)
{
    TemporarySaveDirectory temporary;
    GameSession loadout;
    ASSERT_TRUE(loadout.startNewProfile("alpha-session-continuous-medkit"));
    prepareArmedLoadout(loadout);
    const AssetInstanceId chest = *equippedAsset(
        loadout.profile(), EquipmentSlotKind::ChestRig);
    const AssetInstanceId medkit = assets(
        loadout.profile(), alpha_content::medkit).front();
    ASSERT_TRUE(loadout.executeProfileInventory(
        InventoryMoveCommand{
            medkit,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chest, 2),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "carry-continuous-medkit").succeeded);

    ProfileState wounded = loadout.profile();
    wounded.currentHealth = 40;
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        wounded,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile());
    const std::uint32_t chargesBefore =
        session.profile().assets.find(medkit)->remainingCharges;
    ASSERT_TRUE(session.deployAlpha(93431));
    static_cast<void>(session.takePresentationEvents());
    ASSERT_TRUE(session.startAlphaMedical(medkit));
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::MedicalStarted});

    const float positionBefore = session.world().player().position().x;
    GameplayInput slowMovement{};
    slowMovement.moveRight = true;
    session.update(slowMovement, 0.2F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    EXPECT_NEAR(
        session.world().player().position().x - positionBefore,
        21.6F,
        0.001F);
    EXPECT_EQ(session.profile().currentHealth, 41);
    EXPECT_EQ(session.world().player().health(), 41);
    EXPECT_EQ(
        session.profile().assets.find(medkit)->remainingCharges,
        chargesBefore - 1U);

    GameplayInput heldFire{};
    heldFire.fireJustPressed = true;
    heldFire.firePressed = true;
    session.update(heldFire, 0.0F);
    EXPECT_FALSE(session.raidActionState().active().has_value());
    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(
        session.takePresentationEvents(),
        std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::MedicalInterrupted});

    heldFire.fireJustPressed = false;
    session.update(heldFire, 0.0F);
    EXPECT_FALSE(session.world().shotFiredLastUpdate());

    session.update(GameplayInput{}, 0.0F);
    heldFire.fireJustPressed = true;
    session.update(heldFire, 0.0F);
    EXPECT_TRUE(session.world().shotFiredLastUpdate());
}

TEST(AlphaExtractionSessionTest, BaseFreezesBleedingAndPainkillerTimers)
{
    TemporarySaveDirectory temporary;
    ProfileState profile = makeNewAlphaProfile(
        "alpha-session-base-medical-freeze",
        publishedContentRegistry());
    profile.currentHealth = 72;
    profile.medicalStatus = MedicalStatusState{
        BleedingSeverity::Light, 31000, 650, 120000, 17000};
    SaveRepository repository{temporary.path()};
    ASSERT_TRUE(repository.save(
        profile,
        publishedContentRegistry().contentVersion()).succeeded);

    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.continueProfile());
    const MedicalStatusState before = session.profile().medicalStatus;
    const int healthBefore = session.profile().currentHealth;

    session.update(GameplayInput{}, 30.0F);

    EXPECT_EQ(session.profile().medicalStatus, before);
    EXPECT_EQ(session.profile().currentHealth, healthBefore);
}

TEST(AlphaExtractionSessionTest, DeathSettlesFullLossAndIsIdempotent)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-death"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(7711));
    const std::string settlementId = session.profile().pendingRaid->settlementId;

    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);

    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_FALSE(session.profile().pendingRaid.has_value());
    EXPECT_EQ(session.profile().currentHealth, 100);
    EXPECT_TRUE(session.profile().committedSettlements.contains(settlementId));
    EXPECT_EQ(session.profile().lastRaidResult->outcome,
              RaidResultOutcome::PlayerDead);
    EXPECT_FALSE(equippedAsset(
        session.profile(), EquipmentSlotKind::PrimaryWeapon).has_value());
}

TEST(AlphaExtractionSessionTest, ClosingDuringRaidRestoresExactPreRaidProfile)
{
    TemporarySaveDirectory temporary;
    std::uint64_t preRaidFingerprint{};
    AssetInstanceId rifle{};
    {
        GameSession first;
        first.configurePersistence(temporary.path());
        ASSERT_TRUE(first.startNewProfile("alpha-session-abnormal"));
        prepareArmedLoadout(first);
        rifle = assets(first.profile(), alpha_content::rifle).front();
        preRaidFingerprint = profileStateFingerprint(first.profile());
        ASSERT_TRUE(first.deployAlpha(44771));
        ASSERT_TRUE(first.profile().pendingRaid.has_value());
        const AssetInstanceId chest = *equippedAsset(
            first.profile(), EquipmentSlotKind::ChestRig);
        const AssetInstanceId installed = *installedMagazine(
            first.profile(), rifle);
        const auto magazines = assets(first.profile(), alpha_content::magazine);
        const auto spare = std::find_if(
            magazines.begin(),
            magazines.end(),
            [installed, &first](AssetInstanceId id)
            {
                return id != installed && assetIsCarried(first.profile(), id);
            });
        ASSERT_NE(spare, magazines.end());
        ASSERT_TRUE(first.executeProfileInventory(
            InventoryMoveCommand{
                *spare,
                0,
                StoredAssetLocation{
                    ProfileContainerId::compartment(chest, 0),
                    GridPosition{0, 0}},
                ItemOrientation::Degrees0},
            "raid-rearrange-before-close").succeeded);
        GameplayInput fire{};
        fire.fireJustPressed = true;
        fire.firePressed = true;
        first.update(fire, 0.0F);
        EXPECT_NE(profileStateFingerprint(first.profile()), preRaidFingerprint);
    }

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();

    EXPECT_FALSE(reopened.recoveredAbandonedRaid());
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), preRaidFingerprint);
    EXPECT_EQ(equippedAsset(
        reopened.profile(), EquipmentSlotKind::PrimaryWeapon), rifle);
}

TEST(AlphaExtractionSessionTest, CorruptPrimaryRecoversPreRaidBackup)
{
    TemporarySaveDirectory temporary;
    std::uint64_t preRaidFingerprint{};
    {
        GameSession first;
        first.configurePersistence(temporary.path());
        ASSERT_TRUE(first.startNewProfile("alpha-session-corrupt-pending"));
        prepareArmedLoadout(first);
        preRaidFingerprint = profileStateFingerprint(first.profile());
        ASSERT_TRUE(first.deployAlpha(55771));
        ASSERT_TRUE(first.profile().pendingRaid.has_value());
    }

    std::ofstream corrupt(
        temporary.path() / "profile.json",
        std::ios::trunc);
    corrupt << "{corrupt";
    corrupt.close();

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();

    EXPECT_EQ(reopened.lastSaveLoadStatus(), SaveLoadStatus::RecoveredBackup);
    EXPECT_FALSE(reopened.recoveredAbandonedRaid());
    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), preRaidFingerprint);
}

TEST(AlphaExtractionSessionTest, SecuredRescuePersistsThroughActiveQuit)
{
    TemporarySaveDirectory temporary;
    const std::uint64_t seed = greylineWestSpawnSeed();
    {
        GameSession session;
        session.configurePersistence(temporary.path());
        ASSERT_TRUE(session.startNewProfile("rescue-active-quit"));
        ASSERT_TRUE(session.deployAlpha(
            seed, MapDefinitionId{"map.v0.test"}));
        ASSERT_TRUE(session.world().ordinarySurvivorRescueInteractionInRange());

        GameplayInput transfer;
        transfer.interactPressed = true;
        session.update(transfer, 2.1F);

        ASSERT_TRUE(session.profile().pendingRaid.has_value());
        ASSERT_TRUE(session.profile().pendingRaid->rescue.has_value());
        EXPECT_TRUE(session.profile().pendingRaid->rescue->secured);
        EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, 9U);
        EXPECT_TRUE(session.activeQuitAlphaRaid());
        ASSERT_TRUE(session.profile().lastRaidResult.has_value());
        EXPECT_EQ(
            session.profile().lastRaidResult->rescuedOrdinaryResidents,
            1U);
    }

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().basePopulation.ordinaryResidents, 9U);
    EXPECT_TRUE(reopened.profile().committedRescues.contains(
        RescueDefinitionId{"rescue.ordinary.greyline_depot"}));
    ASSERT_TRUE(reopened.profile().lastRaidResult.has_value());
    EXPECT_EQ(
        reopened.profile().lastRaidResult->rescuedOrdinaryResidents,
        1U);
}

TEST(AlphaExtractionSessionTest, SecuredRescueIsCleanAbnormalExitCheckpoint)
{
    TemporarySaveDirectory temporary;
    const std::uint64_t seed = greylineWestSpawnSeed();
    {
        GameSession session;
        session.configurePersistence(temporary.path());
        ASSERT_TRUE(session.startNewProfile("rescue-abnormal-exit"));
        ASSERT_TRUE(session.deployAlpha(
            seed, MapDefinitionId{"map.v0.test"}));
        GameplayInput transfer;
        transfer.interactPressed = true;
        session.update(transfer, 2.1F);
        ASSERT_EQ(session.profile().basePopulation.ordinaryResidents, 9U);
        ASSERT_TRUE(session.profile().pendingRaid.has_value());
    }

    GameSession reopened;
    reopened.configurePersistence(temporary.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();

    EXPECT_FALSE(reopened.profile().pendingRaid.has_value());
    EXPECT_FALSE(reopened.recoveredAbandonedRaid());
    EXPECT_EQ(reopened.profile().basePopulation.ordinaryResidents, 9U);
    EXPECT_TRUE(reopened.profile().committedRescues.contains(
        RescueDefinitionId{"rescue.ordinary.greyline_depot"}));
    EXPECT_FALSE(reopened.profile().lastRaidResult.has_value());
}

TEST(AlphaExtractionSessionTest, RescueCheckpointSaveFailureIsZeroCommit)
{
    TemporarySaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path());
    ASSERT_TRUE(session.startNewProfile("rescue-save-failure"));
    ASSERT_TRUE(session.deployAlpha(
        greylineWestSpawnSeed(), MapDefinitionId{"map.v0.test"}));
    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    ASSERT_TRUE(session.profile().pendingRaid->rescue.has_value());
    const ProfileRevision revision = session.profile().revision;
    const AssetInstanceId nextAssetId = session.profile().assets.nextAssetId();
    const std::uint64_t currency = session.profile().currency;

    std::error_code removeError;
    std::filesystem::remove_all(temporary.path(), removeError);
    ASSERT_FALSE(removeError);
    std::ofstream blocker(temporary.path(), std::ios::trunc);
    blocker << "not a directory";
    blocker.close();

    GameplayInput transfer;
    transfer.interactPressed = true;
    session.update(transfer, 2.1F);

    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(session.profile().assets.nextAssetId(), nextAssetId);
    EXPECT_EQ(session.profile().currency, currency);
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, 8U);
    EXPECT_TRUE(session.profile().committedRescues.empty());
    EXPECT_FALSE(session.profile().pendingRaid->rescue->secured);
    EXPECT_FLOAT_EQ(
        session.world().ordinarySurvivorRescueProgress(),
        0.0F);
    EXPECT_FALSE(session.persistenceMessage().empty());
}
