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
#include "base_world.h"
#include "base_workforce_domain.h"
#include "game_session.h"
#include "raid_fire_support_domain.h"
#include "weapon_component_domain.h"
#include "profile_ui_test_wait.h"
#include "medical_domain.h"
#include "test_temp_directory.h"
#include "weapon_handling_fixture.h"

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

std::uint64_t hospitalMedicalLootSeed(const ProfileState &profile)
{
    const auto &content = publishedContentRegistry();
    const MapDefinitionId hospital{"map.raid.hospital_district"};
    const RaidSpaceDefinitionId interior{"raid_space.hospital_district.main_building"};
    for (std::uint64_t seed = 1; seed <= 16; ++seed)
    {
        auto probe = profile;
        const auto deployed = executeDeploy(probe, content,
            {"hospital-medical-probe", "hospital-medical-probe-end", seed, hospital},
            {probe.revision, "hospital-medical-probe-deploy"});
        if (!deployed.succeeded)
            throw std::logic_error{deployed.message};
        if (std::any_of(probe.pendingRaid->loot.begin(), probe.pendingRaid->loot.end(), [&](const auto &loot) {
            return loot.spaceId == interior && content.item(loot.definitionId).medicalUse.has_value();
        }))
            return seed;
    }
    throw std::logic_error{"no deterministic hospital medical Loot seed was found"};
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

TEST(AlphaExtractionSessionTest, ExpandingAmmoFollowsLootWorkshopMixedFireAndExtraction)
{
    const auto &content = publishedContentRegistry();
    const MapDefinitionId frontier{"map.raid.frontier_exchange"};
    const ItemDefinitionId precision{"item.loot.precision_components"};
    const ItemDefinitionId expanding{"item.ammunition.5_45x39_expanding"};
    const ItemDefinitionId standard{"item.ammunition.5_45x39_standard"};
    const ItemDefinitionId enhanced{"item.ammunition.5_45x39_enhanced"};
    auto profile = makeNewPublishedProfile("mixed-ammo-loop", content);
    profile.tutorial = TutorialProgress::Complete;
    for (const auto material : assets(profile, precision))
        ASSERT_TRUE(profile.assets.erase(material));
    const auto guns = assets(profile, ItemDefinitionId{"item.weapon.rifle_5_45_service"});
    const auto magazines = assets(profile, ItemDefinitionId{"item.magazine.5_45x39_30"});
    const auto backpacks = assets(profile, alpha_content::backpack);
    ASSERT_FALSE(guns.empty());
    ASSERT_FALSE(magazines.empty());
    ASSERT_FALSE(backpacks.empty());
    const auto gun = guns.front();
    const auto magazine = magazines.front();
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{backpacks.front(), EquipmentSlotKind::Backpack},
        {profile.revision, "ammo-loop-bag"}).succeeded);
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{gun, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "ammo-loop-rifle"}).succeeded);
    EXPECT_TRUE(assets(profile, expanding).empty());

    const auto isPrecisionLoot = [&](const RaidLootSnapshot &item) {
        return item.definitionId == precision && item.sceneContainerAssetId == 0 &&
            item.spaceId == outdoorRaidSpaceId() && !item.requiresHighRisk &&
            (item.resourcePointInstanceId.starts_with("resource_point.frontier.fabrication_stock:") ||
             item.resourcePointInstanceId.starts_with("resource_point.frontier.secured_cargo:"));
    };
    std::uint64_t lootSeed{};
    for (std::uint64_t seed = 1; seed <= 32 && lootSeed == 0; ++seed)
    {
        auto probe = profile;
        ASSERT_TRUE(executeDeploy(probe, content, {"ammo-loot-probe", "ammo-loot-probe-settlement",
            seed, frontier}, {probe.revision, "ammo-loot-probe-deploy"}).succeeded);
        if (std::any_of(probe.pendingRaid->loot.begin(), probe.pendingRaid->loot.end(), isPrecisionLoot))
            lootSeed = seed;
    }
    ASSERT_NE(lootSeed, 0U);

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-ammo-loop-", "workshop-combat-extraction");
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    ASSERT_TRUE(session.deployAlpha(lootSeed, frontier)) << session.persistenceMessage();
    const auto &loot = session.profile().pendingRaid->loot;
    const auto found = std::find_if(loot.begin(), loot.end(), isPrecisionLoot);
    ASSERT_NE(found, loot.end());
    const auto material = found->assetId;
    const Vec2 lootPosition = found->position;
    auto &player = const_cast<Player &>(session.world().player());
    const float half = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({lootPosition.x - half, lootPosition.y - half}));
    GameplayInput pickUp;
    pickUp.aimWorldPosition = {lootPosition.x + 60.0F, lootPosition.y};
    pickUp.interactJustPressed = true;
    session.update(pickUp, 0.0F);
    ASSERT_TRUE(assetIsCarried(session.profile(), material)) << session.persistenceMessage();

    const auto extract = [&] {
        ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
            session.world().extractionPoint().bounds().position));
        for (int frame = 0; frame < 45 && session.profile().pendingRaid; ++frame)
            session.update({}, 0.1F);
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        ASSERT_FALSE(session.profile().pendingRaid);
    };
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_TRUE(assetIsCarried(session.profile(), material));
    const auto stashCell = findFirstProfileFit(session.profile(), content, ProfileContainerId::stash(),
        content.item(precision), ItemOrientation::Degrees0);
    ASSERT_TRUE(stashCell);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{material, 0U, {ProfileContainerId::stash(), *stashCell}},
        "ammo-loop-stash-precision").succeeded);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    const auto order = session.executeStartBaseManufacturing(
        BaseManufacturingRecipeDefinitionId{"base_manufacturing.ammunition_5_45x39_expanding"},
        "ammo-loop-start");
    ASSERT_TRUE(order.succeeded) << order.message;
    ASSERT_TRUE(order.outputAssetId);
    ASSERT_TRUE(session.profile().baseManufacturing.activeOrder);
    EXPECT_EQ(session.profile().baseManufacturing.activeOrder->inputAssetIds,
        std::vector<AssetInstanceId>{material});
    const auto untilComplete = session.profile().baseManufacturing.activeOrder->completionWorldMinute -
        session.profile().worldClock.elapsedWorldMinutes;
    session.advanceBaseWorldClock(static_cast<float>(untilComplete));
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_FALSE(session.profile().baseManufacturing.activeOrder);
    ASSERT_NE(session.profile().assets.find(*order.outputAssetId), nullptr);
    EXPECT_EQ(session.profile().assets.find(*order.outputAssetId)->quantity, 18U);

    const auto standardStacks = assets(session.profile(), standard);
    const auto enhancedStacks = assets(session.profile(), enhanced);
    ASSERT_FALSE(standardStacks.empty());
    ASSERT_FALSE(enhancedStacks.empty());
    ASSERT_TRUE(session.executeProfileWeaponAmmo(LoadMagazineCommand{magazine, standardStacks.front(), 1},
        "ammo-loop-load-standard").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(LoadMagazineCommand{magazine, *order.outputAssetId, 2},
        "ammo-loop-load-expanding").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(LoadMagazineCommand{magazine, enhancedStacks.front(), 1},
        "ammo-loop-load-enhanced").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(LoadMagazineCommand{magazine, *order.outputAssetId, 1},
        "ammo-loop-load-last").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(InstallMagazineAndChamberCommand{gun, magazine},
        "ammo-loop-chamber").succeeded);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{gun, WeaponFireMode::Automatic},
        "ammo-loop-auto").succeeded);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_TRUE(session.deployAlpha(73U, frontier)) << session.persistenceMessage();
    auto arena = handlingArena();
    arena.worldSize = {4000.0F, 3000.0F};
    arena.playerSpawn = {1900.0F, 1400.0F};
    WeaponHandlingTestAccess::world(session, std::move(arena));
    session.update({}, 0.0F);
    GameplayInput fire;
    fire.aimWorldPosition = {2380.0F, 1425.0F};
    fire.firePressed = fire.fireJustPressed = true;
    session.update(fire, 0.0F);
    ASSERT_TRUE(session.world().shotFiredLastUpdate());
    fire.fireJustPressed = false;
    session.update(fire, 0.12F);
    ASSERT_TRUE(session.world().shotFiredLastUpdate());
    const auto shooting = WeaponHandlingTestAccess::shooting(session.world());
    ASSERT_EQ(shooting.flights.size(), 2U);
    EXPECT_EQ(shooting.flights[0].penetration, 4);
    EXPECT_EQ(shooting.flights[0].unarmoredDamageBonus, 0);
    EXPECT_EQ(shooting.flights[1].penetration, 2);
    EXPECT_EQ(shooting.flights[1].unarmoredDamageBonus, 1);
    ASSERT_NO_FATAL_FAILURE(extract());

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.profile().assets.find(gun)->chamberedRound);
    EXPECT_EQ(reopened.profile().assets.find(gun)->chamberedRound->definitionId, expanding);
    const auto &remaining = reopened.profile().assets.find(magazine)->magazineRounds;
    ASSERT_EQ(remaining.size(), 2U);
    EXPECT_EQ(remaining[0].definitionId, enhanced);
    EXPECT_EQ(remaining[1].definitionId, expanding);
    EXPECT_EQ(reopened.profile().assets.find(*order.outputAssetId)->quantity, 15U);
}

TEST(AlphaExtractionSessionTest, SignalCallIsExplicitFreeAndInventoryGated)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-signal-call"));
    ASSERT_TRUE(session.deployAlpha(77235, MapDefinitionId{"map.v0.test"}));
    ASSERT_TRUE(session.world().triggerHighRiskForDeveloper());
    ASSERT_TRUE(session.world().emergencyExtractionPoint().has_value());
    const auto bounds = session.world().emergencyExtractionPoint()->bounds();
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(bounds.position));
    session.update(GameplayInput{}, 0.0F);
    ASSERT_EQ(session.world().raidSession().state(), RaidSessionState::InRaid);
    const auto fingerprint = profileStateFingerprint(session.profile());
    GameplayInput call;
    call.interactJustPressed = true;
    call.inventoryOpen = true;
    session.update(call, 0.0F);
    EXPECT_FALSE(session.world().raidSession().signalCalledLastUpdate());
    call.inventoryOpen = false;
    session.update(call, 0.0F);
    EXPECT_TRUE(session.world().raidSession().signalCalledLastUpdate());
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    session.update(call, 0.0F);
    EXPECT_FALSE(session.world().raidSession().signalCalledLastUpdate());
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
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
    // Put the cache at the signal zone to exercise the overlapping service gate.
    ASSERT_TRUE(session.world().triggerHighRiskForDeveloper());
    const auto signalBounds = session.world().emergencyExtractionPoint()->bounds();
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(signalBounds.position));
    auto &pending = *const_cast<ProfileState &>(session.profile()).pendingRaid;
    pending.selfRecovery->cachePosition = Vec2{
        signalBounds.position.x + half, signalBounds.position.y + half};
    GameplayInput call;
    call.interactJustPressed = true;
    ASSERT_TRUE(session.world().canCallSignalExtraction(call));
    EXPECT_FALSE(session.canCallSignalExtraction());
    session.update(call, 0.0F);
    EXPECT_FALSE(session.world().raidSession().signalCalledLastUpdate());
    // Restore the frozen loss location before exercising the recovery transaction.
    const_cast<ProfileState &>(session.profile()).pendingRaid->selfRecovery->cachePosition = recovery->cachePosition;
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
        Vec2{recovery->cachePosition.x - half, recovery->cachePosition.y - half}));

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

