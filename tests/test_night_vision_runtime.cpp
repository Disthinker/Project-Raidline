#include "game_session.h"
#include "alpha_content_ids.h"
#include "thrown_illumination_domain.h"
#include "tactical_device_domain.h"
#include "weapon_component_domain.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace
{
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};
const ItemDefinitionId pistol{"item.weapon.pistol_basic"};
const ItemDefinitionId rail{"item.component.rail_handguard"};
const ItemDefinitionId grip{"item.component.vertical_grip"};
const ItemDefinitionId lamp{"item.component.weapon_flashlight_basic"};
const ItemDefinitionId helmet{"item.protective_gear.helmet_basic"};
const ItemDefinitionId nightVision{"item.device.night_vision_basic"};
const ItemDefinitionId battery{"item.battery.universal"};
const MapDefinitionId hospital{"map.raid.hospital_district"};

AssetInstanceId findItem(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}

AssetInstanceId addItem(ProfileState &profile, const ItemDefinitionId &id, std::uint32_t quantity = 1)
{
    const auto &content = publishedContentRegistry();
    const auto fit = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
        content.item(id), ItemOrientation::Degrees0);
    EXPECT_TRUE(fit.has_value()) << id.value();
    if (!fit) return 0;
    return profile.assets.create(content.item(id), StoredAssetLocation{ProfileContainerId::stash(), *fit},
        quantity);
}

void armSession(GameSession &session, unsigned startHour, bool withLamp = true)
{
    ASSERT_TRUE(session.startNewProfile("night-vision-runtime"));
    auto &profile = const_cast<ProfileState &>(session.profile());
    const auto &content = publishedContentRegistry();
    profile.worldClock.elapsedWorldMinutes = startHour * kWorldMinutesPerHour;

    const auto helmetId = findItem(profile, helmet);
    const auto deviceId = addItem(profile, nightVision);
    const auto source = addItem(profile, battery, withLamp ? 2 : 1);
    ASSERT_NE(helmetId, 0U);
    ASSERT_NE(deviceId, 0U);
    ASSERT_NE(source, 0U);
    ASSERT_TRUE(executeHelmetDeviceChange(profile, content, {helmetId, deviceId},
        {profile.revision, "install-night-vision"}).succeeded);
    ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {deviceId, source}, {},
        {profile.revision, "power-night-vision"}).succeeded);
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{helmetId, EquipmentSlotKind::Helmet},
        {profile.revision, "equip-night-vision-helmet"}).succeeded);

    if (withLamp)
    {
        const auto weapon = addItem(profile, rifle);
        const auto sidearm = findItem(profile, pistol);
        ASSERT_NE(weapon, 0U);
        ASSERT_NE(sidearm, 0U);
        for (const auto &part : {rail, grip, lamp})
        {
            auto id = findItem(profile, part);
            if (!id) id = addItem(profile, part);
            ASSERT_NE(id, 0U);
            const auto result = executeWeaponComponentChange(profile, content,
                {weapon, content.item(part).weaponComponent->slot, id},
                {profile.revision, "install-" + std::string{part.value()}});
            ASSERT_TRUE(result.succeeded) << result.message;
        }
        ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {findItem(profile, lamp), source}, {},
            {profile.revision, "power-flashlight"}).succeeded);
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon},
            {profile.revision, "equip-flashlight-rifle"}).succeeded);
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{sidearm, EquipmentSlotKind::Sidearm},
            {profile.revision, "equip-sidearm"}).succeeded);
    }
}

void pressU(GameSession &session, float seconds)
{
    GameplayInput input;
    input.lightJustPressed = input.lightPressed = true;
    session.update(input, seconds);
}

void releaseU(GameSession &session)
{
    GameplayInput input;
    input.lightJustReleased = true;
    session.update(input, 0);
}

void tapU(GameSession &session)
{
    pressU(session, 0.025F);
    releaseU(session);
}

RaidWorldConfig nightTestWorld()
{
    RaidWorldConfig config;
    config.lightConeObservationEnabled = true;
    config.worldSize = {3000, 3000};
    config.playerSpawn = {984, 984};
    config.extractionPoint = {{2800, 2800}, {100, 100}};
    return config;
}
} // namespace

