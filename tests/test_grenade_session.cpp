#include "alpha_content_ids.h"
#include "game_session.h"
#include "grenade_domain.h"
#include "noise_decoy_fixture.h"
#include "profile_ui_test_wait.h"
#include "raid_corpse_search.h"
#include "raid_grenade_runtime.h"
#include "save_repository.h"
#include "weapon_ammo_domain.h"
#include "test_temp_directory.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>

// Keep this small accessor identical in test_grenade_performance.cpp. Tests
// stage legal deployments; production services still own every mutation tested.
struct GrenadeTestAccess
{
    static ProfileState &profile(GameSession &session) { return session.profile_; }
    static void betweenRaids(GameSession &session) { session.state_ = GameSessionState::BetweenRaids; }
    static void replaceWorld(GameSession &session, RaidWorldConfig config)
    { session.world_ = std::make_unique<GameplayWorld>(std::move(config)); }
    static EnemyLifecycle &actors(GameplayWorld &world) { return world.activeEnemies(); }
};

namespace
{
const ItemDefinitionId kGrenade{"item.throwable.frag_grenade_basic"};
const auto &content() { return publishedContentRegistry(); }

AssetInstanceId addThrowable(ProfileState &profile, const ItemDefinitionId &definition,
    std::uint32_t quantity = 2, bool inRig = true)
{
    equipDecoyTestRig(profile, content());
    const auto rig = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    if (!rig) return 0;
    ProfileContainerId destination = ProfileContainerId::stash();
    if (inRig)
    {
        const auto &pockets = content().item(profile.assets.find(*rig)->definitionId).containerCompartments;
        for (std::uint32_t index = 0; index < pockets.size(); ++index)
            if (pockets[index].pocketKind == ContainerPocketKind::General)
            { destination = ProfileContainerId::compartment(*rig, index); break; }
    }
    const auto &item = content().item(definition);
    const auto fit = findFirstProfileFit(profile, content(), destination, item, ItemOrientation::Degrees0);
    return fit ? profile.assets.create(item, StoredAssetLocation{destination, *fit}, quantity) : 0;
}

RaidWorldConfig arena()
{
    RaidWorldConfig config;
    config.worldSize = {2000, 1200};
    config.playerSpawn = {100, 100};
    config.playerMaximumHealth = config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    config.extractionPoint = {{1800, 1000}, {100, 100}};
    config.noiseDecoyEnabled = config.grenadeEnabled = true;
    return config;
}

AssetInstanceId stage(GameSession &session, RaidWorldConfig config = arena(),
    bool inRig = true, const MapDefinitionId &map = MapDefinitionId{"map.v0.test"},
    bool bodyArmor = false)
{
    auto &profile = GrenadeTestAccess::profile(session);
    profile = makeNewPublishedProfile("grenade-session", content());
    profile.tutorial = TutorialProgress::Complete;
    profile.currentHealth = config.playerCurrentHealth;
    const auto id = addThrowable(profile, kGrenade, 2, inRig);
    EXPECT_NE(id, 0U);
    if (bodyArmor)
        for (const auto &[assetId, asset] : profile.assets.records())
            if (asset.definitionId == alpha_content::bodyArmor)
            {
                EXPECT_TRUE(executeInventory(profile, content(),
                    InventoryEquipCommand{assetId, EquipmentSlotKind::BodyArmor},
                    {profile.revision, "grenade-equip-armor"}).succeeded);
                break;
            }
    GrenadeTestAccess::betweenRaids(session);
    EXPECT_TRUE(session.deployAlpha(42, map)) << session.persistenceMessage();
    EXPECT_TRUE(profile.pendingRaid);
    GrenadeTestAccess::replaceWorld(session, std::move(config));
    return id;
}

void makeExplosionDue(GameplayWorld &world)
{
    auto runtime = world.grenade();
    runtime.advance(world.activeRaidSpaceId(), 2.0F);
    ASSERT_TRUE(runtime.pendingExplosion());
    world.commitGrenade(std::move(runtime));
}

const Enemy *findEnemy(const GameplayWorld &world, CombatTargetId id)
{
    const auto &enemies = world.enemies();
    const auto found = std::find_if(enemies.begin(), enemies.end(),
        [id](const Enemy &enemy) { return enemy.combatTargetId() == id; });
    return found == enemies.end() ? nullptr : &*found;
}

struct IsolatedSaveDirectory
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-grenade-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    IsolatedSaveDirectory() { std::filesystem::create_directories(path); }
    ~IsolatedSaveDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

std::string fileBytes(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void packIntoWornRig(GameSession &session, AssetInstanceId id, const std::string &transaction)
{
    const auto &profile = session.profile();
    const auto rig = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    ASSERT_TRUE(rig);
    const auto *item = profile.assets.find(id);
    ASSERT_NE(item, nullptr);
    const auto &pockets = content().item(profile.assets.find(*rig)->definitionId).containerCompartments;
    for (std::uint32_t i = 0; i < pockets.size(); ++i)
    {
        if (pockets[i].pocketKind != ContainerPocketKind::General) continue;
        const auto destination = ProfileContainerId::compartment(*rig, i);
        const auto cell = findFirstProfileFit(profile, content(), destination, content().item(item->definitionId), item->orientation);
        if (!cell) continue;
        const auto moved = session.executeProfileInventory(InventoryMoveCommand{id, 0, {destination, *cell}}, transaction);
        ASSERT_TRUE(moved.succeeded) << moved.message;
        ASSERT_TRUE(waitForProfileUiDurability(session));
        return;
    }
    FAIL() << "Test rig has no space for the requested item";
}

void buyAndPackSmoke(GameSession &session, const std::filesystem::path &directory, AssetInstanceId &smoke)
{
    auto initial = makeNewPublishedProfile("smoke-supply", content());
    initial.tutorial = TutorialProgress::Complete;
    AssetInstanceId rig{};
    for (const auto &[id, asset] : initial.assets.records())
    {
        EXPECT_NE(asset.definitionId, kRaidSmokeItemId);
        if (asset.definitionId == alpha_content::chestRig) rig = id;
    }
    ASSERT_NE(rig, 0U);
    ASSERT_TRUE(SaveRepository(directory).save(initial, content().contentVersion()).succeeded);
    session.configurePersistence(directory);
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    const auto purchase = session.executeProfileEconomy(PurchaseCommand{kRaidSmokeItemId, 3}, "buy-smoke");
    ASSERT_TRUE(purchase.succeeded) << purchase.message;
    EXPECT_EQ(purchase.currencyDelta, -180);
    EXPECT_EQ(session.profile().currency, initial.currency - 180);
    for (const auto &[id, asset] : session.profile().assets.records())
        if (asset.definitionId == kRaidSmokeItemId) smoke = id;
    ASSERT_NE(smoke, 0U);
    ASSERT_EQ(session.profile().assets.find(smoke)->quantity, 3U);
    ASSERT_FALSE(smokeInChestRig(session.profile(), content(), smoke));
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{rig, EquipmentSlotKind::ChestRig}, "wear-smoke-rig").succeeded);
    ASSERT_NO_FATAL_FAILURE(packIntoWornRig(session, smoke, "pack-smoke"));
    ASSERT_TRUE(smokeInChestRig(session.profile(), content(), smoke));
    EXPECT_FALSE(session.ordinaryRaidThrowablesAllowed());
    EXPECT_FALSE(session.throwThrowable(smoke, {150, 120}, {session.profile().revision, "smoke-in-base"}).succeeded);
    ASSERT_TRUE(session.deployAlpha(42, MapDefinitionId{"map.raid.frontier_exchange"})) << session.persistenceMessage();
    ASSERT_TRUE(session.ordinaryRaidThrowablesAllowed());
}

Vec2 forwardThrowAim(const GameSession &session)
{
    const auto player = session.world().player().position();
    return {player.x + 220.0F, player.y + 20.0F};
}

Vec2 nearPlayer(const GameSession &session, Vec2 offset)
{
    const auto &player = session.world().player();
    const float half = player.size() * 0.5F;
    return {player.position().x + half + offset.x, player.position().y + half + offset.y};
}
}