TEST(AlphaExtractionSessionTest, HospitalMedicineKeepsCarriedLocationAcrossDiskReloadAndUse)
{
    TemporarySaveDirectory directory;
    AssetInstanceId medicalId{};
    std::optional<AssetLocation> carriedLocation;
    const auto &content = publishedContentRegistry();
    {
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.startNewProfile("hospital-carried-medicine"));
        const auto backpack = assets(session.profile(), alpha_content::backpack).front();
        ASSERT_TRUE(session.executeProfileInventory(
            InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack}, "hospital-equip").succeeded);
        ASSERT_TRUE(session.deployAlpha(hospitalMedicalLootSeed(session.profile()),
            MapDefinitionId{"map.raid.hospital_district"}));
        const auto interior = session.profile().pendingRaid->interiors.front();
        auto &player = const_cast<Player &>(session.world().player());
        ASSERT_TRUE(player.setPosition(interior.exteriorEntrance.position));
        GameplayInput interact;
        interact.interactJustPressed = true;
        session.update(interact, 0);
        ASSERT_FALSE(session.world().inOutdoorRaidSpace());
        const auto &loot = session.profile().pendingRaid->loot;
        const auto found = std::find_if(loot.begin(), loot.end(), [&](const auto &entry) {
            return entry.spaceId == interior.id && content.item(entry.definitionId).medicalUse.has_value();
        });
        ASSERT_NE(found, loot.end());
        medicalId = found->assetId;
        const auto position = found->position;
        ASSERT_TRUE(player.setPosition({position.x - 25, position.y - 25}));
        interact.aimWorldPosition = {position.x + 60.0F, position.y};
        session.update(interact, 0);
        ASSERT_NE(session.profile().assets.find(medicalId), nullptr);
        carriedLocation = session.profile().assets.find(medicalId)->location;
        ASSERT_TRUE(std::holds_alternative<StoredAssetLocation>(*carriedLocation));
        EXPECT_EQ(std::get<StoredAssetLocation>(*carriedLocation).container,
                  ProfileContainerId::compartment(backpack, 0));
        ASSERT_TRUE(player.setPosition(interior.interiorExit.position));
        session.update(interact, 0);
        ASSERT_TRUE(session.world().inOutdoorRaidSpace());
        ASSERT_TRUE(player.setPosition(session.world().extractionPoint().bounds().position));
        for (int i=0; i<40 && session.profile().pendingRaid; ++i)
            session.update(GameplayInput{}, 0.1F);
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        ASSERT_FALSE(session.profile().pendingRaid);
        EXPECT_EQ(session.profile().assets.find(medicalId)->location, *carriedLocation);
    }
    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_NE(reopened.profile().assets.find(medicalId), nullptr);
    EXPECT_EQ(reopened.profile().assets.find(medicalId)->location, *carriedLocation);
    auto profile = reopened.profile();
    profile.currentHealth = 50;
    static_cast<void>(applyWoundRoll(profile.medicalStatus,
        WoundRollCommand{WoundSource::Scratch, 0, 15000}));
    const auto before = profile.assets.find(medicalId)->remainingCharges;
    const auto currency = profile.currency;
    const auto used = executeMedicalUse(profile, content, medicalId, MedicalAccess::AnyOwned,
        CommandContext{profile.revision, "hospital-use-returned-medicine"});
    ASSERT_TRUE(used.succeeded) << used.message;
    const auto *after = profile.assets.find(medicalId);
    EXPECT_TRUE(after == nullptr || after->remainingCharges < before);
    EXPECT_EQ(profile.currency, currency);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(AlphaExtractionSessionTest, FreightKeyLinksTwoSortiesAndWorkshopProduction)
{
    const auto &content = publishedContentRegistry();
    const MapDefinitionId riverside{"map.raid.riverside"};
    const MapDefinitionId frontier{"map.raid.frontier_exchange"};
    const RaidSpaceDefinitionId freight{"raid_space.frontier_exchange.freight_service_bay"};
    const ItemDefinitionId precision{"item.loot.precision_components"};
    auto profile = makeNewPublishedProfile("cross-map-freight", content);
    profile.tutorial = TutorialProgress::Complete;
    for (const auto id : assets(profile, precision))
        ASSERT_TRUE(profile.assets.erase(id));
    const auto bags = assets(profile, alpha_content::backpack);
    ASSERT_FALSE(bags.empty());
    const auto bag = bags.front();
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{bag, EquipmentSlotKind::Backpack},
        {profile.revision, "freight-loop-backpack"}).succeeded);

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-freight-", "two-sorties");
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const auto move = [&](Vec2 position) {
        return const_cast<Player &>(session.world().player()).setPosition(position);
    };
    const auto extract = [&] {
        ASSERT_TRUE(move(session.world().extractionPoint().bounds().position));
        for (int frame = 0; frame < 45 && session.profile().pendingRaid; ++frame)
            session.update({}, 0.1F);
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        ASSERT_FALSE(session.profile().pendingRaid);
    };
    GameplayInput interact;
    interact.interactJustPressed = true;
    ASSERT_TRUE(session.deployAlpha(14U, riverside)) << session.persistenceMessage();
    ASSERT_TRUE(session.profile().pendingRaid->freightKeyLootSlotId);
    const auto &loot = session.profile().pendingRaid->loot;
    const auto keyLoot = std::find_if(loot.begin(), loot.end(), [](const auto &entry) {
        return entry.definitionId == kFreightCrateKeyId;
    });
    ASSERT_NE(keyLoot, loot.end());
    const auto key = keyLoot->assetId;
    const Vec2 keyPosition = keyLoot->position;
    const float half = session.world().player().size() * 0.5F;
    ASSERT_TRUE(move({keyPosition.x - half, keyPosition.y - half}));
    auto pickUp = interact;
    pickUp.aimWorldPosition = {keyPosition.x + 60.0F, keyPosition.y};
    session.update(pickUp, 0.0F);
    ASSERT_TRUE(assetIsCarried(session.profile(), key)) << session.persistenceMessage();
    ASSERT_NO_FATAL_FAILURE(extract());
    EXPECT_TRUE(assetIsCarried(session.profile(), key));

    ASSERT_TRUE(session.deployAlpha(42U, frontier)) << session.persistenceMessage();
    const auto &raid = *session.profile().pendingRaid;
    ASSERT_TRUE(raid.sealedFreightCrate);
    const auto crate = raid.sealedFreightCrate->containerAssetId;
    const auto *crateAsset = session.profile().assets.find(crate);
    ASSERT_NE(crateAsset, nullptr);
    const auto &root = std::get<RaidSceneContainerAssetLocation>(crateAsset->location);
    const Vec2 cratePosition = root.position;
    const auto location = std::find_if(raid.interiors.begin(), raid.interiors.end(), [&](const auto &interior) {
        return interior.id == freight;
    });
    ASSERT_NE(location, raid.interiors.end());
    const auto bay = *location;
    const auto reward = std::find_if(raid.loot.begin(), raid.loot.end(), [&](const auto &entry) {
        return entry.sceneContainerAssetId == crate;
    });
    ASSERT_NE(reward, raid.loot.end());
    EXPECT_EQ(reward->definitionId, precision);
    const auto material = reward->assetId;
    ASSERT_TRUE(move(bay.exteriorEntrance.position));
    session.update(interact, 0.0F);
    ASSERT_EQ(session.world().activeRaidSpaceId(), freight);
    for (const auto &enemy : session.world().enemies())
        static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
    session.update({}, 0.0F);
    ASSERT_TRUE(move({cratePosition.x - half - 48.0F, cratePosition.y - half}));

    GameplayInput hold;
    hold.aimWorldPosition = cratePosition;
    hold.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
    session.update(hold, 0.0F);
    const auto eligible = session.raidFreightUnlock();
    ASSERT_TRUE(eligible);
    ASSERT_EQ(eligible->containerAssetId, crate);
    ASSERT_TRUE(eligible->hasKey);
    hold.interactPressed = hold.interactJustPressed = true;
    std::optional<AssetInstanceId> opened;
    for (int frame = 0; frame < 22 && !opened; ++frame)
    {
        session.update(hold, 0.1F);
        hold.interactJustPressed = false;
        opened = session.takeOpenedRaidFreightCrate();
    }
    ASSERT_EQ(opened, crate) << session.persistenceMessage();
    EXPECT_TRUE(assetIsCarried(session.profile(), key));
    const auto bagCompartment = ProfileContainerId::compartment(bag, 0);
    const auto fit = findFirstProfileFit(session.profile(), content, bagCompartment,
        content.item(precision), ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    const auto taken = session.executeRaidSceneContainerInventory(crate,
        InventoryMoveCommand{material, 0U, {bagCompartment, *fit}}, "freight-loop-take-material");
    ASSERT_TRUE(taken.succeeded) << taken.message;
    EXPECT_FALSE(raidSceneContainerRoot(session.profile(), material));
    EXPECT_TRUE(assetIsCarried(session.profile(), material));
    ASSERT_TRUE(move(bay.interiorExit.position));
    session.update(interact, 0.0F);
    ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    ASSERT_NO_FATAL_FAILURE(extract());

    const auto stash = ProfileContainerId::stash();
    const auto stashFit = findFirstProfileFit(session.profile(), content, stash,
        content.item(precision), ItemOrientation::Degrees0);
    ASSERT_TRUE(stashFit);
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryMoveCommand{material, 0U, {stash, *stashFit}}, "freight-loop-stash-material").succeeded);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    const auto order = session.executeStartBaseManufacturing(
        BaseManufacturingRecipeDefinitionId{"base_manufacturing.ammunition_5_45x39_expanding"}, "freight-loop-order");
    ASSERT_TRUE(order.succeeded) << order.message;
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_TRUE(order.outputAssetId);

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_TRUE(assetIsCarried(reopened.profile(), key));
    ASSERT_TRUE(reopened.profile().baseManufacturing.activeOrder);
    EXPECT_EQ(reopened.profile().baseManufacturing.activeOrder->inputAssetIds,
        std::vector<AssetInstanceId>{material});
    reopened.advanceBaseWorldClock(120.0F);
    ASSERT_TRUE(waitForProfileUiDurability(reopened));
    const auto *output = reopened.profile().assets.find(*order.outputAssetId);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->definitionId, kExpanding545AmmunitionId);
    EXPECT_EQ(output->quantity, 18U);
}

TEST(AlphaExtractionSessionTest, HospitalNightVisionFollowsLootAssemblySecondSortieAndReopen)
{
    const auto &content = publishedContentRegistry();
    const MapDefinitionId hospital{"map.raid.hospital_district"};
    auto profile = makeNewPublishedProfile("hospital-night-loop", content);
    profile.tutorial = TutorialProgress::Complete;
    const auto bags = assets(profile, alpha_content::backpack);
    ASSERT_FALSE(bags.empty());
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{bags.front(), EquipmentSlotKind::Backpack},
        {profile.revision, "night-test-pack"}).succeeded);

    const auto makeStored = [&](const ItemDefinitionId &id) {
        const auto &definition = content.item(id);
        const auto fit = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
            definition, ItemOrientation::Degrees0);
        return fit ? profile.assets.create(definition,
            StoredAssetLocation{ProfileContainerId::stash(), *fit}) : AssetInstanceId{};
    };
    const auto helmet = makeStored(ItemDefinitionId{"item.protective_gear.helmet_basic"});
    const auto battery = makeStored(ItemDefinitionId{"item.battery.universal"});
    ASSERT_NE(helmet, 0U);
    ASSERT_NE(battery, 0U);
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{helmet, EquipmentSlotKind::Helmet},
        {profile.revision, "night-test-helmet"}).succeeded);
    const auto capacity = profile.assets.find(battery)->remainingCharges;

    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-hospital-night-", "two-sorties");
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
    BaseWorld base;
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const auto move = [&](Vec2 position) {
        return const_cast<Player &>(session.world().player()).setPosition(position);
    };
    GameplayInput interact;
    interact.interactJustPressed = true;
    const auto pickUp = [&](const RaidLootSnapshot &loot) {
        const float half = session.world().player().size() * 0.5F;
        ASSERT_TRUE(move({loot.position.x - half, loot.position.y - half}));
        auto action = interact;
        action.aimWorldPosition = {loot.position.x + 60.0F, loot.position.y};
        session.update(action, 0.0F);
        ASSERT_TRUE(assetIsCarried(session.profile(), loot.assetId)) << session.persistenceMessage();
    };
    const auto extract = [&] {
        ASSERT_TRUE(move(session.world().extractionPoint().bounds().position));
        for (int frame = 0; frame < 45 && session.profile().pendingRaid; ++frame)
            session.update({}, 0.1F);
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        ASSERT_FALSE(session.profile().pendingRaid);
    };
    ASSERT_TRUE(session.deployAlpha(42U, hospital)) << session.persistenceMessage();
    EXPECT_EQ(session.worldClockProjection().timeOfDay, WorldTimeOfDay::Day);
    const auto firstInterior = session.profile().pendingRaid->interiors.front();
    ASSERT_TRUE(move(firstInterior.exteriorEntrance.position));
    session.update(interact, 0.0F);
    ASSERT_FALSE(session.world().inOutdoorRaidSpace());
    const auto &firstLoot = session.profile().pendingRaid->loot;
    const auto device = std::find_if(firstLoot.begin(), firstLoot.end(), [](const auto &loot) {
        return loot.definitionId == kHospitalNightVisionId;
    });
    ASSERT_NE(device, firstLoot.end());
    const RaidLootSnapshot foundDevice = *device;
    ASSERT_NO_FATAL_FAILURE(pickUp(foundDevice));
    ASSERT_TRUE(move(firstInterior.interiorExit.position));
    session.update(interact, 0.0F);
    ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    ASSERT_NO_FATAL_FAILURE(extract());

    ASSERT_TRUE(base.canAccessStash());
    session.bindBasePlayerWorld(&base);
    const auto installed = session.requestProfileUiCommand(
        HelmetDeviceCommand{helmet, foundDevice.assetId}, "night-loop-install");
    ASSERT_TRUE(installed.accepted) << installed.message;
    ASSERT_TRUE(waitForProfileUiDurability(session));
    EXPECT_EQ(findNightVision(session.profile(), content, helmet).deviceAssetId, foundDevice.assetId);
    const auto powered = session.changeDeviceBattery({foundDevice.assetId, battery}, "night-loop-power");
    ASSERT_TRUE(powered.succeeded) << powered.message;
    ASSERT_TRUE(session.executeBaseRest(6, "night-loop-rest").succeeded);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    const auto travel = queryRaidTravel(session.profile(), content, content.map(hospital));
    ASSERT_TRUE(travel.reachable);
    EXPECT_EQ(travel.arrival.timeOfDay, WorldTimeOfDay::Night);
    const auto isMedical = [&](const RaidLootSnapshot &loot) {
        return loot.spaceId == firstInterior.id && content.item(loot.definitionId).medicalUse.has_value();
    };
    const auto medicalSeed = hospitalMedicalLootSeed(session.profile());
    session.bindBasePlayerWorld(nullptr);
    ASSERT_TRUE(session.deployAlpha(medicalSeed, hospital)) << session.persistenceMessage();

    GameplayInput tap;
    tap.lightJustPressed = tap.lightJustReleased = true;
    session.update(tap, 0.0F);
    ASSERT_TRUE(session.activeNightVision().output) << session.persistenceMessage();
    session.update({}, 0.1F);
    EXPECT_LT(session.activeNightVision().remainingCharges, capacity);
    const auto secondInterior = session.profile().pendingRaid->interiors.front();
    ASSERT_TRUE(move(secondInterior.exteriorEntrance.position));
    session.update(interact, 0.0F);
    ASSERT_FALSE(session.world().inOutdoorRaidSpace());
    EXPECT_TRUE(session.activeNightVision().output);
    const auto &secondLoot = session.profile().pendingRaid->loot;
    const auto medical = std::find_if(secondLoot.begin(), secondLoot.end(), isMedical);
    ASSERT_NE(medical, secondLoot.end());
    const RaidLootSnapshot foundMedical = *medical;
    ASSERT_NO_FATAL_FAILURE(pickUp(foundMedical));
    ASSERT_TRUE(move(secondInterior.interiorExit.position));
    session.update(interact, 0.0F);
    ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    ASSERT_NO_FATAL_FAILURE(extract());

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    const auto binding = findNightVision(reopened.profile(), content, helmet);
    EXPECT_EQ(binding.deviceAssetId, foundDevice.assetId);
    EXPECT_EQ(binding.batteryAssetId, battery);
    const auto *cell = reopened.profile().assets.find(battery);
    ASSERT_NE(cell, nullptr);
    EXPECT_GT(cell->remainingCharges, 0U);
    EXPECT_LT(cell->remainingCharges, capacity);
    EXPECT_EQ(cell->remainingCharges, session.profile().assets.find(battery)->remainingCharges);
    EXPECT_TRUE(assetIsCarried(reopened.profile(), foundMedical.assetId));
}