TEST(NightVisionRuntimeTest, TapHoldSelectAndCancelKeepDeviceOutputIndependent)
{
    GameSession session;
    ASSERT_NO_FATAL_FAILURE(armSession(session, 18));
    ASSERT_TRUE(session.deployAlpha(9917, hospital)) << session.persistenceMessage();
    ASSERT_EQ(session.worldClockProjection().timeOfDay, WorldTimeOfDay::Night);
    ASSERT_NE(session.activeWeaponLight().binding.deviceAssetId, 0U);
    ASSERT_NE(session.activeNightVision().binding.deviceAssetId, 0U);
    EXPECT_NE(session.activeNightVision().binding.batteryAssetId,
              session.activeWeaponLight().binding.batteryAssetId);

    tapU(session);
    EXPECT_TRUE(session.activeWeaponLight().output);
    EXPECT_FALSE(session.activeNightVision().switchOn);
    EXPECT_FALSE(session.tacticalDeviceWheelOpen());

    pressU(session, 0.20F);
    session.update({}, 0.20F); // A save or modal may consume the physical release.
    EXPECT_FALSE(session.tacticalDeviceWheelOpen());
    releaseU(session);
    EXPECT_TRUE(session.activeWeaponLight().output);
    EXPECT_FALSE(session.activeNightVision().switchOn);

    pressU(session, 0.35F);
    ASSERT_TRUE(session.tacticalDeviceWheelOpen());
    EXPECT_TRUE(session.activeWeaponLight().output);
    EXPECT_FALSE(session.activeNightVision().switchOn);
    session.cancelWeaponLightInput();
    releaseU(session);
    EXPECT_FALSE(session.tacticalDeviceWheelOpen());
    EXPECT_TRUE(session.activeWeaponLight().output);
    EXPECT_FALSE(session.activeNightVision().switchOn);

    pressU(session, 0.35F);
    ASSERT_TRUE(session.tacticalDeviceWheelOpen());
    ASSERT_TRUE(session.selectTacticalDevice(TacticalDeviceKind::NightVision).succeeded);
    releaseU(session);
    ASSERT_FALSE(session.tacticalDeviceWheelOpen());
    EXPECT_TRUE(session.activeWeaponLight().output);
    ASSERT_TRUE(session.activeNightVision().output);
    EXPECT_EQ(session.selectedTacticalDevice(), TacticalDeviceKind::NightVision);
    EXPECT_FLOAT_EQ(session.world().raidObservationKey().angleDegrees, 150);
    EXPECT_FLOAT_EQ(session.world().raidObservationKey().maximumDistance, 850);
    EXPECT_NE(session.world().raidObservationKey().lightAssetId, 0U);

    const auto nightCharge = session.activeNightVision().remainingCharges;
    const auto lampCharge = session.activeWeaponLight().remainingMs;
    session.update({}, 0.05F);
    EXPECT_LT(session.activeNightVision().remainingCharges, nightCharge);
    EXPECT_LT(session.activeWeaponLight().remainingMs, lampCharge);
    const auto beforeMenu = session.activeNightVision().remainingCharges;
    GameplayInput menu;
    menu.inventoryOpen = true;
    session.update(menu, 0.05F);
    EXPECT_EQ(session.activeNightVision().remainingCharges, beforeMenu);

    ASSERT_TRUE(session.startAlphaWeaponSwitch(EquipmentSlotKind::Sidearm));
    session.update({}, 0.025F);
    EXPECT_TRUE(session.activeNightVision().output);
    EXPECT_FALSE(session.activeWeaponLight().output);
    for (int frame = 0; frame < 70 && session.activeAlphaWeaponSlot() != EquipmentSlotKind::Sidearm; ++frame)
        session.update({}, 0.025F);
    ASSERT_EQ(session.activeAlphaWeaponSlot(), EquipmentSlotKind::Sidearm);
    EXPECT_TRUE(session.activeNightVision().output);
    EXPECT_FALSE(session.activeWeaponLight().output);
    EXPECT_EQ(session.world().raidObservationKey().lightAssetId, 0U);

    tapU(session);
    EXPECT_FALSE(session.activeNightVision().switchOn);
    EXPECT_FLOAT_EQ(session.world().raidObservationKey().angleDegrees, 100);
}

