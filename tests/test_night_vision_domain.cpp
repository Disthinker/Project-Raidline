#include "base_ground_domain.h"
#include "economy_domain.h"
#include "lost_raid_domain.h"
#include "raid_lifecycle.h"
#include "raid_visibility_fixture.h"
#include "recovery_task_domain.h"
#include "save_repository.h"
#include "tactical_device_domain.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace
{
const ItemDefinitionId kHelmet{"item.protective_gear.helmet_basic"};
const ItemDefinitionId kScoutHelmet{"item.protective_gear.helmet_scout"};
const ItemDefinitionId kCell{"item.battery.universal"};
const ItemDefinitionId kRifle{"item.weapon.rifle_5_45_service"};
const ItemDefinitionId kBackpack{"item.container.backpack_small"};
const MapDefinitionId kRaidMap{"map.v0.test"};

class NightVisionDomainTest : public testing::Test
{
protected:
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewPublishedProfile("night-vision-domain", content);
    AssetInstanceId helmet{}, device{}, cell{};

    AssetInstanceId stored(const ItemDefinitionId &definitionId, std::uint32_t quantity = 1)
    {
        const auto &definition = content.item(definitionId);
        const auto stash = ProfileContainerId::stash();
        const auto fit = findFirstProfileFit(profile, content, stash, definition, ItemOrientation::Degrees0);
        EXPECT_TRUE(fit);
        if (!fit) return {};
        return profile.assets.create(definition, StoredAssetLocation{stash, *fit}, quantity);
    }

    AssetInstanceId existing(const ItemDefinitionId &definitionId) const
    {
        for (const auto &[id, asset] : profile.assets.records())
            if (asset.definitionId == definitionId) return id;
        return {};
    }

    void SetUp() override
    {
        EXPECT_EQ(existing(kHospitalNightVisionId), 0U);
        helmet = existing(kHelmet);
        if (!helmet) helmet = stored(kHelmet);
        device = stored(kHospitalNightVisionId);
        cell = stored(kCell);
        ASSERT_NE(helmet, 0U);
        ASSERT_NE(device, 0U);
        ASSERT_NE(cell, 0U);
    }

    void mount()
    {
        ASSERT_TRUE(executeHelmetDeviceChange(profile, content, {helmet, device}, {profile.revision, "mount"}).succeeded);
        ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {device, cell}, {}, {profile.revision, "cell"}).succeeded);
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{helmet, EquipmentSlotKind::Helmet},
            {profile.revision, "wear"}).succeeded);
    }

    NightVisionBinding binding() const { return findNightVision(profile, content, helmet); }

    void deploy()
    {
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{existing(kRifle), EquipmentSlotKind::PrimaryWeapon},
            {profile.revision, "rifle"}).succeeded);
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{existing(kBackpack), EquipmentSlotKind::Backpack},
            {profile.revision, "backpack"}).succeeded);
        const auto receipt = executeDeploy(profile, content, DeployCommand{"vision-raid", "vision-result", 7301, kRaidMap},
            {profile.revision, "deploy"});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
    }
};
}