void enterActiveHospitalRoom(GameSession &session, RaidLootSnapshot &floorplan)
{
    ASSERT_TRUE(session.profile().pendingRaid);
    const auto &raid = *session.profile().pendingRaid;
    const auto interior = std::find_if(raid.interiors.begin(), raid.interiors.end(),
        [](const RaidInteriorSnapshot &entry) { return entry.id == kHospitalFloorplanSpaceId; });
    ASSERT_NE(interior, raid.interiors.end());
    const auto loot = std::find_if(raid.loot.begin(), raid.loot.end(),
        [](const RaidLootSnapshot &entry) { return entry.definitionId == kHospitalFloorplanItemId; });
    ASSERT_NE(loot, raid.loot.end());
    floorplan = *loot;
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(interior->exteriorEntrance.position));
    GameplayInput interact;
    interact.interactJustPressed = true;
    session.update(interact, 0.0F);
    EXPECT_EQ(session.world().activeRaidSpaceId(), kHospitalFloorplanSpaceId);
    ASSERT_TRUE(session.world().activeInteriorMapProjection());
}

void enterHospitalFloorplanRoom(GameSession &session, RaidLootSnapshot &floorplan)
{
    ASSERT_TRUE(session.deployAlpha(42U, kHospitalFloorplanMapId)) << session.persistenceMessage();
    ASSERT_NO_FATAL_FAILURE(enterActiveHospitalRoom(session, floorplan));
}

void pickUpHospitalFloorplan(GameSession &session, const RaidLootSnapshot &floorplan)
{
    auto &player = const_cast<Player &>(session.world().player());
    const float half = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({floorplan.position.x - half, floorplan.position.y - half}));
    GameplayInput interact;
    interact.aimWorldPosition = {floorplan.position.x + 60.0F, floorplan.position.y};
    interact.interactJustPressed = true;
    session.update(interact, 0.0F);
}

void equipHospitalFloorplanPack(GameSession &session)
{
    const auto backpacks = assets(session.profile(), alpha_content::backpack);
    ASSERT_FALSE(backpacks.empty());
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{backpacks.front(), EquipmentSlotKind::Backpack},
        "floorplan-pack").succeeded);
}

const RaidLootSnapshot *hospitalPharmacyStock(const PendingRaidSnapshot &raid)
{
    const auto stock = std::find_if(raid.loot.begin(), raid.loot.end(), [](const RaidLootSnapshot &loot) {
        return loot.spaceId == kHospitalFloorplanSpaceId &&
            loot.position.x == 2190.0F && loot.position.y == 390.0F &&
            loot.definitionId == kHospitalFirstAidStockId;
    });
    return stock == raid.loot.end() ? nullptr : &*stock;
}

TEST(AlphaExtractionSessionTest, HospitalLungeFractureFollowsPainReliefExtractionAndClinic)
{
    const ContentRegistry &content = publishedContentRegistry();
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-fracture-raid-", "lunge-extraction");

    MedicalStatusState returnedMedical;
    {
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.startNewProfile("hospital-fracture-raid"));
        ASSERT_NO_FATAL_FAILURE(equipHospitalFloorplanPack(session));
        const auto pack = equippedAsset(session.profile(), EquipmentSlotKind::Backpack);
        ASSERT_TRUE(pack);
        const auto painkillers = assets(session.profile(), alpha_content::painkiller);
        ASSERT_FALSE(painkillers.empty());
        const auto painkiller = painkillers.front();
        const auto pocket = ProfileContainerId::compartment(*pack, 0);
        const auto origin = findFirstProfileFit(session.profile(), content, pocket,
            content.item(alpha_content::painkiller), ItemOrientation::Degrees0);
        ASSERT_TRUE(origin);
        ASSERT_TRUE(session.executeProfileInventory(
            InventoryMoveCommand{painkiller, 0, {pocket, *origin}}, "carry-painkiller").succeeded);

        // Seed 1's first eligible roll is below 65%. Execute the real damage and
        // treatment transitions, with only the attack timing pinned by the test.
        ASSERT_TRUE(session.deployAlpha(1U, kHospitalFloorplanMapId)) << session.persistenceMessage();
        const auto interior = session.profile().pendingRaid->interiors.front();
        RaidLootSnapshot floorplan;
        ASSERT_NO_FATAL_FAILURE(enterActiveHospitalRoom(session, floorplan));
        const auto &roster = session.world().enemies();
        ASSERT_EQ(std::count_if(roster.begin(), roster.end(),
            [](const Enemy &enemy) { return isLowLungeEnemyVariant(enemy.variant()); }), 1);
        for (const auto &enemy : roster)
        {
            if (!isLowLungeEnemyVariant(enemy.variant()))
                static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        }
        const auto lunge = std::find_if(roster.begin(), roster.end(),
            [](const Enemy &enemy) { return isLowLungeEnemyVariant(enemy.variant()); });
        ASSERT_NE(lunge, roster.end());
        const Vec2 enemySize = lunge->size();
        const Vec2 enemyPosition = lunge->position();
        auto &player = const_cast<Player &>(session.world().player());
        const float halfBody = player.size() / 2.0F;
        ASSERT_TRUE(player.setPosition({
            enemyPosition.x + enemySize.x / 2.0F + 62.0F - halfBody,
            enemyPosition.y + enemySize.y / 2.0F - halfBody}));
        ASSERT_TRUE(const_cast<Enemy &>(*lunge).tryStartAttack(EnemyAttackType::LowLunge, {1, 0}));
        for (int frame = 0; frame < 20 && session.profile().currentHealth == 100; ++frame)
            session.update({}, 0.05F);

        ASSERT_TRUE(session.profile().medicalStatus.legFractured) << session.persistenceMessage();
        EXPECT_EQ(session.profile().currentHealth, 83);
        EXPECT_EQ(session.profile().medicalStatus.bleeding, BleedingSeverity::None);
        ASSERT_TRUE(session.lastIncomingDamage());
        EXPECT_EQ(session.lastIncomingDamage()->region, HitRegion::Legs);
        EXPECT_EQ(session.lastIncomingDamage()->damageApplied, 17);

        for (const auto &enemy : session.world().enemies())
            static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        session.update({}, 0.3F);
        const auto moveDistance = [&](bool sprint) {
            constexpr Vec2 openFloor{1800.0F, 970.0F};
            EXPECT_TRUE(player.setPosition(openFloor));
            GameplayInput input;
            input.moveRight = true;
            input.sprint = sprint;
            session.update(input, 0.1F);
            return player.position().x - openFloor.x;
        };
        const float fracturedWalk = moveDistance(false);
        EXPECT_GT(fracturedWalk, 0.0F);
        EXPECT_NEAR(moveDistance(true), fracturedWalk, 0.01F);

        ASSERT_TRUE(session.startAlphaMedical(painkiller));
        for (int frame = 0; frame < 25 && session.raidActionState().active(); ++frame)
            session.update({}, 0.1F);
        ASSERT_TRUE(painIsSuppressed(session.profile().medicalStatus));
        EXPECT_TRUE(session.profile().medicalStatus.legFractured);
        const float treatedWalk = moveDistance(false);
        EXPECT_NEAR(fracturedWalk, 0.7F * treatedWalk, 0.01F);
        EXPECT_NEAR(moveDistance(true), 1.5F * treatedWalk, 0.01F);

        GameplayInput interact;
        interact.interactJustPressed = true;
        ASSERT_TRUE(player.setPosition(interior.interiorExit.position));
        session.update(interact, 0.0F);
        ASSERT_TRUE(session.world().inOutdoorRaidSpace());
        for (const auto &enemy : session.world().enemies())
            static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        ASSERT_TRUE(player.setPosition(session.world().extractionPoint().bounds().position));
        for (int frame = 0; frame < 100 && session.profile().pendingRaid; ++frame)
            session.update({}, 0.1F);
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        ASSERT_FALSE(session.profile().pendingRaid) << session.persistenceMessage();
        ASSERT_TRUE(session.profile().lastRaidResult);
        EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
        returnedMedical = session.profile().medicalStatus;
        EXPECT_TRUE(returnedMedical.legFractured);
        EXPECT_GT(returnedMedical.painkillerRemainingMs, 0U);
    }

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().medicalStatus, returnedMedical);
    EXPECT_EQ(reopened.profile().currentHealth, 83);
    const auto currency = reopened.profile().currency;
    const auto health = reopened.executeBasePaidMedicalService("clinic-fracture-extraction");
    ASSERT_TRUE(health.succeeded) << health.message;
    EXPECT_TRUE(health.clearedFracture);
    EXPECT_EQ(health.currencyPaid, 96U);
    EXPECT_EQ(reopened.profile().currency, currency - 96U);
    EXPECT_EQ(reopened.profile().currentHealth, 100);
    EXPECT_FALSE(reopened.profile().medicalStatus.legFractured);
    EXPECT_EQ(reopened.profile().medicalStatus.painkillerRemainingMs,
              returnedMedical.painkillerRemainingMs);
}

TEST(AlphaExtractionSessionTest, HospitalFloorplanFailedWriteRetriesAndPreservesOnlyKnowledge)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-hospital-floorplan-", "durable-pickup");
    const auto &content = publishedContentRegistry();
    SaveRepository repository(directory.path());
    ProfileState expectedRecovery;
    {
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.startNewProfile("floorplan-durable-pickup"));
        ASSERT_NO_FATAL_FAILURE(equipHospitalFloorplanPack(session));
        RaidLootSnapshot floorplan;
        ASSERT_NO_FATAL_FAILURE(enterHospitalFloorplanRoom(session, floorplan));
        EXPECT_FALSE(session.world().activeInteriorMapProjection()->layoutKnown);
        auto priorCheckpoint = repository.load(content);
        ASSERT_TRUE(priorCheckpoint.profile) << priorCheckpoint.message;
        expectedRecovery = std::move(*priorCheckpoint.profile);
        ASSERT_FALSE(expectedRecovery.pendingRaid);
        ASSERT_FALSE(expectedRecovery.raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));

        const auto blockedTemp = directory.path() / "profile.tmp.json";
        ASSERT_TRUE(std::filesystem::create_directory(blockedTemp));
        const auto before = profileStateFingerprint(session.profile());
        static_cast<void>(session.takePresentationEvents());
        ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, floorplan));
        ASSERT_TRUE(session.profileUiCommitPending());
        EXPECT_FALSE(waitForProfileUiDurability(session));
        EXPECT_EQ(profileStateFingerprint(session.profile()), before);
        const auto *ground = session.profile().assets.find(floorplan.assetId);
        ASSERT_NE(ground, nullptr);
        EXPECT_TRUE(std::holds_alternative<RaidGroundAssetLocation>(ground->location));
        EXPECT_FALSE(session.world().activeInteriorMapProjection()->layoutKnown);
        EXPECT_TRUE(session.takePresentationEvents().empty());
        auto completions = session.takeProfileUiCompletions();
        ASSERT_EQ(completions.size(), 1U);
        EXPECT_EQ(completions.front().kind, ProfileUiCompletionKind::HospitalFloorplan);
        EXPECT_FALSE(completions.front().succeeded);

        ASSERT_TRUE(std::filesystem::remove(blockedTemp));
        ASSERT_TRUE(session.retryProfileUiCommit().accepted) << session.persistenceMessage();
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        EXPECT_TRUE(assetIsCarried(session.profile(), floorplan.assetId));
        EXPECT_TRUE(session.profile().raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));
        EXPECT_TRUE(session.world().activeInteriorMapProjection()->layoutKnown);
        EXPECT_EQ(session.takePresentationEvents(),
            std::vector{GameSessionPresentationEvent::LootPickedUp});
        completions = session.takeProfileUiCompletions();
        ASSERT_EQ(completions.size(), 1U);
        EXPECT_EQ(completions.front().kind, ProfileUiCompletionKind::HospitalFloorplan);
        EXPECT_TRUE(completions.front().succeeded);
        ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, floorplan));
        EXPECT_FALSE(session.profileUiCommitPending());
        EXPECT_TRUE(session.takePresentationEvents().empty());
    }

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    expectedRecovery.raidInteriorIntelligence.knownLayouts.insert(kHospitalFloorplanSpaceId);
    ++expectedRecovery.revision;
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), profileStateFingerprint(expectedRecovery));

    RaidLootSnapshot secondFloorplan;
    ASSERT_NO_FATAL_FAILURE(enterHospitalFloorplanRoom(reopened, secondFloorplan));
    EXPECT_TRUE(reopened.world().activeInteriorMapProjection()->layoutKnown);
    ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(reopened, secondFloorplan));
    EXPECT_TRUE(assetIsCarried(reopened.profile(), secondFloorplan.assetId));
    EXPECT_FALSE(reopened.profileUiCommitPending()); // Known plans are ordinary loot.

    auto &player = const_cast<Player &>(reopened.world().player());
    ASSERT_TRUE(player.setPosition(reopened.profile().pendingRaid->interiors.front().interiorExit.position));
    GameplayInput exit;
    exit.interactJustPressed = true;
    reopened.update(exit, 0.0F);
    ASSERT_TRUE(reopened.world().inOutdoorRaidSpace());
    ASSERT_TRUE(player.setPosition(reopened.world().extractionPoint().bounds().position));
    for (int frame = 0; frame < 45 && reopened.profile().pendingRaid; ++frame)
        reopened.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(reopened)) << reopened.persistenceMessage();
    ASSERT_FALSE(reopened.profile().pendingRaid);
    ASSERT_TRUE(assetIsCarried(reopened.profile(), secondFloorplan.assetId));
    auto returned = reopened.profile();
    const auto recycled = executeEconomy(returned, content, RecycleCommand{secondFloorplan.assetId},
        {returned.revision, "floorplan-recycle"});
    ASSERT_TRUE(recycled.succeeded) << recycled.message;
    EXPECT_EQ(recycled.currencyDelta, 35);
    EXPECT_TRUE(returned.raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));
}