TEST(ProximityAlarmSessionTest, AWorkshopAlarmDeploysLocallyOnceAndExtractionPersistsTheSpend)
{
    auto initial = makeNewAlphaProfile("craft-and-place-alarm", content());
    initial.tutorial = TutorialProgress::Complete;
    equipDecoyTestRig(initial, content());
    const auto stash = ProfileContainerId::stash();
    const ItemDefinitionId wire{"item.loot.copper_wire_spool"};
    std::vector<AssetInstanceId> materials;
    for (const auto &id : {wire, kWeaponSalvageMaterialId})
    {
        const auto &part = content().item(id);
        const auto fit = findFirstProfileFit(initial, content(), stash, part, ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);
        materials.push_back(initial.assets.create(part, StoredAssetLocation{stash, *fit}));
    }
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-alarm-", "craft-and-extract");
    ASSERT_TRUE(SaveRepository(directory.path()).save(initial, content().contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile());
    const auto order = session.executeStartBaseManufacturing(kRaidAlarmRecipeId, "order-alarm");
    ASSERT_TRUE(order.succeeded && order.outputAssetId) << order.message;
    ASSERT_TRUE(session.profile().baseManufacturing.activeOrder);
    const auto alarm = *order.outputAssetId;
    const auto due = session.profile().baseManufacturing.activeOrder->completionWorldMinute;
    session.advanceBaseWorldClock(static_cast<float>(due - session.profile().worldClock.elapsedWorldMinutes));
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_FALSE(session.profile().baseManufacturing.activeOrder);
    ASSERT_NE(session.profile().assets.find(alarm), nullptr);
    EXPECT_EQ(session.profile().assets.find(alarm)->definitionId, kRaidAlarmItemId);
    for (const auto part : materials) EXPECT_EQ(session.profile().assets.find(part), nullptr);
    ASSERT_NO_FATAL_FAILURE(packIntoWornRig(session, alarm, "pack-alarm"));
    EXPECT_FALSE(session.throwThrowable(alarm, {0, 0}, {session.profile().revision, "alarm-in-base"}).succeeded);
    ASSERT_TRUE(session.deployAlpha(42, kFreightCrateMapId)) << session.persistenceMessage();
    const auto point = nearPlayer(session, {52, 0});
    ASSERT_TRUE(session.beginThrowableSelection());
    ASSERT_TRUE(proximityAlarmInChestRig(session.profile(), content(), alarm));
    EXPECT_TRUE(session.world().proximityAlarms().entries().empty());
    const CommandContext command{session.profile().revision, "deploy-crafted-alarm"};
    const auto placed = session.throwThrowable(alarm, point, command);
    ASSERT_TRUE(placed.succeeded) << placed.message;
    ASSERT_EQ(session.world().proximityAlarms().entries().size(), 1U);
    const auto &device = session.world().proximityAlarms().entries().front();
    EXPECT_FLOAT_EQ(device.point.x, point.x); EXPECT_FLOAT_EQ(device.point.y, point.y);
    EXPECT_EQ(device.space, session.world().activeRaidSpaceId());
    EXPECT_FALSE(device.triggered);
    EXPECT_TRUE(session.throwThrowable(alarm, point, command).alreadyCommitted);
    EXPECT_EQ(session.world().proximityAlarms().entries().size(), 1U);
    EXPECT_EQ(session.profile().assets.find(alarm), nullptr);
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(session.world().extractionPoint().bounds().position));
    for (int i = 0; i < 45 && session.profile().pendingRaid; ++i) session.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_TRUE(session.world().proximityAlarms().entries().empty());
    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(reopened.profile().assets.find(alarm), nullptr);
}

TEST(ProximityAlarmSessionTest, RemoteBehindWallStashAndThirdPlacementAllLeaveTheRealStacksAlone)
{
    GameSession session;
    auto &profile = GrenadeTestAccess::profile(session);
    profile = makeNewAlphaProfile("alarm-placement", content());
    profile.tutorial = TutorialProgress::Complete;
    const auto alarms = addThrowable(profile, kRaidAlarmItemId, 2);
    const auto reserve = addThrowable(profile, kRaidAlarmItemId, 1);
    const auto stored = addThrowable(profile, kRaidAlarmItemId, 1, false);
    ASSERT_NE(alarms, 0U); ASSERT_NE(reserve, 0U); ASSERT_NE(stored, 0U);
    GrenadeTestAccess::betweenRaids(session);
    ASSERT_TRUE(session.deployAlpha(41, kFreightCrateMapId));
    auto config = arena();
    config.ballisticBlockers = {{1001, {{147, 70}, {5, 120}}}};
    config.initialEnemies = {EnemySpawn{{10, 90}, {20, 20}, 20}};
    GrenadeTestAccess::replaceWorld(session, std::move(config));
    const auto initial = profile.revision;
    EXPECT_FALSE(session.throwThrowable(alarms, nearPlayer(session, {120, 0}), {initial, "remote"}).succeeded);
    EXPECT_FALSE(session.throwThrowable(alarms, nearPlayer(session, {62, 0}), {initial, "behind-wall"}).succeeded);
    EXPECT_FALSE(session.throwThrowable(stored, nearPlayer(session, {-50, 0}), {initial, "from-stash"}).succeeded);
    EXPECT_FALSE(session.throwThrowable(alarms, nearPlayer(session, {-50, 0}), {initial - 1, "stale"}).succeeded);
    EXPECT_EQ(profile.revision, initial);
    ASSERT_EQ(profile.assets.find(alarms)->quantity, 2U);
    ASSERT_TRUE(session.world().proximityAlarms().entries().empty());
    const auto first = nearPlayer(session, {-50, 0});
    const CommandContext left{initial, "alarm-left"};
    ASSERT_TRUE(session.throwThrowable(alarms, first, left).succeeded);
    EXPECT_FALSE(session.throwThrowable(alarms, first, {profile.revision, "same-spot"}).succeeded);
    EXPECT_EQ(profile.assets.find(alarms)->quantity, 1U);
    ASSERT_TRUE(session.throwThrowable(alarms, nearPlayer(session, {0, 55}), {profile.revision, "alarm-down"}).succeeded);
    ASSERT_EQ(session.world().proximityAlarms().entries().size(), 2U);
    const auto full = profile.revision;
    const auto refused = session.throwThrowable(reserve, nearPlayer(session, {0, -55}), {full, "third-alarm"});
    EXPECT_FALSE(refused.succeeded);
    EXPECT_EQ(refused.message, "ALARM LIMIT REACHED");
    EXPECT_EQ(profile.revision, full);
    EXPECT_EQ(profile.assets.find(alarms), nullptr);
    EXPECT_TRUE(proximityAlarmInChestRig(profile, content(), reserve));
    session.update({}, 0.016F);
    const auto triggered = [](const auto &alarm) { return alarm.triggered; };
    ASSERT_EQ(std::ranges::count_if(session.world().proximityAlarms().entries(), triggered), 1);
    EXPECT_TRUE(session.throwThrowable(alarms, first, left).alreadyCommitted);
    EXPECT_EQ(std::ranges::count_if(session.world().proximityAlarms().entries(), triggered), 1);
    const CommandContext replace{profile.revision, "replace-triggered-alarm"};
    const auto replacement = session.throwThrowable(reserve, nearPlayer(session, {0, -55}), replace);
    ASSERT_TRUE(replacement.succeeded) << replacement.message;
    EXPECT_TRUE(session.throwThrowable(reserve, first, replace).alreadyCommitted);
    EXPECT_EQ(profile.assets.find(reserve), nullptr);
    EXPECT_EQ(session.world().proximityAlarms().entries().size(), 2U);
    EXPECT_EQ(std::ranges::count_if(session.world().proximityAlarms().entries(), triggered), 0);
    EXPECT_FALSE(session.world().proximityAlarms().canPlace());
    ASSERT_TRUE(session.activeQuitAlphaRaid());
    EXPECT_TRUE(session.world().proximityAlarms().entries().empty());
    EXPECT_FALSE(proximityAlarmInChestRig(session.profile(), content(), reserve));
}

TEST(ProximityAlarmSessionTest, RestartRestoresTheRigAndStartsTheNextRaidWithoutADevice)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-alarm-", "interrupted-raid");
    auto initial = makeNewAlphaProfile("interrupt-alarm", content());
    initial.tutorial = TutorialProgress::Complete;
    const auto alarm = addThrowable(initial, kRaidAlarmItemId, 2);
    ASSERT_NE(alarm, 0U);
    ASSERT_TRUE(SaveRepository(directory.path()).save(initial, content().contentVersion()).succeeded);
    std::set<std::string> raidIds, settlementIds;
    for (std::uint64_t attempt = 0; attempt < 2; ++attempt)
    {
        SCOPED_TRACE(attempt);
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile());
        ASSERT_EQ(session.profile().assets.find(alarm)->quantity, 2U);
        ASSERT_TRUE(session.deployAlpha(42, kFreightCrateMapId)) << session.persistenceMessage();
        ASSERT_EQ(session.raidNumber(), 1U);
        ASSERT_TRUE(raidIds.insert(session.profile().pendingRaid->raidId).second);
        ASSERT_TRUE(settlementIds.insert(session.profile().pendingRaid->settlementId).second);
        if (attempt)
            EXPECT_TRUE(session.throwThrowable(alarm, nearPlayer(session, {52, 0}),
                {0, "place-before-exit-0"}).alreadyCommitted);
        EXPECT_TRUE(session.world().proximityAlarms().entries().empty());
        ASSERT_TRUE(session.throwThrowable(alarm, nearPlayer(session, {52, 0}),
            {session.profile().revision, "place-before-exit-" + std::to_string(attempt)}).succeeded);
        ASSERT_EQ(session.world().proximityAlarms().entries().size(), 1U);
        ASSERT_EQ(session.profile().assets.find(alarm)->quantity, 1U);
        // Exercise recovery from an actual pending envelope as well as a normal base checkpoint.
        ASSERT_TRUE(waitForProfileUiDurability(session));
        const SaveRepository snapshot(directory.path());
        ASSERT_TRUE(snapshot.save(session.profile(), content().contentVersion()).succeeded);
        const auto saved = snapshot.load(content());
        ASSERT_TRUE(saved.profile && saved.profile->pendingRaid) << saved.message;
        ASSERT_EQ(saved.profile->assets.find(alarm)->quantity, 1U);
    }
    GameSession restored;
    restored.configurePersistence(directory.path());
    ASSERT_TRUE(restored.continueProfile());
    EXPECT_FALSE(restored.profile().pendingRaid);
    ASSERT_TRUE(proximityAlarmInChestRig(restored.profile(), content(), alarm));
    EXPECT_EQ(restored.profile().assets.find(alarm)->quantity, 2U);
    ASSERT_TRUE(restored.deployAlpha(44, kFreightCrateMapId)) << restored.persistenceMessage();
    EXPECT_TRUE(restored.world().proximityAlarms().entries().empty());
    EXPECT_EQ(restored.raidNumber(), 1U);
    EXPECT_TRUE(raidIds.insert(restored.profile().pendingRaid->raidId).second);
    EXPECT_TRUE(settlementIds.insert(restored.profile().pendingRaid->settlementId).second);
    ASSERT_TRUE(const_cast<Player &>(restored.world().player()).setPosition(restored.world().extractionPoint().bounds().position));
    for (int i = 0; i < 45 && restored.profile().pendingRaid; ++i) restored.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(restored));
    ASSERT_FALSE(restored.profile().pendingRaid);
    ASSERT_TRUE(restored.deployAlpha(45, kFreightCrateMapId)) << restored.persistenceMessage();
    EXPECT_EQ(restored.raidNumber(), 2U);
    EXPECT_EQ(restored.profile().pendingRaid->raidId, initial.profileId + "-raid-2");
    EXPECT_EQ(restored.profile().pendingRaid->settlementId, initial.profileId + "-settlement-2");
}