TEST_F(NightVisionDomainTest, PreviewCompatibilityAndMountedRecycleAreAtomic)
{
    const auto scout = stored(kScoutHelmet);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(queryHelmetDeviceChange(profile, content, {scout, device}).canCommit);
    EXPECT_FALSE(executeHelmetDeviceChange(profile, content, {scout, device}, {profile.revision, "scout"}).succeeded);
    EXPECT_TRUE(queryHelmetDeviceChange(profile, content, {helmet, device}).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    ASSERT_NO_FATAL_FAILURE(mount());
    EXPECT_EQ(binding().deviceAssetId, device);
    EXPECT_EQ(binding().batteryAssetId, cell);
    EXPECT_TRUE(assetIsCarried(profile, device));
    EXPECT_TRUE(assetIsCarried(profile, cell));

    const auto attached = profileStateFingerprint(profile);
    EXPECT_FALSE(executeEconomy(profile, content, RecycleCommand{helmet}, {profile.revision, "recycle"}).succeeded);
    EXPECT_TRUE(executeHelmetDeviceChange(profile, content, {helmet, device}, {0, "mount"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), attached);
    EXPECT_EQ(content.item(kHospitalNightVisionId).marketBuyPrice, 0U);
    EXPECT_EQ(content.item(kHospitalNightVisionId).marketRecyclePrice, 0U);
}

TEST_F(NightVisionDomainTest, FullStashRejectsUnmountThenDeviceAndCellReturnTogether)
{
    ASSERT_NO_FATAL_FAILURE(mount());
    const auto &battery = content.item(kCell);
    const auto stash = ProfileContainerId::stash();
    std::vector<AssetInstanceId> filler;
    while (const auto fit = findFirstProfileFit(profile, content, stash, battery, ItemOrientation::Degrees0))
        filler.push_back(profile.assets.create(battery, StoredAssetLocation{stash, *fit}, 1));
    const auto full = profileStateFingerprint(profile);
    EXPECT_FALSE(queryHelmetDeviceChange(profile, content, {helmet, 0}).canCommit);
    EXPECT_FALSE(executeHelmetDeviceChange(profile, content, {helmet, 0}, {profile.revision, "full"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), full);
    for (const auto id : filler) ASSERT_TRUE(profile.assets.erase(id));
    ASSERT_TRUE(executeHelmetDeviceChange(profile, content, {helmet, 0}, {profile.revision, "remove"}).succeeded);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(device)->location));
    EXPECT_EQ(std::get<InstalledDeviceBatteryLocation>(profile.assets.find(cell)->location).deviceAssetId, device);
    EXPECT_FALSE(assetIsCarried(profile, cell));
    EXPECT_TRUE(assetIsBaseAccessible(profile, cell));
}

TEST_F(NightVisionDomainTest, ChargeTracksOutputAndLowPowerRecoveryKeepsTheOldCell)
{
    const auto spare = stored(kCell);
    ASSERT_NO_FATAL_FAILURE(mount());
    ASSERT_TRUE(executeDeviceSwitch(profile, content, device, {profile.revision, "on"}).succeeded);
    const auto original = binding();
    advanceNightVision(profile, content, original, false, 900.0F);
    EXPECT_EQ(profile.assets.find(cell)->remainingCharges, 600000U);
    for (int frame = 0; frame < 100; ++frame)
        advanceNightVision(profile, content, original, true, 0.001F);
    EXPECT_EQ(profile.assets.find(cell)->remainingCharges, 599667U);
    advanceNightVision(profile, content, original, true, 168.9F);
    advanceNightVision(profile, content, original, true, 1.0F);
    ASSERT_TRUE(queryNightVision(profile, content, original, true).interrupted);
    const auto oldCharge = profile.assets.find(cell)->remainingCharges;
    EXPECT_NEAR(oldCharge, 36666U, 1);
    advanceNightVision(profile, content, original, true, 60.0F);
    EXPECT_EQ(profile.assets.find(cell)->remainingCharges, oldCharge);

    ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {device, spare}, {}, {profile.revision, "swap"}).succeeded);
    EXPECT_EQ(profile.assets.find(cell)->remainingCharges, oldCharge);
    EXPECT_TRUE(queryNightVision(profile, content, binding(), true).output);
    ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {device, cell}, {}, {profile.revision, "reseat"}).succeeded);
    advanceNightVision(profile, content, binding(), true, 11.1F);
    EXPECT_EQ(profile.assets.find(cell), nullptr);
    EXPECT_TRUE(queryNightVision(profile, content, original, true).switchOn);
    EXPECT_FALSE(queryNightVision(profile, content, original, true).output);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST_F(NightVisionDomainTest, Schema61RoundTripAndInvalidHostChecksPreserveAssetIdentity)
{
    ASSERT_NO_FATAL_FAILURE(mount());
    ASSERT_TRUE(executeDeviceSwitch(profile, content, device, {profile.revision, "on"}).succeeded);
    advanceNightVision(profile, content, binding(), true, 1.25F);
    const auto envelope = serializeProfileEnvelope(profile, content.contentVersion());
    EXPECT_EQ(nlohmann::json::parse(envelope).at("schema_version"), 61U);
    const auto loaded = deserializeProfileEnvelope(envelope, content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    EXPECT_EQ(findNightVision(*loaded.profile, content, helmet).batteryAssetId, cell);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content.contentVersion(), 60)), std::invalid_argument);

    auto malformed = profile;
    malformed.assets.findMutable(device)->location = InstalledHelmetDeviceLocation{existing(kRifle)};
    EXPECT_FALSE(validateProfileState(malformed, content).valid);
    const auto duplicate = stored(kHospitalNightVisionId);
    profile.assets.findMutable(duplicate)->location = InstalledHelmetDeviceLocation{helmet};
    EXPECT_FALSE(validateProfileState(profile, content).valid);
}