void standAtHospitalPoint(GameSession &session, Vec2 center)
{
    auto &player = const_cast<Player &>(session.world().player());
    const float half = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({center.x - half, center.y - half}));
}

void accompanyHospitalSurvivorToHandoff(GameSession &session)
{
    auto &world = session.world();
    const auto initial = world.hospitalEscort();
    ASSERT_TRUE(initial);
    ASSERT_EQ(initial->state, HospitalEscortState::AwaitingContact);
    const auto start = initial->position;
    const auto zone = initial->handoff;
    const Vec2 goal{zone.position.x + zone.size.x * .5F, zone.position.y + zone.size.y * .5F};
    const auto residents = session.profile().basePopulation.ordinaryResidents;
    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, goal));
    GameplayInput hold;
    hold.interactPressed = true;
    session.update(hold, 2.1F);
    ASSERT_FALSE(session.profileUiCommitPending());
    ASSERT_FALSE(session.profile().pendingRaid->rescue->secured);
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, residents);

    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, start));
    GameplayInput tap;
    tap.interactJustPressed = true;
    session.update(tap, 0.0F);
    ASSERT_EQ(world.hospitalEscort()->state, HospitalEscortState::Following);
    session.update({}, 0.0F);
    session.update(tap, 0.0F);
    ASSERT_EQ(world.hospitalEscort()->state, HospitalEscortState::Waiting);
    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, goal));
    session.update({}, 0.2F);
    EXPECT_FLOAT_EQ(world.hospitalEscort()->position.x, start.x);
    EXPECT_FLOAT_EQ(world.hospitalEscort()->position.y, start.y);
    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, start));
    session.update(tap, 0.0F);
    ASSERT_EQ(world.hospitalEscort()->state, HospitalEscortState::Following);
    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, goal));
    session.update({}, 0.1F);
    ASSERT_FALSE(world.hospitalEscort()->atHandoff);
    EXPECT_LT(std::hypot(world.hospitalEscort()->position.x - start.x,
                         world.hospitalEscort()->position.y - start.y), 40.0F);

    // Only the player was staged at the handoff. The survivor travels the published
    // outdoor layout using its own collision and navigation until it enters the zone.
    for (int frame = 0; frame < 500 && !world.hospitalEscort()->atHandoff; ++frame)
        session.update({}, 0.1F);
    const auto arrived = world.hospitalEscort();
    ASSERT_TRUE(arrived && arrived->atHandoff);
    EXPECT_GT(std::hypot(arrived->position.x - start.x, arrived->position.y - start.y), 250.0F);
    EXPECT_FALSE(arrived->threatened);
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, residents);
    EXPECT_FALSE(session.profile().pendingRaid->rescue->secured);
}

TEST(AlphaExtractionSessionTest, HospitalEscortThreatBreaksHandoffAndAdmitsOneInjuredMedicEvenWithFullBeds)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-escort-", "hospital-full-beds-and-threat");
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("hospital-escort-full-beds", content);
    auto &population = profile.basePopulation;
    population.professionResidents[baseProfessionIndex(BaseResidentProfession::General)] +=
        population.bedCapacity - population.ordinaryResidents;
    population.ordinaryResidents = population.bedCapacity;
    const auto starting = population;
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile());
    ASSERT_TRUE(session.deployAlpha(42U, kHospitalFloorplanMapId)) << session.persistenceMessage();
    const auto admission = session.ordinarySurvivorRescuePlan();
    ASSERT_TRUE(admission && admission->canCommit);
    EXPECT_EQ(admission->bedShortfallAfter, 1U);
    auto &world = session.world();
    const auto start = *session.profile().pendingRaid->rescue->escortStart;
    const auto size = content.map(kHospitalFloorplanMapId).worldSize;
    const Vec2 away{start.x < size.x / 2 ? size.x - 200 : 200, start.y < size.y / 2 ? size.y - 200 : 200};
    ASSERT_FALSE(world.enemies().empty());
    for (const auto &enemy : world.enemies())
        ASSERT_TRUE(const_cast<Enemy &>(enemy).setPosition(away));
    ASSERT_NO_FATAL_FAILURE(accompanyHospitalSurvivorToHandoff(session));
    GameplayInput hold;
    hold.interactPressed = true;
    session.update(hold, 0.35F);
    ASSERT_GT(world.ordinarySurvivorRescueProgress(), 0.0F);
    const auto survivor = world.hospitalEscort()->position;
    auto &intruder = const_cast<Enemy &>(world.enemies().front());
    ASSERT_TRUE(intruder.setPosition({survivor.x + 35.0F, survivor.y}));
    session.update(hold, 0.05F);
    ASSERT_TRUE(world.hospitalEscort()->threatened);
    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueProgress(), 0.0F);
    EXPECT_EQ(session.profile().basePopulation, starting);
    EXPECT_FALSE(session.profile().committedRescues.contains(kHospitalMedicalRescueId));
    ASSERT_TRUE(intruder.takeDamage(10000));
    session.update({}, 0.1F);
    ASSERT_FALSE(world.hospitalEscort()->threatened);
    session.update(hold, 2.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    ASSERT_TRUE(session.profile().pendingRaid->rescue->secured);
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, starting.ordinaryResidents + 1);
    EXPECT_EQ(session.profile().basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::Medical)],
        starting.injuredByProfession[baseProfessionIndex(BaseResidentProfession::Medical)] + 1);
    EXPECT_EQ(session.profile().basePopulation.bedCapacity, starting.bedCapacity);
    const auto secured = session.profile().basePopulation;
    session.update(hold, 2.1F);
    EXPECT_EQ(session.profile().basePopulation, secured);
}

TEST(AlphaExtractionSessionTest, DownedHospitalEscortCannotAdmitAResidentAndCanBeFoundOnANewSortie)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-escort-", "lost-before-safe-handoff");
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.startNewProfile("hospital-escort-lost-before-handoff"));
    const auto starting = session.profile().basePopulation;
    ASSERT_TRUE(session.deployAlpha(42U, kHospitalFloorplanMapId));
    auto &world = session.world();
    const auto before = world.hospitalEscort();
    ASSERT_TRUE(before);
    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, before->position));
    GameplayInput tap;
    tap.interactJustPressed = true;
    session.update(tap, 0.0F);
    ASSERT_EQ(world.hospitalEscort()->state, HospitalEscortState::Following);
    session.update({}, 0.0F);
    session.update(tap, 0.0F);
    ASSERT_EQ(world.hospitalEscort()->state, HospitalEscortState::Waiting);
    const auto zone = before->handoff;
    ASSERT_NO_FATAL_FAILURE(standAtHospitalPoint(session, {zone.position.x + zone.size.x * .5F,
        zone.position.y + zone.size.y * .5F}));
    auto &actors = WeaponHandlingTestAccess::actors(world);
    ASSERT_GE(actors.size(), 4U);
    const auto position = before->position;
    const Vec2 offsets[]{{34, 0}, {-34, 0}, {0, 34}, {0, -34}};
    for (int i = 0; i < 4; ++i)
    {
        const auto size = actors[i].size();
        ASSERT_TRUE(actors[i].setPosition({position.x + offsets[i].x - size.x / 2,
            position.y + offsets[i].y - size.y / 2}));
    }
    for (int frame = 0; frame < 700 && world.hospitalEscort()->state != HospitalEscortState::Down; ++frame)
        session.update({}, 0.05F);
    ASSERT_EQ(world.hospitalEscort()->state, HospitalEscortState::Down);
    EXPECT_EQ(world.hospitalEscort()->health, 0);
    GameplayInput hold;
    hold.interactPressed = true;
    session.update(hold, 2.1F);
    EXPECT_FALSE(world.ordinarySurvivorRescueReady());
    EXPECT_FALSE(session.profileUiCommitPending());
    EXPECT_FALSE(session.profile().pendingRaid->rescue->secured);
    EXPECT_EQ(session.profile().basePopulation, starting);
    ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().basePopulation, starting);
    EXPECT_FALSE(reopened.profile().committedRescues.contains(kHospitalMedicalRescueId));
    ASSERT_TRUE(reopened.deployAlpha(43U, kHospitalFloorplanMapId)) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.world().hospitalEscort());
    EXPECT_EQ(reopened.world().hospitalEscort()->state, HospitalEscortState::AwaitingContact);
    EXPECT_GT(reopened.world().hospitalEscort()->health, 0);
}