TEST(SmokeCoverSessionTest, LegacyContentAcceptsAnOldInventoryAndRefusesNewSupplies)
{
    auto profile = makeNewPublishedProfile("smoke-legacy", content());
    const auto legacy = serializeProfileEnvelope(profile, kRiversideArmedScavengersContentVersion, 75);
    ASSERT_TRUE(deserializeProfileEnvelope(legacy, content()).profile);
    ASSERT_NE(addThrowable(profile, kRaidSmokeItemId, 1, false), 0U);
    EXPECT_THROW(serializeProfileEnvelope(profile, kRiversideArmedScavengersContentVersion, 75), std::invalid_argument);
    const auto current = serializeProfileEnvelope(profile, kHospitalHybridOutdoorContentVersion, 75);
    ASSERT_TRUE(deserializeProfileEnvelope(current, content()).profile);
    auto legacyWithSmoke = current;
    const auto offset = legacyWithSmoke.find(kHospitalHybridOutdoorContentVersion);
    ASSERT_NE(offset, std::string::npos);
    legacyWithSmoke.replace(offset, kHospitalHybridOutdoorContentVersion.size(), kRiversideArmedScavengersContentVersion);
    const auto rejected = deserializeProfileEnvelope(legacyWithSmoke, content());
    EXPECT_FALSE(rejected.profile);
    EXPECT_NE(rejected.message.find("tactical supplies"), std::string::npos);
}

TEST(FireZoneSessionTest, WorkshopOrderCanBeCollectedByHandPackedThrownAndSettledOnce)
{
    const auto &catalog = content();
    auto initial = makeNewAlphaProfile("workshop-fire-zone", catalog);
    initial.tutorial = TutorialProgress::Complete;
    equipDecoyTestRig(initial, catalog);
    const auto rig = equippedAsset(initial, EquipmentSlotKind::ChestRig);
    ASSERT_TRUE(rig);
    const auto pack = std::ranges::find_if(initial.assets.records(), [](const auto &entry) {
        return entry.second.definitionId == alpha_content::backpack;
    });
    ASSERT_NE(pack, initial.assets.records().end());
    const auto packId = pack->first;
    ASSERT_TRUE(executeInventory(initial, catalog, InventoryEquipCommand{packId, EquipmentSlotKind::Backpack},
        {initial.revision, "equip-workshop-pack"}).succeeded);
    const auto stash = ProfileContainerId::stash();
    const auto bag = ProfileContainerId::compartment(packId, 0);
    const auto put = [&](const ItemDefinitionId &type, ProfileContainerId into) -> AssetInstanceId {
        const auto &definition = catalog.item(type);
        const auto cell = findFirstProfileFit(initial, catalog, into, definition, ItemOrientation::Degrees0);
        return cell ? initial.assets.create(definition, StoredAssetLocation{into, *cell}) : 0;
    };
    const auto fuel = put(kRaidFuelCanId, stash);
    const auto parts = put(kWeaponSalvageMaterialId, stash);
    const auto spareFuel = put(kRaidFuelCanId, bag);
    const auto spareParts = put(kWeaponSalvageMaterialId, bag);
    ASSERT_NE(fuel, 0U); ASSERT_NE(parts, 0U); ASSERT_NE(spareFuel, 0U); ASSERT_NE(spareParts, 0U);
    const auto fuelCell = std::get<StoredAssetLocation>(initial.assets.find(fuel)->location);
    const auto partsCell = std::get<StoredAssetLocation>(initial.assets.find(parts)->location);
    const auto originalBagCell = std::get<StoredAssetLocation>(initial.assets.find(spareParts)->location);
    // A full legal warehouse makes collection an explicit player action. The spare supplies
    // in the backpack will fill the two exact spaces freed when the order starts.
    const auto grid = profileContainerSize(initial, catalog, stash);
    for (const auto &type : {alpha_content::backpack, alpha_content::lootCola})
    {
        const auto &filler = catalog.item(type);
        for (int y = 0; y < grid.height; ++y)
            for (int x = 0; x < grid.width; ++x)
                if (profilePlacementFits(initial, catalog, stash, {x, y}, filler, ItemOrientation::Degrees0))
                    static_cast<void>(initial.assets.create(filler, StoredAssetLocation{stash, {x, y}}));
    }
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-fire-zone-", "real-order-collection-raid");
    ASSERT_TRUE(SaveRepository(directory.path()).save(initial, catalog.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile());
    const auto order = session.executeStartBaseManufacturing(kRaidFireBottleRecipeId, "order-zone");
    ASSERT_TRUE(order.succeeded && order.outputAssetId) << order.message;
    const auto output = *order.outputAssetId;
    const auto due = session.profile().baseManufacturing.activeOrder->completionWorldMinute;
    ASSERT_TRUE(session.executeProfileInventory(InventoryMoveCommand{spareFuel, 0, fuelCell}, "put-away-fuel").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(InventoryMoveCommand{spareParts, 0, partsCell}, "put-away-parts").succeeded);
    session.advanceBaseWorldClock(static_cast<float>(due - session.profile().worldClock.elapsedWorldMinutes));
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_TRUE(session.profile().baseManufacturing.activeOrder);
    ASSERT_TRUE(session.profile().baseManufacturing.activeOrder->outputReady);
    EXPECT_FALSE(session.executeCollectBaseManufacturing("full-warehouse").succeeded);
    ASSERT_TRUE(session.executeProfileInventory(InventoryMoveCommand{spareParts, 0, originalBagCell}, "make-collection-space").succeeded);
    const auto collected = session.executeCollectBaseManufacturing("collect-zone");
    ASSERT_TRUE(collected.succeeded) << collected.message;
    ASSERT_EQ(collected.outputAssetId, order.outputAssetId);
    EXPECT_FALSE(session.executeCollectBaseManufacturing("collect-again").succeeded);
    ASSERT_EQ(session.profile().assets.find(output)->quantity, 1U);
    ASSERT_NO_FATAL_FAILURE(packIntoWornRig(session, output, "pack-zone"));
    EXPECT_FALSE(session.throwThrowable(output, {200, 200}, {session.profile().revision, "base-zone"}).succeeded);
    ASSERT_TRUE(fireBottleInChestRig(session.profile(), catalog, output));
    ASSERT_TRUE(session.deployAlpha(42, kFreightCrateMapId)) << session.persistenceMessage();
    ASSERT_TRUE(session.beginThrowableSelection());
    const CommandContext command{session.profile().revision, "throw-workshop-zone"};
    const auto result = session.throwThrowable(output, forwardThrowAim(session), command);
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(session.profile().assets.find(output), nullptr);
    EXPECT_TRUE(session.throwThrowable(output, forwardThrowAim(session), command).alreadyCommitted);
    ASSERT_TRUE(session.world().fireZones().flight());
    session.update({}, 0.5F);
    ASSERT_EQ(session.world().fireZones().zones().size(), 1U);
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(session.world().extractionPoint().bounds().position));
    for (int i = 0; i < 45 && session.profile().pendingRaid; ++i) session.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_TRUE(session.world().fireZones().zones().empty());
    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(reopened.profile().assets.find(output), nullptr);
    EXPECT_EQ(reopened.profile().assets.find(fuel), nullptr);
    EXPECT_EQ(reopened.profile().assets.find(parts), nullptr);
    EXPECT_NE(reopened.profile().assets.find(spareFuel), nullptr);
    EXPECT_NE(reopened.profile().assets.find(spareParts), nullptr);
}