TEST(NightVisionRuntimeTest, WheelOpeningFramePreemptsTheTriggerAndPortal)
{
    GameSession session;
    ASSERT_NO_FATAL_FAILURE(armSession(session, 18));
    auto &profile = const_cast<ProfileState &>(session.profile());
    const auto &content = publishedContentRegistry();
    const auto weapon = findItem(profile, rifle);
    const auto magazine = addItem(profile, ItemDefinitionId{"item.magazine.5_45x39_30"});
    const auto round = addItem(profile, ItemDefinitionId{"item.ammunition.5_45x39_standard"});
    ASSERT_NE(magazine, 0U);
    ASSERT_NE(round, 0U);
    ASSERT_TRUE(executeWeaponAmmo(profile, content, LoadMagazineCommand{magazine, round, 1},
        {profile.revision, "load-threshold-round"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(profile, content, InstallMagazineCommand{weapon, magazine},
        {profile.revision, "install-threshold-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(profile, content, ChamberWeaponCommand{weapon},
        {profile.revision, "chamber-threshold-round"}).succeeded);
    ASSERT_TRUE(session.deployAlpha(9917, hospital)) << session.persistenceMessage();

    const auto &entrance = session.profile().pendingRaid->interiors.front().exteriorEntrance;
    auto &player = const_cast<Player &>(session.world().player());
    const float half = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({entrance.position.x + entrance.size.x * 0.5F - half,
        entrance.position.y + entrance.size.y * 0.5F - half}));
    pressU(session, 0.20F);
    ASSERT_TRUE(session.world().raidSpacePortalInteractionInRange());

    GameplayInput crossing;
    crossing.lightPressed = true;
    crossing.fireJustPressed = crossing.firePressed = crossing.firePhysicallyPressed = true;
    crossing.interactJustPressed = crossing.interactPressed = true;
    session.update(crossing, 0.16F);
    ASSERT_TRUE(session.tacticalDeviceWheelOpen());
    EXPECT_TRUE(session.world().inOutdoorRaidSpace());
    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_TRUE(session.profile().assets.find(weapon)->chamberedRound.has_value());
}

TEST(NightVisionRuntimeTest, SoleDeviceUsesTapAndRetainsFullChargeInDaylight)
{
    GameSession session;
    ASSERT_NO_FATAL_FAILURE(armSession(session, 8, false));
    ASSERT_TRUE(session.deployAlpha(9917, hospital)) << session.persistenceMessage();
    ASSERT_EQ(session.worldClockProjection().timeOfDay, WorldTimeOfDay::Day);
    tapU(session);
    ASSERT_TRUE(session.activeNightVision().switchOn);
    EXPECT_FALSE(session.activeNightVision().output);
    const auto charge = session.activeNightVision().remainingCharges;
    ASSERT_GT(charge, 0U);
    pressU(session, 0.35F);
    EXPECT_FALSE(session.tacticalDeviceWheelOpen());
    releaseU(session);
    session.update({}, 0.05F);
    EXPECT_TRUE(session.activeNightVision().switchOn);
    EXPECT_EQ(session.activeNightVision().remainingCharges, charge);
    EXPECT_FLOAT_EQ(session.world().raidObservationKey().angleDegrees, 180);
    EXPECT_FLOAT_EQ(session.world().raidObservationKey().maximumDistance, 1000);
}

TEST(NightVisionRuntimeTest, OneOccludedConeFollowsDarkRoomAndOutdoorBoundaries)
{
    auto config = nightTestWorld();
    config.ballisticBlockers.push_back({1, {{1110, 990}, {15, 80}}});
    RaidInteriorWorldConfig room;
    room.id = RaidSpaceDefinitionId{"raid_space.night_vision.test"};
    room.displayName = "Test room";
    room.worldSize = {1800, 1500};
    room.interiorSpawn = {400, 350};
    room.exteriorEntrance = {{950, 950}, {100, 100}};
    room.exteriorReturn = config.playerSpawn;
    room.interiorExit = {{370, 320}, {100, 100}};
    config.interiors.push_back(room);

    GameplayWorld world(config);
    world.setRaidDaylight(false);
    GameplayInput aim;
    aim.aimWorldPosition = Vec2{1500, 1000};
    aim.aimWorldBounds = Rect{{}, config.worldSize};
    world.update(aim, 0);
    const auto dark = world.raidObservation().key();
    EXPECT_FLOAT_EQ(dark.maximumDistance, 600);
    const auto builds = world.indoorObservationBuildCount();
    world.setNightVision({150, 850}, {140, 750});
    const auto &lit = world.raidObservation();
    ASSERT_TRUE(lit.validGeometry());
    EXPECT_EQ(world.indoorObservationBuildCount(), builds + 1);
    EXPECT_FALSE(lit.matches(dark));
    EXPECT_FLOAT_EQ(lit.angleDegrees, 150);
    EXPECT_FLOAT_EQ(lit.maximumDistance, 850);
    EXPECT_TRUE(lit.pointVisible({1540, 1400}));
    EXPECT_FALSE(lit.pointVisible({1180, 1020}));
    EXPECT_FALSE(lit.pointVisible({980, 1000}));
    static_cast<void>(world.raidBoundsObservable({{1540, 1400}, {20, 20}}));
    EXPECT_EQ(world.indoorObservationBuildCount(), builds + 1);

    GameplayInput enter;
    enter.interactJustPressed = true;
    world.update(enter, 0);
    ASSERT_EQ(world.activeRaidSpaceId(), room.id);
    EXPECT_FLOAT_EQ(world.raidObservationKey().angleDegrees, 140);
    EXPECT_FLOAT_EQ(world.raidObservationKey().maximumDistance, 750);
    world.setRaidDaylight(true);
    world.update(enter, 0);
    ASSERT_TRUE(world.inOutdoorRaidSpace());
    EXPECT_FLOAT_EQ(world.raidObservationKey().angleDegrees, 180);
    EXPECT_FLOAT_EQ(world.raidObservationKey().maximumDistance, 1000);
}

TEST(NightVisionPerformanceTest, DualDeviceObservationSharesOneSnapshotWithinThreeMilliseconds)
{
    GameSession loadout;
    ASSERT_NO_FATAL_FAILURE(armSession(loadout, 18));
    const auto &content = publishedContentRegistry();
    const auto vision = findNightVision(loadout.profile(), content, findItem(loadout.profile(), helmet));
    const auto light = findWeaponLight(loadout.profile(), content, findItem(loadout.profile(), rifle));
    ASSERT_NE(vision.batteryAssetId, 0U);
    ASSERT_NE(light.batteryAssetId, 0U);
    for (int count : {32, 100})
    {
        auto profile = loadout.profile();
        ASSERT_TRUE(executeDeviceSwitch(profile, content, vision.deviceAssetId,
            {profile.revision, "benchmark-night-vision"}).succeeded);
        ASSERT_TRUE(executeDeviceSwitch(profile, content, light.deviceAssetId,
            {profile.revision, "benchmark-flashlight"}).succeeded);
        auto config = nightTestWorld();
        for (int n = 0; n < 220; ++n)
        {
            const float angle = n * 6.28318530718F / 220;
            const float distance = 260 + (n % 5) * 126.0F;
            config.ballisticBlockers.push_back({static_cast<std::uint64_t>(n + 1),
                {{1000 + std::cos(angle) * distance, 1000 + std::sin(angle) * distance}, {5, 24}}});
        }
        for (int n = 0; n < count; ++n)
        {
            const float angle = n * 6.28318530718F / count;
            const float distance = 100 + (n % 7) * 90.0F;
            config.initialEnemies.push_back(EnemySpawn{
                {975 + std::cos(angle) * distance, 975 + std::sin(angle) * distance},
                {50, 50}, 100});
        }
        GameplayWorld world(config);
        world.setRaidDaylight(false);
        std::vector<double> samples;
        samples.reserve(150);
        volatile int seen{};
        for (int frame = 0; frame < 160; ++frame)
        {
            const float angle = frame * 0.09F;
            const Vec2 center{1000 + 3 * std::sin(angle), 1000 + 3 * std::cos(angle)};
            ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition({center.x - 16, center.y - 16}));
            GameplayInput aim;
            aim.aimWorldPosition = Vec2{center.x + std::cos(angle) * 400, center.y + std::sin(angle) * 400};
            aim.aimWorldBounds = Rect{{}, config.worldSize};
            world.update(aim, 0);

            const auto solves = world.enemyObservationSolveCount();
            const auto start = std::chrono::steady_clock::now();
            advanceNightVision(profile, content, vision, true, 0.016F);
            advanceWeaponLight(profile, content, light, true, 0.016F);
            const auto poweredVision = queryNightVision(profile, content, vision, true);
            const auto poweredLight = queryWeaponLight(profile, content, light, true);
            world.setNightVision({poweredVision.vision.outdoorAngleDegrees, poweredVision.vision.outdoorDistance},
                                 {poweredVision.vision.indoorAngleDegrees, poweredVision.vision.indoorDistance});
            world.setWeaponLight(light.deviceAssetId, light.batteryAssetId, poweredLight.light.angleDegrees,
                                 poweredLight.light.maximumDistance, poweredLight.light.backgroundRadius);
            const auto &view = world.raidObservation();
            for (int consumer = 0; consumer < 4; ++consumer)
                for (const auto &enemy : world.enemies())
                    seen = seen + (world.raidEnemyObservable(enemy.combatTargetId()) ? 1 : 0);
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            ASSERT_TRUE(poweredVision.output);
            ASSERT_TRUE(poweredLight.output);
            ASSERT_TRUE(view.validGeometry());
            ASSERT_FLOAT_EQ(view.angleDegrees, 150);
            ASSERT_EQ(view.lightAssetId, light.deviceAssetId);
            EXPECT_EQ(world.enemyObservationSolveCount() - solves, static_cast<std::uint64_t>(count));
            if (frame >= 10) samples.push_back(elapsed);
        }
        ASSERT_EQ(samples.size(), 150U);
        std::sort(samples.begin(), samples.end());
        EXPECT_LE(samples[142], 3.0) << "enemies=" << count << " blockers=220 samples=150";
        RecordProperty("dual_device_" + std::to_string(count) + "_p95_ms", std::to_string(samples[142]));
    }
}

TEST(NightVisionRuntimeTest, ThrownIlluminationCausesGlareOnFirstDirectLookOnly)
{
    auto config = nightTestWorld();
    config.grenadeEnabled = true;
    GameplayWorld world(config);
    world.setRaidDaylight(false);
    world.setNightVision({150, 850}, {145, 750});
    GameplayInput aim;
    aim.aimWorldBounds = Rect{{}, config.worldSize};
    aim.aimWorldPosition = Vec2{500, 1000};
    const ThrownIlluminationUseDefinition use;
    auto landing = world.previewThrownIllumination({1090, 1000}, use);
    ASSERT_TRUE(landing);
    landing->effectIdentity = "glare-first-look";
    ThrownIlluminationRuntime prepared;
    ASSERT_TRUE(prepared.start(*landing));
    world.commitThrownIllumination(std::move(prepared));
    world.update(aim, use.flightSeconds);
    ASSERT_TRUE(world.thrownIllumination().source());
    EXPECT_FALSE(world.thrownIllumination().source()->nightVisionGlareTriggered);
    EXPECT_FLOAT_EQ(world.thrownIlluminationGlareSeconds(), 0);

    aim.aimWorldPosition = Vec2{1500, 1000};
    world.update(aim, 0.016F);
    ASSERT_TRUE(world.thrownIllumination().source()->nightVisionGlareTriggered);
    EXPECT_FLOAT_EQ(world.thrownIlluminationGlareSeconds(), 0.35F);
    world.update(aim, 0.36F);
    EXPECT_FLOAT_EQ(world.thrownIlluminationGlareSeconds(), 0);
    EXPECT_TRUE(world.thrownIllumination().source()->nightVisionGlareTriggered);
    world.update(aim, 0.016F);
    EXPECT_FLOAT_EQ(world.thrownIlluminationGlareSeconds(), 0);
    EXPECT_TRUE(world.raidObservation().pointVisible(landing->point));
}

TEST(NightVisionRuntimeTest, ThrownIlluminationOnLegacyObservationMapsOnlyLightsTerrain)
{
    auto config = nightTestWorld();
    config.grenadeEnabled = true;
    config.lightConeObservationEnabled = false;
    GameplayWorld world(config);
    world.setRaidDaylight(false);
    auto landing = world.previewThrownIllumination({1200, 1000}, {});
    ASSERT_TRUE(landing);
    landing->effectIdentity = "legacy-terrain-light";
    ThrownIlluminationRuntime prepared;
    ASSERT_TRUE(prepared.start(*landing));
    world.commitThrownIllumination(std::move(prepared));
    world.update({}, landing->use.flightSeconds);
    ASSERT_TRUE(world.thrownIllumination().source());
    EXPECT_TRUE(world.thrownIlluminationBackgroundObservations().front().validGeometry());
    EXPECT_FALSE(world.raidObservation().illuminationKey.source);
    EXPECT_FALSE(world.raidObservation().illumination);
}

TEST(NightVisionPerformanceTest, ThrownIlluminationWithPoweredDevicesStaysWithinThreeMilliseconds)
{
    GameSession loadout;
    ASSERT_NO_FATAL_FAILURE(armSession(loadout, 18));
    auto &equipment = const_cast<ProfileState &>(loadout.profile());
    const auto &content = publishedContentRegistry();
    const auto vision = findNightVision(equipment, content, findItem(equipment, helmet));
    const auto light = findWeaponLight(equipment, content, findItem(equipment, rifle));
    ASSERT_NE(vision.batteryAssetId, 0U);
    ASSERT_NE(light.batteryAssetId, 0U);
    const auto chestRig = findItem(equipment, alpha_content::chestRig);
    ASSERT_NE(chestRig, 0U);
    ASSERT_TRUE(executeInventory(equipment, content,
        InventoryEquipCommand{chestRig, EquipmentSlotKind::ChestRig},
        {equipment.revision, "benchmark-flare-rig"}).succeeded);
    const auto &definition = content.item(kThrownIlluminationItemId);
    ASSERT_TRUE(definition.thrownIlluminationUse);
    const auto carrier = ProfileContainerId::compartment(chestRig, 0);
    const auto position = findFirstProfileFit(equipment, content, carrier,
        definition, ItemOrientation::Degrees0);
    ASSERT_TRUE(position);
    const auto flare = equipment.assets.create(
        definition, StoredAssetLocation{carrier, *position}, 2);

    for (const int count : {32, 100})
    {
        auto profile = equipment;
        ASSERT_TRUE(executeDeviceSwitch(profile, content, vision.deviceAssetId,
            {profile.revision, "benchmark-flare-night-vision"}).succeeded);
        ASSERT_TRUE(executeDeviceSwitch(profile, content, light.deviceAssetId,
            {profile.revision, "benchmark-flare-flashlight"}).succeeded);
        auto config = nightTestWorld();
        config.grenadeEnabled = true;
        for (int n = 0; n < 220; ++n)
        {
            const float angle = n * 6.28318530718F / 220;
            const float radius = 260.0F + (n % 5) * 126.0F;
            const float x = 1000.0F + std::cos(angle) * radius;
            float y = 1000.0F + std::sin(angle) * radius;
            // A narrow open corridor lets the real throw reach dense geometry
            // on both sides, with at least one target beyond the old range.
            if (y > 930 && y < 1046) y = y < 1000 ? 930 : 1046;
            config.ballisticBlockers.push_back({
                static_cast<std::uint64_t>(n + 1), {{x, y}, {5, 24}}});
        }
        config.initialEnemies.push_back(EnemySpawn{{1695, 975}, {50, 50}, 100});
        for (int n = 1; n < count; ++n)
        {
            const float angle = n * 6.28318530718F / count;
            const float radius = 420.0F + (n % 7) * 60.0F;
            config.initialEnemies.push_back(EnemySpawn{
                {975 + std::cos(angle) * radius, 975 + std::sin(angle) * radius},
                {50, 50}, 100});
        }
        GameplayWorld world(config);
        world.setRaidDaylight(false);
        GameplayInput aim;
        aim.aimWorldBounds = Rect{{}, config.worldSize};
        aim.aimWorldPosition = Vec2{1720, 1000};
        auto landing = world.previewThrownIllumination(
            {1445, 1000}, *definition.thrownIlluminationUse);
        ASSERT_TRUE(landing);
        EXPECT_FLOAT_EQ(landing->point.x, 1445);
        landing->effectIdentity = "benchmark-throw-flare";
        ThrownIlluminationRuntime prepared;
        ASSERT_TRUE(prepared.start(*landing));
        ASSERT_TRUE(consumeThrownIllumination(profile, content, flare,
            {profile.revision, landing->effectIdentity}).succeeded);
        ASSERT_EQ(profile.assets.find(flare)->quantity, 1U);
        world.commitThrownIllumination(std::move(prepared));
        world.update(aim, landing->use.flightSeconds);
        ASSERT_TRUE(world.thrownIllumination().source());
        ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition({764, 984}));
        world.update(aim, 0.0F);

        const auto setDevices = [&] {
            advanceNightVision(profile, content, vision, true, 0.016F);
            advanceWeaponLight(profile, content, light, true, 0.016F);
            const auto poweredVision = queryNightVision(profile, content, vision, true);
            const auto poweredLight = queryWeaponLight(profile, content, light, true);
            world.setNightVision({poweredVision.vision.outdoorAngleDegrees,
                poweredVision.vision.outdoorDistance},
                {poweredVision.vision.indoorAngleDegrees,
                poweredVision.vision.indoorDistance});
            world.setWeaponLight(light.deviceAssetId, light.batteryAssetId,
                poweredLight.light.angleDegrees, poweredLight.light.maximumDistance,
                poweredLight.light.backgroundRadius);
            return poweredVision.output && poweredLight.output;
        };
        using Clock = std::chrono::steady_clock;
        const auto milliseconds = [](Clock::time_point start) {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        };

        ASSERT_TRUE(setDevices());
        auto before = Clock::now();
        const auto &firstView = world.raidObservation();
        const auto landingMs = milliseconds(before);
        ASSERT_TRUE(firstView.illumination);
        const auto source = firstView.illumination->source;
        EXPECT_TRUE(firstView.pointVisible({1720, 1000}));
        EXPECT_GT(std::hypot(1720.0F - firstView.observer.x, 1000.0F - firstView.observer.y),
            firstView.maximumDistance);
        RecordProperty("illumination_first_observation_" + std::to_string(count) + "_ms",
            std::to_string(landingMs));

        std::vector<double> samples;
        samples.reserve(150);
        volatile int seen{};
        for (int frame = 0; frame < 160; ++frame)
        {
            const float angle = std::sin(frame * 0.08F) * 0.17F;
            const Vec2 center{780 + 3 * std::sin(angle), 1000 + 3 * std::cos(angle)};
            ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition(
                {center.x - 16, center.y - 16}));
            aim.aimWorldPosition = Vec2{
                center.x + std::cos(angle) * 500, center.y + std::sin(angle) * 500};
            world.update(aim, 0.016F);
            const auto solves = world.enemyObservationSolveCount();
            before = Clock::now();
            const bool powered = setDevices();
            const auto &view = world.raidObservation();
            for (int consumer = 0; consumer < 4; ++consumer)
                for (const auto &enemy : world.enemies())
                    seen = seen + (world.raidEnemyObservable(enemy.combatTargetId()) ? 1 : 0);
            const auto elapsed = milliseconds(before);
            ASSERT_TRUE(powered);
            ASSERT_TRUE(view.illumination);
            EXPECT_EQ(view.illumination->source, source);
            EXPECT_EQ(world.enemyObservationSolveCount() - solves, static_cast<std::uint64_t>(count));
            if (frame >= 10) samples.push_back(elapsed);
        }
        ASSERT_EQ(samples.size(), 150U);
        std::sort(samples.begin(), samples.end());
        EXPECT_LE(samples[142], 3.0) << "enemies=" << count << " blockers=220 samples=150";
        RecordProperty("illumination_" + std::to_string(count) + "_p95_ms",
            std::to_string(samples[142]));

        world.update(aim, landing->use.durationSeconds);
        ASSERT_FALSE(world.thrownIllumination().active());
        before = Clock::now();
        const auto &expired = world.raidObservation();
        const auto expiredMs = milliseconds(before);
        EXPECT_FALSE(expired.illuminationKey.source);
        EXPECT_FALSE(expired.illumination);
        RecordProperty("illumination_expiry_observation_" + std::to_string(count) + "_ms",
            std::to_string(expiredMs));
    }
}