TEST(AlphaExtractionSessionTest, HospitalRescueAndFloorplanShareCheckpointInEitherOrder)
{
    const auto &content = publishedContentRegistry();
    for (const bool rescueFirst : {false, true})
    {
        SCOPED_TRACE(rescueFirst);
        raidline::test::TemporaryDirectory directory;
        directory.claim("raidline-hospital-rescue-", rescueFirst ? "rescue-first" : "floorplan-first");
        const SaveRepository repository(directory.path());
        BasePopulationState initialPopulation;
        std::size_t initialFirstAid{};
        {
            GameSession session;
            session.configurePersistence(directory.path());
            ASSERT_TRUE(session.startNewProfile("hospital-rescue-checkpoint"));
            ASSERT_NO_FATAL_FAILURE(equipHospitalFloorplanPack(session));
            initialPopulation = session.profile().basePopulation;
            initialFirstAid = assets(session.profile(), kHospitalFirstAidStockId).size();
            ASSERT_TRUE(session.deployAlpha(42U, kHospitalFloorplanMapId)) << session.persistenceMessage();
            ASSERT_TRUE(session.profile().pendingRaid && session.profile().pendingRaid->rescue);
            ASSERT_TRUE(session.profile().pendingRaid->rescue->escortStart);
            for (const auto &enemy : session.world().enemies())
                ASSERT_TRUE(const_cast<Enemy &>(enemy).takeDamage(1000));
            auto &player = const_cast<Player &>(session.world().player());
            const auto pickUpFloorplanAndStock = [&] {
                RaidLootSnapshot floorplan;
                ASSERT_NO_FATAL_FAILURE(enterActiveHospitalRoom(session, floorplan));
                ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, floorplan));
                ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
                ASSERT_TRUE(assetIsCarried(session.profile(), floorplan.assetId));
                const auto &raid = *session.profile().pendingRaid;
                const auto *stock = hospitalPharmacyStock(raid);
                ASSERT_NE(stock, nullptr);
                const AssetInstanceId stockId = stock->assetId;
                const ContentRect exit = raid.interiors.front().interiorExit;
                ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, *stock));
                ASSERT_TRUE(assetIsCarried(session.profile(), stockId));
                ASSERT_TRUE(player.setPosition(exit.position));
                GameplayInput leave;
                leave.interactJustPressed = true;
                session.update(leave, 0.0F);
                ASSERT_TRUE(session.world().inOutdoorRaidSpace());
            };
            const auto transfer = [&] {
                ASSERT_NO_FATAL_FAILURE(accompanyHospitalSurvivorToHandoff(session));
                ASSERT_TRUE(session.world().ordinarySurvivorRescueInteractionInRange());
                GameplayInput hold;
                hold.interactPressed = true;
                session.update(hold, 1.0F);
                EXPECT_FALSE(session.profile().pendingRaid->rescue->secured);
                session.update({}, 0.0F);
                EXPECT_FLOAT_EQ(session.world().ordinarySurvivorRescueProgress(), 0.0F);
                const auto blockedTemp = directory.path() / "profile.tmp.json";
                ASSERT_TRUE(std::filesystem::create_directory(blockedTemp));
                const auto before = repository.load(content);
                ASSERT_TRUE(before.profile) << before.message;
                session.update(hold, 2.0F);
                ASSERT_TRUE(session.profileUiCommitPending());
                const auto beforeRetry = profileStateFingerprint(session.profile());
                EXPECT_FALSE(waitForProfileUiDurability(session));
                EXPECT_EQ(profileStateFingerprint(session.profile()), beforeRetry);
                const auto failed = repository.load(content);
                ASSERT_TRUE(failed.profile) << failed.message;
                EXPECT_EQ(profileStateFingerprint(*failed.profile), profileStateFingerprint(*before.profile));
                EXPECT_FALSE(session.profile().committedRescues.contains(kHospitalMedicalRescueId));
                ASSERT_TRUE(std::filesystem::remove(blockedTemp));
                ASSERT_TRUE(session.retryProfileUiCommit().accepted) << session.persistenceMessage();
                ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
                EXPECT_TRUE(session.profile().pendingRaid->rescue->secured);
                EXPECT_TRUE(session.profile().committedRescues.contains(kHospitalMedicalRescueId));
                ASSERT_EQ(session.world().hospitalEscort()->state, HospitalEscortState::Delivered);
                const auto admitted = session.profile().basePopulation;
                session.update(hold, 2.1F);
                EXPECT_FALSE(session.profileUiCommitPending());
                EXPECT_EQ(session.profile().basePopulation, admitted);
            };
            if (rescueFirst)
            {
                ASSERT_NO_FATAL_FAILURE(transfer());
                ASSERT_NO_FATAL_FAILURE(pickUpFloorplanAndStock());
            }
            else
            {
                ASSERT_NO_FATAL_FAILURE(pickUpFloorplanAndStock());
                ASSERT_NO_FATAL_FAILURE(transfer());
            }
            EXPECT_TRUE(session.profile().raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));
            EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, initialPopulation.ordinaryResidents + 1U);
            EXPECT_GT(assets(session.profile(), kHospitalFirstAidStockId).size(), initialFirstAid);
        }
        GameSession reopened;
        reopened.configurePersistence(directory.path());
        ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
        EXPECT_FALSE(reopened.profile().pendingRaid);
        EXPECT_TRUE(reopened.profile().committedRescues.contains(kHospitalMedicalRescueId));
        EXPECT_TRUE(reopened.profile().raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));
        EXPECT_EQ(assets(reopened.profile(), kHospitalFirstAidStockId).size(), initialFirstAid);
        EXPECT_TRUE(assets(reopened.profile(), kHospitalFloorplanItemId).empty());
        const auto medical = baseProfessionIndex(BaseResidentProfession::Medical);
        EXPECT_EQ(reopened.profile().basePopulation.injuredByProfession[medical],
            initialPopulation.injuredByProfession[medical] + 1U);
        EXPECT_EQ(reopened.profile().basePopulation.professionResidents[medical],
            initialPopulation.professionResidents[medical] + 1U);
        ASSERT_TRUE(reopened.deployAlpha(42U, kHospitalFloorplanMapId)) << reopened.persistenceMessage();
        ASSERT_TRUE(reopened.profile().pendingRaid);
        EXPECT_FALSE(reopened.profile().pendingRaid->rescue);
        EXPECT_TRUE(reopened.profile().pendingRaid->hospitalMedicalReserveEnabled);
        if (!rescueFirst)
        {
            RaidLootSnapshot floorplan;
            ASSERT_NO_FATAL_FAILURE(enterActiveHospitalRoom(reopened, floorplan));
            const auto *stock = hospitalPharmacyStock(*reopened.profile().pendingRaid);
            ASSERT_NE(stock, nullptr);
            const AssetInstanceId stockId = stock->assetId;
            const auto exit = reopened.profile().pendingRaid->interiors.front().interiorExit.position;
            ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(reopened, *stock));
            ASSERT_TRUE(assetIsCarried(reopened.profile(), stockId));
            auto &player = const_cast<Player &>(reopened.world().player());
            ASSERT_TRUE(player.setPosition(exit));
            GameplayInput leave;
            leave.interactJustPressed = true;
            reopened.update(leave, 0.0F);
            ASSERT_TRUE(reopened.world().inOutdoorRaidSpace());
            ASSERT_TRUE(player.setPosition(reopened.world().extractionPoint().bounds().position));
            for (int frame = 0; frame < 45 && reopened.profile().pendingRaid; ++frame)
                reopened.update({}, 0.1F);
            ASSERT_TRUE(waitForProfileUiDurability(reopened)) << reopened.persistenceMessage();
            ASSERT_FALSE(reopened.profile().pendingRaid);
            ASSERT_TRUE(reopened.profile().lastRaidResult);
            EXPECT_EQ(reopened.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
            EXPECT_TRUE(assetIsCarried(reopened.profile(), stockId));
        }
    }
}

TEST(AlphaExtractionSessionTest, HospitalFloorplanFullPackLeavesLootAndLayoutUntouched)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-hospital-floorplan-", "full-pack");
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("floorplan-full-pack", content);
    const auto backpacks = assets(profile, alpha_content::backpack);
    ASSERT_FALSE(backpacks.empty());
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryEquipCommand{backpacks.front(), EquipmentSlotKind::Backpack},
        {profile.revision, "floorplan-full-pack-equip"}).succeeded);
    const auto pack = ProfileContainerId::compartment(backpacks.front(), 0);
    const auto &filler = content.item(alpha_content::ammunition);
    while (const auto fit = findFirstProfileFit(profile, content, pack, filler, ItemOrientation::Degrees0))
        static_cast<void>(profile.assets.create(filler, StoredAssetLocation{pack, *fit}));
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    RaidLootSnapshot floorplan;
    ASSERT_NO_FATAL_FAILURE(enterHospitalFloorplanRoom(session, floorplan));

    const auto before = profileStateFingerprint(session.profile());
    ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, floorplan));
    EXPECT_FALSE(session.profileUiCommitPending());
    EXPECT_EQ(session.persistenceMessage(), "equipped containers cannot hold this Loot");
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_FALSE(assetIsCarried(session.profile(), floorplan.assetId));
    EXPECT_FALSE(session.world().activeInteriorMapProjection()->layoutKnown);
}

TEST(AlphaExtractionSessionTest, HospitalFloorplanKnowledgeSurvivesDeathWhileLootUsesNormalLoss)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("floorplan-death"));
    ASSERT_NO_FATAL_FAILURE(equipHospitalFloorplanPack(session));
    RaidLootSnapshot floorplan;
    ASSERT_NO_FATAL_FAILURE(enterHospitalFloorplanRoom(session, floorplan));
    ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, floorplan));
    ASSERT_TRUE(assetIsCarried(session.profile(), floorplan.assetId));
    ASSERT_TRUE(session.world().activeInteriorMapProjection()->layoutKnown);

    ASSERT_TRUE(session.world().markPlayerDead());
    session.update({}, 0.0F);
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::PlayerDead);
    EXPECT_FALSE(assetIsCarried(session.profile(), floorplan.assetId));
    EXPECT_TRUE(session.profile().raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));
}

TEST(AlphaExtractionSessionTest, PublishedHospitalDeploysFreezesAndSettlesThroughRealSession)
{
    for (const std::uint64_t seed : {1ULL, 42ULL, 910223ULL})
    {
        SCOPED_TRACE(seed);
        raidline::test::TemporaryDirectory directory;
        directory.claim("raidline-hospital-doors-", std::to_string(seed));
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.startNewProfile("hospital-session"));
        ASSERT_NO_FATAL_FAILURE(equipHospitalFloorplanPack(session));
        ASSERT_TRUE(session.deployAlpha(seed, MapDefinitionId{"map.raid.hospital_district"}))
            << session.persistenceMessage();
        ASSERT_TRUE(session.profile().pendingRaid.has_value());
        const auto &pending = *session.profile().pendingRaid;
        ASSERT_EQ(pending.interiors.size(), 2U);
        EXPECT_EQ(pending.interiors.front().id,
                  RaidSpaceDefinitionId{"raid_space.hospital_district.main_building"});
        const auto depot = pending.interiors.back();
        ASSERT_EQ(depot.id, RaidSpaceDefinitionId{"raid_space.hospital_district.transport_depot"});
        const auto material = std::find_if(pending.loot.begin(), pending.loot.end(),
            [&](const auto &item) { return item.spaceId == depot.id; });
        ASSERT_NE(material, pending.loot.end());
        const auto depotLoot = *material;
        EXPECT_EQ(session.world().tacticalMap().specialLocations().size(), 2U);
        EXPECT_TRUE(pending.highRiskCrisis.has_value());
        const auto loaded = deserializeProfileEnvelope(
            serializeProfileEnvelope(session.profile(), publishedContentRegistry().contentVersion()),
            publishedContentRegistry());
        ASSERT_TRUE(loaded.profile.has_value()) << loaded.message;
        EXPECT_EQ(serializeProfileEnvelope(*loaded.profile, publishedContentRegistry().contentVersion()),
                  serializeProfileEnvelope(session.profile(), publishedContentRegistry().contentVersion()));
        const auto interior = pending.interiors.front();
        GameplayInput interact;
        interact.interactJustPressed = true;
        ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
            interior.exteriorEntrance.position));
        session.update(interact, 0.0F);
        ASSERT_FALSE(session.world().inOutdoorRaidSpace());
        ASSERT_EQ(session.world().enemies().size(), 4U);
        auto &enemy = const_cast<Enemy &>(session.world().enemies().front());
        static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
        session.update(GameplayInput{}, 0.0F);
        const auto survivors = session.world().enemies().size();
        ASSERT_LT(survivors, 4U);
        ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
            interior.interiorExit.position));
        session.update(interact, 0.0F);
        ASSERT_TRUE(session.world().inOutdoorRaidSpace());
        auto &player = const_cast<Player &>(session.world().player());
        EXPECT_FLOAT_EQ(player.position().x, interior.exteriorReturn.x);
        EXPECT_FLOAT_EQ(player.position().y, interior.exteriorReturn.y);
        ASSERT_TRUE(player.setPosition(depot.exteriorEntrance.position));
        session.update(interact, 0.0F);
        ASSERT_EQ(session.world().activeRaidSpaceId(), depot.id);
        ASSERT_EQ(session.world().enemies().size(), 1U);
        ASSERT_NO_FATAL_FAILURE(pickUpHospitalFloorplan(session, depotLoot));
        ASSERT_TRUE(assetIsCarried(session.profile(), depotLoot.assetId));
        ASSERT_TRUE(player.setPosition(depot.interiorExit.position));
        session.update(interact, 0.0F);
        ASSERT_TRUE(session.world().inOutdoorRaidSpace());
        EXPECT_FLOAT_EQ(player.position().x, depot.exteriorReturn.x);
        EXPECT_FLOAT_EQ(player.position().y, depot.exteriorReturn.y);
        ASSERT_TRUE(player.setPosition(depot.exteriorEntrance.position));
        session.update(interact, 0.0F);
        ASSERT_EQ(session.world().activeRaidSpaceId(), depot.id);
        const auto &remaining = session.profile().pendingRaid->loot;
        EXPECT_EQ(std::count_if(remaining.begin(), remaining.end(), [&](const auto &item) {
            return item.spaceId == depot.id && !item.collected;
        }), 0);
        ASSERT_TRUE(player.setPosition(depot.interiorExit.position));
        session.update(interact, 0.0F);
        ASSERT_TRUE(session.world().inOutdoorRaidSpace());
        ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
            interior.exteriorEntrance.position));
        session.update(interact, 0.0F);
        ASSERT_FALSE(session.world().inOutdoorRaidSpace());
        EXPECT_EQ(session.world().enemies().size(), survivors);
        if (seed == 42U)
        {
            ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
                interior.interiorExit.position));
            session.update(interact, 0.0F);
            ASSERT_TRUE(session.world().inOutdoorRaidSpace());
            ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
                session.world().extractionPoint().bounds().position));
            for (int frame = 0; frame < 40 && session.profile().pendingRaid; ++frame)
                session.update(GameplayInput{}, 0.1F);
            ASSERT_TRUE(waitForProfileUiDurability(session));
            GameSession reopened;
            reopened.configurePersistence(directory.path());
            ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
            ASSERT_TRUE(reopened.profile().lastRaidResult);
            EXPECT_EQ(reopened.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
            EXPECT_TRUE(assetIsCarried(reopened.profile(), depotLoot.assetId));
        }
        else
        {
            ASSERT_TRUE(session.activeQuitAlphaRaid());
        }
        EXPECT_FALSE(session.profile().pendingRaid.has_value());
        EXPECT_FALSE(session.activeQuitAlphaRaid());
    }
}