TEST(FireZoneSessionTest, SmokeAndFireFlightsShareTheInputLockAndCapacityNeverSpendsRejectedStacks)
{
    GameSession session;
    auto &profile = GrenadeTestAccess::profile(session);
    profile = makeNewAlphaProfile("field-throw-interlock", content());
    profile.tutorial = TutorialProgress::Complete;
    const auto two = addThrowable(profile, kRaidFireBottleItemId, 2);
    const auto reserve = addThrowable(profile, kRaidFireBottleItemId, 1);
    const auto smokes = addThrowable(profile, kRaidSmokeItemId, 2);
    ASSERT_NE(two, 0U); ASSERT_NE(reserve, 0U); ASSERT_NE(smokes, 0U);
    GrenadeTestAccess::betweenRaids(session);
    ASSERT_TRUE(session.deployAlpha(42, kFreightCrateMapId));
    const auto aim = forwardThrowAim(session);
    EXPECT_FALSE(session.throwThrowable(two, {std::numeric_limits<float>::quiet_NaN(), aim.y},
        {profile.revision, "invalid-zone"}).succeeded);
    ASSERT_TRUE(session.throwThrowable(smokes, aim, {profile.revision, "smoke-flight"}).succeeded);
    EXPECT_FALSE(session.throwThrowable(two, aim, {profile.revision, "fire-during-smoke"}).succeeded);
    EXPECT_EQ(profile.assets.find(two)->quantity, 2U);
    session.update({}, 0.5F);
    ASSERT_TRUE(session.throwThrowable(two, aim, {profile.revision, "first-zone"}).succeeded);
    EXPECT_FALSE(session.canSelectNoiseDecoy());
    EXPECT_FALSE(session.throwThrowable(smokes, aim, {profile.revision, "smoke-during-fire"}).succeeded);
    EXPECT_EQ(profile.assets.find(smokes)->quantity, 1U);
    session.update({}, 0.5F);
    ASSERT_TRUE(session.throwThrowable(two, aim, {profile.revision, "second-zone"}).succeeded);
    session.update({}, 0.5F);
    ASSERT_EQ(session.world().fireZones().zones().size(), 2U);
    const auto before = profile.revision;
    const auto refused = session.throwThrowable(reserve, aim, {before, "third-zone"});
    EXPECT_FALSE(refused.succeeded);
    EXPECT_EQ(refused.message, "FIRE ZONE LIMIT REACHED");
    EXPECT_EQ(profile.revision, before);
    EXPECT_EQ(profile.assets.find(two), nullptr);
    ASSERT_EQ(profile.assets.find(reserve)->quantity, 1U);
    for (int i = 0; i < 65; ++i) session.update({}, 0.1F);
    ASSERT_TRUE(session.world().fireZones().zones().empty());
    EXPECT_TRUE(session.throwThrowable(reserve, aim, {profile.revision, "zone-after-expiry"}).succeeded);
}

TEST(FireZoneSessionTest, InterruptingRaidRestoresTheOriginalStackAndCreatesNoZoneOnTheNextDeployment)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-fire-zone-", "interrupted-session");
    auto initial = makeNewAlphaProfile("interrupted-fire-zone", content());
    initial.tutorial = TutorialProgress::Complete;
    const auto bottle = addThrowable(initial, kRaidFireBottleItemId, 2);
    ASSERT_NE(bottle, 0U);
    ASSERT_TRUE(SaveRepository(directory.path()).save(initial, content().contentVersion()).succeeded);
    {
        GameSession session;
        session.configurePersistence(directory.path());
        ASSERT_TRUE(session.continueProfile());
        ASSERT_TRUE(session.deployAlpha(42, kFreightCrateMapId));
        ASSERT_TRUE(session.throwThrowable(bottle, forwardThrowAim(session), {session.profile().revision, "interrupt-zone"}).succeeded);
        session.update({}, 0.5F);
        ASSERT_EQ(session.world().fireZones().zones().size(), 1U);
        EXPECT_EQ(session.profile().assets.find(bottle)->quantity, 1U);
    }
    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_FALSE(reopened.profile().pendingRaid);
    ASSERT_TRUE(fireBottleInChestRig(reopened.profile(), content(), bottle));
    EXPECT_EQ(reopened.profile().assets.find(bottle)->quantity, 2U);
    ASSERT_TRUE(reopened.deployAlpha(43, kFreightCrateMapId));
    EXPECT_TRUE(reopened.world().fireZones().zones().empty());
    EXPECT_FALSE(reopened.world().fireZones().flight());
}