TEST_F(NightVisionDomainTest, GroundAndFailedRaidRecoveryKeepTheWholeMountedTree)
{
    ASSERT_NO_FATAL_FAILURE(mount());
    profile.assets.findMutable(cell)->remainingCharges = 170123;
    const BaseGroundAccess access{RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"},
        {120, 100}, {160, 100}, true, 84};
    EXPECT_FALSE(queryBaseGround(profile, content, DropBaseGroundAssetCommand{device, 0, ItemOrientation::Degrees0, access}).canCommit);
    const auto dropped = executeBaseGround(profile, content, DropBaseGroundAssetCommand{helmet, 0, ItemOrientation::Degrees0, access},
        {profile.revision, "drop"});
    ASSERT_TRUE(dropped.succeeded) << dropped.message;
    EXPECT_FALSE(assetIsCarried(profile, cell));
    EXPECT_EQ(binding().batteryAssetId, cell);
    ASSERT_TRUE(executeBaseGround(profile, content, PickupBaseGroundAssetCommand{helmet, access}, {profile.revision, "pickup"}).succeeded);
    if (equippedAsset(profile, EquipmentSlotKind::Helmet) != helmet)
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{helmet, EquipmentSlotKind::Helmet},
            {profile.revision, "rewear"}).succeeded);

    ASSERT_NO_FATAL_FAILURE(deploy());
    ASSERT_TRUE(settlePendingRaid(profile, content, "vision-result", RaidResultOutcome::PlayerDead).succeeded);
    const auto lost = lostRaidRecordForAsset(profile, helmet);
    ASSERT_TRUE(lost);
    EXPECT_EQ(lostRaidRecordForAsset(profile, device), lost);
    EXPECT_EQ(lostRaidRecordForAsset(profile, cell), lost);
    ASSERT_TRUE(executeStartRecoveryTask(profile, content, *lost, {profile.revision, "recover"}).succeeded);
    const bool recovered = profile.recoveryTask->recoveredAssetIds.contains(helmet);
    EXPECT_EQ(profile.recoveryTask->recoveredAssetIds.contains(device), recovered);
    EXPECT_EQ(profile.recoveryTask->recoveredAssetIds.contains(cell), recovered);
    profile.worldClock.elapsedWorldMinutes = profile.recoveryTask->completionWorldMinute;
    ASSERT_TRUE(applyRecoveryTaskThrough(profile).becameReady);
    ASSERT_TRUE(executeCollectRecoveryTask(profile, content, {profile.revision, "collect"}).succeeded);
    if (recovered)
    {
        ASSERT_NE(profile.assets.find(cell), nullptr);
        EXPECT_EQ(profile.assets.find(cell)->remainingCharges, 170123U);
        EXPECT_EQ(binding().deviceAssetId, device);
    }
    else
    {
        EXPECT_EQ(profile.assets.find(helmet), nullptr);
        EXPECT_EQ(profile.assets.find(device), nullptr);
        EXPECT_EQ(profile.assets.find(cell), nullptr);
    }
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST_F(NightVisionDomainTest, GroundPickupCannotMergeAPartialCellIntoFullOnes)
{
    ASSERT_NO_FATAL_FAILURE(mount());
    profile.assets.findMutable(cell)->remainingCharges = 170123U;
    ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {device, 0}, {}, {profile.revision, "unload"}).succeeded);
    const auto bag = existing(kBackpack);
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{bag, EquipmentSlotKind::Backpack},
        {profile.revision, "bag"}).succeeded);
    const auto full = profile.assets.create(content.item(kCell), StoredAssetLocation{ProfileContainerId::compartment(bag, 0), {0, 0}}, 2);
    const BaseGroundAccess access{RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"},
        {120, 100}, {160, 100}, true, 84};
    ASSERT_TRUE(executeBaseGround(profile, content, DropBaseGroundAssetCommand{cell, 0, ItemOrientation::Degrees0, access},
        {profile.revision, "drop-cell"}).succeeded);
    const auto pickedUp = executeBaseGround(profile, content, PickupBaseGroundAssetCommand{cell, access},
        {profile.revision, "pick-cell"});
    ASSERT_TRUE(pickedUp.succeeded) << pickedUp.message;
    EXPECT_EQ(pickedUp.affectedAssetId, cell);
    EXPECT_EQ(profile.assets.find(cell)->remainingCharges, 170123U);
    EXPECT_EQ(profile.assets.find(full)->quantity, 2U);
}

TEST_F(NightVisionDomainTest, SelfRecoveryAndRepeatedSettlementKeepOneDeviceAndCell)
{
    ASSERT_NO_FATAL_FAILURE(mount());
    profile.assets.findMutable(cell)->remainingCharges = 321012;
    ASSERT_NO_FATAL_FAILURE(deploy());
    ASSERT_TRUE(settlePendingRaid(profile, content, "vision-result", RaidResultOutcome::PlayerDead).succeeded);
    const auto recovered = executeDeploy(profile, content, DeployCommand{"recover-vision", "vision-recovered", 9403, kRaidMap, {}, "vision-result"},
        {profile.revision, "self-recovery"});
    ASSERT_TRUE(recovered.succeeded) << recovered.message;
    ASSERT_TRUE(openObservedRaidSelfRecovery(profile, content, {profile.revision, "open"}).succeeded);
    ASSERT_TRUE(pickupObservedRaidLoot(profile, content, helmet, {profile.revision, "pickup"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(profile, content, "vision-recovered", RaidResultOutcome::Extracted).succeeded);
    ASSERT_NE(profile.assets.find(cell), nullptr);
    EXPECT_EQ(profile.assets.find(cell)->remainingCharges, 321012U);
    EXPECT_EQ(binding().deviceAssetId, device);
    EXPECT_EQ(binding().batteryAssetId, cell);
    const auto settled = profileStateFingerprint(profile);
    static_cast<void>(settlePendingRaid(profile, content, "vision-recovered", RaidResultOutcome::Extracted));
    EXPECT_EQ(profileStateFingerprint(profile), settled);
}