TEST(AlphaExtractionSessionTest,
     PurchasedKnifeCanCancelAndCompleteOneHeavySwingInTheHospitalDepot)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("depot-heavy-attack"));
    const auto bought = session.executeProfileEconomy(PurchaseCommand{alpha_content::utilityKnife, 1},
                                                       "buy-knife-for-raid");
    ASSERT_TRUE(bought.succeeded) << bought.message;
    const auto knives = assets(session.profile(), alpha_content::utilityKnife);
    ASSERT_FALSE(knives.empty());
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{knives.back(), EquipmentSlotKind::QuickMelee}, "equip-depot-knife").succeeded);
    ASSERT_TRUE(session.deployAlpha(42, kHospitalFloorplanMapId)) << session.persistenceMessage();
    const auto &rooms = session.profile().pendingRaid->interiors;
    const auto depot = std::find_if(rooms.begin(), rooms.end(),
        [](const auto &room) { return room.id == kHospitalDepotSpaceId; });
    ASSERT_NE(depot, rooms.end());
    auto &player = const_cast<Player &>(session.world().player());
    ASSERT_TRUE(player.setPosition(depot->exteriorEntrance.position));
    GameplayInput enter;
    enter.interactJustPressed = true;
    session.update(enter, 0.0F);
    ASSERT_EQ(session.world().activeRaidSpaceId(), kHospitalDepotSpaceId);

    GameplayInput heavy;
    heavy.aimWorldPosition = {player.position().x + 160, player.position().y + 16};
    heavy.meleePressed = true;
    heavy.meleeHeavyHeld = true;
    heavy.meleeHeavyJustPressed = true;
    session.update(heavy, 0.1F);
    ASSERT_TRUE(session.world().melee().chargingHeavy());
    heavy.meleeHeavyJustPressed = false;
    session.update(heavy, 0.1F);
    EXPECT_GT(session.world().melee().heavyChargeProgress(), 0.0F);
    session.update({}, 0.1F);
    EXPECT_FLOAT_EQ(session.world().melee().heavyChargeProgress(), 0.0F);
    EXPECT_FALSE(session.world().melee().result().attempted);
    EXPECT_FLOAT_EQ(session.world().melee().recoverySeconds(), 0.0F);

    heavy.meleeHeavyJustPressed = true;
    session.update(heavy, 0.1F);
    heavy.meleeHeavyJustPressed = false;
    unsigned int attempts{};
    for (int frame = 0; frame < 25; ++frame)
    {
        session.update(heavy, 0.1F);
        if (session.world().melee().result().attempted)
        {
            EXPECT_TRUE(session.world().melee().heavySwing());
            ++attempts;
        }
    }
    EXPECT_EQ(attempts, 1U);
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
    ASSERT_TRUE(session.profile().pendingRaid->highRiskCrisis.has_value());
    const auto hiddenCrisis = session.raidHighRiskCrisisProjection();
    ASSERT_TRUE(hiddenCrisis.has_value());
    EXPECT_FALSE(hiddenCrisis->detailsKnown);
    EXPECT_FALSE(hiddenCrisis->active);
    EXPECT_EQ(hiddenCrisis->focusArea,
              session.profile().pendingRaid->highRiskCrisis->focusArea);
    EXPECT_EQ(session.world().highRiskAdvancedResourceArea(),
              std::optional<ContentRect>{hiddenCrisis->focusArea});
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

    session.world().update(GameplayInput{}, 1200.5F);
    const auto activeCrisis = session.raidHighRiskCrisisProjection();
    ASSERT_TRUE(activeCrisis.has_value());
    EXPECT_TRUE(activeCrisis->detailsKnown);
    EXPECT_TRUE(activeCrisis->active);
    EXPECT_FALSE(activeCrisis->displayName.empty());
    EXPECT_FALSE(activeCrisis->warning.empty());
}

TEST(AlphaExtractionSessionTest,
     EnemyIntelligenceRevealsFrozenCrisisBeforeHighRisk)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-crisis-intel"));
    const MapDefinitionId mapId{"map.raid.frontier_exchange"};
    ASSERT_TRUE(session.purchaseRaidIntelligence(
        RaidIntelligencePurchaseCommand{
            mapId, RaidIntelligenceCategory::Enemy},
        "buy-frontier-crisis-intel").succeeded);
    RaidIntelligenceLoadout intelligence;
    intelligence.set(RaidIntelligenceCategory::Enemy, true);
    ASSERT_TRUE(session.deployAlpha(88125U, mapId, intelligence))
        << session.persistenceMessage();

    const auto crisis = session.raidHighRiskCrisisProjection();
    ASSERT_TRUE(crisis.has_value());
    EXPECT_TRUE(crisis->detailsKnown);
    EXPECT_FALSE(crisis->active);
    EXPECT_FALSE(crisis->displayName.empty());
    EXPECT_FALSE(crisis->warning.empty());
}

TEST(AlphaExtractionSessionTest,
     DeveloperHighRiskTriggerUsesFrozenCrisisWithoutMutatingProfile)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("alpha-session-crisis-developer"));
    ASSERT_TRUE(session.deployAlpha(
        88126U, MapDefinitionId{"map.raid.frontier_exchange"}))
        << session.persistenceMessage();

    ASSERT_TRUE(session.profile().pendingRaid.has_value());
    const RaidHighRiskCrisisSnapshot &snapshot =
        *session.profile().pendingRaid->highRiskCrisis;
    const std::uint64_t fingerprint = profileStateFingerprint(session.profile());
    const auto before = session.raidHighRiskCrisisProjection();
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->definitionId, snapshot.definitionId);
    EXPECT_EQ(before->districtInstanceId, snapshot.districtInstanceId);
    EXPECT_EQ(before->resourcePointInstanceId,
              snapshot.resourcePointInstanceId);
    EXPECT_EQ(before->initialWaveDelaySeconds,
              snapshot.initialWaveDelaySeconds);
    EXPECT_EQ(before->waveIntervalSeconds, snapshot.waveIntervalSeconds);
    EXPECT_EQ(before->waveSize, snapshot.waveSize);
    EXPECT_EQ(before->activeEnemyCap, snapshot.activeEnemyCap);
    EXPECT_EQ(before->pressureSpawnCount, snapshot.pressureSpawns.size());
    ASSERT_GT(before->pressureSpawnCount, 0U);
    for (std::size_t index{}; index < before->pressureSpawnCount; ++index)
    {
        const RaidHighRiskPressureSpawnSnapshot &spawn =
            snapshot.pressureSpawns[index];
        EXPECT_FLOAT_EQ(before->pressureSpawnCenters[index].x,
                        spawn.position.x + spawn.size.x * 0.5F);
        EXPECT_FLOAT_EQ(before->pressureSpawnCenters[index].y,
                        spawn.position.y + spawn.size.y * 0.5F);
    }
    EXPECT_FLOAT_EQ(before->nextWaveSeconds,
                    snapshot.initialWaveDelaySeconds);
    EXPECT_FALSE(before->active);

    EXPECT_TRUE(session.triggerDeveloperHighRisk());
    EXPECT_FALSE(session.triggerDeveloperHighRisk());
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    const auto after = session.raidHighRiskCrisisProjection();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->active);
    EXPECT_TRUE(after->detailsKnown);
    EXPECT_FLOAT_EQ(after->nextWaveSeconds,
                    snapshot.initialWaveDelaySeconds);

    session.world().update(GameplayInput{}, 0.25F);
    const auto advancing = session.raidHighRiskCrisisProjection();
    ASSERT_TRUE(advancing.has_value());
    EXPECT_LT(advancing->nextWaveSeconds, after->nextWaveSeconds);
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
                ProfileContainerId::compartment(chests[0], 0),
                GridPosition{1, 0}},
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
    reload.raidReloadIntents.push_back({RaidReloadInputKind::Automatic,
        *session.activeAlphaWeapon(), std::nullopt, 0});
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

TEST(AlphaExtractionSessionTest, SignalReadinessMatchesReloadAndInventoryGates)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("signal-readiness"));
    prepareArmedLoadout(session);
    ASSERT_TRUE(session.deployAlpha(77235, MapDefinitionId{"map.v0.test"}));
    ASSERT_TRUE(session.world().triggerHighRiskForDeveloper());
    const auto bounds = session.world().emergencyExtractionPoint()->bounds();
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(bounds.position));
    ASSERT_TRUE(session.canCallSignalExtraction());
    EXPECT_FALSE(session.canCallSignalExtraction(true));
    GameplayInput reload;
    reload.raidReloadIntents.push_back({RaidReloadInputKind::Automatic,
        *session.activeAlphaWeapon(), std::nullopt, 0});
    session.update(reload, 0.0F);
    ASSERT_TRUE(session.raidActionState().active().has_value());
    EXPECT_FALSE(session.canCallSignalExtraction());
    GameplayInput call;
    call.interactJustPressed = true;
    session.update(call, 0.0F);
    EXPECT_FALSE(session.world().raidSession().signalCalledLastUpdate());
    session.update(GameplayInput{}, 2.0F);
    ASSERT_FALSE(session.raidActionState().active().has_value());
    ASSERT_TRUE(session.canCallSignalExtraction());
    session.update(call, 0.0F);
    EXPECT_TRUE(session.world().raidSession().signalCalledLastUpdate());
    EXPECT_FALSE(session.canCallSignalExtraction());
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
    reload.raidReloadIntents.push_back({RaidReloadInputKind::Automatic,
        *session.activeAlphaWeapon(), std::nullopt, 0});
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
        (std::vector<GameSessionPresentationEvent>{
            GameSessionPresentationEvent::WeaponMagazineRemoved,
            GameSessionPresentationEvent::WeaponMagazineInserted,
            GameSessionPresentationEvent::WeaponChambered,
            GameSessionPresentationEvent::ReloadCompleted}));
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

class FireSupportTreatmentSessionTest : public testing::Test
{
protected:
    raidline::test::TemporaryDirectory directory;
    const ContentRegistry &content = publishedContentRegistry();
    GameSession session;
    AssetInstanceId kit{}, spareKit{};
    Vec2 spawn{};

    void SetUp() override
    {
        auto profile = makeNewAlphaProfile("teammate-medical-session", content);
        profile.tutorial = TutorialProgress::Complete;
        ++profile.basePopulation.ordinaryResidents;
        ++profile.basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::Combat)];
        profile.committedRescues.insert(kRaidFireSupportReplacementRescueId);
        const auto add = [&](const ItemDefinitionId &id, ProfileContainerId container, unsigned quantity = 1U) {
            const auto &definition = content.item(id);
            const auto fit = findFirstProfileFit(profile, content, container, definition, ItemOrientation::Degrees0);
            if (!fit)
                throw std::runtime_error("no room in treatment fixture for " + std::string(id.value()));
            return profile.assets.create(definition, StoredAssetLocation{container, *fit}, quantity);
        };
        const auto rifle = add(kRaidFireSupportRifleId, ProfileContainerId::stash());
        add(kRaidFireSupportAmmunitionId, ProfileContainerId::stash(), 42U);
        const auto packs = assets(profile, alpha_content::backpack);
        ASSERT_FALSE(packs.empty());
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{packs.front(), EquipmentSlotKind::Backpack},
            {profile.revision, "support-medical-backpack"}).succeeded);
        const auto bag = ProfileContainerId::compartment(packs.front(), 0);
        const auto startingKits = assets(profile, alpha_content::medkit);
        ASSERT_FALSE(startingKits.empty());
        kit = startingKits.front();
        const auto kitFit = findFirstProfileFit(profile, content, bag, content.item(alpha_content::medkit),
            ItemOrientation::Degrees0);
        ASSERT_TRUE(kitFit);
        ASSERT_TRUE(executeInventory(profile, content, InventoryMoveCommand{kit, 0, {bag, *kitFit}},
            {profile.revision, "support-medical-carry"}).succeeded);
        spareKit = add(alpha_content::medkit, bag);
        directory.claim("raidline-support-treatment-", testing::UnitTest::GetInstance()->current_test_info()->name());
        ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
        AlphaDeployRequest request;
        request.seed = 6;
        request.mapDefinitionId = kHospitalFloorplanMapId;
        request.fireSupport = RaidFireSupportLoadout{rifle, std::nullopt};
        const auto deployed = session.requestDeployAlpha(request);
        ASSERT_TRUE(deployed.accepted) << deployed.message;
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        ASSERT_TRUE(session.alphaRaidActive());
        auto &world = session.world();
        ASSERT_TRUE(world.fireSupport());
        spawn = world.player().position();
        auto &support = WeaponHandlingTestAccess::support(world);
        support.setPosition({spawn.x + 65.0F, spawn.y});
        ASSERT_TRUE(world.canInteractWithFireSupport());
        ASSERT_FALSE(session.queryFireSupportTreatment(kit).canCommit);
        support.applyContact(PlayerDamageObservation{45}, 0.0F);
        ASSERT_LT(*world.fireSupportHealth(), kFireSupportMaxHealth);
        ASSERT_GT(*world.fireSupportHealth(), 0);
        ASSERT_EQ(world.player().health(), 100);
    }

    std::uint32_t charges(AssetInstanceId id) const { return session.profile().assets.find(id)->remainingCharges; }
};