TEST(VolatileBarrelSessionTest, ARealRifleShotSpendsOneRoundAndAppliesTheBlastToTheShooter)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-barrels-", "actual-fire");
    auto initial = makeNewPublishedProfile("barrel-live-shot", content());
    initial.tutorial = TutorialProgress::Complete;
    const auto findAsset = [&](const ItemDefinitionId &definition) {
        for (const auto &[id, asset] : initial.assets.records())
            if (asset.definitionId == definition) return id;
        return AssetInstanceId{};
    };
    const ItemDefinitionId rounds{"item.ammunition.5_45x39_standard"};
    const auto gun = findAsset(ItemDefinitionId{"item.weapon.rifle_5_45_service"});
    const auto mag = findAsset(ItemDefinitionId{"item.magazine.5_45x39_30"});
    ASSERT_NE(gun, 0U);
    ASSERT_NE(mag, 0U);
    ASSERT_TRUE(executeEconomy(initial, content(), PurchaseCommand{rounds, 3},
        {initial.revision, "buy-barrel-rounds"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(initial, content(), LoadMagazineCommand{mag, findAsset(rounds), 3},
        {initial.revision, "load-barrel-rounds"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(initial, content(), InstallMagazineAndChamberCommand{gun, mag},
        {initial.revision, "chamber-barrel-round"}).succeeded);
    ASSERT_TRUE(executeInventory(initial, content(), InventoryEquipCommand{gun, EquipmentSlotKind::PrimaryWeapon},
        {initial.revision, "equip-barrel-rifle"}).succeeded);
    ASSERT_TRUE(SaveRepository(directory.path()).save(initial, content().contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(directory.path());
    ASSERT_TRUE(session.continueProfile());
    ASSERT_TRUE(session.deployAlpha(42, MapDefinitionId{"map.raid.frontier_exchange"})) << session.persistenceMessage();
    const auto &pending = *session.profile().pendingRaid;
    ASSERT_EQ(pending.volatileBarrels.size(), 2U);
    const auto baseline = SaveRepository(directory.path()).load(content());
    ASSERT_TRUE(baseline.profile) << baseline.message;
    EXPECT_FALSE(baseline.profile->pendingRaid);
    EXPECT_EQ(magazineRoundCount(*baseline.profile, mag), magazineRoundCount(initial, mag));
    ASSERT_EQ(session.world().volatileBarrels().size(), 2U);
    const auto target = session.world().volatileBarrels().front();
    ASSERT_FALSE(target.spent);
    auto &player = const_cast<Player &>(session.world().player());
    ASSERT_TRUE(player.setPosition({target.center.x - player.size() * 0.5F,
        target.center.y - player.size() * 0.5F - 46.0F}));
    GameplayInput aim;
    aim.aimWorldPosition = target.center;
    for (int i = 0; i < 3; ++i) session.update(aim, 0.016F);
    EXPECT_FALSE(session.world().volatileBarrels().front().spent);
    const int before = session.profile().currentHealth;
    const auto roundsBefore = magazineRoundCount(session.profile(), mag);
    aim.firePressed = aim.fireJustPressed = aim.firePhysicallyPressed = true;
    session.update(aim, 0.016F);
    ASSERT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(magazineRoundCount(session.profile(), mag) + 1, roundsBefore);
    ASSERT_TRUE(session.world().volatileBarrels().front().spent);
    EXPECT_FALSE(session.world().volatileBarrels().back().spent);
    EXPECT_LT(session.profile().currentHealth, before);
    const auto after = session.profile().currentHealth;
    session.update({}, 0.016F);
    EXPECT_EQ(session.profile().currentHealth, after);

    ASSERT_TRUE(player.setPosition(session.world().extractionPoint().bounds().position));
    for (int i = 0; i < 65 && session.profile().pendingRaid; ++i) session.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_FALSE(session.profile().pendingRaid);
    GameSession restored;
    restored.configurePersistence(directory.path());
    ASSERT_TRUE(restored.continueProfile());
    ASSERT_TRUE(restored.profile().lastRaidResult);
    EXPECT_EQ(restored.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_EQ(magazineRoundCount(restored.profile(), mag) + 1, roundsBefore);
}

TEST(SmokeCoverSessionTest, BuysEquipsAndThrowsRealStacksAtMostTwiceThenExtractsTheRemainder)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-smoke-", "normal-extraction");
    GameSession session;
    AssetInstanceId smoke{};
    ASSERT_NO_FATAL_FAILURE(buyAndPackSmoke(session, directory.path(), smoke));
    const Vec2 aim = forwardThrowAim(session);
    const auto firstRevision = session.profile().revision;
    ASSERT_TRUE(session.beginThrowableSelection());
    EXPECT_EQ(session.profile().revision, firstRevision);
    EXPECT_FALSE(session.world().smoke().flight());
    EXPECT_FALSE(session.throwThrowable(smoke, {std::numeric_limits<float>::quiet_NaN(), aim.y},
        {firstRevision, "invalid-aim"}).succeeded);
    EXPECT_FALSE(session.throwThrowable(smoke, aim, {firstRevision - 1, "stale-aim"}).succeeded);
    EXPECT_EQ(session.profile().revision, firstRevision);
    ASSERT_EQ(session.profile().assets.find(smoke)->quantity, 3U);
    const CommandContext first{firstRevision, "smoke-first"};
    const auto thrown = session.throwThrowable(smoke, aim, first);
    ASSERT_TRUE(thrown.succeeded) << thrown.message;
    ASSERT_TRUE(session.world().smoke().flight());
    EXPECT_EQ(session.world().smoke().flight()->space, session.world().activeRaidSpaceId());
    EXPECT_TRUE(session.throwThrowable(smoke, aim, first).alreadyCommitted);
    EXPECT_FALSE(session.canSelectNoiseDecoy());
    EXPECT_FALSE(session.canSelectThrowable());
    EXPECT_EQ(session.profile().assets.find(smoke)->quantity, 2U);
    session.update({}, 0.5F);
    ASSERT_EQ(session.world().smoke().clouds().size(), 1U);
    ASSERT_TRUE(session.beginThrowableSelection());
    const auto second = session.throwThrowable(smoke, aim, {session.profile().revision, "smoke-second"});
    ASSERT_TRUE(second.succeeded) << second.message;
    session.update({}, 0.5F);
    ASSERT_EQ(session.world().smoke().clouds().size(), 2U);
    const auto revision = session.profile().revision;
    const auto full = session.throwThrowable(smoke, aim, {revision, "smoke-over-limit"});
    EXPECT_FALSE(full.succeeded);
    EXPECT_EQ(full.message, "SMOKE LIMIT REACHED");
    EXPECT_EQ(session.profile().revision, revision);
    ASSERT_EQ(session.profile().assets.find(smoke)->quantity, 1U);

    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(session.world().extractionPoint().bounds().position));
    for (int i = 0; i < 45 && session.profile().pendingRaid; ++i) session.update({}, 0.1F);
    ASSERT_TRUE(waitForProfileUiDurability(session));
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_TRUE(session.world().smoke().clouds().empty());
    EXPECT_FALSE(session.world().smoke().flight());
    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile());
    ASSERT_TRUE(smokeInChestRig(reopened.profile(), content(), smoke));
    EXPECT_EQ(reopened.profile().assets.find(smoke)->quantity, 1U);
    ASSERT_TRUE(reopened.deployAlpha(43, MapDefinitionId{"map.raid.frontier_exchange"}));
    EXPECT_TRUE(reopened.world().smoke().clouds().empty());
    EXPECT_FALSE(reopened.world().smoke().flight());
}

TEST(SmokeCoverSessionTest, InterruptedRaidRollsBackThePurchasedRigStackWithoutRestoringACloud)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-smoke-", "interrupted-raid");
    AssetInstanceId smoke{};
    {
        GameSession session;
        ASSERT_NO_FATAL_FAILURE(buyAndPackSmoke(session, directory.path(), smoke));
        const auto result = session.throwThrowable(smoke, forwardThrowAim(session),
            {session.profile().revision, "smoke-before-interruption"});
        ASSERT_TRUE(result.succeeded) << result.message;
        session.update({}, 0.5F);
        ASSERT_EQ(session.world().smoke().clouds().size(), 1U);
        ASSERT_EQ(session.profile().assets.find(smoke)->quantity, 2U);
    }
    GameSession restored;
    restored.configurePersistence(directory.path());
    ASSERT_TRUE(restored.continueProfile()) << restored.persistenceMessage();
    ASSERT_FALSE(restored.profile().pendingRaid);
    ASSERT_TRUE(smokeInChestRig(restored.profile(), content(), smoke));
    EXPECT_EQ(restored.profile().assets.find(smoke)->quantity, 3U);
    ASSERT_TRUE(restored.deployAlpha(44, MapDefinitionId{"map.raid.frontier_exchange"}));
    EXPECT_TRUE(restored.world().smoke().clouds().empty());
    EXPECT_FALSE(restored.world().smoke().flight());
}

TEST(SmokeCoverSessionTest, TrainingAndStashSuppliesCannotDeploySmoke)
{
    GameSession training;
    ASSERT_TRUE(training.startDeveloperRange());
    ASSERT_TRUE(training.grantDeveloperRangeLoadout(alpha_content::rifle, alpha_content::ammunition, std::nullopt));
    const auto trainingSmoke = addThrowable(GrenadeTestAccess::profile(training), kRaidSmokeItemId, 1);
    const auto trainingAlarm = addThrowable(GrenadeTestAccess::profile(training), kRaidAlarmItemId, 1);
    ASSERT_NE(trainingSmoke, 0U);
    ASSERT_NE(trainingAlarm, 0U);
    EXPECT_FALSE(training.ordinaryRaidThrowablesAllowed());
    const auto trainingRevision = training.profile().revision;
    EXPECT_FALSE(training.throwThrowable(trainingSmoke, forwardThrowAim(training), {trainingRevision, "training-smoke"}).succeeded);
    EXPECT_EQ(training.profile().revision, trainingRevision);
    EXPECT_FALSE(training.world().smoke().flight());
    EXPECT_FALSE(training.throwThrowable(trainingAlarm, nearPlayer(training, {45, 0}), {trainingRevision, "training-alarm"}).succeeded);
    EXPECT_TRUE(proximityAlarmInChestRig(training.profile(), content(), trainingAlarm));
    EXPECT_TRUE(training.world().proximityAlarms().entries().empty());

    GameSession session;
    auto &profile = GrenadeTestAccess::profile(session);
    profile = makeNewPublishedProfile("stash-smoke", content());
    profile.tutorial = TutorialProgress::Complete;
    const auto stored = addThrowable(profile, kRaidSmokeItemId, 1, false);
    ASSERT_NE(stored, 0U);
    GrenadeTestAccess::betweenRaids(session);
    ASSERT_TRUE(session.deployAlpha(44, MapDefinitionId{"map.raid.frontier_exchange"}));
    EXPECT_TRUE(session.ordinaryRaidThrowablesAllowed());
    const auto revision = profile.revision;
    const auto refused = session.throwThrowable(stored, forwardThrowAim(session), {revision, "stash-smoke"});
    EXPECT_FALSE(refused.succeeded);
    EXPECT_EQ(refused.message, "NO CHEST RIG SMOKE");
    EXPECT_EQ(profile.revision, revision);
    EXPECT_NE(profile.assets.find(stored), nullptr);
    EXPECT_FALSE(session.world().smoke().flight());
}

TEST(SmokeCoverSessionTest, FailedSettlementRetainsCloudAndSpendUntilTheRealQuitCommits)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-smoke-", "quit-write-failure");
    GameSession session;
    AssetInstanceId smoke{};
    ASSERT_NO_FATAL_FAILURE(buyAndPackSmoke(session, directory.path(), smoke));
    const auto primary = directory.path() / "profile.json";
    const auto checkpoint = fileBytes(primary);
    ASSERT_FALSE(checkpoint.empty());
    ASSERT_TRUE(session.throwThrowable(smoke, forwardThrowAim(session), {session.profile().revision, "throw-before-quit"}).succeeded);
    session.update({}, 0.5F);
    ASSERT_EQ(session.world().smoke().clouds().size(), 1U);
    const auto blocker = directory.path() / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(blocker));
    ASSERT_FALSE(session.activeQuitAlphaRaid());
    ASSERT_EQ(session.state(), GameSessionState::SettlementBlocked);
    ASSERT_TRUE(session.profile().pendingRaid);
    ASSERT_EQ(session.profile().assets.find(smoke)->quantity, 2U);
    EXPECT_EQ(session.world().smoke().clouds().size(), 1U);
    EXPECT_EQ(fileBytes(primary), checkpoint);
    ASSERT_TRUE(std::filesystem::remove(blocker));
    ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_TRUE(session.world().smoke().clouds().empty());
    EXPECT_FALSE(smokeInChestRig(session.profile(), content(), smoke));
}

TEST(SmokeCoverSessionTest, LostKitRecoveryRejectsAnOtherwiseEligibleRigStack)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("smoke-recovery"));
    auto &profile = GrenadeTestAccess::profile(session);
    const auto rifle = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::rifle; });
    ASSERT_NE(rifle, profile.assets.records().end());
    ASSERT_TRUE(session.executeProfileInventory(
        InventoryEquipCommand{rifle->first, EquipmentSlotKind::PrimaryWeapon}, "lose-rifle").succeeded);
    const MapDefinitionId map{"map.v0.test"};
    ASSERT_TRUE(session.deployAlpha(77191, map));
    ASSERT_TRUE(session.activeQuitAlphaRaid());
    const auto records = session.lostRaidRecordProjections();
    ASSERT_EQ(records.size(), 1U);
    const auto carried = addThrowable(profile, kRaidSmokeItemId, 2);
    const auto bottle = addThrowable(profile, kRaidFireBottleItemId, 1);
    const auto alarm = addThrowable(profile, kRaidAlarmItemId, 1);
    ASSERT_NE(carried, 0U);
    ASSERT_NE(bottle, 0U);
    ASSERT_NE(alarm, 0U);
    ASSERT_TRUE(session.deployAlpha(77192, map, {}, records.front().recordId));
    ASSERT_TRUE(profile.pendingRaid->selfRecovery);
    ASSERT_TRUE(smokeInChestRig(profile, content(), carried));
    ASSERT_FALSE(session.ordinaryRaidThrowablesAllowed());
    const auto revision = profile.revision;
    const auto result = session.throwThrowable(carried, forwardThrowAim(session), {revision, "recovery-smoke"});
    EXPECT_FALSE(result.succeeded);
    EXPECT_EQ(result.message, "SMOKE UNAVAILABLE HERE");
    EXPECT_EQ(profile.revision, revision);
    EXPECT_EQ(profile.assets.find(carried)->quantity, 2U);
    EXPECT_FALSE(session.world().smoke().flight());
    EXPECT_FALSE(session.throwThrowable(bottle, forwardThrowAim(session), {revision, "recovery-fire-zone"}).succeeded);
    EXPECT_EQ(profile.revision, revision);
    EXPECT_TRUE(fireBottleInChestRig(profile, content(), bottle));
    EXPECT_FALSE(session.world().fireZones().flight());
    EXPECT_FALSE(session.throwThrowable(alarm, nearPlayer(session, {45, 0}), {revision, "recovery-alarm"}).succeeded);
    EXPECT_TRUE(proximityAlarmInChestRig(profile, content(), alarm));
    EXPECT_TRUE(session.world().proximityAlarms().entries().empty());
}