TEST_F(FireSupportTreatmentSessionTest, SelectedCarriedKitHealsOnceAtCompletionAndRestoresOrders)
{
    auto &world = session.world();
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Follow, FireSupportFireOrder::FreeFire));
    const auto kits = session.fireSupportTreatmentKits();
    ASSERT_NE(std::find(kits.begin(), kits.end(), kit), kits.end());
    ASSERT_NE(std::find(kits.begin(), kits.end(), spareKit), kits.end());
    const auto selected = spareKit;
    const auto plan = session.queryFireSupportTreatment(selected);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_GT(plan.durationMs, 0U);
    const auto position = world.fireSupport()->bounds.position;
    const int wounded = *world.fireSupportHealth();
    const auto before = charges(selected);
    const auto unselected = charges(kit);
    ASSERT_TRUE(session.startFireSupportTreatment(selected));
    EXPECT_FALSE(session.startFireSupportTreatment(kit));
    // Twenty short updates exercise the live world throughout the treatment.
    const float step = static_cast<float>(plan.durationMs) / 20000.0F;
    for (int frame = 0; frame < 19; ++frame)
    {
        session.update({}, step);
        ASSERT_TRUE(session.playerActionState().active()) << frame;
        const auto current = world.fireSupport();
        ASSERT_TRUE(current);
        EXPECT_FLOAT_EQ(current->bounds.position.x, position.x);
        EXPECT_FLOAT_EQ(current->bounds.position.y, position.y);
        EXPECT_FALSE(world.fireSupportShotLastUpdate());
        EXPECT_EQ(current->health, wounded);
        EXPECT_EQ(charges(selected), before);
    }
    for (int frame = 0; frame < 3 && session.playerActionState().active(); ++frame)
        session.update({}, step);
    ASSERT_FALSE(session.playerActionState().active());
    const int healed = std::min(kFireSupportMaxHealth, wounded +
        static_cast<int>(content.item(alpha_content::medkit).medicalUse->effectMagnitude));
    EXPECT_EQ(*world.fireSupportHealth(), healed);
    EXPECT_EQ(charges(selected), before - 1);
    EXPECT_EQ(charges(kit), unselected);
    EXPECT_EQ(world.player().health(), 100);
    session.update({}, 0.1F);
    const auto resumed = world.fireSupport();
    ASSERT_TRUE(resumed);
    EXPECT_EQ(resumed->movementOrder, FireSupportMovementOrder::Follow);
    EXPECT_EQ(resumed->fireOrder, FireSupportFireOrder::FreeFire);
    EXPECT_LT(resumed->bounds.position.x, position.x);
    EXPECT_EQ(resumed->health, healed);
    EXPECT_EQ(charges(selected), before - 1);
}

TEST_F(FireSupportTreatmentSessionTest, FireLostRangeLostKitAndDeathCancelWithoutUsingEitherKit)
{
    auto &world = session.world();
    auto &support = WeaponHandlingTestAccess::support(world);
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::FreeFire));
    const auto before = charges(kit);
    const auto spareBefore = charges(spareKit);
    const int wounded = *world.fireSupportHealth();
    GameplayInput fire;
    fire.fireJustPressed = fire.firePressed = true;
    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    session.update({}, 0.4F);
    auto &enemies = WeaponHandlingTestAccess::actors(world);
    ASSERT_FALSE(enemies.empty());
    const auto targetId = enemies.front().combatTargetId();
    const auto originalTargetPosition = enemies.front().position();
    ASSERT_TRUE(enemies.front().setPosition({spawn.x + 65.0F, spawn.y + 72.0F}));
    session.update({}, 1.0F / 60.0F);
    ASSERT_TRUE(session.playerActionState().active());
    EXPECT_FALSE(world.fireSupportShotLastUpdate());
    session.update(fire, 0.0F);
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_EQ(*world.fireSupportHealth(), wounded);
    EXPECT_EQ(charges(kit), before);
    EXPECT_EQ(world.fireSupport()->movementOrder, FireSupportMovementOrder::Hold);
    EXPECT_EQ(world.fireSupport()->fireOrder, FireSupportFireOrder::FreeFire);
    session.update({}, 1.0F / 60.0F);
    EXPECT_TRUE(world.fireSupportShotLastUpdate());
    for (auto &enemy : enemies)
        if (enemy.combatTargetId() == targetId)
            ASSERT_TRUE(enemy.setPosition(originalTargetPosition));

    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    support.setPosition({spawn.x + 180.0F, spawn.y});
    session.update({}, 0.1F);
    ASSERT_FALSE(session.playerActionState().active());
    EXPECT_FALSE(session.queryFireSupportTreatment(kit).canCommit);
    EXPECT_EQ(*world.fireSupportHealth(), wounded);
    EXPECT_EQ(charges(kit), before);
    support.setPosition({spawn.x + 40.0F, spawn.y});

    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    auto *selected = WeaponHandlingTestAccess::profile(session).assets.findMutable(kit);
    ASSERT_NE(selected, nullptr);
    const auto carried = selected->location;
    const auto stash = ProfileContainerId::stash();
    const auto fit = findFirstProfileFit(session.profile(), content, stash, content.item(alpha_content::medkit),
        ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    selected->location = StoredAssetLocation{stash, *fit};
    session.update({}, 0.1F);
    ASSERT_FALSE(session.playerActionState().active());
    EXPECT_EQ(*world.fireSupportHealth(), wounded);
    EXPECT_EQ(charges(kit), before);
    EXPECT_EQ(charges(spareKit), spareBefore);
    WeaponHandlingTestAccess::profile(session).assets.findMutable(kit)->location = carried;

    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    session.update({}, 0.05F);
    support.applyContact(PlayerDamageObservation{5}, 0.3F);
    const int afterHit = *world.fireSupportHealth();
    ASSERT_LT(afterHit, wounded);
    session.update({}, 0.05F);
    ASSERT_FALSE(session.playerActionState().active());
    EXPECT_EQ(*world.fireSupportHealth(), afterHit);
    EXPECT_EQ(charges(kit), before);
    EXPECT_FALSE(session.queryFireSupportTreatment(kit).canCommit);
    for (int frame = 0; frame < 8; ++frame)
        session.update({}, 0.05F);

    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    support.applyContact(PlayerDamageObservation{1000}, 0.0F);
    session.update({}, 0.1F);
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_FALSE(session.startFireSupportTreatment(kit));
    EXPECT_EQ(*world.fireSupportHealth(), 0);
    EXPECT_EQ(charges(kit), before);
    EXPECT_EQ(charges(spareKit), spareBefore);
}

TEST_F(FireSupportTreatmentSessionTest, HospitalWallAndSeparateInteriorPreventTreatment)
{
    auto &world = session.world();
    auto &support = WeaponHandlingTestAccess::support(world);
    auto &player = const_cast<Player &>(world.player());
    ASSERT_TRUE(world.setFireSupportOrders(FireSupportMovementOrder::Hold, FireSupportFireOrder::HoldFire));
    const auto before = charges(kit);
    const int wounded = *world.fireSupportHealth();
    const auto &walls = world.ballisticBlockers();
    const auto wall = std::find_if(walls.begin(), walls.end(), [](const auto &blocker) {
        return blocker.bounds.position.x > 60 && blocker.bounds.position.y > 60 &&
            blocker.bounds.size.x > 32 && blocker.bounds.size.y > 32;
    });
    ASSERT_NE(wall, walls.end());
    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    // Actor footprints lie on different sides of one real corner, within the 72-unit reach.
    const auto corner = wall->bounds.position;
    ASSERT_TRUE(player.setPosition({corner.x - 34, corner.y + 7}));
    support.setPosition({corner.x + 7, corner.y - 34});
    ASSERT_FALSE(world.canInteractWithFireSupport());
    session.update({}, 0.0F);
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_FALSE(session.startFireSupportTreatment(kit));
    EXPECT_EQ(*world.fireSupportHealth(), wounded);
    EXPECT_EQ(charges(kit), before);

    const auto entrance = session.profile().pendingRaid->interiors.front().exteriorEntrance.position;
    ASSERT_TRUE(player.setPosition(entrance));
    support.setPosition(entrance);
    ASSERT_TRUE(session.startFireSupportTreatment(kit));
    GameplayInput portal;
    portal.interactJustPressed = true;
    session.update(portal, 0.0F);
    ASSERT_FALSE(world.inOutdoorRaidSpace());
    ASSERT_FALSE(world.fireSupport()->sameSpace);
    session.update({}, 0.1F);
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_FALSE(session.startFireSupportTreatment(kit));
    EXPECT_EQ(*world.fireSupportHealth(), wounded);
    EXPECT_EQ(charges(kit), before);
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
                ProfileContainerId::compartment(chest, 0),
                GridPosition{2, 0}},
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
    ASSERT_TRUE(session.world().triggerHighRiskForDeveloper());
    const auto signalBounds = session.world().emergencyExtractionPoint()->bounds();
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(signalBounds.position));
    ASSERT_TRUE(session.canCallSignalExtraction());
    ASSERT_TRUE(session.startAlphaMedical(medkit));
    EXPECT_FALSE(session.canCallSignalExtraction());
    GameplayInput signalCall;
    signalCall.interactJustPressed = true;
    session.update(signalCall, 0.0F);
    EXPECT_FALSE(session.world().raidSession().signalCalledLastUpdate());
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

namespace
{
void saveNightRendezvousProfile(const SaveRepository &repository, std::string profileId)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile(std::move(profileId), content);
    profile.worldClock.elapsedWorldMinutes = 2U * kWorldMinutesPerDay + 16U * kWorldMinutesPerHour;
    profile.nightRendezvousUnlocked = true;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);
}

void placeAtTransferCenter(GameSession &session, ContentRect point)
{
    auto &player = const_cast<Player &>(session.world().player());
    const float playerHalf = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({
        point.position.x + point.size.x * 0.5F - playerHalf,
        point.position.y + point.size.y * 0.5F - playerHalf}));
}

void clearNightRendezvousEnemies(GameSession &session)
{
    for (const Enemy &enemy : session.world().enemies())
        EXPECT_TRUE(const_cast<Enemy &>(enemy).takeDamage(1000));
}

void completeNightRendezvousCover(GameSession &session)
{
    ASSERT_TRUE(session.profile().pendingRaid && session.profile().pendingRaid->nightRendezvous);
    ASSERT_NO_FATAL_FAILURE(placeAtTransferCenter(session,
        session.profile().pendingRaid->nightRendezvous->transferPoint));
    GameplayInput signal;
    signal.interactJustPressed = true;
    session.update(signal, 0.0F);
    ASSERT_TRUE(session.world().nightRendezvousView()->inProgress);
    session.update({}, kRaidNightRendezvousDurationSeconds);
    EXPECT_TRUE(session.world().nightRendezvousView()->ready);
}
}

TEST(AlphaExtractionSessionTest, NightRendezvousSaveFailureRetriesAndIndependentRescuesBothSettle)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-rendezvous-", "two-rescues");
    const SaveRepository repository{directory.path()};
    ASSERT_NO_FATAL_FAILURE(saveNightRendezvousProfile(repository, "night-rendezvous-two-rescues"));

    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    ASSERT_TRUE(session.deployAlpha(1U, kRaidNightRendezvousMapId)) << session.persistenceMessage();
    ASSERT_TRUE(session.profile().pendingRaid && session.profile().pendingRaid->rescue);
    ASSERT_NO_FATAL_FAILURE(clearNightRendezvousEnemies(session));
    const auto staffBefore = session.profile().basePopulation.professionResidents[
        baseProfessionIndex(BaseResidentProfession::Engineering)];
    const auto blocker = directory.path() / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(blocker));
    ASSERT_NO_FATAL_FAILURE(completeNightRendezvousCover(session));
    ASSERT_TRUE(session.profileUiCommitPending());
    EXPECT_FALSE(waitForProfileUiDurability(session));
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, 8U);
    EXPECT_FALSE(session.profile().committedRescues.contains(kRaidNightRendezvousRescueId));
    EXPECT_FALSE(session.profile().pendingRaid->nightRendezvous->secured);
    EXPECT_TRUE(session.world().nightRendezvousView()->ready);
    ASSERT_TRUE(std::filesystem::remove(blocker));
    ASSERT_TRUE(session.retryProfileUiCommit().accepted) << session.persistenceMessage();
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();

    EXPECT_TRUE(session.profile().pendingRaid->nightRendezvous->secured);
    EXPECT_TRUE(session.profile().committedRescues.contains(kRaidNightRendezvousRescueId));
    EXPECT_TRUE(session.world().nightRendezvousView()->secured);
    EXPECT_EQ(session.profile().basePopulation.injuredResidents, 0U);
    EXPECT_EQ(session.profile().basePopulation.professionResidents[
        baseProfessionIndex(BaseResidentProfession::Engineering)], staffBefore + 1U);

    const auto ordinary = *session.profile().pendingRaid->rescue;
    ASSERT_NE(ordinary.definitionId, kRaidNightRendezvousRescueId);
    ASSERT_NO_FATAL_FAILURE(placeAtTransferCenter(session, ordinary.transferPoint));
    GameplayInput hold;
    hold.interactPressed = true;
    session.update(hold, 2.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    EXPECT_TRUE(session.profile().committedRescues.contains(ordinary.definitionId));
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, 10U);
    EXPECT_EQ(session.profile().basePopulation.injuredResidents, 1U);

    ASSERT_TRUE(session.activeQuitAlphaRaid());
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->rescuedOrdinaryResidents, 2U);
    EXPECT_EQ(session.profile().lastRaidResult->rescuedInjuredResidents, 1U);
}