TEST(GrenadeSessionTest, ExactStackAndReplayCommitOnlyOneRealFlight)
{
    GameSession session;
    const auto id = stage(session);
    ASSERT_TRUE(session.canSelectThrowable());
    ASSERT_TRUE(session.beginThrowableSelection());
    const CommandContext command{session.profile().revision, "grenade-one"};
    const auto thrown = session.throwThrowable(id, {500, 116}, command);
    ASSERT_TRUE(thrown.succeeded) << thrown.message;
    ASSERT_TRUE(session.world().grenade().flight());
    EXPECT_EQ(session.profile().assets.find(id)->quantity, 1U);
    const auto fingerprint = profileStateFingerprint(session.profile());
    const auto replay = session.throwThrowable(id, {900, 500}, command);
    EXPECT_TRUE(replay.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_FLOAT_EQ(session.world().grenade().flight()->landing.point.x, 500);
    EXPECT_FALSE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-second"}).succeeded);
    EXPECT_FALSE(session.canSelectThrowable());
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);

    makeExplosionDue(session.world());
    session.update({}, 0);
    ASSERT_FALSE(session.world().grenade().active());
    const auto afterExplosion = profileStateFingerprint(session.profile());
    EXPECT_TRUE(session.throwThrowable(id, {600, 200}, command).alreadyCommitted);
    EXPECT_FALSE(session.world().grenade().active());
    EXPECT_EQ(profileStateFingerprint(session.profile()), afterExplosion);
}

TEST(GrenadeSessionTest, InvalidAimStaleMissingStashAndDisabledActivityAreZeroModification)
{
    for (int refusal = 0; refusal < 5; ++refusal)
    {
        SCOPED_TRACE(refusal);
        GameSession session;
        auto config = arena();
        if (refusal == 4) config.grenadeEnabled = false;
        auto id = stage(session, config, refusal != 3);
        Vec2 aim{500, 116};
        CommandContext command{session.profile().revision, "grenade-reject"};
        if (refusal == 0) aim = {116, 116};
        if (refusal == 1) ++command.expectedRevision;
        if (refusal == 2) id = std::numeric_limits<AssetInstanceId>::max();
        const auto before = profileStateFingerprint(session.profile());
        EXPECT_FALSE(session.throwThrowable(id, aim, command).succeeded);
        EXPECT_EQ(profileStateFingerprint(session.profile()), before);
        EXPECT_FALSE(session.world().grenade().active());
        EXPECT_FALSE(session.world().lastGrenadeExplosion());
        EXPECT_EQ(session.world().score(), 0);
    }
}

TEST(GrenadeSessionTest, TrainingUsesTheSharedRealThrowAndExactStackConsumption)
{
    GameSession session;auto config=arena();config.developerRange=true;
    const auto id=stage(session,config);const auto quantity=session.profile().assets.find(id)->quantity;
    const auto receipt=session.throwThrowable(id,{500,116},{session.profile().revision,"training-real-grenade"});
    ASSERT_TRUE(receipt.succeeded)<<receipt.message;
    ASSERT_TRUE(session.world().grenade().flight());
    ASSERT_NE(session.profile().assets.find(id),nullptr);
    EXPECT_EQ(session.profile().assets.find(id)->quantity,quantity-1);
    EXPECT_EQ(session.world().score(),0);EXPECT_FALSE(session.world().lastGrenadeExplosion());
}

TEST(GrenadeSessionTest, WorldExplosionUsesArmorWallsAndStableIdentitiesWithoutCrossingSpace)
{
    auto config = arena();
    config.ballisticBlockers = {{1, {{540, 20}, {8, 220}}}};
    auto armor = content().enemyCombatDefinition(EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    ASSERT_TRUE(armor);
    config.initialEnemies = {
        EnemySpawn{{470, 106}, {20, 20}, 12},
        EnemySpawn{{490, 106}, {20, 20}, 12},
        EnemySpawn{{560, 106}, {20, 20}, 12},
        EnemySpawn{{490, 136}, {20, 20}, 12},
        EnemySpawn{{1000, 106}, {20, 20}, 12}};
    config.initialEnemies[1].torsoArmor = armor;
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.grenade_test_room"};
    room.displayName = "隔离空间";
    room.worldSize = config.worldSize;
    room.exteriorEntrance = {{80, 80}, {100, 100}};
    room.exteriorReturn = room.interiorSpawn = {100, 100};
    room.interiorExit = {{80, 80}, {100, 100}};
    room.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 12}};
    config.interiors = {room};
    GameSession session;
    const auto id = stage(session, config);
    const auto ordinaryFirst = session.world().enemies()[0].combatTargetId();
    const auto armored = session.world().enemies()[1].combatTargetId();
    const auto blocked = session.world().enemies()[2].combatTargetId();
    const auto ordinaryLast = session.world().enemies()[3].combatTargetId();
    const auto outsideRadius = session.world().enemies()[4].combatTargetId();
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-multiple"}).succeeded);
    makeExplosionDue(session.world());
    const auto prepared = session.world().prepareGrenadeExplosion();
    ASSERT_TRUE(prepared);
    ASSERT_EQ(prepared->enemies.size(), 3U);
    EXPECT_FALSE(prepared->playerDamage);
    session.update({}, 0);
    EXPECT_EQ(session.world().enemies().size(), 3U);
    EXPECT_FALSE(findEnemy(session.world(), ordinaryFirst));
    EXPECT_FALSE(findEnemy(session.world(), ordinaryLast));
    ASSERT_TRUE(findEnemy(session.world(), armored));
    EXPECT_EQ(findEnemy(session.world(), armored)->health(), 1);
    EXPECT_EQ(findEnemy(session.world(), armored)->torsoArmor()->durability, armor->durability - 6U);
    EXPECT_EQ(findEnemy(session.world(), blocked)->health(), 12);
    EXPECT_EQ(findEnemy(session.world(), outsideRadius)->health(), 12);
    EXPECT_EQ(session.world().score(), 200);
    EXPECT_FALSE(session.world().commitPreparedGrenadeExplosion(*prepared));
    session.update({}, 0);
    EXPECT_EQ(session.world().score(), 200);
    EXPECT_EQ(findEnemy(session.world(), armored)->health(), 1);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());

    GameplayInput enter;
    enter.interactJustPressed = true;
    session.update(enter, 0);
    EXPECT_EQ(session.world().activeRaidSpaceId(), room.id);
    ASSERT_EQ(session.world().enemies().size(), 1U);
    EXPECT_EQ(session.world().enemies().front().health(), 12);
}

TEST(GrenadeSessionTest, MutatedTargetRejectsPreparedExplosionBeforeAnyOtherTargetChanges)
{
    auto config = arena();
    config.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 100},
        EnemySpawn{{490, 136}, {20, 20}, 100}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-stale-plan"}).succeeded);
    makeExplosionDue(session.world());
    const auto plan = session.world().prepareGrenadeExplosion();
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->enemies.size(), 2U);
    auto &actors = GrenadeTestAccess::actors(session.world());
    ASSERT_FALSE(actors.back().takeDamage(1));
    EXPECT_FALSE(session.world().validatePreparedGrenadeExplosion(*plan));
    EXPECT_FALSE(session.world().commitPreparedGrenadeExplosion(*plan));
    EXPECT_EQ(actors.front().health(), 100);
    EXPECT_EQ(actors.back().health(), 99);
    EXPECT_EQ(session.world().score(), 0);
    EXPECT_TRUE(session.world().grenade().pendingExplosion());
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
}

TEST(GrenadeSessionTest, ReorderedAndRepeatedPreparedHitsStillUseStableIdentity)
{
    auto config = arena();
    config.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 100},
        EnemySpawn{{490, 136}, {20, 20}, 100}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-reordered-plan"}).succeeded);
    auto &world = session.world();
    makeExplosionDue(world);
    auto plan = world.prepareGrenadeExplosion();
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->enemies.size(), 2U);
    const auto first = plan->enemies.front();
    const auto second = plan->enemies.back();
    plan->enemies = {second, first, second};

    auto missingTarget = *plan;
    missingTarget.enemies.back().id = kInvalidCombatTargetId;
    EXPECT_FALSE(world.commitPreparedGrenadeExplosion(missingTarget));
    EXPECT_EQ(world.enemies()[0].health(), first.health);
    EXPECT_EQ(world.enemies()[1].health(), second.health);

    ASSERT_TRUE(world.validatePreparedGrenadeExplosion(*plan));
    ASSERT_TRUE(world.commitPreparedGrenadeExplosion(*plan));
    ASSERT_EQ(world.enemies().size(), 2U);
    EXPECT_EQ(world.enemies()[0].combatTargetId(), first.id);
    EXPECT_EQ(world.enemies()[0].health(), first.health - first.damage.damageApplied);
    EXPECT_EQ(world.enemies()[1].combatTargetId(), second.id);
    EXPECT_EQ(world.enemies()[1].health(), second.health - 2 * second.damage.damageApplied);
    EXPECT_FALSE(world.commitPreparedGrenadeExplosion(*plan));
}

TEST(GrenadeSessionTest, ThreeFrozenCorpsesOpenOnceWhileOtherDeathsGrantNoNewLoot)
{
    GameSession session;
    const auto id = stage(session, arena(), true, MapDefinitionId{"map.raid.frontier_exchange"});
    std::vector<std::string> sources;
    for (const auto &[assetId, asset] : session.profile().assets.records())
    {
        static_cast<void>(assetId);
        if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
            sources.push_back(corpse->sourceEnemyId);
    }
    ASSERT_EQ(sources.size(), 3U);
    auto config = arena();
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        EnemySpawn enemy{{470 + 20.0F * static_cast<float>(index), 106}, {12, 12}, 12};
        enemy.sourceEnemyId = sources[index];
        config.initialEnemies.push_back(enemy);
    }
    config.initialEnemies.push_back(EnemySpawn{{490, 130}, {12, 12}, 12});
    GrenadeTestAccess::replaceWorld(session, config);
    const auto frozenEnemies = session.profile().pendingRaid->enemies;
    const auto frozenLoot = session.profile().pendingRaid->loot;
    const auto highWater = session.profile().assets.nextAssetId();
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-corpse"}).succeeded);
    makeExplosionDue(session.world());
    session.update({}, 1.0F / 60.0F);
    EXPECT_TRUE(session.world().enemies().empty());
    EXPECT_EQ(session.world().score(), 400);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
    std::size_t exposed = 0;
    for (const auto &[assetId, asset] : session.profile().assets.records())
    {
        static_cast<void>(assetId);
        if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
            if (corpse->deathPosition) ++exposed;
    }
    EXPECT_EQ(exposed, 3U);
    EXPECT_EQ(session.profile().assets.nextAssetId(), highWater);
    EXPECT_EQ(session.profile().pendingRaid->enemies, frozenEnemies);
    EXPECT_EQ(session.profile().pendingRaid->loot, frozenLoot);
    const auto after = profileStateFingerprint(session.profile());
    session.update({}, 0);
    EXPECT_EQ(session.world().score(), 400);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
    EXPECT_EQ(profileStateFingerprint(session.profile()), after);
    EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
}

TEST(GrenadeSessionTest, SelfDamageUsesProfileArmorAndDoesNotRollNewBleeding)
{
    for (bool armored : {false, true})
    {
        SCOPED_TRACE(armored);
        GameSession session;
        const auto id = stage(session, arena(), true, MapDefinitionId{"map.v0.test"}, armored);
        const auto armorId = equippedAsset(session.profile(), EquipmentSlotKind::BodyArmor);
        const auto durability = armorId ? session.profile().assets.find(*armorId)->currentDurability : 0U;
        const auto medical = session.profile().medicalStatus;
        const auto expected = queryIncomingDamage(session.profile(), content(),
            IncomingDamageCommand{24, HitRegion::Torso, 3, 6, false, {}});
        ASSERT_TRUE(expected.canCommit);
        if (!armored) EXPECT_EQ(expected.resolution.damageApplied, 24);
        ASSERT_TRUE(session.throwThrowable(id, {160, 116},
            {session.profile().revision, "grenade-self"}).succeeded);
        makeExplosionDue(session.world());
        session.update({}, 0);
        EXPECT_EQ(session.profile().currentHealth, 100 - expected.resolution.damageApplied);
        EXPECT_EQ(session.world().player().health(), session.profile().currentHealth);
        EXPECT_EQ(session.profile().medicalStatus, medical);
        ASSERT_TRUE(session.lastIncomingDamage());
        EXPECT_EQ(session.lastIncomingDamage()->semantic, HitSemantic::Normal);
        if (armorId)
            EXPECT_EQ(session.profile().assets.find(*armorId)->currentDurability,
                durability - expected.resolution.armorDurabilityLoss);
        EXPECT_FALSE(session.world().grenade().active());
    }
}

TEST(GrenadeSessionTest, PlayerCandidateFailureLeavesEnemiesArmorScoreAndExplosionUncommitted)
{
    auto config = arena();
    EnemySpawn enemy{{150, 106}, {12, 12}, 12};
    enemy.torsoArmor = content().enemyCombatDefinition(EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    config.initialEnemies = {enemy, EnemySpawn{{180, 106}, {12, 12}, 12}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {160, 116},
        {session.profile().revision, "grenade-failure"}).succeeded);
    makeExplosionDue(session.world());
    ASSERT_TRUE(session.world().prepareGrenadeExplosion()->playerDamage);
    std::vector<EnemyRuntimeCheckpoint> checkpoints;
    for (const auto &actor : session.world().enemies()) checkpoints.push_back(actor.checkpoint());
    GrenadeTestAccess::profile(session).revision = std::numeric_limits<ProfileRevision>::max();
    const auto fingerprint = profileStateFingerprint(session.profile());
    session.update({}, 1.0F / 60.0F);
    EXPECT_EQ(session.state(), GameSessionState::SettlementBlocked);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    ASSERT_EQ(session.world().enemies().size(), checkpoints.size());
    // The earlier whole-checkpoint assertion also rejected ordinary perception
    // and facing updates at zero time. Compare only blast-owned participants.
    for (std::size_t index = 0; index < checkpoints.size(); ++index)
    {
        const auto actual = session.world().enemies()[index].checkpoint();
        const auto &before = checkpoints[index];
        EXPECT_EQ(actual.id, before.id);
        EXPECT_EQ(actual.position, before.position);
        EXPECT_EQ(actual.health, before.health);
        EXPECT_EQ(actual.maximumHealth, before.maximumHealth);
        EXPECT_EQ(actual.torsoArmor, before.torsoArmor);
    }
    EXPECT_EQ(session.world().player().health(), 100);
    EXPECT_EQ(session.world().score(), 0);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
    EXPECT_TRUE(session.world().grenade().pendingExplosion());
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
}

TEST(GrenadeSessionTest, FlightAndFuseBlockPortalsAndExtractionUntilTheExplosionCommits)
{
    auto config = arena();
    config.extractionPoint = {{80, 80}, {100, 100}};
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.grenade_gate_room"};
    room.displayName = "门禁测试";
    room.worldSize = config.worldSize;
    room.exteriorEntrance = room.interiorExit = {{80, 80}, {100, 100}};
    room.exteriorReturn = room.interiorSpawn = {100, 100};
    config.interiors = {room};
    GameSession session;
    const auto id = stage(session, config);
    session.update({}, 0.25F);
    ASSERT_EQ(session.world().raidSession().state(), RaidSessionState::Extracting);
    ASSERT_GT(session.world().raidSession().extractionProgress(), 0);
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-gates"}).succeeded);
    EXPECT_EQ(session.world().raidSession().extractionProgress(), 0);
    GameplayInput enter;
    enter.interactJustPressed = true;
    session.update(enter, 0.5F);
    EXPECT_EQ(session.world().activeRaidSpaceId(), outdoorRaidSpaceId());
    EXPECT_TRUE(session.world().grenade().fuse());
    EXPECT_EQ(session.world().raidSession().extractionProgress(), 0);
    session.update(enter, 1.49F);
    EXPECT_TRUE(session.world().grenade().active());
    EXPECT_EQ(session.world().activeRaidSpaceId(), outdoorRaidSpaceId());
    EXPECT_EQ(session.world().raidSession().extractionProgress(), 0);
    session.update({}, 0.02F);
    ASSERT_FALSE(session.world().grenade().active());
    session.update(enter, 0);
    EXPECT_EQ(session.world().activeRaidSpaceId(), room.id);
    EXPECT_TRUE(session.profile().pendingRaid);
}

TEST(GrenadeSessionTest, InventoryMapAndWheelInputsDoNotFreezeTheRealFuse)
{
    for (int mode = 0; mode < 3; ++mode)
    {
        SCOPED_TRACE(mode);
        GameSession session;
        const auto id = stage(session);
        ASSERT_TRUE(session.throwThrowable(id, {500, 116},
            {session.profile().revision, "grenade-clock"}).succeeded);
        session.update({}, 0.5F);
        ASSERT_TRUE(session.world().grenade().fuse());
        GameplayInput interfaceInput;
        interfaceInput.inventoryOpen = mode == 0;
        interfaceInput.freezeObservationDirection = mode == 1;
        interfaceInput.throwableModal = mode == 2;
        session.update(interfaceInput, 1.0F);
        ASSERT_TRUE(session.world().grenade().fuse());
        EXPECT_NEAR(session.world().grenade().fuse()->elapsed, 1.0, 0.0001);
        session.update(interfaceInput, 0); // The client pause path supplies no advancing time.
        EXPECT_NEAR(session.world().grenade().fuse()->elapsed, 1.0, 0.0001);
        session.update(interfaceInput, 0.51F);
        EXPECT_FALSE(session.world().grenade().active());
        EXPECT_TRUE(session.world().lastGrenadeExplosion());
    }
}

TEST(GrenadeSessionTest, LethalSelfDamageSettlesOnceAfterTheSameBlastKillsEnemies)
{
    auto config = arena();
    config.playerCurrentHealth = 20;
    config.initialEnemies = {EnemySpawn{{150, 106}, {12, 12}, 12},
        EnemySpawn{{180, 106}, {12, 12}, 12}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {160, 116},
        {session.profile().revision, "grenade-lethal"}).succeeded);
    makeExplosionDue(session.world());
    session.update({}, 1.0F / 60.0F);
    EXPECT_TRUE(session.world().player().isDead());
    EXPECT_TRUE(session.world().enemies().empty());
    EXPECT_EQ(session.world().score(), 200);
    EXPECT_FALSE(session.world().grenade().active());
    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
    const auto fingerprint = profileStateFingerprint(session.profile());
    session.update({}, 0);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_EQ(session.world().score(), 200);
}

TEST(GrenadeSessionTest, ExistingTerminalStateAndActiveQuitClearPendingGrenadeWithoutDetonation)
{
    for (bool alreadyDead : {false, true})
    {
        SCOPED_TRACE(alreadyDead);
        auto config = arena();
        config.initialEnemies = {EnemySpawn{{470, 106}, {20, 20}, 12}};
        GameSession session;
        const auto id = stage(session, config);
        ASSERT_TRUE(session.throwThrowable(id, {500, 116},
            {session.profile().revision, "grenade-end"}).succeeded);
        makeExplosionDue(session.world());
        if (alreadyDead)
        {
            ASSERT_TRUE(session.world().markPlayerDead());
            session.update({}, 1.0F / 60.0F);
        }
        else ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
        EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
        EXPECT_FALSE(session.profile().pendingRaid);
        EXPECT_FALSE(session.world().grenade().active());
        EXPECT_FALSE(session.world().lastGrenadeExplosion());
        EXPECT_EQ(session.world().score(), 0);
        ASSERT_EQ(session.world().enemies().size(), 1U);
        EXPECT_EQ(session.world().enemies().front().health(), 12);
    }
}