TEST(AlphaExtractionSessionTest, NightRendezvousSurvivesAbnormalExitAndCannotBeRepeated)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-rendezvous-", "abnormal-exit");
    const SaveRepository repository{directory.path()};
    ASSERT_NO_FATAL_FAILURE(saveNightRendezvousProfile(repository, "night-rendezvous-recovery"));
    const auto &content = publishedContentRegistry();
    const auto before = repository.load(content);
    ASSERT_TRUE(before.profile) << before.message;
    {
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
        ASSERT_TRUE(session.deployAlpha(1U, kRaidNightRendezvousMapId)) << session.persistenceMessage();
        ASSERT_NO_FATAL_FAILURE(clearNightRendezvousEnemies(session));
        ASSERT_NO_FATAL_FAILURE(completeNightRendezvousCover(session));
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, 9U);
    }

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_EQ(reopened.profile().worldClock, before.profile->worldClock);
    EXPECT_FALSE(reopened.profile().pendingRaid);
    EXPECT_TRUE(reopened.profile().committedRescues.contains(kRaidNightRendezvousRescueId));
    EXPECT_EQ(reopened.profile().basePopulation.ordinaryResidents, 9U);
    ASSERT_TRUE(reopened.deployAlpha(1U, kRaidNightRendezvousMapId)) << reopened.persistenceMessage();
    EXPECT_FALSE(reopened.profile().pendingRaid->nightRendezvous);
    EXPECT_TRUE(reopened.profile().pendingRaid->rescue);
}

TEST(AlphaExtractionSessionTest, RiversideGunfireAndCorpseKeepTheSamePistolUntilExit)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-riverside-session-", "gunfire-and-corpse");
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.startNewProfile("armed-session")) << session.persistenceMessage();
    ASSERT_TRUE(session.deployAlpha(9403U, kFieldIntelligenceSourceMapId)) << session.persistenceMessage();
    const auto &views = session.world().armedScavengers();
    ASSERT_EQ(views.size(), 2U);
    const auto roster = session.profile().pendingRaid->armedScavengers;
    ASSERT_EQ(roster.size(), 2U);
    const auto &first = roster.front();
    ASSERT_EQ(views.front().sourceEnemyId, first.sourceEnemyId);
    const auto &enemies = session.world().enemies();
    const auto gunner = std::find_if(enemies.begin(), enemies.end(),
        [&](const auto &enemy) { return enemy.combatTargetId() == views.front().id; });
    ASSERT_NE(gunner, enemies.end());
    const Vec2 post = gunner->position();
    for (const auto &enemy : enemies)
        if (!enemy.isArmedScavenger()) ASSERT_TRUE(const_cast<Enemy &>(enemy).takeDamage(1000));
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition({post.x - 90.0F, post.y}));
    for (int frame = 0; frame < 75 && views.front().shotsFired == 0; ++frame)
        session.update({}, 1.0F / 60.0F);
    ASSERT_EQ(views.front().shotsFired, 1);
    EXPECT_EQ(magazineRoundCount(session.profile(), first.firstMagazineId), 4U);
    EXPECT_EQ(magazineRoundCount(session.profile(), first.secondMagazineId), 6U);
    ASSERT_TRUE(session.profile().assets.find(first.pistolAssetId)->chamberedRound);

    // World removals may compact the roster; resolve the stable ID after the shot.
    for (const auto &enemy : session.world().enemies())
        if (enemy.combatTargetId() == views.front().id)
            ASSERT_TRUE(const_cast<Enemy &>(enemy).takeDamage(1000));
    session.update({}, 1.0F / 60.0F);
    ASSERT_FALSE(views.front().alive);
    const auto *corpse = session.profile().assets.find(first.corpseAssetId);
    ASSERT_NE(corpse, nullptr);
    const auto *location = std::get_if<RaidCorpseContainerAssetLocation>(&corpse->location);
    ASSERT_NE(location, nullptr);
    EXPECT_TRUE(location->deathPosition);
    EXPECT_EQ(location->sourceKind, RaidCorpseSourceKind::ArmedScavenger);
    EXPECT_EQ(location->sourceEnemyId, first.sourceEnemyId);
    EXPECT_EQ(installedMagazine(session.profile(), first.pistolAssetId), first.firstMagazineId);
    EXPECT_EQ(magazineRoundCount(session.profile(), first.firstMagazineId), 4U);
}

TEST(AlphaExtractionSessionTest, FireSupportReplacementCommitSurvivesAbnormalExitOnce)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-fire-support-", "replacement-rescue");
    const auto &content = publishedContentRegistry();
    const SaveRepository repository{directory.path()};
    auto profile = makeNewAlphaProfile("fire-support-replacement", content);
    const std::string death{"previous-teammate-death"};
    profile.committedSettlements.insert(death);
    profile.committedRescues.insert(kRaidFireSupportReplacementRescueId);
    profile.fireSupportReplacement = RaidFireSupportReplacement{death, kRaidFireSupportReplacementDelayMinutes};
    profile.worldClock.elapsedWorldMinutes = kRaidFireSupportReplacementDelayMinutes;
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);

    {
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
        ASSERT_TRUE(session.deployAlpha(1U, kRaidFireSupportReplacementMapId)) << session.persistenceMessage();
        ASSERT_TRUE(session.profile().pendingRaid->rescue);
        ASSERT_EQ(session.profile().pendingRaid->fireSupportReplacementSource, death);
        ASSERT_NO_FATAL_FAILURE(clearNightRendezvousEnemies(session));
        ASSERT_TRUE(session.ordinarySurvivorRescuePlan()->canCommit);
        ASSERT_FALSE(session.ordinarySurvivorRescuePlan()->alreadyCommitted);
        ASSERT_NO_FATAL_FAILURE(placeAtTransferCenter(session, session.profile().pendingRaid->rescue->transferPoint));
        GameplayInput hold;
        hold.interactPressed = true;
        session.update(hold, session.profile().pendingRaid->rescue->interactionDurationSeconds + 0.1F);
        ASSERT_TRUE(session.profileUiCommitPending()) << session.persistenceMessage();
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
        EXPECT_TRUE(session.profile().pendingRaid->rescue->secured);
        EXPECT_FALSE(session.profile().fireSupportReplacement);
    }

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_FALSE(reopened.profile().pendingRaid);
    EXPECT_FALSE(reopened.profile().fireSupportReplacement);
    EXPECT_EQ(reopened.profile().basePopulation.ordinaryResidents, profile.basePopulation.ordinaryResidents + 1U);
    EXPECT_TRUE(reopened.profile().committedTransactions.contains(raidFireSupportReplacementTransactionId(death)));
    ASSERT_TRUE(reopened.deployAlpha(1U, kRaidFireSupportReplacementMapId)) << reopened.persistenceMessage();
    EXPECT_FALSE(reopened.profile().pendingRaid->rescue);
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

        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
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
        ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
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

    EXPECT_FALSE(waitForProfileUiDurability(session));
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(session.profile().assets.nextAssetId(), nextAssetId);
    EXPECT_EQ(session.profile().currency, currency);
    EXPECT_EQ(session.profile().basePopulation.ordinaryResidents, 8U);
    EXPECT_TRUE(session.profile().committedRescues.empty());
    EXPECT_FALSE(session.profile().pendingRaid->rescue->secured);
    EXPECT_FLOAT_EQ(
        session.world().ordinarySurvivorRescueProgress(),
        1.0F); // The completed interaction waits for its immutable save candidate.
    EXPECT_FALSE(session.persistenceMessage().empty());
}

TEST(AlphaExtractionSessionTest, InstalledHighOpticOpensInBaseAndRaidAndBlocksHeldInteraction)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewPublishedProfile("optics-session", content);
    const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};
    const auto &optic = content.item(ItemDefinitionId{"item.component.optic_4x_5_45"});
    const auto guns = assets(profile, rifle);
    ASSERT_FALSE(guns.empty());
    const auto gun = guns.front();
    const auto grid = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
        optic, ItemOrientation::Degrees0);
    ASSERT_TRUE(grid);
    const auto component = profile.assets.create(optic,
        StoredAssetLocation{ProfileContainerId::stash(), *grid});
    ASSERT_TRUE(executeWeaponComponentChange(profile, content,
        {gun, WeaponComponentSlot::Optic, component}, {profile.revision, "install-optic"}).succeeded);
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{gun, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "equip-gun"}).succeeded);
    profile.tutorial = TutorialProgress::Complete;
    TemporarySaveDirectory directory;
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);

    BaseWorld base;
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    GameplayInput aim;
    aim.aimWorldPosition = {800, 400};
    aim.aimDownSights = true;

    static_cast<void>(session.updateBaseWorld(base, aim, 0.5F));
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    static_cast<void>(session.updateBaseWorld(base, aim, 0.5F));
    EXPECT_EQ(base.activeWeaponOptic(), WeaponOpticMode::High);
    const auto &baseView = base.observation();
    EXPECT_FLOAT_EQ(baseView.angleDegrees, 0.65F * raidSightParameters(baseView.environment).angleDegrees);
    session.cancelWeaponOptics();
    EXPECT_EQ(base.activeWeaponOptic(), WeaponOpticMode::None);

    ASSERT_TRUE(session.deployAlpha(77235, MapDefinitionId{"map.v0.test"}))
        << session.persistenceMessage();
    auto &world = session.world();
    aim.allowWeaponOptics = false;
    session.update(aim, 0.5F);
    EXPECT_EQ(world.activeWeaponOptic(), WeaponOpticMode::None);
    aim.allowWeaponOptics = true;
    session.update(aim, 0.5F);
    ASSERT_EQ(world.activeWeaponOptic(), WeaponOpticMode::High);
    ASSERT_TRUE(world.triggerHighRiskForDeveloper());
    ASSERT_TRUE(world.emergencyExtractionPoint());
    const auto bounds = world.emergencyExtractionPoint()->bounds();
    ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition(bounds.position));

    aim.interactJustPressed = aim.interactPressed = true;
    EXPECT_TRUE(session.blocksInteractionForOptics(aim, 0.0F));
    session.update(aim, 0.0F);
    EXPECT_FALSE(world.raidSession().signalCalledLastUpdate());
    aim.aimDownSights = aim.interactJustPressed = false;
    EXPECT_TRUE(session.blocksInteractionForOptics(aim, 0.0F));
    session.update(aim, 0.0F);
    EXPECT_EQ(world.activeWeaponOptic(), WeaponOpticMode::None);
    EXPECT_FALSE(world.raidSession().signalCalledLastUpdate());

    aim.interactPressed = false;
    session.update(aim, 0.0F);
    aim.interactJustPressed = aim.interactPressed = true;
    EXPECT_FALSE(session.blocksInteractionForOptics(aim, 0.0F));
    session.update(aim, 0.0F);
    EXPECT_TRUE(world.raidSession().signalCalledLastUpdate());
}

TEST(AlphaExtractionSessionTest, OpticTransitionUsesHandlingAfterInstallation)
{
    const auto &content = publishedContentRegistry();
    const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};
    const ItemDefinitionId optic{"item.component.optic_4x_5_45"};
    const ItemDefinitionId suppressor{"item.component.suppressor_5_45"};
    for (const bool fitSuppressor : {false, true})
    {
        SCOPED_TRACE(fitSuppressor);
        auto profile = makeNewPublishedProfile("optic-handling", content);
        profile.tutorial = TutorialProgress::Complete;
        const auto guns = assets(profile, rifle);
        ASSERT_FALSE(guns.empty());
        const auto gun = guns.front();
        std::vector<ItemDefinitionId> parts{optic};
        if (fitSuppressor)
            parts.push_back(suppressor);
        for (const auto &part : parts)
        {
            const auto &definition = content.item(part);
            ASSERT_TRUE(definition.weaponComponent);
            const auto grid = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
                definition, ItemOrientation::Degrees0);
            ASSERT_TRUE(grid);
            const auto component = profile.assets.create(definition,
                StoredAssetLocation{ProfileContainerId::stash(), *grid});
            ASSERT_TRUE(executeWeaponComponentChange(profile, content,
                {gun, definition.weaponComponent->slot, component},
                {profile.revision, "fit-" + std::string{part.value()}}).succeeded);
        }
        ASSERT_EQ(effectiveWeaponUse(profile, content, gun).handlingSpeed,
                  fitSuppressor ? 48U : 56U);
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{gun, EquipmentSlotKind::PrimaryWeapon},
            {profile.revision, "equip-optic-rifle"}).succeeded);
        TemporarySaveDirectory directory;
        ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);

        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
        ASSERT_TRUE(session.deployAlpha(77235, MapDefinitionId{"map.v0.test"}))
            << session.persistenceMessage();
        GameplayInput aim;
        aim.aimWorldPosition = {800, 400};
        aim.aimViewport = Rect{{}, {1280, 720}};
        aim.aimMotionDelta = Vec2{};
        aim.aimDownSights = true;

        // 185 ms lies between the stock 181.6 ms and the suppressed 192.8 ms.
        for (int frame = 0; frame < 37; ++frame)
            session.update(aim, 0.005F);
        ASSERT_TRUE(session.world().weaponOpticView());
        EXPECT_EQ(session.world().activeWeaponOptic(),
                  fitSuppressor ? WeaponOpticMode::None : WeaponOpticMode::High);
        for (int frame = 0; frame < 3; ++frame)
            session.update(aim, 0.005F);
        EXPECT_EQ(session.world().activeWeaponOptic(), WeaponOpticMode::High);
    }
}