TEST(GrenadeSessionTest, SettlementSaveFailurePreservesCheckpointAndRetryDoesNotRefundOrExplode)
{
    IsolatedSaveDirectory temporary;
    GameSession session;
    session.configurePersistence(temporary.path);
    const auto id = stage(session);
    const auto checkpoint = fileBytes(temporary.path / "profile.json");
    ASSERT_FALSE(checkpoint.empty());
    ASSERT_TRUE(session.throwThrowable(id, {500, 116},
        {session.profile().revision, "grenade-save-failure"}).succeeded);
    const auto afterThrow = profileStateFingerprint(session.profile());
    std::filesystem::create_directory(temporary.path / "profile.tmp.json");
    EXPECT_FALSE(session.activeQuitAlphaRaid());
    EXPECT_EQ(session.state(), GameSessionState::SettlementBlocked);
    EXPECT_EQ(profileStateFingerprint(session.profile()), afterThrow);
    EXPECT_EQ(fileBytes(temporary.path / "profile.json"), checkpoint);
    EXPECT_EQ(session.profile().assets.find(id)->quantity, 1U);
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
    std::filesystem::remove(temporary.path / "profile.tmp.json");
    ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
    EXPECT_FALSE(session.profile().pendingRaid);
    EXPECT_FALSE(session.world().grenade().active());
    EXPECT_FALSE(session.world().lastGrenadeExplosion());
    EXPECT_EQ(session.world().score(), 0);
    EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
}

TEST(GrenadeSessionTest, ExplosionInvestigatesItsPointWithoutExpandingPlayerAcquisitionDistance)
{
    auto config = arena();
    // Player center (116,116) is 400 units away: beyond acquisition (360),
    // inside pursuit retention (460), with an unobstructed view. The blast is
    // deliberately below the enemy, while the player is to its left.
    config.initialEnemies = {EnemySpawn{{506, 106}, {20, 20}, 100}};
    GameSession session;
    const auto id = stage(session, config);
    ASSERT_EQ(session.world().enemies().front().awarenessState(), EnemyAwarenessState::Unaware);
    ASSERT_FALSE(session.world().enemies().front().lastKnownTargetPosition());
    const Vec2 blast{500, 200};
    ASSERT_TRUE(session.throwThrowable(id, blast,
        {session.profile().revision, "grenade-investigate-only"}).succeeded);
    makeExplosionDue(session.world());
    session.update({}, 0);
    const auto &investigator = session.world().enemies().front();
    ASSERT_GT(investigator.health(), 0);
    EXPECT_LT(investigator.health(), 100);
    EXPECT_EQ(investigator.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(investigator.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(investigator.lastKnownTargetPosition()->x, blast.x);
    EXPECT_FLOAT_EQ(investigator.lastKnownTargetPosition()->y, blast.y);
    const auto before = investigator.position();

    GameplayInput moveAway;
    moveAway.moveLeft = true;
    session.update(moveAway, 0.1F);
    const auto &after = session.world().enemies().front();
    EXPECT_EQ(after.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(after.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(after.lastKnownTargetPosition()->x, blast.x);
    EXPECT_FLOAT_EQ(after.lastKnownTargetPosition()->y, blast.y);
    // Verify the real navigation consumer, not just its memory label: following
    // the player would move left, whereas investigating the blast moves down.
    EXPECT_GT(after.position().y, before.y + 4.0F);
    EXPECT_GT(after.position().x, before.x - 4.0F);
    const auto player = session.world().player().position();
    const float half = session.world().player().size() / 2;
    EXPECT_NE(after.lastKnownTargetPosition()->x, player.x + half);
    EXPECT_NE(after.lastKnownTargetPosition()->y, player.y + half);
}

TEST(GrenadeSessionTest, AlertedListenerPropagatesToItsGroupWithoutReplacingItsExistingPursuit)
{
    auto config = arena();
    config.worldSize = {2600, 1200};
    const std::string group = "encounter.grenade-propagation";
    EnemySpawn pursuing{{506, 106}, {20, 20}, 100};
    EnemySpawn remote{{1790, 190}, {20, 20}, 100};
    EnemySpawn unrelated{{1790, 290}, {20, 20}, 100};
    pursuing.encounterGroupInstanceId = remote.encounterGroupInstanceId = group;
    unrelated.encounterGroupInstanceId = "encounter.unrelated";
    config.initialEnemies = {pursuing, remote, unrelated};
    GameSession session;
    const auto id = stage(session, config);
    const Vec2 blast{500, 200};
    ASSERT_TRUE(session.throwThrowable(id, blast,
        {session.profile().revision, "grenade-group-relay"}).succeeded);
    makeExplosionDue(session.world());
    const Vec2 knownPlayer{116, 116};
    GrenadeTestAccess::actors(session.world()).front().hearTarget(knownPlayer);
    session.update({}, 0);
    ASSERT_EQ(session.world().enemies().size(), 3U);
    const auto &near = session.world().enemies()[0];
    const auto &far = session.world().enemies()[1];
    const auto &other = session.world().enemies()[2];
    EXPECT_EQ(near.awarenessState(), EnemyAwarenessState::Alerted);
    ASSERT_TRUE(near.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(near.lastKnownTargetPosition()->x, knownPlayer.x);
    EXPECT_FLOAT_EQ(near.lastKnownTargetPosition()->y, knownPlayer.y);
    EXPECT_LT(near.health(), 100); // Preserving pursuit does not grant blast immunity.
    EXPECT_EQ(far.health(), 100);
    EXPECT_EQ(far.awarenessState(), EnemyAwarenessState::Searching);
    ASSERT_TRUE(far.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(far.lastKnownTargetPosition()->x, blast.x);
    EXPECT_FLOAT_EQ(far.lastKnownTargetPosition()->y, blast.y);
    EXPECT_EQ(other.awarenessState(), EnemyAwarenessState::Unaware);
    EXPECT_FALSE(other.lastKnownTargetPosition());
    EXPECT_EQ(session.world().score(), 0);
}

TEST(GrenadeSessionTest, DueExplosionRetiresAnActiveScratchBeforeContactAndNeverReplaysItsHit)
{
    auto config = arena();
    config.initialEnemies = {EnemySpawn{{150, 100}, {32, 32}, 12}};
    const auto readyScratch = [](GameSession &session)
    {
        auto &enemy = GrenadeTestAccess::actors(session.world()).front();
        if (!enemy.tryStartAttack(EnemyAttackType::Scratch, {-1, 0})) return false;
        const auto attack = enemy.attackConfig();
        if (!attack) return false;
        static_cast<void>(enemy.update(attack->windupDuration + 0.00001F, 2000, 1200));
        return enemy.attackPhase() == EnemyAttackPhase::Active && enemy.hasAttackHitOpportunity();
    };

    // This identical live contact is a real counterfactual, not an out-of-range
    // attacker. Its accepted scratch reaches the Profile damage consumer.
    GameSession control;
    ASSERT_NE(stage(control, config), 0U);
    ASSERT_TRUE(readyScratch(control));
    control.update({}, 0.02F);
    ASSERT_TRUE(control.lastIncomingDamage());
    EXPECT_EQ(control.lastIncomingDamage()->damageBeforeArmor, 12);
    EXPECT_EQ(control.profile().currentHealth, 88);
    EXPECT_EQ(control.world().player().health(), 88);
    EXPECT_EQ(control.world().score(), 0);

    GameSession session;
    const auto id = stage(session, config);
    ASSERT_TRUE(session.throwThrowable(id, {160, 116},
        {session.profile().revision, "grenade-before-contact"}).succeeded);
    auto runtime = session.world().grenade();
    runtime.advance(session.world().activeRaidSpaceId(), 1.99F);
    ASSERT_TRUE(runtime.fuse());
    ASSERT_FALSE(runtime.pendingExplosion());
    session.world().commitGrenade(std::move(runtime));
    ASSERT_TRUE(readyScratch(session));
    const auto medical = session.profile().medicalStatus;
    session.update({}, 0.02F); // Fuse expires before this prepared contact can run.
    ASSERT_TRUE(session.world().lastGrenadeExplosion());
    EXPECT_TRUE(session.world().grenadeExplodedLastUpdate());
    EXPECT_TRUE(session.world().enemies().empty());
    EXPECT_EQ(session.world().score(), 100);
    EXPECT_EQ(session.profile().currentHealth, 76);
    EXPECT_EQ(session.world().player().health(), 76);
    EXPECT_EQ(session.profile().medicalStatus, medical);
    ASSERT_TRUE(session.lastIncomingDamage());
    EXPECT_EQ(session.lastIncomingDamage()->damageBeforeArmor, 24);
    EXPECT_TRUE(session.world().takePlayerDamageObservations().empty());
    EXPECT_FALSE(session.world().grenade().active());

    session.update({}, 0.05F);
    EXPECT_EQ(session.profile().currentHealth, 76);
    EXPECT_EQ(session.world().player().health(), 76);
    EXPECT_EQ(session.profile().medicalStatus, medical);
    EXPECT_FALSE(session.lastIncomingDamage());
    EXPECT_TRUE(session.world().takePlayerDamageObservations().empty());
    EXPECT_EQ(session.world().score(), 100);
    EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
}
